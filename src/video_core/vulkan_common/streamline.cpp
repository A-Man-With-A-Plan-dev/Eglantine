// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// NVIDIA DLSS through NVIDIA Streamline, with Streamline's manual hooking.

#include "video_core/vulkan_common/streamline.h"

#ifdef EGLANTINE_STREAMLINE

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <fmt/format.h>

#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/settings.h"
#include "video_core/vulkan_common/streamline_api.h"

#include <sl_security.h>

#include <softpub.h>
#include <wintrust.h>

namespace Vulkan::Streamline {
namespace {

// A custom engine names itself with its own project id; an application id from NVIDIA is only
// needed without an engine type. Keep it fixed: the driver may key behaviour off it.
constexpr const char* PROJECT_ID = "8d3e2f9a-6b41-4c7e-9a52-1f0c3b7d5e64";
constexpr const wchar_t* INTERPOSER = L"sl.interposer.dll";
constexpr const wchar_t* MODEL = L"nvngx_dlss.dll";
// The first DLSS with preset K.
constexpr u32 MIN_DLSS_MAJOR = 310;

using Version = std::array<u32, 4>;

struct CachedFrameToken {
    u32 frame_id{0xFFFFFFFFu};
    sl::FrameToken* token{};
};

struct State {
    HMODULE module{};
    Api api{};
    PFun_slSetTag* set_tag{};
    PFun_slSetTagForFrame* set_tag_for_frame{};
    PFun_slReflexGetState* reflex_get_state{};
    PFun_slReflexSleep* reflex_sleep{};
    PFun_slReflexSetOptions* reflex_set_options{};
    PFun_slPCLGetState* pcl_get_state{};
    PFun_slPCLSetMarker* pcl_set_marker{};
    PFun_slPCLSetOptions* pcl_set_options{};
    PFun_slDLSSGGetState* dlssg_get_state{};
    PFun_slDLSSGSetOptions* dlssg_set_options{};
    std::wstring plugin_dir;
    std::wstring model_dir;
    std::vector<std::string> instance_extensions;
    std::vector<std::string> device_extensions;
    std::mutex token_mutex;
    std::array<CachedFrameToken, 8> token_cache{};
    bool started{};
    bool attached{};
    bool reflex_available{};
    bool pcl_available{};
    bool dlssg_available{};
    bool dlssg_enabled{};
    u32 dlssg_max_frames{};
    u32 dlssg_frames_to_generate{};
    std::atomic<bool> logged_first_dlssg_tag{false};
};

State& GetState() {
    static State state;
    return state;
}

int LogLevel() {
    const char* const value = std::getenv("EDEN_NX2_DLSS_LOG");
    return value ? std::atoi(value) : 0;
}

bool ReflexWantedByEnv() {
    const char* const value = std::getenv("EDEN_NX2_REFLEX");
    if (!value || !*value) {
        return true;
    }
    const std::string_view sv{value};
    return sv != "0" && sv != "off" && sv != "false";
}

bool ReflexBoostByEnv() {
    const char* const value = std::getenv("EDEN_NX2_REFLEX");
    return value && std::string_view{value} == "boost";
}

bool ReflexSleepWantedByEnv() {
    const char* const value = std::getenv("EDEN_NX2_REFLEX_SLEEP");
    if (!value || !*value) {
        return true;
    }
    const std::string_view sv{value};
    return sv != "0" && sv != "off" && sv != "false";
}

bool DlssGProbeWantedByEnv() {
    const char* const value = std::getenv("EDEN_NX2_DLSSG_PROBE");
    return value && std::atoi(value) != 0;
}

u32 DlssGFramesFromEnv() {
    const char* const value = std::getenv("EDEN_NX2_DLSSG_FRAMES");
    if (!value || !*value) {
        return 0;
    }
    const int parsed = std::atoi(value);
    if (parsed <= 0) {
        return 0;
    }
    return static_cast<u32>(std::clamp(parsed, 1, 3));
}

void LogFromStreamline(sl::LogType type, const char* message) {
    std::string_view text{message ? message : ""};
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    switch (type) {
    case sl::LogType::eError:
        LOG_ERROR(Render_Vulkan, "Streamline: {}", text);
        break;
    case sl::LogType::eWarn:
        LOG_WARNING(Render_Vulkan, "Streamline: {}", text);
        break;
    default:
        if (LogLevel() > 0) {
            LOG_INFO(Render_Vulkan, "Streamline: {}", text);
        }
        break;
    }
}

/// Where the user may put sl.interposer.dll: EDEN_STREAMLINE_DIR, the user folder's "streamline"
/// folder, then a "streamline" folder next to the executable.
std::vector<std::filesystem::path> SearchDirs() {
    std::vector<std::filesystem::path> dirs;
    if (const char* const dir = std::getenv("EDEN_STREAMLINE_DIR"); dir && *dir) {
        dirs.emplace_back(dir);
    }
    dirs.push_back(UserFolder());
    dirs.push_back(Common::FS::GetExeDirectory() / "streamline");
    return dirs;
}

template <typename T>
bool Resolve(HMODULE module, const char* name, T*& function) {
    function = reinterpret_cast<T*>(GetProcAddress(module, name));
    if (!function) {
        LOG_ERROR(Render_Vulkan, "sl.interposer.dll has no {}", name);
    }
    return function != nullptr;
}

template <typename T>
void ResolveOptional(HMODULE module, const char* name, T*& function) {
    function = reinterpret_cast<T*>(GetProcAddress(module, name));
}

std::optional<Version> ReadVersion(const std::filesystem::path& path) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) {
        return std::nullopt;
    }
    std::vector<u8> data(size);
    VS_FIXEDFILEINFO* info{};
    UINT length = 0;
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info) {
        return std::nullopt;
    }
    return Version{HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                   HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS)};
}

