// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// The switch table: names, default and live-safe masks and the env parser. Only nx2_perf.cpp,
// the switch GUI and the tests include it, so a default flip or a note here rebuilds those
// alone; every On() call site includes nx2_perf.h, which holds just the enum and the hot check.

#include <array>
#include <cstddef>
#include <string_view>

#include "common/common_types.h"
#include "common/logging.h"
#include "video_core/nx2_perf.h"

namespace VideoCommon::Nx2Perf {

inline constexpr std::array<std::string_view, static_cast<size_t>(Opt::Count)> opt_names{
    "bounds",         "method",          "cb_batch",    "clean_words",   "index_rebind",
    "usage_tracker",  "bank_check",      "split_overlap", "front_sleep", "submit_budget",
    "fragment_barrier", "fence_flush",   "frames_ahead", "pipeline_retention",
    "write_intersections", "sorted_page_batches",
    "qmd_scratch", "query_diagnostics", "immediate_callbacks", "query_upload_scratch",
    "macro_read_early",
    "acquire_early", "macro_read_cb_overlay", "macro_read_writer_set", "preflush_aging",
    "eager_publish",
    "front_same_first", "front_class_table", "front_run",
    "macro_batch", "macro_borrow", "macro_state",
    "download_batch",
    "dispatch_batch", "pipeline_fast_cache", "draw_preamble", "bind_memo",
    "macro_read_overlay_writers",
    "submit_while_waited",
    "gpu_core_pin",
    "present_one_ahead", "present_guest_wait", "release_before_hook", "acquire_trigger_early",
    "pipeline_keep_preloaded",
    "writer_set_exec",
    "exact_noted_writes",
    "guest_ccd",
    "bank_once_per_frame",
    "landed_prev_always", "one_ahead_landed",
    "split_rings",
    "bind_memo_local",
    "dirty_gather_fast",
    "handle_page_cache",
    "dma_forward",
    "guest_landed_syncpoints",
    "war_guard_probe",
    "draw_begin_coalesce",
    "hook_lazy_flush",
    "inflight_write_wait", "inflight_read_exact",
    "fence_timeline",
    "dma_forward_memo",
    "run_ahead_ledger",
    "compute_writer_ranges",
    "acquire_queued_release",
    "nonwriter_proof_memo",
    "inflight_wait_v2",
    "list_fetch_after_sync",
    "spin_handoff",
    "run_ahead_cover",
    "run_ahead_cow_probe",
    "sync_free_fetch",
    "pending_wake",
    "bank_hash_shadow",
    "run_ahead_raw_probe",
    "list_writer_hook_copies_only",
    "front_macro_memo",
    "run_ahead_snapshot_probe",
    "diag_draw_lite",
    "report_submit_early",
    "dma_forward_tape",
    "run_ahead_overlay_budget",
    "acquire_drain_lite",
    "exec_same_drop",
    "state_dead_store",
    "run_ahead_own_write_probe",
    "bank_dirty_skip",
    "back_replay_lean",
    "front_macro_call",
    "shader_build_budget",
    "compute_rebuild_async",
    "compute_preload_variants",
    "stale_target_skip",
    "geometry_consistency",
    "build_order_priority",
    "replay_disk_recordings",
    "rebar_upload",
    "super_macro",
    "run_ahead_overlay",
    "reflex_sleep",
    "vrr_frame_pace",
    "dlss_upscale",
};

static_assert(static_cast<size_t>(Opt::Count) <= 128, "the off-mask is 128 bits");
static_assert(static_cast<unsigned>(Opt::DispatchBatch) == 33 &&
              static_cast<unsigned>(Opt::BindMemo) == 36 &&
              static_cast<unsigned>(Opt::WriterSetExec) == 45 &&
              static_cast<unsigned>(Opt::ExactNotedWrites) == 46 &&
              static_cast<unsigned>(Opt::GuestCcd) == 47 &&
              static_cast<unsigned>(Opt::BankOncePerFrame) == 48 &&
              static_cast<unsigned>(Opt::LandedPrevAlways) == 49 &&
              static_cast<unsigned>(Opt::OneAheadLanded) == 50 &&
              static_cast<unsigned>(Opt::SplitRings) == 51 &&
              static_cast<unsigned>(Opt::BindMemoLocal) == 52 &&
              static_cast<unsigned>(Opt::WarGuardProbe) == 57);

/// Off unless EDEN_NX2_PERF_ON names them: quarantined or not yet verified.
inline constexpr OffMask DefaultOffMask() {
    OffMask mask{};
    for (const Opt opt : {Opt::SplitOverlap, Opt::AcquireEarly, Opt::MacroReadCbOverlay,
                          Opt::MacroReadWriterSet, Opt::PreflushAging, Opt::EagerPublish,
                          Opt::FrontSameFirst, Opt::FrontClassTable, Opt::FrontRun,
                          Opt::MacroBatch, Opt::MacroBorrow, Opt::MacroState, Opt::DownloadBatch,
                          Opt::DispatchBatch, Opt::PipelineFastCache, Opt::DrawPreamble,
                          Opt::BindMemo, Opt::MacroReadOverlayWriters,
                          Opt::SubmitWhileWaited, Opt::GpuCorePin, Opt::PresentOneAhead,
                          Opt::PresentGuestWait,
                          Opt::ReleaseBeforeHook,
                          Opt::AcquireTriggerEarly,
                          Opt::WriterSetExec, Opt::ExactNotedWrites, Opt::GuestCcd, Opt::DmaForward,
                          Opt::HookLazyFlush, Opt::DmaForwardMemo,
                          Opt::BankOncePerFrame, Opt::LandedPrevAlways, Opt::OneAheadLanded,
                          Opt::SplitRings, Opt::BindMemoLocal, Opt::GuestLandedSyncpoints,
                          Opt::WarGuardProbe, Opt::DrawBeginCoalesce, Opt::InflightWriteWait,
                          Opt::InflightReadExact,
                          Opt::FenceTimeline, Opt::RunAheadLedger, Opt::ComputeWriterRanges, Opt::AcquireQueuedRelease, Opt::NonwriterProofMemo,
                          Opt::InflightWaitV2,
                          Opt::ListFetchAfterSync,
                          Opt::SpinHandoff,
                          Opt::RunAheadCover,
                          Opt::RunAheadCowProbe,
                          Opt::SyncFreeFetch,
                          Opt::PendingWake,
                          Opt::BankHashShadow,
                          Opt::RunAheadRawProbe,
                          Opt::ListWriterHookCopiesOnly,
                          Opt::FrontMacroMemo,
                          Opt::RunAheadSnapshotProbe,
                          Opt::DiagDrawLite,
                          Opt::ReportSubmitEarly,
                          Opt::DmaForwardTape,
                          Opt::RunAheadOverlayBudget,
                          Opt::AcquireDrainLite,
                          Opt::ExecSameDrop,
                          Opt::StateDeadStore,
                          Opt::RunAheadOwnWriteProbe,
                          Opt::BankDirtySkip,
                          Opt::BackReplayLean,
                          Opt::FrontMacroCall,
                          Opt::SuperMacro,
                          Opt::RunAheadOverlay}) {
        mask |= Bit(opt);
    }
    return mask;
}

/// Off unless named in EDEN_NX2_PERF_ON: split_overlap (quarantined); acquire_early (its early
/// reads see "not met" where a drain sees "met", so channels wait up to 2 ms longer);
/// dispatch_batch (delays GPU work; suspected of the flashes, no verify mode);
/// macro_read_overlay_writers (+0.3 FPS, noise); gpu_core_pin (pair -2.3 FPS, separate +0.4);
/// present_guest_wait (+10 FPS in Ocarina of Time: core 3's game threads run during the present
/// wait, which naps in the guest kernel for up to 50 ms before the host wake-up; Mario Kart World
/// with it ran its workers ahead of the GPU thread: a semaphore stall, then a corrupt render
/// target format, so it stays opt-in until the front's pushbuffer consumption guards it);
/// present_one_ahead (+9 FPS, but the
/// guest rewrites constants the back has not read yet: incessant brightness flashes in play);
/// frames_ahead (same hazard: the lab's flash check sees 25-40% of frames at another brightness
/// with it on, 0 with it off; ~5 ms/frame slower at the Kokiri spawn without it);
/// release_before_hook (+0.0 FPS);
/// acquire_trigger_early (+0.2 FPS); writer_set_exec,
/// exact_noted_writes, guest_ccd, landed_prev_always, one_ahead_landed, split_rings,
/// bind_memo_local, guest_landed_syncpoints (+0.9 FPS), hook_lazy_flush (+0.9 FPS);
/// war_guard_probe (measurement, adds page traps); inflight_write_wait and inflight_read_exact
/// (timing change: flash check and a user play test first); fence_timeline (measurement); dma_forward_memo (unmeasured);
/// run_ahead_ledger (measurement); compute_writer_ranges (-2.5 FPS: many short list-writer waits),
/// acquire_queued_release (no gain, ~46 acquires/s forwarded), nonwriter_proof_memo (+0.3 FPS,
/// round 7); inflight_wait_v2 (verify: bank pages changed before the fence, 16 reads differ),
/// list_fetch_after_sync (+0.2 FPS), spin_handoff (+0.3 FPS), run_ahead_cover (measurement; round 8),
/// run_ahead_cow_probe (measurement, adds page traps; round 9), sync_free_fetch (-0.5 FPS),
/// bank_hash_shadow (-0.3 FPS; round 9), run_ahead_raw_probe (measurement; round 10),
/// list_writer_hook_copies_only (submit timing; round 10),
/// front_macro_memo (unmeasured; round 10), run_ahead_snapshot_probe (measurement; round 11),
/// report_submit_early (submit timing; round 11),
/// dma_forward_tape (+0.35 FPS; round 11), run_ahead_overlay_budget (measurement; round 12),
/// acquire_drain_lite (-0.2 FPS; round 12), exec_same_drop (~4.7k binds/s left out; round 12),
/// state_dead_store (~950 folds/s; round 12), run_ahead_own_write_probe (measurement; round 13),
/// bank_dirty_skip (-0.8 FPS; round 13), back_replay_lean (-0.2 FPS; round 13),
/// front_macro_call (-0.1 FPS; round 13).
/// dma_forward and draw_begin_coalesce (round 5), pending_wake (round 9) and diag_draw_lite
/// (round 11) are on by default; SAFE turns them off.
inline constexpr OffMask always_off = Bit(Opt::SplitOverlap) | Bit(Opt::AcquireEarly) |
                                  Bit(Opt::DispatchBatch) | Bit(Opt::MacroReadOverlayWriters) |
                                  Bit(Opt::GpuCorePin) | Bit(Opt::PresentGuestWait) |
                                  Bit(Opt::PresentOneAhead) | Bit(Opt::FramesAhead) |
                                  Bit(Opt::ReleaseBeforeHook) |
                                  Bit(Opt::AcquireTriggerEarly) |
                                  Bit(Opt::WriterSetExec) |
                                  Bit(Opt::ExactNotedWrites) | Bit(Opt::GuestCcd) |
                                  Bit(Opt::HookLazyFlush) |
                                  Bit(Opt::LandedPrevAlways) | Bit(Opt::OneAheadLanded) |
                                  Bit(Opt::SplitRings) | Bit(Opt::BindMemoLocal) |
                                  Bit(Opt::GuestLandedSyncpoints) |
                                  Bit(Opt::WarGuardProbe) | Bit(Opt::InflightWriteWait) |
                                  Bit(Opt::InflightReadExact) |
                                  Bit(Opt::FenceTimeline) | Bit(Opt::DmaForwardMemo) |
                                  Bit(Opt::RunAheadLedger) |
                                  Bit(Opt::ComputeWriterRanges) |
                                  Bit(Opt::AcquireQueuedRelease) |
                                  Bit(Opt::NonwriterProofMemo) |
                                  Bit(Opt::InflightWaitV2) |
                                  Bit(Opt::ListFetchAfterSync) |
                                  Bit(Opt::SpinHandoff) |
                                  Bit(Opt::RunAheadCover) |
                                  Bit(Opt::RunAheadCowProbe) |
                                  Bit(Opt::SyncFreeFetch) |
                                  Bit(Opt::BankHashShadow) |
                                  Bit(Opt::RunAheadRawProbe) |
                                  Bit(Opt::ListWriterHookCopiesOnly) |
                                  Bit(Opt::FrontMacroMemo) |
                                  Bit(Opt::RunAheadSnapshotProbe) |
                                  Bit(Opt::ReportSubmitEarly) |
                                  Bit(Opt::DmaForwardTape) |
                                  Bit(Opt::RunAheadOverlayBudget) |
                                  Bit(Opt::AcquireDrainLite) |
                                  Bit(Opt::ExecSameDrop) |
                                  Bit(Opt::StateDeadStore) |
                                  Bit(Opt::RunAheadOwnWriteProbe) |
                                  Bit(Opt::BankDirtySkip) |
                                  Bit(Opt::BackReplayLean) |
                                  Bit(Opt::FrontMacroCall) |
                                  Bit(Opt::SuperMacro) |
                                  Bit(Opt::RunAheadOverlay);

static_assert(IsOff(always_off, 33));
static_assert(static_cast<unsigned>(Opt::PipelineKeepPreloaded) == 44 && !IsOff(always_off, 44) &&
              !IsOff(DefaultOffMask(), 44));
static_assert(IsOff(always_off, 45) && IsOff(DefaultOffMask(), 45));
static_assert(IsOff(DefaultOffMask(), 36));
static_assert(!IsOff(DefaultOffMask(), 0));
static_assert(IsOff(always_off, 49) && IsOff(DefaultOffMask(), 49) && IsOff(always_off, 50) &&
              IsOff(DefaultOffMask(), 50));
static_assert(IsOff(always_off, 47) && IsOff(DefaultOffMask(), 47));
// bank_once_per_frame is on by default; SAFE turns it off (suspected of the flashes).
static_assert(!IsOff(always_off, 48) && IsOff(DefaultOffMask(), 48));
static_assert(IsOff(always_off, 51) && IsOff(DefaultOffMask(), 51));
static_assert(IsOff(always_off, 52) && IsOff(DefaultOffMask(), 52));
static_assert(static_cast<unsigned>(Opt::GuestLandedSyncpoints) == 56 && IsOff(always_off, 56) &&
              IsOff(DefaultOffMask(), 56));
static_assert(static_cast<unsigned>(Opt::WarGuardProbe) == 57 && IsOff(always_off, 57) &&
              IsOff(DefaultOffMask(), 57));
static_assert(static_cast<unsigned>(Opt::DrawBeginCoalesce) == 58 && !IsOff(always_off, 58) &&
              IsOff(DefaultOffMask(), 58));
static_assert(IsOff(always_off, static_cast<size_t>(Opt::FenceTimeline)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::FenceTimeline)));
static_assert(!IsOff(always_off, static_cast<size_t>(Opt::DmaForward)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::DmaForward)));
static_assert(IsOff(always_off, static_cast<size_t>(Opt::DmaForwardMemo)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::DmaForwardMemo)));
static_assert(IsOff(always_off, static_cast<size_t>(Opt::HookLazyFlush)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::HookLazyFlush)));
static_assert(IsOff(always_off, static_cast<size_t>(Opt::InflightWriteWait)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::InflightWriteWait)) &&
              IsOff(always_off, static_cast<size_t>(Opt::InflightReadExact)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::InflightReadExact)));
