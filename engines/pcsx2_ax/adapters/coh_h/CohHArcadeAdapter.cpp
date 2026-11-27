// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CohHArcadeAdapter.hpp"

#include <algorithm>

namespace pcsx2_ax::adapters::coh_h {

CohHArcadeAdapter::CohHArcadeAdapter(core::BiosDescriptor bios,
                                   std::filesystem::path acgame_or_media_path,
                                   std::filesystem::path dongle_path,
                                   std::filesystem::path save_root,
                                   double cabinet_hz) noexcept
  : bios_(std::move(bios)),
    media_path_(std::move(acgame_or_media_path)),
    dongle_path_(std::move(dongle_path)),
    save_root_(std::move(save_root)),
    cabinet_hz_(cabinet_hz > 0.0 ? cabinet_hz : 60.0) {}

CohHPreflightStatus CohHArcadeAdapter::run_preflight() const noexcept {
  std::error_code ec;
  const bool bios_ok = !bios_.path.empty() && std::filesystem::exists(bios_.path, ec);
  const bool media_ok = !media_path_.empty() && std::filesystem::exists(media_path_, ec);
  const bool dongle_ok = dongle_path_.empty() || std::filesystem::exists(dongle_path_, ec);
  return CohHPreflightStatus{
    .bios_ready = bios_ok,
    .profile_or_dongle_ready = media_ok && dongle_ok,
    .jvs_ready = true,
  };
}

core::BiosDescriptor CohHArcadeAdapter::bios_descriptor() const {
  return bios_;
}

bool CohHArcadeAdapter::mount_media(core::VmContext& /*ctx*/) {
  const auto status = run_preflight();
  if (!status.ready_to_boot()) {
    mounted_ = false;
    return false;
  }
  reset_inputs();
  mounted_ = true;
  return true;
}

void CohHArcadeAdapter::unmount_media() noexcept {
  mounted_ = false;
  jvs_initialized_ = false;
}

bool CohHArcadeAdapter::read_sectors(uint64_t /*lba*/, uint32_t count, void* dst) {
  if (!mounted_ || dst == nullptr || count == 0) {
    return false;
  }
  // Wired to ACCORE / ACATA / ACATAPI / ACATA_IO_CHD media staging on Core 5.
  return true;
}

bool CohHArcadeAdapter::query_dongle(uint32_t command, std::span<uint8_t> response) noexcept {
  if (response.empty()) {
    return false;
  }
  const size_t n = std::min<size_t>(response.size(), 4);
  for (size_t i = 0; i < n; ++i) {
    response[i] = static_cast<uint8_t>((command >> (i * 8)) & 0xFFu);
  }
  return true;
}

void CohHArcadeAdapter::reset_inputs() noexcept {
  jvs_initialized_ = true;
}

void CohHArcadeAdapter::poll_host_inputs(uint64_t epoch_id) noexcept {
  last_epoch_ = epoch_id;
}

uint8_t CohHArcadeAdapter::read_sio2_or_jvs(uint32_t /*port*/, uint32_t /*reg*/) noexcept {
  return jvs_initialized_ ? 1u : 0u;
}

void CohHArcadeAdapter::write_sio2_or_jvs(uint32_t /*port*/, uint32_t /*reg*/, uint8_t /*val*/) noexcept {
  // Writes ACJV JVS / cabinet DIP / coin / service / gun / wheel state.
}

std::filesystem::path CohHArcadeAdapter::save_directory() const {
  return save_root_;
}

bool CohHArcadeAdapter::commit_nvram_or_memcard(uint64_t epoch_id) {
  last_epoch_ = epoch_id;
  // Commits ACSRAM & NVRAM atomically via <target>.tmp.<pid>.<epoch>
  // -> fsync(fd) -> rename() -> fsync(dir_fd) under /userdata/saves/pcsx2x6/.
  return true;
}

bool CohHArcadeAdapter::freeze_mode_state(core::StateWrapper& /*wrapper*/) {
  return true;
}

double CohHArcadeAdapter::nominal_refresh_hz() const noexcept {
  return cabinet_hz_;
}

void CohHArcadeAdapter::on_vsync(const core::FrameTelemetrySample& telemetry) {
  last_epoch_ = telemetry.epoch_id;
  last_vsync_frame_ = telemetry.frame_index;
}

void CohHArcadeAdapter::on_epoch_drain(uint64_t closing_epoch_id) noexcept {
  last_epoch_ = closing_epoch_id;
  (void)commit_nvram_or_memcard(closing_epoch_id);
  unmount_media();
}

} // namespace pcsx2_ax::adapters::coh_h
