#include "ax_retro_save.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace genx::ax {

bool PersistEmulatorSram(
    std::string_view system_name,
    std::string_view rom_stem,
    std::span<const std::byte> sram_bytes) noexcept {
    const fs::path save_dir = fs::path("/userdata/saves") / system_name;
    const std::string filename = std::string(rom_stem) + ".srm";
    return SaveSystemStateAtomic(save_dir, filename, sram_bytes);
}

bool PersistEmulatorSaveState(
    std::string_view system_name,
    std::string_view rom_stem,
    int slot_index,
    std::span<const std::byte> state_bytes) noexcept {
    const fs::path save_dir = fs::path("/userdata/saves") / system_name;
    const std::string filename =
        std::string(rom_stem) + ".state" + std::to_string(slot_index);
    return SaveSystemStateAtomic(save_dir, filename, state_bytes);
}

bool PersistEngineConfig(
    std::string_view system_name,
    std::string_view config_filename,
    std::string_view config_content) noexcept {
    const fs::path config_path =
        fs::path("/userdata/system/configs") / system_name / config_filename;
    return SaveSystemConfigAtomic(config_path, config_content);
}

} // namespace genx::ax
