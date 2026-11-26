#pragma once

#include "ax/persist.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>

namespace genx::ax {

[[nodiscard]] inline bool SaveSystemStateAtomic(
    const std::filesystem::path& system_save_dir,
    std::string_view filename,
    std::span<const std::byte> payload) noexcept {
    if (system_save_dir.empty() || filename.empty()) {
        return false;
    }

    std::error_code ec;
    if (std::filesystem::exists(system_save_dir, ec) && !ec) {
        (void)ax::core::CleanupStaleTempFiles(system_save_dir, ".ax_tmp_");
    }

    const std::filesystem::path target = system_save_dir / filename;

    ax::core::AtomicWriteOptions opts{};
    opts.file_mode = 0664;
    opts.create_parent_dirs = true;
    opts.fsync_file = true;
    opts.fsync_parent_dir = true;

    return ax::core::WriteFileAtomic(target, payload, opts).ok;
}

[[nodiscard]] inline bool SaveSystemConfigAtomic(
    const std::filesystem::path& config_path,
    std::string_view config_text) noexcept {
    if (config_path.empty()) {
        return false;
    }

    const std::filesystem::path parent = config_path.parent_path();
    std::error_code ec;
    if (!parent.empty() && std::filesystem::exists(parent, ec) && !ec) {
        (void)ax::core::CleanupStaleTempFiles(parent, ".ax_tmp_");
    }

    ax::core::AtomicWriteOptions opts{};
    opts.file_mode = 0644;
    opts.create_parent_dirs = true;
    opts.fsync_file = true;
    opts.fsync_parent_dir = true;

    return ax::core::WriteFileAtomic(config_path, config_text, opts).ok;
}

} // namespace genx::ax
