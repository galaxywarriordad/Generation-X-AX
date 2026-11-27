// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/ModeCapabilities.hpp"

#include <cstdint>
#include <filesystem>
#include <span>

namespace pcsx2_ax::adapters::coh_h {

struct CohHPreflightStatus {
  bool bios_ready = false;
  bool profile_or_dongle_ready = false;
  bool jvs_ready = false;

  [[nodiscard]] bool ready_to_boot() const noexcept {
    return bios_ready && profile_or_dongle_ready && jvs_ready;
  }
};

class CohHArcadeAdapter final
  : public core::IMediaProvider,
    public core::IInputBus,
    public core::IPersistenceSink,
    public core::IClockIrqPolicy {
public:
  CohHArcadeAdapter(core::BiosDescriptor bios,
                    std::filesystem::path acgame_or_media_path,
                    std::filesystem::path dongle_path,
                    std::filesystem::path save_root = "/userdata/saves/pcsx2x6",
                    double cabinet_hz = 60.0) noexcept;

  // Pre-launch check for arcade BIOS, .acgame (subdir=) / COH-H10020 dongle, and JVS I/O
  [[nodiscard]] CohHPreflightStatus run_preflight() const noexcept;

  // IMediaProvider
  core::BiosDescriptor bios_descriptor() const override;
  bool mount_media(core::VmContext& ctx) override;
  void unmount_media() noexcept override;
  bool read_sectors(uint64_t lba, uint32_t count, void* dst) override;
  bool query_dongle(uint32_t command, std::span<uint8_t> response) noexcept override;

  // IInputBus
  void reset_inputs() noexcept override;
  void poll_host_inputs(uint64_t epoch_id) noexcept override;
  uint8_t read_sio2_or_jvs(uint32_t port, uint32_t reg) noexcept override;
  void write_sio2_or_jvs(uint32_t port, uint32_t reg, uint8_t val) noexcept override;

  // IPersistenceSink
  std::filesystem::path save_directory() const override;
  bool commit_nvram_or_memcard(uint64_t epoch_id) override;
  bool freeze_mode_state(core::StateWrapper& wrapper) override;

  // IClockIrqPolicy
  double nominal_refresh_hz() const noexcept override;
  void on_vsync(const core::FrameTelemetrySample& telemetry) override;
  void on_epoch_drain(uint64_t closing_epoch_id) noexcept override;

  [[nodiscard]] core::ModeEnvironment make_environment() noexcept {
    return core::ModeEnvironment{
      .mode = core::HardwareMode::CohHArcade,
      .media = this,
      .input = this,
      .persistence = this,
      .clock_irq = this,
    };
  }

private:
  core::BiosDescriptor bios_;
  std::filesystem::path media_path_;
  std::filesystem::path dongle_path_;
  std::filesystem::path save_root_;
  double cabinet_hz_ = 60.0;
  bool mounted_ = false;
  bool jvs_initialized_ = false;
  uint64_t last_epoch_ = 0;
  uint64_t last_vsync_frame_ = 0;
};

} // namespace pcsx2_ax::adapters::coh_h
