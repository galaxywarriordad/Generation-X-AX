#pragma once

namespace ax::pcsx2::adapters::coh_h {

// Planning-phase contract skeleton for PCSX2x6 / COH-H (System 246 & 256) mode (--mode coh-h).
// Handles COH-H / System 246 & 256 BIOS, .acgame profiles, COH-H10020 dongle data,
// JVS/Coin/Test/Service/DIP1/wheel/gun I/O, and atomic ACSRAM/NVRAM persistence under
// /userdata/saves/pcsx2x6/ (guarded against sudden quit/hang and audio lock release).
struct CohHArcadeAdapterConfig {
    bool guard_abrupt_exit_persistence = true;
};

} // namespace ax::pcsx2::adapters::coh_h