static_assert(static_cast<unsigned>(Opt::RunAheadLedger) == 64 &&
              IsOff(always_off, static_cast<size_t>(Opt::RunAheadLedger)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::RunAheadLedger)));

static_assert(static_cast<unsigned>(Opt::ComputeWriterRanges) == 65 &&
              IsOff(always_off, static_cast<size_t>(Opt::ComputeWriterRanges)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::ComputeWriterRanges)));

static_assert(static_cast<unsigned>(Opt::AcquireQueuedRelease) == 66 &&
              IsOff(always_off, static_cast<size_t>(Opt::AcquireQueuedRelease)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::AcquireQueuedRelease)));

static_assert(static_cast<unsigned>(Opt::NonwriterProofMemo) == 67 &&
              IsOff(always_off, static_cast<size_t>(Opt::NonwriterProofMemo)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::NonwriterProofMemo)));

static_assert(static_cast<unsigned>(Opt::InflightWaitV2) == 68 &&
              IsOff(always_off, static_cast<size_t>(Opt::InflightWaitV2)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::InflightWaitV2)));

static_assert(static_cast<unsigned>(Opt::ListFetchAfterSync) == 69 &&
              IsOff(always_off, static_cast<size_t>(Opt::ListFetchAfterSync)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::ListFetchAfterSync)));

