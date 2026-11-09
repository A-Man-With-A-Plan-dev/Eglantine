// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "common/common_types.h"
#include "common/logging.h"

// EXPERIMENT (not for nx2): GPU back-thread optimizations for Ocarina of Time.
// split_overlap is quarantined; the other optimizations are on by default.
// EDEN_NX2_PERF_OPT=0 turns every one off for the A/B; EDEN_NX2_PERF_ON=<name>,... turns an
// experimental one on; EDEN_NX2_PERF_OFF=<name>,... turns off
// only those named (Opt below). EDEN_NX2_PERF_VERIFY=1 computes each optimized answer the old
// way too, logs any difference and uses the old answer.
namespace VideoCommon::Nx2Perf {

enum class Opt : unsigned {
    Bounds,            ///< MaxContinuousRange / GetMemoryLayoutSize from map-time runs
    Method,            ///< Maxwell3D fast method dispatch on the back
    CbBatch,           ///< Consecutive constant buffer words uploaded in one write
    CleanWords,        ///< SynchronizeBuffer skips clean tracker words
    IndexRebind,       ///< Repeated vkCmdBindIndexBuffer skipped
    UsageTracker,      ///< UsageTracker::Track skips pages known to be used
    BankCheck,         ///< Native 64 KiB banks: per-frame set checked first
    SplitOverlap,      ///< Quarantined: pending writes are not a proof of safe early MME reads
    FrontSleep,        ///< The split front sleeps instead of yielding while it waits
    SubmitBudget,      ///< 4096-draw native submission budget outside MoltenVK
    FragmentBarrier,   ///< FragmentBarrier deferred to the next draw that reads the pass
    FenceFlush,        ///< 5 ms DeferFlush, command list copies submitted at once
    FramesAhead,       ///< Ocarina presents a frame ahead
    PipelineRetention, ///< Sixteen sessions of used pipelines, saved every 15 s
    WriteIntersections,
    SortedPageBatches,
    QmdScratch,
    QueryDiagnostics,
    ImmediateCallbacks,
    QueryUploadScratch,
    MacroReadEarly, ///< MME DMA reads outside GPU-written 1 MiB regions skip the full drain
    // Experimental, off until a PERF_VERIFY run shows zero mismatches: EDEN_NX2_PERF_ON=<name> turns one on.
    AcquireEarly,       ///< Semaphore acquires check the word without a full drain of the back
    MacroReadCbOverlay, ///< Early MME reads overlay queued constant buffer words instead of waiting
    MacroReadWriterSet, ///< Early MME reads wait only for entries that can write memory
    PreflushAging,      ///< Preflush marks the CPU stopped reading age out
    EagerPublish,       ///< The front publishes ring entries while the back is idle, and at writers
    FrontSameFirst,     ///< Split front: same-value state writes skipped before any other work
    FrontClassTable,    ///< Split front: executable method classes from one precomputed table
    FrontRun,           ///< Split front: a command's direct 3D register words written in one call
    MacroBatch,  ///< Front macro writes buffered in the JIT, same-value ones dropped inline
    MacroBorrow, ///< Front macros read their parameters from the command words in place
    MacroState,  ///< The front macro JIT keeps its State across calls
    DownloadBatch,      ///< Fence-time downloads: one copy per buffer, ranges <= 4 KiB apart merged
    DispatchBatch,      ///< Ampere draws dispatch the scheduler every 128 draws instead of 32
    PipelineFastCache,  ///< Direct-mapped graphics pipeline cache in front of the slow path
    DrawPreamble,       ///< GPU::TickWork skips the sync mutex when no request is queued
    BindMemo,           ///< Unchanged non-written binds skip touch, sync and usage across draws
    /// With MacroReadCbOverlay and MacroReadWriterSet: the overlay waits only for writers, not
    /// for queued proven draws, and the write tracker keeps write kinds apart
    MacroReadOverlayWriters,
    /// Split back, Ampere: submits recorded draws while the front waits for the back or the
    /// host GPU has run dry, so a command list copy waits only for the tail behind it
    SubmitWhileWaited,
    GpuCorePin,         ///< The GPU front and back get a reserved V-Cache core (EDEN_NX2_GPU_PIN)
    /// Ocarina (frames ahead): a present waits only for the previous frame, also when the GPU
    /// threads are behind
    PresentOneAhead,
    PresentGuestWait,   ///< vi2's present fence wait sleeps in the guest kernel, not on a host cv
    /// Split back: a front waiting at a non-list site (macro DMA writes, early acquire, map)
    /// sees its position consumed before the back runs its idle hook
    ReleaseBeforeHook,
    /// Semaphore acquire triggers take an early word only when it meets the value; an unmet
    /// early word falls through to the drained decision (independent of AcquireEarly)
    AcquireTriggerEarly,
    /// Pipeline eviction keeps the ones built at boot that this session has not drawn with yet
    PipelineKeepPreloaded,
    /// Split front: an executable draw-manager method that only draws is a writer only when
    /// its bound programs are not proven non-writing (MacroReadWriterSet), not always
    WriterSetExec,
    /// Noted writes keep their exact bytes; early MME DMA reads and semaphore reads wait only
    /// for noted writes to the bytes they read, not to their whole page
    ExactNotedWrites,
    /// Guest CPU cores and HostTiming run on the non-V-Cache CCD; the GPU threads keep the
    /// V-Cache CCD (asymmetric L3 with V-Cache pinning only)
    GuestCcd,
    /// Native 64 KiB uniform banks: guest bytes compared with the shadow on the frame's first
    /// bind only, not on every draw (a later CPU rewrite in the frame shows one frame late)
    BankOncePerFrame,
    /// The host syncpoint value rises only once everything recorded before the increment
    /// (downloads, query results, fence operations) has landed in guest memory; vi2's early
    /// returns on the frames-ahead path wait for the previous frame's landed value
    LandedPrevAlways,
    /// With PresentOneAhead: a present that skips this frame's wait first waits for the
    /// previous frame's landed (host) syncpoint value
    OneAheadLanded,
    /// Split front: queued constant buffer words, noted writes and draw epochs in preallocated
    /// rings instead of std::deque (one heap block per element on MSVC)
    SplitRings,
    /// With BindMemo: a memo hit is decided by the bound range's own CPU-modified bits and a
    /// buffer create/delete epoch, not by the global tracker epochs; a new scheduler tick only
    /// redoes the usage mark
    BindMemoLocal,
    /// GPUDirtyMemoryManager::Gather returns without its locks when nothing was collected
    DirtyGatherFast,
    /// Texture handles of one draw stage read through one translated constant buffer page,
    /// not a GPU page walk per handle
    HandlePageCache,
    /// Split front: a native macro call whose MME DMA read only feeds its last writes (render
    /// enable) runs without waiting for the back; the back writes those from the real words
    DmaForward,
    /// Guest-visible syncpoint values rise on the fencing thread with the host value, once
    /// everything recorded before the increment has landed in guest memory
    GuestLandedSyncpoints,
    /// Measurement only: after vi2's Ocarina reply, write-protects CPU-written GPU pages and the
    /// native uniform bank pages to time the guest's first access of them (nx2_war_probe.h)
    WarGuardProbe,
    /// Split front: an instance loop's draw.begin right after the same one, still unpublished,
    /// is counted into that ring entry (bits 24-31); the back adds the instances
    DrawBeginCoalesce,
    /// Split back: its idle hook submits deferred work only when the back sleeps, someone asked,
    /// the front waits for GPU results, or the deferred submit is older than
    /// EDEN_NX2_HOOK_FLUSH_US (default 1000), not at every catch-up
    HookLazyFlush,
    /// With frames_ahead: vi2's early reply opens an epoch for this frame's fence; the guest's
    /// first write of a page the back may still read for it waits for that fence (nx2_war_probe.h)
    InflightWriteWait,
    /// With frames_ahead: while the epoch is open, a guest read served from a preflush block whose
    /// last GPU writer is still in flight waits for that writer's host syncpoint
    InflightReadExact,
    /// Measurement only: per-syncpoint stamps of the front's parse of a fence increment, the
    /// back's increment and the back's idle time, reported by vi2's present (FenceTimeline)
    FenceTimeline,
    /// With DmaForward: a forwarded macro call's dry-run plan is cached by code generation, start,
    /// parameters and the reads it made; the ring entry names a shared code slot, not a code copy
    DmaForwardMemo,
    /// Measurement only: the device pages the split back reads per frame, which of them the guest
    /// rewrites before the next present, and how early in the frame the writers of preflushed
    /// blocks the guest reads ran (Nx2Ledger in nx2_war_probe.h)
    RunAheadLedger,
    /// Split front: a compute call whose dispatches' storage buffer writes the front can place
    /// (back-published program records, final QMD and descriptor words) notes those ranges; a
    /// command list fetch waits only for the writers that can touch its pages
    ComputeWriterRanges,
    /// Split front: a semaphore acquire is met from the payload of a release still queued in
    /// the ring when no entry queued after it can write the word's 16 bytes (no drain)
    AcquireQueuedRelease,
    /// With MacroReadWriterSet: the back skips the non-writer proof lookup when the draw's
    /// programs, program epoch and the set's version repeat its last proven draw
    NonwriterProofMemo,
    /// Ocarina runs a frame ahead with the inflight write wait and exact reads; a wait escapes
    /// only when the trapped write may be a semaphore release a suspended channel waits on, or
    /// when nothing progresses for 20 ms while a channel is suspended (nx2_war_probe.h)
    InflightWaitV2,
    /// Split front: a command list fetch from pages no GPU work wrote waits only for the work
    /// before the last host-level sync, not every queued writer (the PBDMA fetches ahead of the
    /// engines); a back-side detector falls back to the full wait if the skip ever mattered
    ListFetchAfterSync,
    /// Split: a front wait spins (EDEN_NX2_SPIN_HANDOFF_US, default 300) while the back works
    /// instead of sleeping; the idle back spins for the next publish while the GPU thread parses
    SpinHandoff,
    /// Measurement only, with RunAheadLedger: K-frame unions of the back's read pages, tracker
    /// state of rewritten pages and the simulated run-ahead RAW stall per wait rule
    RunAheadCover,
    /// Measurement only, with RunAheadLedger: under a forced one-ahead reply, the guest's
    /// first-write traps of every device-mapped page (the CoW budget) and the bytes the front
    /// and back read after the reply that differ from the reply-time bytes (Nx2Cow)
    RunAheadCowProbe,
    /// Split front: an acquire met early (acquire_trigger_early) creates no sync point for list
    /// fetches; implies the acquire_trigger_early and list_fetch_after_sync paths
    SyncFreeFetch,
    /// A suspended channel's 1 ms front sleep ends at once on a new submit or on a GPU release
    /// that latches a waiter (Tegra::Nx2PendingWake in nx2.h). On by default (round 9: +1.5 FPS)
    PendingWake,
    /// Native uniform banks: per-page 64-bit hashes of the guest bytes replace the 64 KiB byte
    /// shadow's memcmp/memcpy (nx2_page_hash.h)
    BankHashShadow,
    /// Measurement only, with RunAheadCowProbe: guest reads of preflushed blocks under a real
    /// one-ahead reply (writer known or not yet, covered by a guest wait, stale once it lands)
    /// and the class of each unarmed_changed page (Nx2Raw in nx2_war_probe.h)
    RunAheadRawProbe,
    /// Split back: the idle hook after a list-writer wait submits only the list copies, leaving
    /// other deferred work to DeferFlush's window (IdleHookReason::WaitLists)
    ListWriterHookCopiesOnly,
    /// Split front: a native macro call whose code uses no data RAM and no MME DMA replays the
    /// emits recorded for the same start, parameters and register reads (front_macro_memo.h)
    FrontMacroMemo,
    /// Measurement only, with RunAheadCowProbe: a reply-time snapshot of the last 16 frames' read
    /// pages plus dirty unprotected tracker pages; classifies the cow probe's hazard bytes
    RunAheadSnapshotProbe,
    /// Lab diagnostics (EDEN_NX2_DIAG*): the back counts draws in a thread-local table with
    /// cached ablate/inspect answers and folds them into the shared state under one lock.
    /// On by default (round 11: +1.0 FPS in the lab, which runs with EDEN_NX2_DIAG)
    DiagDrawLite,
    /// Split back: a report a forwarded 0x16 call waited on before is covered by a fence submitted
    /// at the next draw boundary; the next wait for it waits on that fence instead of a new one
    ReportSubmitEarly,
    /// With DmaForward: the back evaluates a short tape of the DMA-word arithmetic, recorded by
    /// the dry run, instead of re-running the macro interpreter for a forwarded call
    DmaForwardTape,
    /// Measurement only, with RunAheadSnapshotProbe: the reply-time copy set without the arm set
    /// (unprotected union pages, timed copy), trap coverage of the arm set, span-read hazard bytes
    /// and the front_macro residual's page category
    RunAheadOverlayBudget,
    /// Split front: a trigger or polled acquire waits only until the back consumed everything
    /// sent (its idle hook runs before that), not for Drain's back_quiet; unmet or flushed
    /// words take the full drain
    AcquireDrainLite,
    /// Split front: executable writes the back would run as a no-op are left out (store-only
    /// registers holding the value, constant buffer binds repeating the last one sent)
    ExecSameDrop,
    /// Split front: a 3D state write whose register has an unpublished single-register entry
    /// since the last executable entry is folded into that entry (its value patched)
    StateDeadStore,
    /// Measurement only, with RunAheadOverlayBudget: records the byte ranges the emulated GPU
    /// writes (fencing thread, split back, query results, downloads) and splits the front_macro
    /// residual and unarmed_changed pages into GPU-own and guest writes
    RunAheadOwnWriteProbe,
    /// Native uniform banks: a page is compared only when a write hook, a trap of its
    /// write-protected guest page or the tracker marked it since its last compare (nx2_bank_dirty.h)
    BankDirtySkip,
    /// Split back: a run of plain 3D method entries goes through Maxwell3D::BackRun (one tight
    /// loop) instead of one Execute per entry; it stops where BackLoop would release the front
    BackReplayLean,
    /// Split front: a 3D macro call whose words are all in the entry goes from its header to
    /// FrontRunMacro in one call (Maxwell3D::FrontCallMacro), its parameters borrowed in place
    FrontMacroCall,
    /// Async shaders: once a frame spent its budget building pipelines at run time, a draw that
    /// may be skipped while its pipeline builds is skipped without building it (built next frame)
    ShaderBuildBudget,
    /// Stale-slot compute rebuilds keep dispatching the previous pipeline while the replacement
    /// compiles on a worker thread, swapping it in once IsBuilt() is true
    ComputeRebuildAsync,
    /// Boot disk-cache loading compiles every persisted compute translation with a distinct
    /// MappedStoragePointerKeys and swaps to a built alternate on a stale-slot trigger
    ComputePreloadVariants,
    /// Async shaders: skip a small no-depth draw instead of waiting when its color target already
    /// holds an earlier frame's output and was not fully cleared this frame; treat indirect draws
    /// (draw_element_count == 0) as not small
    StaleTargetSkip,
    /// Async shaders: when a depth-tested color draw is skipped, record its geometry identity and
    /// skip depth-only draws for the same geometry for up to 2 frames or until the material
    /// pipeline is built
    GeometryConsistency,
    /// Async shaders: reserve half the per-frame shader build budget for G-buffer draws and the
    /// highest-priority deferred keys from the previous frame (G-buffer first, then by draw count)
    BuildOrderPriority,
    /// Async shaders: keep FileEnvironments of graphics pipelines skipped at boot and replay their
    /// translation and compilation on background workers on first skippable use
    ReplayDiskRecordings,
    /// Vulkan staging buffer pool: place <= 1 MiB upload staging buffers in PCIe ReBAR
    /// (device-local, host-visible, coherent memory) when a >= 1 GiB ReBAR heap is present
    RebarUpload,
    /// Maxwell3D split front: 64-byte (16-word) single-cache-line micro-cache for FrontDmaRead
    /// within a single macro invocation
    SuperMacro,
    /// Run-ahead page snapshot overlay: serve pre-boundary front macro and back reads from the
    /// reply-time page snapshot pool with GPU-own-write forwarding
    RunAheadOverlay,
    /// NVIDIA Streamline Reflex: low-latency mode and per-frame slReflexSleep pacing
    ReflexSleep,
    /// Vulkan present thread: fractional VRR multi-frame cadence hold for 2x/3x/4x generated
    /// frames when hardware flip-metering (DLSS-G) is not active
    VrrFramePace,
    /// NVIDIA Streamline DLSS: replace game DLSS / FSR 3.1 and Ocarina of Time post-tonemap
    /// anti-aliasing with Streamline DLSS Super Resolution / DLAA
    DlssUpscale,
    Count,
};

/// Bit i of an off-mask is opt_names[i]: set means off. Bits 0-63 in lo, 64-127 in hi.
struct OffMask {
    u64 lo{};
    u64 hi{};