std::string VersionText(const std::filesystem::path& path) {
    const std::optional<Version> version = path.empty() ? std::nullopt : ReadVersion(path);
    if (!version) {
        return {};
    }
    const Version& v = *version;
    return v[3] == 0 ? fmt::format("{}.{}.{}", v[0], v[1], v[2])
                     : fmt::format("{}.{}.{}.{}", v[0], v[1], v[2], v[3]);
}

/// The NVIDIA app keeps every DLSS it downloads under ProgramData; pick the newest.
std::filesystem::path FindAppModel() {
    const wchar_t* const program_data = _wgetenv(L"ProgramData");
    if (!program_data) {
        return {};
    }
    const std::filesystem::path root =
        std::filesystem::path{program_data} / L"NVIDIA" / L"NGX" / L"models" / L"dlss" / L"versions";
    std::filesystem::path best_path;
    Version best{};
    std::error_code ec;
    for (const auto& version_dir : std::filesystem::directory_iterator(root, ec)) {
        std::error_code files_ec;
        for (const auto& file :
             std::filesystem::directory_iterator(version_dir.path() / L"files", files_ec)) {
            if (file.path().extension() != L".bin") {
                continue;
            }
            const std::optional<Version> version = ReadVersion(file.path());
            if (version && (*version)[0] >= MIN_DLSS_MAJOR && *version > best) {
                best = *version;
                best_path = file.path();
            }
        }
    }
    return best_path;
}

/// Copies the NVIDIA app's DLSS into our folder as nvngx_dlss.dll, the name Streamline loads.
std::filesystem::path UseAppModel(const DlssFiles& files) {
    const std::filesystem::path dir = UserFolder() / L"nvidia-app";
    const std::filesystem::path target = dir / MODEL;
    std::error_code ec;
    const bool current = std::filesystem::is_regular_file(target, ec) &&
                         std::filesystem::file_size(target, ec) ==
                             std::filesystem::file_size(files.app_model, ec) &&
                         ReadVersion(target) == ReadVersion(files.app_model);
    if (!current) {
        if (!IsSignedByNvidia(files.app_model)) {
            LOG_WARNING(Render_Vulkan, "The NVIDIA app's DLSS {} is not signed by NVIDIA",
                        Common::FS::PathToUTF8String(files.app_model));
            return {};
        }
        std::filesystem::create_directories(dir, ec);
        std::filesystem::copy_file(files.app_model, target,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            LOG_WARNING(Render_Vulkan, "Could not copy the NVIDIA app's DLSS to {}: {}",
                        Common::FS::PathToUTF8String(target), ec.message());
            return {};
        }
    }
    return dir;
}

bool Load(State& state, const std::filesystem::path& path) {
    if (!IsStreamlineModule(path)) {
        LOG_ERROR(Render_Vulkan, "{} is not signed by NVIDIA; not loading it",
                  Common::FS::PathToUTF8String(path));
        return false;
    }
    state.module = LoadLibraryW(path.c_str());
    if (!state.module) {
        LOG_ERROR(Render_Vulkan, "Could not load {} (error {})", Common::FS::PathToUTF8String(path),
                  GetLastError());
        return false;
    }
    Api& api = state.api;
    const HMODULE m = state.module;
    ResolveOptional(m, "slSetTag", state.set_tag);
    ResolveOptional(m, "slSetTagForFrame", state.set_tag_for_frame);
    if (Resolve(m, "slInit", api.init) && Resolve(m, "slShutdown", api.shutdown) &&
        Resolve(m, "slIsFeatureSupported", api.is_feature_supported) &&
        Resolve(m, "slGetFeatureRequirements", api.get_feature_requirements) &&
        Resolve(m, "slGetFeatureVersion", api.get_feature_version) &&
        Resolve(m, "slGetFeatureFunction", api.get_feature_function) &&
        Resolve(m, "slGetNewFrameToken", api.get_new_frame_token) &&
        Resolve(m, "slSetConstants", api.set_constants) &&
        Resolve(m, "slEvaluateFeature", api.evaluate_feature) &&
        Resolve(m, "slFreeResources", api.free_resources) &&
        Resolve(m, "slSetVulkanInfo", api.set_vulkan_info)) {
        return true;
    }
    FreeLibrary(state.module);
    state.module = nullptr;
    state.api = {};
    state.set_tag = nullptr;
    state.set_tag_for_frame = nullptr;
    return false;
}

std::vector<std::string> Names(uint32_t count, const char** names) {
    std::vector<std::string> result;
    for (uint32_t i = 0; i < count; ++i) {
        result.emplace_back(names[i]);
    }
    return result;
}

