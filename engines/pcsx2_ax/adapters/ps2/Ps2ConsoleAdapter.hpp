// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/IHardwareModeAdapter.hpp"

#include <atomic>
#include <cstdint>
#include <span>
#include <string_view>

namespace pcsx2_ax::adapters::ps2 {

class Ps2ConsoleAdapter final : public pcsx2_ax::core::IHardwareModeAdapter {
public:
	static constexpr std::string_view kSaveRoot = "/userdata/saves/ps2/";

	Ps2ConsoleAdapter() noexcept = default;
	~Ps2ConsoleAdapter() noexcept override { shutdown(); }

	Ps2ConsoleAdapter(const Ps2ConsoleAdapter&)            = delete;
	Ps2ConsoleAdapter& operator=(const Ps2ConsoleAdapter&) = delete;

	[[nodiscard]] pcsx2_ax::core::HardwareMode mode() const noexcept override {
		return pcsx2_ax::core::HardwareMode::Ps2Console;
	}

	[[nodiscard]] std::string_view save_directory() const noexcept override {
		return kSaveRoot;
	}

	[[nodiscard]] bool initialize() override {
		if (initialized_.load(std::memory_order_acquire)) {
			return true;
		}
		// Prototype Phase hook: wire BiosTools, IopBios, CDVD, SIO2, and Pad init here
		bios_info_ = pcsx2_ax::core::BiosDescriptor{
			.rom_version     = 0x0220u,
			.bios_crc        = 0u,
			.region_code     = "PS2",
			.is_arcade_coh_h = false,
		};
		initialized_.store(true, std::memory_order_release);
		return true;
	}

	[[nodiscard]] bool reset() override {
		if (!initialized_.load(std::memory_order_acquire)) {
			return initialize();
		}
		vsync_ticks_.store(0u, std::memory_order_release);
		return true;
	}

	void shutdown() noexcept override {
		if (!initialized_.exchange(false, std::memory_order_acq_rel)) {
			return;
		}
		// Prototype Phase hook: flush/release retail PS2 disc and memory card state
	}

	void on_vsync() noexcept override {
		if (!initialized_.load(std::memory_order_acquire)) {
			return;
		}
		vsync_ticks_.fetch_add(1u, std::memory_order_relaxed);
	}

	[[nodiscard]] pcsx2_ax::core::BiosDescriptor bios_descriptor() const noexcept override {
		return bios_info_;
	}

	[[nodiscard]] pcsx2_ax::core::ModeEpochSnapshot capture_snapshot(uint64_t epoch) const noexcept override {
		return pcsx2_ax::core::ModeEpochSnapshot{
			.epoch          = epoch,
			.mode           = mode(),
			.bios           = bios_info_,
			.save_directory = kSaveRoot,
		};
	}

	[[nodiscard]] bool read_media_sectors(uint32_t lba,
	                                      uint32_t sector_count,
	                                      std::span<uint8_t> out_bytes,
	                                      uint16_t& out_sector_bytes) override {
		constexpr uint16_t kDefaultPs2SectorSize = 2048u;
		const std::size_t required = static_cast<std::size_t>(sector_count) * kDefaultPs2SectorSize;
		if (!initialized_.load(std::memory_order_acquire) || sector_count == 0u || out_bytes.size() < required) {
			return false;
		}
		(void)lba;
		// Prototype Phase hook: delegate sector reads to CDVDdiscReader / CdvdSpscRing
		out_sector_bytes = kDefaultPs2SectorSize;
		return true;
	}

	[[nodiscard]] bool freeze_state(StateWrapper& wrapper) override {
		(void)wrapper;
		return initialized_.load(std::memory_order_acquire);
	}

	[[nodiscard]] bool is_initialized() const noexcept { return initialized_.load(std::memory_order_acquire); }
	[[nodiscard]] uint64_t vsync_ticks() const noexcept { return vsync_ticks_.load(std::memory_order_acquire); }

private:
	std::atomic<bool>              initialized_{false};
	std::atomic<uint64_t>          vsync_ticks_{0u};
	pcsx2_ax::core::BiosDescriptor bios_info_{};
};

static_assert(pcsx2_ax::core::HardwareModeAdapterConcept<Ps2ConsoleAdapter>);

} // namespace pcsx2_ax::adapters::ps2