    constexpr OffMask operator|(const OffMask& other) const {
        return {lo | other.lo, hi | other.hi};
    }
    constexpr OffMask operator&(const OffMask& other) const {
        return {lo & other.lo, hi & other.hi};
    }
    constexpr OffMask operator~() const {
        return {~lo, ~hi};
    }
    constexpr bool operator==(const OffMask&) const = default;
    constexpr OffMask& operator|=(const OffMask& other) {
        lo |= other.lo;
        hi |= other.hi;
        return *this;
    }
    constexpr OffMask& operator&=(const OffMask& other) {
        lo &= other.lo;
        hi &= other.hi;
        return *this;
    }
};

constexpr OffMask Bit(Opt opt) {
    const unsigned index = static_cast<unsigned>(opt);
    return index < 64 ? OffMask{u64{1} << index, 0} : OffMask{0, u64{1} << (index & 63)};
}

constexpr bool IsOff(const OffMask& mask, size_t index) {
    return (((index < 64 ? mask.lo : mask.hi) >> (index & 63)) & 1) != 0;
}

inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("EDEN_NX2_PERF_OPT");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

/// One control file: the whole arm, computed from the startup off-mask.
struct ControlResult {
    OffMask mask;
    std::string label;
    OffMask on_vs_startup;
    OffMask off_vs_startup;
    std::string ignored; ///< Names not live-safe or unknown and unknown directives, comma list
};
ControlResult ParseControl(const OffMask& startup_mask, std::string_view text,
                           const OffMask& live_safe_mask);
/// "set=<label> on=<names|-> off=<names|-> ignored=<names|-> mask=0x<32 hex, hi then lo>"
std::string FormatControl(const ControlResult& result, const OffMask& effective_mask);

/// Startup off-mask (after env vars, before any live flips).
OffMask StartupOffMask();
/// Current live off-mask in effect right now.
OffMask CurrentOffMask();
/// Overwrites the live off-mask in effect for all switches (0..Opt::Count-1) and logs the flip.
void SetOffMask(const OffMask& mask, std::string_view label = "gui");
/// Flips a single switch on or off live at runtime and logs the change.
void Set(Opt opt, bool on, std::string_view label = "gui");

namespace detail {
/// Off-mask in effect, lo then hi (bit set = off; every switch when EDEN_NX2_PERF_OPT=0); both
/// words ~0 before the first On(). The live poller stores hi then lo.
inline std::array<std::atomic<u64>, 2> live_off_mask{{~u64{0}, ~u64{0}}};
OffMask InitOffMask();
} // namespace detail

inline bool On(Opt opt) {
    const size_t index = static_cast<size_t>(opt);
    u64 off = detail::live_off_mask[index >> 6].load(std::memory_order_relaxed);
    if (off == ~u64{0}) [[unlikely]] {
        // Possibly the sentinel (both words ~0). InitOffMask waits for an initialization still
        // running elsewhere and returns the live pair, so a half-stored pair is never trusted.
        const OffMask mask = detail::InitOffMask();
        off = index < 64 ? mask.lo : mask.hi;
    }
    return ((off >> (index & 63)) & 1) == 0;
}

inline bool Verify() {
    static const bool verify = [] {
        const char* value = std::getenv("EDEN_NX2_PERF_VERIFY");
        return value != nullptr && value[0] == '1';
    }();
    return verify;
}

/// landed_prev_always / one_ahead_landed / guest_landed_syncpoints: the host syncpoint value
/// rises on the fencing thread, after everything recorded before it has landed. Also under
/// EDEN_NX2_PERF_VERIFY, so vi2 can report presents that return before the previous frame landed.
inline bool LandedSyncpoints() {
    return On(Opt::LandedPrevAlways) || On(Opt::OneAheadLanded) ||
           On(Opt::GuestLandedSyncpoints) || Verify();
}

/// guest_landed_syncpoints, outside Verify: the guest value rises with the landed host value.
inline bool GuestLandedSyncpoints() {
    return On(Opt::GuestLandedSyncpoints) && !Verify();
}

/// guest_landed_syncpoints: counts the fence manager's forced submits (deferred work sent so a
/// guest value can land) and logs their rate about once a second.
void NoteLandedForcedSubmit();

/// Where the guest observes a syncpoint value.
enum class GuestSite : u32 { QueryCounter, RoPage, SlotEvent, Vi2Wait, Count };

namespace detail {
/// EDEN_NX2_PERF_VERIFY: syncpoints the fence manager has signalled, a bit per id (192 ids).
inline std::array<std::atomic<u64>, 3> fence_syncpoints{};
} // namespace detail

/// EDEN_NX2_PERF_VERIFY: the fence manager signals syncpoint `id` (< 192).
inline void MarkFenceSyncpoint(u32 id) {
    std::atomic<u64>& word = detail::fence_syncpoints[id / 64];
    const u64 bit = u64{1} << (id % 64);
    if ((word.load(std::memory_order_relaxed) & bit) == 0) {
        word.fetch_or(bit, std::memory_order_relaxed);
    }
}

/// EDEN_NX2_PERF_VERIFY for guest_landed_syncpoints: the guest observed syncpoint `id` at
/// `value` while its landed (host) value was `host`. Logs the first 64 values seen before they
/// landed, then per-second counts. Skips syncpoints the fence manager never signalled (only the
/// CPU raises those; their host value never moves).
void NoteGuestObservation(GuestSite site, u32 id, u32 value, u32 host);

/// fence_timeline (measurement only): when the front parsed each syncpoint increment, when the
/// back raised the guest value, when the front last popped a command list, and how long the
/// back sat idle. Relaxed stores only; vi2's present matches them to its fence wait.
namespace FenceTimeline {
inline constexpr u32 NumIds = 192;
inline constexpr u32 Depth = 8; ///< Increments of one id remembered (several per frame)

/// The last Depth stamps of one syncpoint id. One writer thread per ring.
struct Ring {
    std::array<std::atomic<s64>, Depth> ns{};
    std::array<std::atomic<u32>, Depth> seq{};   ///< Ordinal of the increment (0 = empty)
    std::array<std::atomic<u32>, Depth> value{}; ///< Back: guest value after the increment
    std::atomic<u32> count{};
};

inline std::array<Ring, NumIds> front_parse{};
inline std::array<Ring, NumIds> back_incr{};
inline std::atomic<s64> last_submit_pop_ns{};
inline std::atomic<s64> back_idle_ns_total{}; ///< Read as a difference
inline std::atomic<s64> back_idle_since_ns{}; ///< 0 while the back works
inline std::atomic<s64> guest_wait_entered_ns{}; ///< vi2's fence wait in progress since (0: none)

inline bool On() {
    return Nx2Perf::On(Opt::FenceTimeline);
}

inline s64 NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline void Stamp(Ring& ring, s64 ns, u32 value) {
    const u32 n = ring.count.load(std::memory_order_relaxed) + 1;
    ring.count.store(n, std::memory_order_relaxed);
    const u32 slot = n % Depth;
    ring.seq[slot].store(0, std::memory_order_relaxed);
    ring.ns[slot].store(ns, std::memory_order_relaxed);
    ring.value[slot].store(value, std::memory_order_relaxed);
    ring.seq[slot].store(n, std::memory_order_relaxed);
}

/// The front parsed an increment of syncpoint `id`.
inline void StampFront(u32 id) {
    if (id < NumIds) {
        Stamp(front_parse[id], NowNs(), 0);
    }
}

/// The back raised syncpoint `id`'s guest value to `value`.
inline void StampBack(u32 id, u32 value) {
    if (id < NumIds) {
        Stamp(back_incr[id], NowNs(), value);
    }
}

inline void BeginBackIdle() {
    back_idle_since_ns.store(NowNs(), std::memory_order_relaxed);
}

inline void EndBackIdle() {
    const s64 since = back_idle_since_ns.load(std::memory_order_relaxed);
    if (since != 0) {
        back_idle_ns_total.store(back_idle_ns_total.load(std::memory_order_relaxed) + NowNs() -
                                     since,
                                 std::memory_order_relaxed);
        back_idle_since_ns.store(0, std::memory_order_relaxed);
    }
}

/// The back's idle total at `now`, counting an idle stretch still running.
inline s64 BackIdleAt(s64 now) {
    const s64 since = back_idle_since_ns.load(std::memory_order_relaxed);
    const s64 total = back_idle_ns_total.load(std::memory_order_relaxed);
    return total + (since != 0 && now > since ? now - since : 0);
}

/// Stamps of the increment that raised `id` to `value`: false if no remembered one did.
inline bool Find(u32 id, u32 value, s64& front_ns, s64& back_ns) {
    if (id >= NumIds) {
        return false;
    }
    const Ring& back = back_incr[id];
    for (u32 slot = 0; slot < Depth; ++slot) {
        const u32 n = back.seq[slot].load(std::memory_order_relaxed);
        if (n == 0 || back.value[slot].load(std::memory_order_relaxed) != value) {
            continue;
        }
        back_ns = back.ns[slot].load(std::memory_order_relaxed);
        const Ring& front = front_parse[id];
        const u32 front_slot = n % Depth;
        if (front.seq[front_slot].load(std::memory_order_relaxed) != n) {
            return false;
        }
        front_ns = front.ns[front_slot].load(std::memory_order_relaxed);
        return true;
    }
    return false;
}
} // namespace FenceTimeline

/// exec_same_drop: the last constant buffer bind the split front sent for each shader stage,
/// with what the back's bind reads (the CB address and size registers) and what may change its
/// translation (a queued function, the GPU map serial).
struct ExecBindMemo {
    struct Entry {
        u32 word;
        u32 address_high;
        u32 address_low;
        u32 size;
        u64 function_position;
        u64 map_serial;
        bool valid;
    };
    static constexpr size_t NumStages = 5;
    std::array<Entry, NumStages> stages{};

    /// Whether binding `word` on `stage` repeats the last bind sent there.
    [[nodiscard]] bool Same(size_t stage, u32 word, u32 address_high, u32 address_low, u32 size,
                            u64 function_position, u64 map_serial) const {
        const Entry& entry = stages[stage];
        return entry.valid && entry.word == word && entry.address_high == address_high &&
               entry.address_low == address_low && entry.size == size &&
               entry.function_position == function_position && entry.map_serial == map_serial;
    }
    void Record(size_t stage, u32 word, u32 address_high, u32 address_low, u32 size,
                u64 function_position, u64 map_serial) {
        stages[stage] = {word, address_high, address_low, size, function_position, map_serial,
                         true};
    }
    void Forget(size_t stage) {
        stages[stage].valid = false;
    }
    void Clear() {
        for (Entry& entry : stages) {
            entry.valid = false;
        }
    }
};

} // namespace VideoCommon::Nx2Perf
