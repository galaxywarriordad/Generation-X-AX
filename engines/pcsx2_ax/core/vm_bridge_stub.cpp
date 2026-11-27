#include "AxSdkSiliconBridge.hpp"
// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "core/FlacAxAudioBridge.hpp"
#include "core/IHardwareModeAdapter.hpp"
#include "core/VMManager_Internal.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <utility>

namespace pcsx2_ax::vm_bridge {

namespace {

pcsx2_ax::core::VmContext g_default_ctx{};
std::atomic<pcsx2_ax::core::VmContext*> g_active_ctx{&g_default_ctx};
std::atomic<pcsx2_ax::core::IHardwareModeAdapter*> g_bound_adapter{nullptr};
pcsx2_ax::audio::FlacAxAudioBridge g_audio_bridge{};
pcsx2_ax::sdk::AxSdkSiliconBridge g_sdk_bridge{};
std::string g_media_path{};
std::atomic<bool> g_media_ready{false};
std::atomic<bool> g_memcard_busy{false};

uint64_t ensure_nonzero_epoch(uint64_t epoch) noexcept {
return epoch == 0u ? 1u : epoch;
}

void publish_snapshot(core::VmContext& ctx, uint64_t epoch) noexcept {
const uint64_t norm_epoch = ensure_nonzero_epoch(epoch);
ctx.epoch.store(norm_epoch, std::memory_order_release);

core::IHardwareModeAdapter* adapter = g_bound_adapter.load(std::memory_order_acquire);
core::ModeEpochSnapshot snap{};
if (adapter != nullptr) {
snap = adapter->capture_snapshot(norm_epoch);
snap.epoch = norm_epoch;
snap.mode = adapter->mode();
if (!adapter->save_directory().empty()) {
snap.save_directory = adapter->save_directory();
}
} else {
snap.epoch = norm_epoch;
}

const uint32_t cur_slot = ctx.active_snapshot_slot.load(std::memory_order_acquire);
const uint32_t next_slot = (cur_slot + 1u) & 1u;
ctx.snapshots[next_slot] = std::move(snap);
ctx.active_snapshot_slot.store(next_slot, std::memory_order_release);
}

} // namespace

void set_active_context(core::VmContext* ctx) noexcept {
g_active_ctx.store(ctx != nullptr ? ctx : &g_default_ctx, std::memory_order_release);
}

core::VmContext& active_context() noexcept {
core::VmContext* ctx = g_active_ctx.load(std::memory_order_acquire);
return ctx != nullptr ? *ctx : g_default_ctx;
}

void bind_mode_adapter(core::IHardwareModeAdapter* adapter, core::VmContext& ctx) noexcept {
g_bound_adapter.store(adapter, std::memory_order_release);
ctx.adapter.store(adapter, std::memory_order_release);
const core::HardwareMode mode =
(adapter != nullptr) ? adapter->mode() : core::HardwareMode::Ps2Console;
g_audio_bridge.configure(mode, "fc3", pcsx2_ax::audio::AudioLoadTarget::Speakers);
const uint64_t ep = ensure_nonzero_epoch(ctx.epoch.load(std::memory_order_acquire));
publish_snapshot(ctx, ep);
}

core::IHardwareModeAdapter* get_mode_adapter(const core::VmContext& ctx) noexcept {
core::IHardwareModeAdapter* ctx_adapter = ctx.adapter.load(std::memory_order_acquire);
if (ctx_adapter != nullptr) {
return ctx_adapter;
}
return g_bound_adapter.load(std::memory_order_acquire);
}

uint64_t current_epoch(const core::VmContext& ctx) noexcept {
return ensure_nonzero_epoch(ctx.epoch.load(std::memory_order_acquire));
}

uint64_t advance_epoch(core::VmContext& ctx) noexcept {
const uint64_t next = ensure_nonzero_epoch(ctx.epoch.load(std::memory_order_acquire)) + 1u;
g_sdk_bridge.on_epoch_transition(next);
	g_audio_bridge.on_epoch_transition(next);
publish_snapshot(ctx, next);
return next;
}

core::ModeEpochSnapshot current_snapshot(const core::VmContext& ctx) noexcept {
const uint64_t ep = ensure_nonzero_epoch(ctx.epoch.load(std::memory_order_acquire));
const uint32_t slot = ctx.active_snapshot_slot.load(std::memory_order_acquire) & 1u;
core::ModeEpochSnapshot snap = ctx.snapshots[slot];
if (snap.epoch == 0u) {
core::IHardwareModeAdapter* adapter = get_mode_adapter(ctx);
if (adapter != nullptr) {
snap = adapter->capture_snapshot(ep);
snap.mode = adapter->mode();
if (!adapter->save_directory().empty()) {
snap.save_directory = adapter->save_directory();
}
}
snap.epoch = ep;
}
return snap;
}

void record_telemetry(const core::FrameTelemetrySample& sample, core::VmContext& ctx) noexcept {
const uint64_t idx = ctx.telemetry_write_idx.fetch_add(1u, std::memory_order_acq_rel);
ctx.telemetry_ring[idx & core::kTelemetryRingMask] = sample;
}

bool read_latest_telemetry(core::FrameTelemetrySample& out_sample, const core::VmContext& ctx) noexcept {
const uint64_t write_idx = ctx.telemetry_write_idx.load(std::memory_order_acquire);
if (write_idx == 0u) {
return false;
}
out_sample = ctx.telemetry_ring[(write_idx - 1u) & core::kTelemetryRingMask];
return true;
}

bool init_mode_subsystems(core::VmContext& ctx) {
const uint64_t ep = ensure_nonzero_epoch(ctx.epoch.load(std::memory_order_acquire));
core::IHardwareModeAdapter* adapter = get_mode_adapter(ctx);
const core::HardwareMode mode =
(adapter != nullptr) ? adapter->mode() : core::HardwareMode::Ps2Console;
g_audio_bridge.configure(mode, "fc3", pcsx2_ax::audio::AudioLoadTarget::Speakers);
if (!g_audio_bridge.initialize(ep, true)) {
return false;
}
publish_snapshot(ctx, ep);
if (adapter != nullptr && !adapter->initialize()) {
g_audio_bridge.shutdown();
	g_sdk_bridge.shutdown();
return false;
}
return true;
}

void notify_mode_vsync(core::VmContext& ctx) noexcept {
core::IHardwareModeAdapter* adapter = get_mode_adapter(ctx);
if (adapter != nullptr) {
adapter->on_vsync();
}
const uint64_t ep = ensure_nonzero_epoch(ctx.epoch.load(std::memory_order_acquire));
publish_snapshot(ctx, ep);
}

bool reset_mode_subsystems(core::VmContext& ctx) {
core::IHardwareModeAdapter* adapter = get_mode_adapter(ctx);
if (adapter != nullptr && !adapter->reset()) {
return false;
}
const uint64_t ep = ensure_nonzero_epoch(ctx.epoch.load(std::memory_order_acquire));
(void)g_audio_bridge.reset(ep);
publish_snapshot(ctx, ep);
return true;
}

void shutdown_mode_subsystems(core::VmContext& ctx) noexcept {
g_audio_bridge.shutdown();
	g_sdk_bridge.shutdown();
core::IHardwareModeAdapter* adapter = get_mode_adapter(ctx);
if (adapter != nullptr) {
adapter->shutdown();
}
ctx.adapter.store(nullptr, std::memory_order_release);
g_bound_adapter.store(nullptr, std::memory_order_release);
}

core::BiosDescriptor current_bios_info(const core::VmContext& ctx) noexcept {
core::IHardwareModeAdapter* adapter = get_mode_adapter(ctx);
if (adapter != nullptr) {
return adapter->bios_descriptor();
}
return {};
}

std::string get_media_path(const core::VmContext&) {
return g_media_path;
}

void set_media_path(std::string path, bool ready, core::VmContext& ctx) {
g_media_path = std::move(path);
ctx.media_path = g_media_path;
g_media_ready.store(ready, std::memory_order_release);
ctx.media_ready.store(ready, std::memory_order_release);
}

bool is_media_ready(const core::VmContext& ctx) noexcept {
return ctx.media_ready.load(std::memory_order_acquire) || g_media_ready.load(std::memory_order_acquire);
}

void set_memcard_busy(bool busy, core::VmContext& ctx) noexcept {
g_memcard_busy.store(busy, std::memory_order_release);
ctx.memcard_busy.store(busy, std::memory_order_release);
}

bool is_memcard_busy(const core::VmContext& ctx) noexcept {
return ctx.memcard_busy.load(std::memory_order_acquire) || g_memcard_busy.load(std::memory_order_acquire);
}


pcsx2_ax::audio::FlacAxAudioTelemetry current_audio_telemetry() noexcept {
	return g_audio_bridge.telemetry();
}


pcsx2_ax::sdk::SiliconProfile current_silicon_profile() noexcept {
	return g_sdk_bridge.profile();
}

core::VmContext& default_context() noexcept {
return g_default_ctx;
}

} // namespace pcsx2_ax::vm_bridge
