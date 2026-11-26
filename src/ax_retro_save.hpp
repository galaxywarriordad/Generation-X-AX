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
```[cite: 27]

---

### Updated `src/ax_retro_save_callers.cpp`

```cpp
#include "ax_retro_save.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace genx::ax {

[[nodiscard]] bool PersistEmulatorSram(
    std::string_view system_name,
    std::string_view rom_stem,
    std::span<const std::byte> sram_bytes) noexcept {
    if (system_name.empty() || rom_stem.empty()) {
        return false;
    }
    const fs::path save_dir = fs::path("/userdata/saves") / system_name;
    std::string filename;
    filename.reserve(rom_stem.size() + 4U);
    filename.append(rom_stem);
    filename.append(".srm");
    return SaveSystemStateAtomic(save_dir, filename, sram_bytes);
}

[[nodiscard]] bool PersistEmulatorSaveState(
    std::string_view system_name,
    std::string_view rom_stem,
    int slot_index,
    std::span<const std::byte> state_bytes) noexcept {
    if (system_name.empty() || rom_stem.empty() || slot_index < 0) {
        return false;
    }

    std::array<char, 16> slot_buf{};
    const auto [ptr, ec] = std::to_chars(slot_buf.data(), slot_buf.data() + slot_buf.size(), slot_index);
    if (ec != std::errc{}) {
        return false;
    }
    const std::string_view slot_view(slot_buf.data(), static_cast<std::size_t>(ptr - slot_buf.data()));

    static constexpr std::string_view kStatePrefix = ".state";
    const fs::path save_dir = fs::path("/userdata/saves") / system_name;
    std::string filename;
    filename.reserve(rom_stem.size() + kStatePrefix.size() + slot_view.size());
    filename.append(rom_stem);
    filename.append(kStatePrefix);
    filename.append(slot_view);
    return SaveSystemStateAtomic(save_dir, filename, state_bytes);
}

[[nodiscard]] bool PersistEngineConfig(
    std::string_view system_name,
    std::string_view config_filename,
    std::string_view config_content) noexcept {
    if (system_name.empty() || config_filename.empty()) {
        return false;
    }
    const fs::path config_path =
        fs::path("/userdata/system/configs") / system_name / config_filename;
    return SaveSystemConfigAtomic(config_path, config_content);
}

} // namespace genx::ax
