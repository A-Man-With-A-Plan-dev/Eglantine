// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// NVIDIA DLSS through NVIDIA Streamline. Only Streamline's MIT-licensed
// headers are built in: NVIDIA's DLLs are loaded at run time from the user's PC, and none ship
// with the emulator.

#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "common/common_types.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

#if defined(EGLANTINE_STREAMLINE) && __has_include(<sl_pcl.h>)
#include <sl_pcl.h>
#else
namespace sl {
enum class PCLMarker : u32 {
    eSimulationStart = 0,
    eSimulationEnd = 1,
    eRenderSubmitStart = 2,
    eRenderSubmitEnd = 3,
    ePresentStart = 4,
    ePresentEnd = 5,
    eTriggerFlash = 7,
    ePCLatencyPing = 8,
    eOutOfBandRenderSubmitStart = 9,
    eOutOfBandRenderSubmitEnd = 10,
    eOutOfBandPresentStart = 11,
    eOutOfBandPresentEnd = 12,
    eControllerInputSample = 13,
    eDeltaTCalculation = 14,
    eLateWarpPresentStart = 15,
    eLateWarpPresentEnd = 16,
    eCameraConstructed = 17,
    eLateWarpRenderSubmitStart = 18,
    eLateWarpRenderSubmitEnd = 19,
    eVendorInternalAsyncPresentStart = 20,
    eVendorInternalAsyncPresentEnd = 21,
    eNumPresentsInBatch = 22,
    eMaximum
};
} // namespace sl
#endif