sl::FrameToken* GetFrameToken(State& state, u32 frame_id) {
    std::scoped_lock lock{state.token_mutex};
    CachedFrameToken& slot = state.token_cache[frame_id % state.token_cache.size()];
    if (slot.frame_id == frame_id && slot.token != nullptr) {
        return slot.token;
    }
    sl::FrameToken* token = nullptr;
    const uint32_t index = frame_id;
    if (state.api.get_new_frame_token(token, &index) != sl::Result::eOk || !token) {
        return nullptr;
    }
    slot.frame_id = frame_id;
    slot.token = token;
    return token;
}

sl::float4x4 ToMatrix(const std::array<f32, 16>& m) {
    sl::float4x4 matrix;
    matrix.setRow(0, {m[0], m[1], m[2], m[3]});
    matrix.setRow(1, {m[4], m[5], m[6], m[7]});
    matrix.setRow(2, {m[8], m[9], m[10], m[11]});
    matrix.setRow(3, {m[12], m[13], m[14], m[15]});
    return matrix;
}

sl::float4x4 IdentityMatrix() {
    sl::float4x4 matrix;
    matrix.setRow(0, {1.0f, 0.0f, 0.0f, 0.0f});
    matrix.setRow(1, {0.0f, 1.0f, 0.0f, 0.0f});
    matrix.setRow(2, {0.0f, 0.0f, 1.0f, 0.0f});
    matrix.setRow(3, {0.0f, 0.0f, 0.0f, 1.0f});
    return matrix;
}

sl::Resource MakeResource(const DlssGImage& img) {
    sl::Resource res{sl::ResourceType::eTex2d, img.image, nullptr, img.view,
                     static_cast<uint32_t>(img.layout)};
    res.width = img.extent.width;
    res.height = img.extent.height;
    res.nativeFormat = static_cast<uint32_t>(img.format);
    res.mipLevels = 1;
    res.arrayLayers = 1;
    res.flags = 0;
    res.usage = img.usage;
    return res;
}

} // Anonymous namespace

std::filesystem::path UserFolder() {
    return Common::FS::GetEdenPath(Common::FS::EdenPath::StreamlineDir);
}

bool IsStreamlineModule(const std::filesystem::path& path) {
    return sl::security::verifyEmbeddedSignature(path.c_str());
}

bool IsSignedByNvidia(const std::filesystem::path& path) {
    const std::wstring file_path = std::filesystem::absolute(path).wstring();
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = file_path.c_str();
    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    bool valid = WinVerifyTrust(nullptr, &action, &data) == ERROR_SUCCESS;
    if (valid) {
        CRYPT_PROVIDER_DATA* const provider = WTHelperProvDataFromStateData(data.hWVTStateData);
        CRYPT_PROVIDER_SGNR* const signer =
            provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        CRYPT_PROVIDER_CERT* const cert = signer ? WTHelperGetProvCertFromChain(signer, 0) : nullptr;
        wchar_t name[128]{};
        valid = cert &&
                CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name,
                                   static_cast<DWORD>(std::size(name))) > 1 &&
                std::wstring_view{name} == L"NVIDIA Corporation";
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &data);
    return valid;
}

DlssFiles FindDlssFiles() {
    DlssFiles files;
    files.folder = UserFolder();
    std::error_code ec;
    for (const std::filesystem::path& dir : SearchDirs()) {
        if (std::filesystem::is_regular_file(dir / INTERPOSER, ec)) {
            files.folder = dir;
            files.interposer = dir / INTERPOSER;
            break;
        }
    }
    if (std::filesystem::is_regular_file(files.folder / MODEL, ec)) {
        files.model = files.folder / MODEL;
    }
    files.app_model = FindAppModel();
    files.interposer_version = VersionText(files.interposer);
    files.model_version = VersionText(files.model);
    files.app_model_version = VersionText(files.app_model);
    return files;
}