static_assert(static_cast<unsigned>(Opt::SpinHandoff) == 70 &&
              IsOff(always_off, static_cast<size_t>(Opt::SpinHandoff)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::SpinHandoff)));

static_assert(static_cast<unsigned>(Opt::RunAheadCover) == 71 &&
              IsOff(always_off, static_cast<size_t>(Opt::RunAheadCover)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::RunAheadCover)));

static_assert(static_cast<unsigned>(Opt::RunAheadCowProbe) == 72 &&
              IsOff(always_off, static_cast<size_t>(Opt::RunAheadCowProbe)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::RunAheadCowProbe)));
static_assert(static_cast<unsigned>(Opt::SyncFreeFetch) == 73 &&
              IsOff(always_off, static_cast<size_t>(Opt::SyncFreeFetch)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::SyncFreeFetch)));
static_assert(static_cast<unsigned>(Opt::PendingWake) == 74 &&
              !IsOff(always_off, static_cast<size_t>(Opt::PendingWake)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::PendingWake)));
static_assert(static_cast<unsigned>(Opt::BankHashShadow) == 75 &&
              IsOff(always_off, static_cast<size_t>(Opt::BankHashShadow)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::BankHashShadow)));
static_assert(static_cast<unsigned>(Opt::RunAheadRawProbe) == 76 &&
              IsOff(always_off, static_cast<size_t>(Opt::RunAheadRawProbe)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::RunAheadRawProbe)));
