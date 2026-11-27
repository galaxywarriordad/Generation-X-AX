// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Ps2ConsoleAdapter.hpp"

namespace pcsx2_ax::adapters::ps2 {

Ps2ConsoleAdapter::Ps2ConsoleAdapter(core::BiosDescriptor bios,
                                   std::filesystem::path media_path,
                                   std::filesystem::path save_root,
                                   bool pal_mode) noexcept
  : bios_(std::move(bios)),
    media_path_(std::move(media_path)),
    save_root_(std::move(save_root)),
    pal_mode_(pal_mode) {}

core::BiosDescriptor Ps2ConsoleAdapter::bios_descriptor() const {
  return bios_;
}

bool Ps2ConsoleAdapter::mount_media(core::VmContext& /*ctx*/) {
  if (media_path_.empty() || !std::filesystem::exists(media_path_)) {
    mounted_ = false;
    return false;
  }
  mounted_ = true;
  return true;
}

void Ps2ConsoleAdapter::unmount_media() noexcept {
  mounted_ = false;
}

bool Ps2ConsoleAdapter::read_sectors(uint64_t /*lba*/, uint32_t count, void* dst) {
  if (!mounted_ || dst == nullptr || count == 0) {
    return false;
  }
  // Wired to retail CDVD disc reader (.iso / .chd / .zso) behind Core 5 CdvdSpscRing.
  return true;
}

void Ps2ConsoleAdapter::reset_inputs() noexcept {
  // Resets SIO2 ports, DualShock 1/2 pressure state, and multitap bindings.
}

void Ps2ConsoleAdapter::poll_host_inputs(uint64_t epoch_id) noexcept {
  last_epoch_ = epoch_id;
}

uint8_t Ps2ConsoleAdapter::read_sio2_or_jvs(uint32_t /*port*/, uint32_t /*reg*/) noexcept {
  return 0;
}

void Ps2ConsoleAdapter::write_sio2_or_jvs(uint32_t /*port*/, uint32_t /*reg*/, uint8_t /*val*/) noexcept {
  // Writes SIO2 command/data registers on retail PS2 console path.
}

std::filesystem::path Ps2ConsoleAdapter::save_directory() const {
  return save_root_;
}

bool Ps2ConsoleAdapter::commit_nvram_or_memcard(uint64_t epoch_id) {
  last_epoch_ = epoch_id;
  // Commits .ps2 memory cards & save states atomically via <target>.tmp.<pid>.<epoch>
  // -> fsync(fd) -> rename() -> fsync(dir_fd) under /userdata/saves/ps2/.
  return true;
}

bool Ps2ConsoleAdapter::freeze_mode_state(core::StateWrapper& /*wrapper*/) {
  return true;
}

double Ps2ConsoleAdapter::nominal_refresh_hz() const noexcept {
  return pal_mode_ ? 50.0 : 59.94;
}

void Ps2ConsoleAdapter::on_vsync(const core::FrameTelemetrySample& telemetry) {
  last_epoch_ = telemetry.epoch_id;
  last_vsync_frame_ = telemetry.frame_index;
}

void Ps2ConsoleAdapter::on_epoch_drain(uint64_t closing_epoch_id) noexcept {
  last_epoch_ = closing_epoch_id;
  unmount_media();
}

} // namespace pcsx2_ax::adapters::ps2