bool Start() {
    State& state = GetState();
    if (state.started) {
        return true;
    }
    if (!Settings::values.nvidia_dlss.GetValue()) {
        return false;
    }
    if (!state.module) {
        const DlssFiles files = FindDlssFiles();
        if (files.interposer.empty()) {
            LOG_WARNING(Render_Vulkan,
                        "NVIDIA DLSS is on, but sl.interposer.dll (NVIDIA Streamline) is not in "
                        "{} (the Graphics settings can download it); using FSR 3.1",
                        Common::FS::PathToUTF8String(files.folder));
            return false;
        }
        if (!Load(state, files.interposer)) {
            return false;
        }
        state.plugin_dir = files.folder.wstring();
        state.model_dir.clear();
        if (files.model.empty() && !files.app_model.empty()) {
            if (const std::filesystem::path dir = UseAppModel(files); !dir.empty()) {
                state.model_dir = dir.wstring();
                LOG_INFO(Render_Vulkan, "Using the NVIDIA app's DLSS {}", files.app_model_version);
            }
        } else if (files.model.empty()) {
            LOG_WARNING(Render_Vulkan, "No nvngx_dlss.dll in {} and no DLSS from the NVIDIA app",
                        Common::FS::PathToUTF8String(files.folder));
        }
    }

    // Streamline's plugins come from the loader's folder or the NVIDIA app's downloads (OTA); the
    // DLSS model from the loader's folder, else the NVIDIA app's copy.
    const wchar_t* plugin_paths[] = {state.plugin_dir.c_str(), state.model_dir.c_str()};
    const bool probe_dlssg = DlssGProbeWantedByEnv();
    const bool want_reflex = ReflexWantedByEnv() || probe_dlssg;
    std::vector<sl::Feature> features_to_load;
    features_to_load.push_back(sl::kFeatureDLSS);
    if (probe_dlssg) {
        features_to_load.push_back(sl::kFeatureDLSS_G);
    }
    if (want_reflex) {
        features_to_load.push_back(sl::kFeatureReflex);
        features_to_load.push_back(sl::kFeaturePCL);
    }

    sl::Preferences preferences{};
    preferences.logLevel = LogLevel() >= 2 ? sl::LogLevel::eVerbose : sl::LogLevel::eDefault;
    preferences.logMessageCallback = LogFromStreamline;
    preferences.pathsToPlugins = plugin_paths;
    preferences.numPathsToPlugins = state.model_dir.empty() ? 1 : 2;
    preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking |
                        sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eAllowOTA |
                        sl::PreferenceFlags::eLoadDownloadedPlugins;
    preferences.featuresToLoad = features_to_load.data();
    preferences.numFeaturesToLoad = static_cast<uint32_t>(features_to_load.size());
    preferences.engine = sl::EngineType::eCustom;
    preferences.engineVersion = "1.0";
    preferences.projectId = PROJECT_ID;
    preferences.renderAPI = sl::RenderAPI::eVulkan;
    const Api& api = state.api;
    sl::Result init_res = api.init(preferences, sl::kSDKVersion);
    if (init_res != sl::Result::eOk && features_to_load.size() > 1) {
        const sl::Feature dlss_only[] = {sl::kFeatureDLSS};
        preferences.featuresToLoad = dlss_only;
        preferences.numFeaturesToLoad = 1u;
        init_res = api.init(preferences, sl::kSDKVersion);
    }
    if (init_res != sl::Result::eOk) {
        LOG_ERROR(Render_Vulkan, "NVIDIA Streamline failed to start (result {}); using FSR 3.1",
                  static_cast<int>(init_res));
        return false;
    }
    if (probe_dlssg) {
        const auto log_feature_probe = [&](sl::Feature feature, const char* name) {
            sl::FeatureRequirements req{};
            const sl::Result req_res = api.get_feature_requirements(feature, req);
            sl::FeatureVersion ver{};
            const sl::Result ver_res = api.get_feature_version(feature, ver);
            const auto join = [](uint32_t count, const char** items) {
                std::string out;
                for (uint32_t i = 0; i < count; ++i) {
                    if (items && items[i]) {
                        if (!out.empty()) {
                            out += ",";
                        }
                        out += items[i];
                    }
                }
                return out;
            };
            LOG_INFO(Render_Vulkan,
                     "Streamline probe {}: req_result={} ver_result={} sl={} ngx={} flags={:#x} "
                     "(vk_supported={}) queues(gfx={},comp={},of={}) tags={} inst_ext=[{}] "
                     "dev_ext=[{}] feat12=[{}] feat13=[{}]",
                     name, static_cast<int>(req_res), static_cast<int>(ver_res),
                     ver.versionSL.toStr(), ver.versionNGX.toStr(), static_cast<u32>(req.flags),
                     (static_cast<u32>(req.flags) &
                      static_cast<u32>(sl::FeatureRequirementFlags::eVulkanSupported)) != 0,
                     req.vkNumGraphicsQueuesRequired, req.vkNumComputeQueuesRequired,
                     req.vkNumOpticalFlowQueuesRequired, req.numRequiredTags,
                     join(req.vkNumInstanceExtensions, req.vkInstanceExtensions),
                     join(req.vkNumDeviceExtensions, req.vkDeviceExtensions),
                     join(req.vkNumFeatures12, req.vkFeatures12),
                     join(req.vkNumFeatures13, req.vkFeatures13));
        };
        log_feature_probe(sl::kFeatureDLSS_G, "kFeatureDLSS_G");
        log_feature_probe(sl::kFeatureReflex, "kFeatureReflex");
        log_feature_probe(sl::kFeaturePCL, "kFeaturePCL");
    }
    sl::FeatureRequirements requirements{};
    if (const sl::Result result = api.get_feature_requirements(sl::kFeatureDLSS, requirements);
         result != sl::Result::eOk) {
        LOG_WARNING(Render_Vulkan,
                    "Streamline found no DLSS plugin (result {}); put sl.dlss.dll next to "
                    "sl.interposer.dll or install the NVIDIA app. Using FSR 3.1",
                    static_cast<int>(result));
        api.shutdown();
        return false;
    }
    if (requirements.vkNumComputeQueuesRequired != 0 ||
        requirements.vkNumGraphicsQueuesRequired != 0) {
        LOG_WARNING(Render_Vulkan, "This DLSS wants extra Vulkan queues; using FSR 3.1");
        api.shutdown();
        return false;
    }
    state.instance_extensions =
        Names(requirements.vkNumInstanceExtensions, requirements.vkInstanceExtensions);
    state.device_extensions =
        Names(requirements.vkNumDeviceExtensions, requirements.vkDeviceExtensions);
    // The KHR and EXT buffer device address extensions can't both be enabled.
    const auto& device_extensions = state.device_extensions;
    if (std::ranges::find(device_extensions, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) !=
        device_extensions.end()) {
        std::erase(state.device_extensions, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    }
    sl::FeatureVersion version{};
    api.get_feature_version(sl::kFeatureDLSS, version);
    LOG_INFO(Render_Vulkan, "NVIDIA Streamline {} started from {} (DLSS {})",
             version.versionSL.toStr(),
             Common::FS::PathToUTF8String(std::filesystem::path{state.plugin_dir}),
             version.versionNGX.toStr());
    state.started = true;
    return true;
}

bool WantsDevice() {
    const State& state = GetState();
    return state.started && !state.attached;
}

std::vector<std::string> InstanceExtensions() {
    const State& state = GetState();
    return state.started ? state.instance_extensions : std::vector<std::string>{};
}

std::vector<std::string> DeviceExtensions() {
    const State& state = GetState();
    return state.started ? state.device_extensions : std::vector<std::string>{};
}

bool AttachDevice(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                  u32 graphics_family, vk::DeviceDispatch& dld) {
    State& state = GetState();
    if (!WantsDevice()) {
        return false;
    }
    sl::VulkanInfo info{};
    info.device = device;
    info.instance = instance;
    info.physicalDevice = physical;
    info.computeQueueFamily = graphics_family;
    info.graphicsQueueFamily = graphics_family;
    if (const sl::Result result = state.api.set_vulkan_info(info); result != sl::Result::eOk) {
        LOG_ERROR(Render_Vulkan, "Streamline did not take the Vulkan device (result {})",
                  static_cast<int>(result));
        Stop();
        return false;
    }
    sl::AdapterInfo adapter{};
    adapter.vkPhysicalDevice = physical;

    const bool probe_dlssg = DlssGProbeWantedByEnv();
    const bool want_reflex = ReflexWantedByEnv() || probe_dlssg;

    if (want_reflex) {
        const sl::Result pcl_supported = state.api.is_feature_supported(sl::kFeaturePCL, adapter);
        if (pcl_supported == sl::Result::eOk) {
            void* fn_set_marker = nullptr;
            void* fn_get_state = nullptr;
            void* fn_set_options = nullptr;
            void(state.api.get_feature_function(sl::kFeaturePCL, "slPCLSetMarker", fn_set_marker));
            void(state.api.get_feature_function(sl::kFeaturePCL, "slPCLGetState", fn_get_state));
            void(state.api.get_feature_function(sl::kFeaturePCL, "slPCLSetOptions", fn_set_options));
            state.pcl_set_marker = reinterpret_cast<PFun_slPCLSetMarker*>(fn_set_marker);
            state.pcl_get_state = reinterpret_cast<PFun_slPCLGetState*>(fn_get_state);
            state.pcl_set_options = reinterpret_cast<PFun_slPCLSetOptions*>(fn_set_options);
            state.pcl_available = state.pcl_set_marker != nullptr;
        }

        const sl::Result reflex_supported =
            state.api.is_feature_supported(sl::kFeatureReflex, adapter);
        if (reflex_supported == sl::Result::eOk) {
            void* fn_set_options = nullptr;
            void* fn_sleep = nullptr;
            void* fn_get_state = nullptr;
            void(state.api.get_feature_function(sl::kFeatureReflex, "slReflexSetOptions",
                                                fn_set_options));
            void(state.api.get_feature_function(sl::kFeatureReflex, "slReflexSleep", fn_sleep));
            void(state.api.get_feature_function(sl::kFeatureReflex, "slReflexGetState",
                                                fn_get_state));
            state.reflex_set_options = reinterpret_cast<PFun_slReflexSetOptions*>(fn_set_options);
            state.reflex_sleep = reinterpret_cast<PFun_slReflexSleep*>(fn_sleep);
            state.reflex_get_state = reinterpret_cast<PFun_slReflexGetState*>(fn_get_state);
            if (state.reflex_set_options) {
                sl::ReflexOptions reflex_options{};
                reflex_options.mode = ReflexBoostByEnv() ? sl::ReflexMode::eLowLatencyWithBoost
                                                         : sl::ReflexMode::eLowLatency;
                reflex_options.frameLimitUs = 0;
                const sl::Result set_res = state.reflex_set_options(reflex_options);
                bool low_latency_hw = false;
                if (state.reflex_get_state) {
                    sl::ReflexState reflex_state{};
                    if (state.reflex_get_state(reflex_state) == sl::Result::eOk) {
                        low_latency_hw = reflex_state.lowLatencyAvailable;
                    }
                }
                state.reflex_available = set_res == sl::Result::eOk;
                LOG_INFO(Render_Vulkan,
                         "Streamline Reflex configured: mode={} result={} low_latency_available={} "
                         "pcl_available={}",
                         reflex_options.mode == sl::ReflexMode::eLowLatencyWithBoost
                             ? "eLowLatencyWithBoost"
                             : "eLowLatency",
                         static_cast<int>(set_res), low_latency_hw, state.pcl_available);
            }
        }
    }

    if (probe_dlssg) {
        const sl::Result dlssg_supported =
            state.api.is_feature_supported(sl::kFeatureDLSS_G, adapter);
        const sl::Result reflex_supported =
            state.api.is_feature_supported(sl::kFeatureReflex, adapter);
        const sl::Result pcl_supported = state.api.is_feature_supported(sl::kFeaturePCL, adapter);
        LOG_INFO(Render_Vulkan,
                 "Streamline AttachDevice support probe: kFeatureDLSS_G={} kFeatureReflex={} "
                 "kFeaturePCL={}",
                 static_cast<int>(dlssg_supported), static_cast<int>(reflex_supported),
                 static_cast<int>(pcl_supported));
        if (dlssg_supported == sl::Result::eOk) {
            void* get_state_fn = nullptr;
            const sl::Result fn_res =
                state.api.get_feature_function(sl::kFeatureDLSS_G, "slDLSSGGetState", get_state_fn);
            void* set_options_fn = nullptr;
            void(state.api.get_feature_function(sl::kFeatureDLSS_G, "slDLSSGSetOptions",
                                                set_options_fn));
            state.dlssg_get_state = reinterpret_cast<PFun_slDLSSGGetState*>(get_state_fn);
            state.dlssg_set_options = reinterpret_cast<PFun_slDLSSGSetOptions*>(set_options_fn);
            if (fn_res == sl::Result::eOk && state.dlssg_get_state != nullptr) {
                sl::DLSSGState dlssg_state{};
                const sl::Result state_res =
                    state.dlssg_get_state(sl::ViewportHandle{0}, dlssg_state, nullptr);
                state.dlssg_max_frames = dlssg_state.numFramesToGenerateMax;
                state.dlssg_available = state.dlssg_set_options != nullptr;
                LOG_INFO(Render_Vulkan,
                         "Streamline slDLSSGGetState probe: result={} status={:#x} max_gen={} "
                         "vsync_supported={} dyn_mfg_supported={} min_dim={} vram_bytes={}",
                         static_cast<int>(state_res), static_cast<u32>(dlssg_state.status),
                         dlssg_state.numFramesToGenerateMax,
                         static_cast<int>(dlssg_state.bIsVsyncSupportAvailable),
                         static_cast<int>(dlssg_state.bIsDynamicMFGSupported),
                         dlssg_state.minWidthOrHeight, dlssg_state.estimatedVRAMUsageInBytes);
            } else {
                LOG_INFO(Render_Vulkan, "Streamline slDLSSGGetState resolve result={}",
                         static_cast<int>(fn_res));
            }
        }
    }
    if (const sl::Result result = state.api.is_feature_supported(sl::kFeatureDLSS, adapter);
        result != sl::Result::eOk) {
        LOG_WARNING(Render_Vulkan,
                    "NVIDIA DLSS is not supported on this GPU or driver (Streamline result {}); "
                    "using FSR 3.1",
                    static_cast<int>(result));
        Stop();
        return false;
    }
    // Manual hooking: Streamline does its per-frame work in these.
    const auto hook = [&](auto& function, const char* name) {
        if (const FARPROC proxy = GetProcAddress(state.module, name)) {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(proxy);
        } else {
            LOG_WARNING(Render_Vulkan, "sl.interposer.dll has no {}", name);
        }
    };
    hook(dld.vkCreateSwapchainKHR, "vkCreateSwapchainKHR");
    hook(dld.vkDestroySwapchainKHR, "vkDestroySwapchainKHR");
    hook(dld.vkGetSwapchainImagesKHR, "vkGetSwapchainImagesKHR");
    hook(dld.vkAcquireNextImageKHR, "vkAcquireNextImageKHR");
    hook(dld.vkQueuePresentKHR, "vkQueuePresentKHR");
    hook(dld.vkDeviceWaitIdle, "vkDeviceWaitIdle");
    state.attached = true;
    if (probe_dlssg && state.dlssg_available) {
        if (const u32 env_frames = DlssGFramesFromEnv(); env_frames > 0) {
            void(ConfigureDLSSG(true, env_frames));
        }
    }
    LOG_INFO(Render_Vulkan, "NVIDIA DLSS is available through Streamline");
    return true;
}

bool IsDlssAvailable() {
    const State& state = GetState();
    return state.started && state.attached;
}

bool IsReflexAvailable() {
    const State& state = GetState();
    return state.started && state.attached && state.reflex_available;
}

bool IsPclAvailable() {
    const State& state = GetState();
    return state.started && state.attached && state.pcl_available;
}

bool IsDlssGAvailable() {
    const State& state = GetState();
    return state.started && state.attached && state.dlssg_available;
}

bool IsDlssGEnabled() {
    const State& state = GetState();
    return state.started && state.attached && state.dlssg_enabled;
}

u32 DlssGMaxFramesToGenerate() {
    const State& state = GetState();
    return state.started && state.attached ? state.dlssg_max_frames : 0u;
}

void Marker(sl::PCLMarker marker, u32 frame_id) {
    State& state = GetState();
    if (!state.started || !state.attached || !state.pcl_available || !state.pcl_set_marker) {
        return;
    }
    sl::FrameToken* const token = GetFrameToken(state, frame_id);
    if (!token) {
        return;
    }
    void(state.pcl_set_marker(marker, *token));
}

void ReflexSleep(u32 frame_id) {
    State& state = GetState();
    if (!state.started || !state.attached || !state.reflex_available || !state.reflex_sleep) {
        return;
    }
    if (!ReflexSleepWantedByEnv()) {
        return;
    }
    sl::FrameToken* const token = GetFrameToken(state, frame_id);
    if (!token) {
        return;
    }
    void(state.reflex_sleep(*token));
}

bool ConfigureDLSSG(bool enabled, u32 num_frames_to_generate) {
    State& state = GetState();
    if (!state.started || !state.attached || !state.dlssg_available || !state.dlssg_set_options) {
        return false;
    }
    const u32 max_allowed =
        state.dlssg_max_frames > 0 ? std::min<u32>(3u, state.dlssg_max_frames) : 3u;
    const u32 clamped_frames = std::clamp<u32>(num_frames_to_generate, 1u, max_allowed);

    sl::DLSSGOptions options{};
    options.mode = enabled ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
    options.numFramesToGenerate = clamped_frames;
    options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;

    const sl::ViewportHandle viewport{0u};
    const sl::Result set_res = state.dlssg_set_options(viewport, options);
    if (set_res != sl::Result::eOk) {
        LOG_ERROR(Render_Vulkan,
                  "Streamline slDLSSGSetOptions(enabled={}, frames={}) failed: result={}", enabled,
                  clamped_frames, static_cast<int>(set_res));
        return false;
    }

    sl::DLSSGState dlssg_state{};
    if (state.dlssg_get_state) {
        void(state.dlssg_get_state(viewport, dlssg_state, nullptr));
    }
    state.dlssg_enabled = enabled;
    state.dlssg_frames_to_generate = enabled ? clamped_frames : 0u;
    LOG_INFO(Render_Vulkan,
             "Streamline DLSS-G configured: mode={} multiplier={}x (numFramesToGenerate={}/{}) "
             "status={:#x}",
             enabled ? "eOn" : "eOff", enabled ? (clamped_frames + 1u) : 1u, clamped_frames,
             state.dlssg_max_frames, static_cast<u32>(dlssg_state.status));
    return true;
}

bool TagDLSSGInputs(const DlssGInputs& in) {
    State& state = GetState();
    if (!state.started || !state.attached || (!state.set_tag && !state.set_tag_for_frame)) {
        return false;
    }
    sl::FrameToken* const frame = GetFrameToken(state, in.frame_id);
    if (!frame) {
        return false;
    }

    const sl::ViewportHandle viewport{0u};
    if (state.dlssg_available && state.dlssg_set_options && !state.dlssg_enabled) {
        if (const u32 env_frames = DlssGFramesFromEnv(); env_frames > 0) {
            void(ConfigureDLSSG(true, env_frames));
        }
    }

    sl::Constants constants{};
    constants.cameraViewToClip = ToMatrix(in.camera_view_to_clip);
    constants.clipToCameraView = ToMatrix(in.clip_to_camera_view);
    constants.clipToLensClip = IdentityMatrix();
    constants.clipToPrevClip = ToMatrix(in.clip_to_prev_clip);
    constants.prevClipToClip = ToMatrix(in.prev_clip_to_clip);
    constants.jitterOffset = {in.jitter[0], in.jitter[1]};
    constants.mvecScale = {in.motion_vector_scale[0], in.motion_vector_scale[1]};
    constants.cameraPinholeOffset = {0.0f, 0.0f};
    constants.cameraPos = {in.camera_pos[0], in.camera_pos[1], in.camera_pos[2]};
    constants.cameraUp = {in.camera_up[0], in.camera_up[1], in.camera_up[2]};
    constants.cameraRight = {in.camera_right[0], in.camera_right[1], in.camera_right[2]};
    constants.cameraFwd = {in.camera_fwd[0], in.camera_fwd[1], in.camera_fwd[2]};
    constants.cameraNear = in.camera_near;
    constants.cameraFar = in.camera_far;
    constants.cameraFOV = in.camera_fov;
    constants.cameraAspectRatio = in.camera_aspect_ratio;
    constants.depthInverted = in.depth_inverted ? sl::eTrue : sl::eFalse;
    constants.cameraMotionIncluded = in.camera_motion_included ? sl::eTrue : sl::eFalse;
    constants.motionVectors3D = in.motion_vectors_3d ? sl::eTrue : sl::eFalse;
    constants.reset = in.reset ? sl::eTrue : sl::eFalse;
    constants.orthographicProjection = sl::eFalse;
    constants.motionVectorsDilated = sl::eFalse;
    constants.motionVectorsJittered = sl::eFalse;

    const sl::Result const_res = state.api.set_constants(constants, *frame, viewport);

    const sl::ResourceLifecycle lifecycle = in.cmdbuf != VK_NULL_HANDLE
                                                ? sl::ResourceLifecycle::eOnlyValidNow
                                                : sl::ResourceLifecycle::eValidUntilPresent;
    sl::Resource hudless_res = MakeResource(in.hudless_color);
    sl::Resource ui_res = MakeResource(in.ui_color_alpha);
    sl::Resource depth_res = MakeResource(in.depth);
    sl::Resource mvec_res = MakeResource(in.motion_vectors);

    const sl::Extent hudless_ext{0, 0, in.hudless_color.extent.width,
                                 in.hudless_color.extent.height};
    const sl::Extent ui_ext{0, 0, in.ui_color_alpha.extent.width, in.ui_color_alpha.extent.height};
    const sl::Extent depth_ext{0, 0, in.depth.extent.width, in.depth.extent.height};
    const sl::Extent mvec_ext{0, 0, in.motion_vectors.extent.width,
                              in.motion_vectors.extent.height};

    std::array<sl::ResourceTag, 5> tags{
        sl::ResourceTag{in.hudless_color.image ? &hudless_res : nullptr,
                        sl::kBufferTypeHUDLessColor, lifecycle,
                        in.hudless_color.image ? &hudless_ext : nullptr},
        sl::ResourceTag{in.ui_color_alpha.image ? &ui_res : nullptr, sl::kBufferTypeUIColorAndAlpha,
                        lifecycle, in.ui_color_alpha.image ? &ui_ext : nullptr},
        sl::ResourceTag{in.depth.image ? &depth_res : nullptr, sl::kBufferTypeDepth, lifecycle,
                        in.depth.image ? &depth_ext : nullptr},
        sl::ResourceTag{in.motion_vectors.image ? &mvec_res : nullptr, sl::kBufferTypeMotionVectors,
                        lifecycle, in.motion_vectors.image ? &mvec_ext : nullptr},
        sl::ResourceTag{nullptr, sl::kBufferTypeBackbuffer,
                        sl::ResourceLifecycle::eValidUntilPresent, nullptr},
    };
    uint32_t tag_count = 4u;
    const sl::Extent backbuffer_ext{0, 0, in.backbuffer_extent.width, in.backbuffer_extent.height};
    if (in.backbuffer_extent.width > 0 && in.backbuffer_extent.height > 0) {
        tags[4] = sl::ResourceTag{nullptr, sl::kBufferTypeBackbuffer,
                                  sl::ResourceLifecycle::eValidUntilPresent, &backbuffer_ext};
        tag_count = 5u;
    }

    auto* const sl_cmdbuf = reinterpret_cast<sl::CommandBuffer*>(in.cmdbuf);
    const sl::Result tag_res =
        state.set_tag ? state.set_tag(viewport, tags.data(), tag_count, sl_cmdbuf)
                      : state.set_tag_for_frame(*frame, viewport, tags.data(), tag_count, sl_cmdbuf);

    if (!state.logged_first_dlssg_tag.exchange(true) || LogLevel() > 0) {
        LOG_INFO(Render_Vulkan,
                 "Streamline TagDLSSGInputs: frame={} const_res={} tag_res={} hudless={}x{} "
                 "ui={}x{} depth={}x{} mvec={}x{}",
                 in.frame_id, static_cast<int>(const_res), static_cast<int>(tag_res),
                 in.hudless_color.extent.width, in.hudless_color.extent.height,
                 in.ui_color_alpha.extent.width, in.ui_color_alpha.extent.height,
                 in.depth.extent.width, in.depth.extent.height, in.motion_vectors.extent.width,
                 in.motion_vectors.extent.height);
    }
    return const_res == sl::Result::eOk && tag_res == sl::Result::eOk;
}

bool TagDLSSGInputs(u32 frame_id, VkCommandBuffer cmdbuf, const DlssGImage& hudless_color,
                    const DlssGImage& ui_color_alpha, const DlssGImage& depth,
                    const DlssGImage& motion_vectors, bool reset) {
    DlssGInputs inputs{};
    inputs.frame_id = frame_id;
    inputs.cmdbuf = cmdbuf;
    inputs.hudless_color = hudless_color;
    inputs.ui_color_alpha = ui_color_alpha;
    inputs.depth = depth;
    inputs.motion_vectors = motion_vectors;
    inputs.reset = reset;
    if (depth.extent.width > 0 && depth.extent.height > 0) {
        inputs.camera_aspect_ratio =
            static_cast<f32>(depth.extent.width) / static_cast<f32>(depth.extent.height);
    }
    return TagDLSSGInputs(inputs);
}

void Stop() {
    State& state = GetState();
    if (!state.started) {
        return;
    }
    if (state.dlssg_enabled && state.dlssg_set_options) {
        sl::DLSSGOptions off_options{};
        off_options.mode = sl::DLSSGMode::eOff;
        void(state.dlssg_set_options(sl::ViewportHandle{0u}, off_options));
    }
    const sl::Result result = state.api.shutdown();
    {
        std::scoped_lock lock{state.token_mutex};
        state.token_cache = {};
    }
    state.started = false;
    state.attached = false;
    state.reflex_available = false;
    state.pcl_available = false;
    state.dlssg_available = false;
    state.dlssg_enabled = false;
    state.dlssg_max_frames = 0;
    state.dlssg_frames_to_generate = 0;
    state.reflex_get_state = nullptr;
    state.reflex_sleep = nullptr;
    state.reflex_set_options = nullptr;
    state.pcl_get_state = nullptr;
    state.pcl_set_marker = nullptr;
    state.pcl_set_options = nullptr;
    state.dlssg_get_state = nullptr;
    state.dlssg_set_options = nullptr;
    state.logged_first_dlssg_tag.store(false);
    LOG_INFO(Render_Vulkan, "NVIDIA Streamline stopped (result {})", static_cast<int>(result));
}

const Api& GetApi() {
    return GetState().api;
}

} // namespace Vulkan::Streamline

#endif // EGLANTINE_STREAMLINE