static_assert(static_cast<unsigned>(Opt::ListWriterHookCopiesOnly) == 77 &&
              IsOff(always_off, static_cast<size_t>(Opt::ListWriterHookCopiesOnly)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::ListWriterHookCopiesOnly)));
static_assert(static_cast<unsigned>(Opt::FrontMacroMemo) == 78 &&
              IsOff(always_off, static_cast<size_t>(Opt::FrontMacroMemo)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::FrontMacroMemo)));
static_assert(static_cast<unsigned>(Opt::RunAheadSnapshotProbe) == 79 &&
              IsOff(always_off, static_cast<size_t>(Opt::RunAheadSnapshotProbe)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::RunAheadSnapshotProbe)));
static_assert(static_cast<unsigned>(Opt::DiagDrawLite) == 80 &&
              !IsOff(always_off, static_cast<size_t>(Opt::DiagDrawLite)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::DiagDrawLite)));
static_assert(static_cast<unsigned>(Opt::ReportSubmitEarly) == 81 &&
              IsOff(always_off, static_cast<size_t>(Opt::ReportSubmitEarly)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::ReportSubmitEarly)));
static_assert(static_cast<unsigned>(Opt::DmaForwardTape) == 82 &&
              IsOff(always_off, static_cast<size_t>(Opt::DmaForwardTape)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::DmaForwardTape)));
