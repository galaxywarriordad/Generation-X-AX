// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

class StateWrapper;

namespace pcsx2_ax::core {

inline constexpr std::size_t kCacheLineBytes = 64u;
inline constexpr std::size_t kTelemetryRingCapacity = 256u; // Power of two
inline constexpr std::size_t kTelemetryRingMask = kTelemetryRingCapacity - 1u;
inline constexpr double kVramPruneGib = 5.00;
inline constexpr double kVramCeilingGib = 5.50;

enum class HardwareMode : uint8_t {
	Ps2Console = 0u, // Retail PS2 (--mode ps2 -> /userdata/saves/ps2/)
	CohHArcade = 1u, // Namco System 246/256 (--mode coh-h -> /userdata/saves/pcsx2x6/)
};

struct BiosDescriptor {
	uint32_t rom_version{0u};
	uint32_t bios_crc{0u};
	std::string_view region_code{};
	bool is_arcade_coh_h{false};
};

// Immutable epoch-stamped snapshot of active hardware/mode metadata
struct alignas(kCacheLineBytes) ModeEpochSnapshot {
	uint64_t epoch{1u};
	HardwareMode mode{HardwareMode::Ps2Console};
	BiosDescriptor bios{};
	std::string_view save_directory{"/userdata/saves/ps2/"};
};

// Lock-free per-frame telemetry sample for ax-sdk v1_36_2 observability
struct alignas(kCacheLineBytes) FrameTelemetrySample {
	uint64_t frame_index{0u};
	uint64_t epoch{1u};
	uint64_t ee_cycles{0u};
	uint64_t vu1_cycles{0u};
	uint64_t gs_cycles{0u};
	uint64_t framebuffer_crc64{0u};
	uint32_t cdvd_cache_hits{0u};
	uint32_t cdvd_cache_misses{0u};
	double vram_allocated_gib{0.0};
	bool vram_prune_triggered{false};
};

// C++20 concept for static adapter instantiation in single-mode/harness builds
template <typename T>
concept HardwareModeAdapterConcept = requires(T adapter, const T cadapter, uint64_t epoch, StateWrapper& wrapper) {
	{ cadapter.mode() } noexcept -> std::convertible_to<HardwareMode>;
	{ cadapter.save_directory() } noexcept -> std::convertible_to<std::string_view>;
	{ cadapter.bios_descriptor() } noexcept -> std::convertible_to<BiosDescriptor>;
	{ cadapter.capture_snapshot(epoch) } noexcept -> std::convertible_to<ModeEpochSnapshot>;
	{ adapter.initialize() } -> std::convertible_to<bool>;
	{ adapter.reset() } -> std::convertible_to<bool>;
	{ adapter.shutdown() } noexcept;
	{ adapter.on_vsync() } noexcept;
	{ adapter.freeze_state(wrapper) } -> std::convertible_to<bool>;
};

// Narrow boundary interface inside libax_pcsx2_core so shared core sources
// never directly include adapters/ps2/ or adapters/coh_h/.
class IHardwareModeAdapter {
public:
	virtual ~IHardwareModeAdapter() noexcept = default;

	[[nodiscard]] virtual HardwareMode mode() const noexcept = 0;
	[[nodiscard]] virtual std::string_view save_directory() const noexcept = 0;

	[[nodiscard]] virtual bool initialize() = 0;
	[[nodiscard]] virtual bool reset() = 0;
	virtual void shutdown() noexcept = 0;
	virtual void on_vsync() noexcept = 0;

	[[nodiscard]] virtual BiosDescriptor bios_descriptor() const noexcept = 0;

	[[nodiscard]] virtual ModeEpochSnapshot capture_snapshot(uint64_t epoch) const noexcept {
		return ModeEpochSnapshot{
			.epoch = epoch,
			.mode = mode(),
			.bios = bios_descriptor(),
			.save_directory = save_directory(),
		};
	}

	[[nodiscard]] virtual bool read_media_sectors(uint32_t lba,
	                                              uint32_t sector_count,
	                                              std::span<uint8_t> out_bytes,
	                                              uint16_t& out_sector_bytes) = 0;

	[[nodiscard]] virtual bool freeze_state(StateWrapper& wrapper) = 0;
};

// Explicit re-entrant VM state context handle (no hidden singleton required)
struct alignas(kCacheLineBytes) VmContext {
	std::atomic<uint64_t> epoch{1u};
	std::atomic<uint32_t> active_snapshot_slot{0u};
	ModeEpochSnapshot snapshots[2]{};

	std::atomic<IHardwareModeAdapter*> adapter{nullptr};
	std::atomic<bool> media_ready{false};
	std::atomic<bool> memcard_busy{false};
	std::string media_path{};

	std::atomic<uint64_t> telemetry_write_idx{0u};
	FrameTelemetrySample telemetry_ring[kTelemetryRingCapacity]{};
};

// Zero-vcall static fast-path helper when concrete adapter type satisfies the concept
template <HardwareModeAdapterConcept AdapterT>
[[nodiscard]] inline bool run_static_mode_frame(AdapterT& adapter, uint64_t epoch) {
	const ModeEpochSnapshot snap = adapter.capture_snapshot(epoch);
	(void)snap;
	adapter.on_vsync();
	return true;
}

} // namespace pcsx2_ax::core
