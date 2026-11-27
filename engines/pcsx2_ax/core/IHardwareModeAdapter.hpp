// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ModeCapabilities.hpp"

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

class StateWrapper;

namespace pcsx2_ax::core {

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
                ModeEpochSnapshot snap{};
                snap.epoch = epoch;
                snap.epoch_id = epoch;
                snap.mode = mode();
                snap.active_mode = mode();
                snap.bios = bios_descriptor();
                snap.save_directory = std::filesystem::path{std::string{save_directory()}};
                return snap;
        }

        [[nodiscard]] virtual bool read_media_sectors(uint32_t lba,
                                                      uint32_t sector_count,
                                                      std::span<uint8_t> out_bytes,
                                                      uint16_t& out_sector_bytes) = 0;

        [[nodiscard]] virtual bool freeze_state(StateWrapper& wrapper) = 0;
};

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

template <HardwareModeAdapterConcept AdapterT>
[[nodiscard]] inline bool run_static_mode_frame(AdapterT& adapter, uint64_t epoch) {
        const ModeEpochSnapshot snap = adapter.capture_snapshot(epoch);
        (void)snap;
        adapter.on_vsync();
        return true;
}

} // namespace pcsx2_ax::core