static_assert(static_cast<unsigned>(Opt::RunAheadOverlayBudget) == 83 &&
              IsOff(always_off, static_cast<size_t>(Opt::RunAheadOverlayBudget)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::RunAheadOverlayBudget)));
static_assert(static_cast<unsigned>(Opt::AcquireDrainLite) == 84 &&
              IsOff(always_off, static_cast<size_t>(Opt::AcquireDrainLite)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::AcquireDrainLite)));
static_assert(static_cast<unsigned>(Opt::ExecSameDrop) == 85 &&
              IsOff(always_off, static_cast<size_t>(Opt::ExecSameDrop)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::ExecSameDrop)));
static_assert(static_cast<unsigned>(Opt::StateDeadStore) == 86 &&
              IsOff(always_off, static_cast<size_t>(Opt::StateDeadStore)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::StateDeadStore)));
static_assert(static_cast<unsigned>(Opt::RunAheadOwnWriteProbe) == 87 &&
              IsOff(always_off, static_cast<size_t>(Opt::RunAheadOwnWriteProbe)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::RunAheadOwnWriteProbe)));
static_assert(static_cast<unsigned>(Opt::BankDirtySkip) == 88 &&
              IsOff(always_off, static_cast<size_t>(Opt::BankDirtySkip)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::BankDirtySkip)));
