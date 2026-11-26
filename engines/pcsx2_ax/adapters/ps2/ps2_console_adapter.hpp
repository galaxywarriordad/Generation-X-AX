#pragma once

namespace ax::pcsx2::adapters::ps2 {

// Planning-phase contract skeleton for retail PS2 mode (--mode ps2).
// Handles retail PS2 BIOS, .iso/.chd/.zso media, .ps2 memory cards,
// save slots, DualShock mapping, and atomic persistence under /userdata/saves/ps2/.
struct Ps2ConsoleAdapterConfig {
    bool atomic_share_persistence = true;
};

} // namespace ax::pcsx2::adapters::ps2