namespace Vulkan::Streamline {

/// NVIDIA's files DLSS needs, as found on this PC. Paths are empty when missing.
struct DlssFiles {
    std::filesystem::path folder;     ///< Where the loader is, or where it should go
    std::filesystem::path interposer; ///< sl.interposer.dll (Streamline's loader)
    std::filesystem::path model;      ///< nvngx_dlss.dll next to the loader
    std::filesystem::path app_model;  ///< Newest DLSS the NVIDIA app downloaded
    std::string interposer_version;
    std::string model_version;
    std::string app_model_version;
};

/// Image descriptor for tagging DLSS-G resources via Streamline.
struct DlssGImage {
    VkImage image{};
    VkImageView view{};
    VkFormat format{VK_FORMAT_UNDEFINED};
    VkImageLayout layout{VK_IMAGE_LAYOUT_GENERAL};
    VkExtent2D extent{};
    VkImageUsageFlags usage{VK_IMAGE_USAGE_SAMPLED_BIT};
};

/// Per-frame resource tags and camera/motion constants for DLSS-G.
struct DlssGInputs {
    u32 frame_id{};
    VkCommandBuffer cmdbuf{};
    DlssGImage hudless_color{};
    DlssGImage ui_color_alpha{};
    DlssGImage depth{};
    DlssGImage motion_vectors{};
    VkExtent2D backbuffer_extent{};
    std::array<f32, 2> jitter{0.0f, 0.0f};
    std::array<f32, 2> motion_vector_scale{1.0f, 1.0f};
    std::array<f32, 16> camera_view_to_clip{
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    std::array<f32, 16> clip_to_camera_view{
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    std::array<f32, 16> clip_to_prev_clip{
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    std::array<f32, 16> prev_clip_to_clip{
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    std::array<f32, 3> camera_pos{0.0f, 0.0f, 0.0f};
    std::array<f32, 3> camera_up{0.0f, 1.0f, 0.0f};
    std::array<f32, 3> camera_right{1.0f, 0.0f, 0.0f};
    std::array<f32, 3> camera_fwd{0.0f, 0.0f, 1.0f};
    f32 camera_near{0.1f};
    f32 camera_far{1000.0f};
    f32 camera_fov{1.0f};
    f32 camera_aspect_ratio{16.0f / 9.0f};
    bool depth_inverted{true};
    bool camera_motion_included{true};
    bool motion_vectors_3d{false};
    bool reset{false};
};

#ifdef EGLANTINE_STREAMLINE

[[nodiscard]] DlssFiles FindDlssFiles();

/// The user folder's "streamline" folder.
[[nodiscard]] std::filesystem::path UserFolder();

/// Streamline's own modules carry a second, Streamline-only signature.
[[nodiscard]] bool IsStreamlineModule(const std::filesystem::path& path);

/// A valid Authenticode signature from NVIDIA Corporation (DLSS models).
[[nodiscard]] bool IsSignedByNvidia(const std::filesystem::path& path);

/// When the NVIDIA DLSS setting is on, loads the user's sl.interposer.dll (checking NVIDIA's
/// signature) and starts Streamline for DLSS. Call before creating the renderer's instance.
bool Start();

/// Whether Streamline is running and waiting for the renderer's device.
[[nodiscard]] bool WantsDevice();

/// Instance and device extensions DLSS asks for (empty when Streamline isn't running).
[[nodiscard]] std::vector<std::string> InstanceExtensions();
[[nodiscard]] std::vector<std::string> DeviceExtensions();

/// Hands the renderer's device to Streamline and routes the swapchain calls through it, as
/// Streamline's manual hooking needs. Stops Streamline and returns false when DLSS can't run.
bool AttachDevice(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                  u32 graphics_family, vk::DeviceDispatch& dld);

/// Whether DLSS can run on the attached device.
[[nodiscard]] bool IsDlssAvailable();

/// Whether Streamline Reflex / PCL low-latency markers are active on the attached device.
[[nodiscard]] bool IsReflexAvailable();
[[nodiscard]] bool IsPclAvailable();

/// Whether Streamline DLSS-G Multi-Frame Generation is supported / currently enabled.
[[nodiscard]] bool IsDlssGAvailable();
[[nodiscard]] bool IsDlssGEnabled();
[[nodiscard]] u32 DlssGMaxFramesToGenerate();

/// Emits a Streamline PCL latency marker (eSimulationStart/End, eRenderSubmitStart/End,
/// ePresentStart/End) for the specified frame_id when PCL/Reflex is active.
void Marker(sl::PCLMarker marker, u32 frame_id);

/// Invokes slReflexSleep for the specified frame_id when Reflex is active.
void ReflexSleep(u32 frame_id);

/// Configures DLSS-G Multi-Frame Generation (num_frames_to_generate = 1..3 for 2x/3x/4x).
bool ConfigureDLSSG(bool enabled, u32 num_frames_to_generate);

/// Sets DLSS-G options/constants and tags HUDLessColor, UIColorAndAlpha, Depth, and MotionVectors.
bool TagDLSSGInputs(const DlssGInputs& inputs);
bool TagDLSSGInputs(u32 frame_id, VkCommandBuffer cmdbuf, const DlssGImage& hudless_color,
                    const DlssGImage& ui_color_alpha, const DlssGImage& depth,
                    const DlssGImage& motion_vectors, bool reset = false);

/// Shuts Streamline down; call before the device is destroyed.
void Stop();

#else

[[nodiscard]] inline DlssFiles FindDlssFiles() {
    return {};
}
[[nodiscard]] inline std::filesystem::path UserFolder() {
    return {};
}
[[nodiscard]] inline bool IsStreamlineModule(const std::filesystem::path&) {
    return false;
}
[[nodiscard]] inline bool IsSignedByNvidia(const std::filesystem::path&) {
    return false;
}
inline bool Start() {
    return false;
}
[[nodiscard]] inline bool WantsDevice() {
    return false;
}
[[nodiscard]] inline std::vector<std::string> InstanceExtensions() {
    return {};
}
[[nodiscard]] inline std::vector<std::string> DeviceExtensions() {
    return {};
}
inline bool AttachDevice(VkInstance, VkPhysicalDevice, VkDevice, u32, vk::DeviceDispatch&) {
    return false;
}
[[nodiscard]] inline bool IsDlssAvailable() {
    return false;
}
[[nodiscard]] inline bool IsReflexAvailable() {
    return false;
}
[[nodiscard]] inline bool IsPclAvailable() {
    return false;
}
[[nodiscard]] inline bool IsDlssGAvailable() {
    return false;
}
[[nodiscard]] inline bool IsDlssGEnabled() {
    return false;
}
[[nodiscard]] inline u32 DlssGMaxFramesToGenerate() {
    return 0;
}
inline void Marker(sl::PCLMarker, u32) {}
inline void ReflexSleep(u32) {}
inline bool ConfigureDLSSG(bool, u32) {
    return false;
}
inline bool TagDLSSGInputs(const DlssGInputs&) {
    return false;
}
inline bool TagDLSSGInputs(u32, VkCommandBuffer, const DlssGImage&, const DlssGImage&,
                           const DlssGImage&, const DlssGImage&, bool = false) {
    return false;
}
inline void Stop() {}

#endif // EGLANTINE_STREAMLINE

} // namespace Vulkan::Streamline