static_assert(static_cast<unsigned>(Opt::BackReplayLean) == 89 &&
              IsOff(always_off, static_cast<size_t>(Opt::BackReplayLean)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::BackReplayLean)));
static_assert(static_cast<unsigned>(Opt::FrontMacroCall) == 90 &&
              IsOff(always_off, static_cast<size_t>(Opt::FrontMacroCall)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::FrontMacroCall)));
static_assert(static_cast<unsigned>(Opt::ShaderBuildBudget) == 91 &&
              !IsOff(always_off, static_cast<size_t>(Opt::ShaderBuildBudget)) &&
              !IsOff(DefaultOffMask(), static_cast<size_t>(Opt::ShaderBuildBudget)));
static_assert(static_cast<unsigned>(Opt::ComputeRebuildAsync) == 92 &&
              !IsOff(always_off, static_cast<size_t>(Opt::ComputeRebuildAsync)) &&
              !IsOff(DefaultOffMask(), static_cast<size_t>(Opt::ComputeRebuildAsync)));
static_assert(static_cast<unsigned>(Opt::ComputePreloadVariants) == 93 &&
              !IsOff(always_off, static_cast<size_t>(Opt::ComputePreloadVariants)) &&
              !IsOff(DefaultOffMask(), static_cast<size_t>(Opt::ComputePreloadVariants)));
static_assert(static_cast<unsigned>(Opt::StaleTargetSkip) == 94 &&
              !IsOff(always_off, static_cast<size_t>(Opt::StaleTargetSkip)) &&
              !IsOff(DefaultOffMask(), static_cast<size_t>(Opt::StaleTargetSkip)));
static_assert(static_cast<unsigned>(Opt::GeometryConsistency) == 95 &&
              !IsOff(always_off, static_cast<size_t>(Opt::GeometryConsistency)) &&
              !IsOff(DefaultOffMask(), static_cast<size_t>(Opt::GeometryConsistency)));
static_assert(static_cast<unsigned>(Opt::BuildOrderPriority) == 96 &&
              !IsOff(always_off, static_cast<size_t>(Opt::BuildOrderPriority)) &&
              !IsOff(DefaultOffMask(), static_cast<size_t>(Opt::BuildOrderPriority)));
static_assert(static_cast<unsigned>(Opt::ReplayDiskRecordings) == 97 &&
              !IsOff(always_off, static_cast<size_t>(Opt::ReplayDiskRecordings)) &&
              !IsOff(DefaultOffMask(), static_cast<size_t>(Opt::ReplayDiskRecordings)));
static_assert(static_cast<unsigned>(Opt::RebarUpload) == 98 &&
              !IsOff(always_off, static_cast<size_t>(Opt::RebarUpload)) &&
              !IsOff(DefaultOffMask(), static_cast<size_t>(Opt::RebarUpload)));
static_assert(static_cast<unsigned>(Opt::SuperMacro) == 99 &&
              IsOff(always_off, static_cast<size_t>(Opt::SuperMacro)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::SuperMacro)));
static_assert(static_cast<unsigned>(Opt::RunAheadOverlay) == 100 &&
              IsOff(always_off, static_cast<size_t>(Opt::RunAheadOverlay)) &&
              IsOff(DefaultOffMask(), static_cast<size_t>(Opt::RunAheadOverlay)));
static_assert(static_cast<unsigned>(Opt::ReflexSleep) == 101 &&
              !IsOff(always_off, static_cast<size_t>(Opt::ReflexSleep)) &&
              !IsOff(DefaultOffMask(), static_cast<size_t>(Opt::ReflexSleep)));
static_assert(static_cast<unsigned>(Opt::VrrFramePace) == 102 &&
              !IsOff(always_off, static_cast<size_t>(Opt::VrrFramePace)) &&
              !IsOff(DefaultOffMask(), static_cast<size_t>(Opt::VrrFramePace)));
