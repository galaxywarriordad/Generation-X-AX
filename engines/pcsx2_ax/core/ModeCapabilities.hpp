// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace pcsx2_ax::core {

inline constexpr std::size_t kCacheLineBytes = 64u;
inline constexpr std::size_t kTelemetryRingCapacity = 256u; // Power of two
inline constexpr std::size_t kTelemetryRingMask = kTelemetryRingCapacity - 1u;
inline constexpr double kVramPruneGib = 5.00;
inline constexpr double kVramCeilingGib = 5.50;

#ifndef PCSX2_AX_CORE_MODE_TYPES_DEFINED
#define PCSX2_AX_CORE_MODE_TYPES_DEFINED

enum class HardwareMode : uint8_t {
  Ps2Console = 0u, // Retail PS2 console BIOS + disc (.iso/.chd/.zso) + .ps2 memory cards
  CohHArcade = 1u  // System 246 / 256 / Super 256 COH-H board + .acgame + dongle + ACSRAM/NVRAM
};

struct BiosDescriptor {
  std::filesystem::path path{};
  uint32_t rom_version{0u};
  uint32_t bios_crc{0u};
  uint32_t crc32{0u};
  std::string_view region_code{};
  bool is_arcade_coh_h{false};
};

struct alignas(kCacheLineBytes) ModeEpochSnapshot {
  uint64_t epoch{1u};
  uint64_t epoch_id{0u};
  uint64_t cycle_timestamp{0u};
  HardwareMode mode{HardwareMode::Ps2Console};
  HardwareMode active_mode{HardwareMode::Ps2Console};
  BiosDescriptor bios{};
  std::filesystem::path save_directory{"/userdata/saves/ps2/"};
};

struct alignas(kCacheLineBytes) FrameTelemetrySample {
  uint64_t frame_index{0u};
  uint64_t epoch{1u};
  uint64_t epoch_id{0u};
  uint64_t ee_cycles{0u};
  uint64_t vu1_cycles{0u};
  uint64_t gs_cycles{0u};
  uint64_t framebuffer_crc64{0u};
  double frame_time_ms{0.0};
  double transfer_queue_ms{0.0};
  double vram_allocated_gib{0.0};
  size_t vram_allocated_bytes{0u};
  size_t bar1_ring_used_bytes{0u};
  uint32_t bar1_stalls{0u};
  uint32_t gs_draw_calls{0u};
  uint32_t vu_micro_kicks{0u};
  uint32_t vif1_wait_us{0u};
  uint32_t gif_path_wait_us{0u};
  uint32_t dmac_stall_us{0u};
  uint32_t sif_sync_wait_us{0u};
  uint32_t cdvd_inflight_chunks{0u};
  uint32_t cdvd_cache_hits{0u};
  uint32_t cdvd_cache_misses{0u};
  bool gameplay_active{false};
  bool vram_prune_triggered{false};
};

#endif // PCSX2_AX_CORE_MODE_TYPES_DEFINED

class VmContext;
class StateWrapper;

class IMediaProvider {
public:
  virtual ~IMediaProvider() = default;
  virtual BiosDescriptor bios_descriptor() const = 0;
  virtual bool mount_media(VmContext& ctx) = 0;
  virtual void unmount_media() noexcept = 0;
  virtual bool read_sectors(uint64_t lba, uint32_t count, void* dst) = 0;
  virtual bool query_dongle(uint32_t command, std::span<uint8_t> response) noexcept {
    (void)command;
    (void)response;
    return false;
  }
};

class IInputBus {
public:
  virtual ~IInputBus() = default;
  virtual void reset_inputs() noexcept = 0;
  virtual void poll_host_inputs(uint64_t epoch_id) noexcept = 0;
  virtual uint8_t read_sio2_or_jvs(uint32_t port, uint32_t reg) noexcept = 0;
  virtual void write_sio2_or_jvs(uint32_t port, uint32_t reg, uint8_t val) noexcept = 0;
};

class IPersistenceSink {
public:
  virtual ~IPersistenceSink() = default;
  virtual std::filesystem::path save_directory() const = 0;
  virtual bool commit_nvram_or_memcard(uint64_t epoch_id) = 0;
  virtual bool freeze_mode_state(StateWrapper& wrapper) = 0;
};

class IClockIrqPolicy {
public:
  virtual ~IClockIrqPolicy() = default;
  virtual double nominal_refresh_hz() const noexcept = 0;
  virtual void on_vsync(const FrameTelemetrySample& telemetry) = 0;
  virtual void on_epoch_drain(uint64_t closing_epoch_id) noexcept = 0;
};

struct ModeEnvironment {
  HardwareMode mode = HardwareMode::Ps2Console;
  IMediaProvider* media = nullptr;
  IInputBus* input = nullptr;
  IPersistenceSink* persistence = nullptr;
  IClockIrqPolicy* clock_irq = nullptr;

  [[nodiscard]] bool is_complete() const noexcept {
    return media != nullptr && input != nullptr && persistence != nullptr && clock_irq != nullptr;
  }
};

template <typename T>
concept MediaProviderConcept = std::derived_from<T, IMediaProvider> && requires(T m, VmContext& ctx) {
  { m.bios_descriptor() } -> std::same_as<BiosDescriptor>;
  { m.mount_media(ctx) } -> std::same_as<bool>;
};

template <typename T>
concept InputBusConcept = std::derived_from<T, IInputBus> && requires(T bus, uint64_t epoch_id, uint32_t port, uint32_t reg, uint8_t val) {
  { bus.reset_inputs() } noexcept;
  { bus.poll_host_inputs(epoch_id) } noexcept;
  { bus.read_sio2_or_jvs(port, reg) } noexcept -> std::same_as<uint8_t>;
  { bus.write_sio2_or_jvs(port, reg, val) } noexcept;
};

template <typename T>
concept PersistenceSinkConcept = std::derived_from<T, IPersistenceSink> && requires(T sink, uint64_t epoch_id, StateWrapper& sw) {
  { sink.save_directory() } -> std::same_as<std::filesystem::path>;
  { sink.commit_nvram_or_memcard(epoch_id) } -> std::same_as<bool>;
  { sink.freeze_mode_state(sw) } -> std::same_as<bool>;
};

template <typename T>
concept ClockIrqPolicyConcept = std::derived_from<T, IClockIrqPolicy> && requires(T policy, const FrameTelemetrySample& sample, uint64_t epoch_id) {
  { policy.nominal_refresh_hz() } noexcept -> std::same_as<double>;
  { policy.on_vsync(sample) };
  { policy.on_epoch_drain(epoch_id) } noexcept;
};

} // namespace pcsx2_ax::core