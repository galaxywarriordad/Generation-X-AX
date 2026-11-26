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