static_assert(static_cast<unsigned>(Opt::DlssUpscale) == 103 &&
              !IsOff(always_off, static_cast<size_t>(Opt::DlssUpscale)) &&
              !IsOff(DefaultOffMask(), static_cast<size_t>(Opt::DlssUpscale)));

/// Switches the name front_sync_trio (an alias with no bit of its own) turns on together.
/// EDEN_NX2_PERF_OFF still turns each of them off.
inline constexpr OffMask front_sync_trio_set =
    Bit(Opt::AcquireTriggerEarly) | Bit(Opt::ExactNotedWrites) | Bit(Opt::WriterSetExec);

/// The off-mask from EDEN_NX2_PERF_SAFE (safe), EDEN_NX2_PERF_ON (on), EDEN_NX2_PERF_OFF (off)
/// and EDEN_NX2_PERF_FRAMES_AHEAD=0 (frames_ahead_off).
inline OffMask ParseOffMask(bool safe, std::string_view on, std::string_view off,
                        bool frames_ahead_off) {
    // Every candidate is on for the user's play test; EDEN_NX2_PERF_SAFE=1 puts the experimental
    // set back to off (split_overlap stays quarantined either way).
    OffMask mask = safe ? DefaultOffMask() | always_off : always_off;
    while (!on.empty()) {
        const size_t comma = on.find(',');
        const std::string_view name = on.substr(0, comma);
        for (size_t i = 0; i < opt_names.size(); ++i) {
            if (name == opt_names[i]) {
                mask &= ~Bit(static_cast<Opt>(i));
            }
        }
        if (name == "front_sync_trio") {
            mask &= ~front_sync_trio_set;
        }
        on = comma == std::string_view::npos ? std::string_view{} : on.substr(comma + 1);
    }
    if (frames_ahead_off) {
        mask |= Bit(Opt::FramesAhead);
    }
    while (!off.empty()) {
        const size_t comma = off.find(',');
        const std::string_view name = off.substr(0, comma);
        bool known = false;
        for (size_t i = 0; i < opt_names.size(); ++i) {
            if (name == opt_names[i]) {
                mask |= Bit(static_cast<Opt>(i));
                known = true;
            }
        }
        if (!known && !name.empty()) {
            LOG_WARNING(HW_GPU, "Nx2Perf: unknown name {} in EDEN_NX2_PERF_OFF", name);
        }
        off = comma == std::string_view::npos ? std::string_view{} : off.substr(comma + 1);
    }
    return mask;
}

/// Live switches (EDEN_NX2_PERF_CONTROL, nx2_perf.cpp). New switches are not live-safe until a
/// call-site audit shows none caches On() or keeps state the other branch does not handle.
/// Switches without an On() call site (bank_check, write_intersections, immediate_callbacks)
/// are left out so a live flip of them is reported as ignored instead of measuring nothing.
inline constexpr OffMask live_safe =
    Bit(Opt::CbBatch) | Bit(Opt::CleanWords) | Bit(Opt::UsageTracker) | Bit(Opt::FrontSleep) |
    Bit(Opt::FragmentBarrier) | Bit(Opt::FramesAhead) | Bit(Opt::SortedPageBatches) |
    Bit(Opt::QueryDiagnostics) | Bit(Opt::QueryUploadScratch) | Bit(Opt::FrontRun) |
    Bit(Opt::DownloadBatch) | Bit(Opt::AcquireTriggerEarly) | Bit(Opt::PipelineKeepPreloaded) |
    Bit(Opt::ShaderBuildBudget) | Bit(Opt::ComputeRebuildAsync) | Bit(Opt::StaleTargetSkip) |
    Bit(Opt::GeometryConsistency) | Bit(Opt::BuildOrderPriority) | Bit(Opt::ReplayDiskRecordings) |
    Bit(Opt::RebarUpload) | Bit(Opt::SuperMacro) | Bit(Opt::RunAheadOverlay) |
    Bit(Opt::ReflexSleep) | Bit(Opt::VrrFramePace) | Bit(Opt::DlssUpscale);

} // namespace VideoCommon::Nx2Perf
