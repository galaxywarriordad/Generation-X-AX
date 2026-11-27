#include "core/IHardwareModeAdapter.hpp"
#include "core/VMManager_Internal.h"
#include <cstdint>
#include <string>
#include <utility>

namespace pcsx2_ax::vm_bridge {

static pcsx2_ax::core::VmContext g_default_ctx{};
static pcsx2_ax::core::VmContext* g_active_ctx = &g_default_ctx;
static pcsx2_ax::core::IHardwareModeAdapter* g_bound_adapter = nullptr;
static std::string g_media_path{};
static bool g_media_ready = false;

void set_active_context(core::VmContext* ctx) noexcept {
    g_active_ctx = ctx ? ctx : &g_default_ctx;
}

core::VmContext& active_context() noexcept {
    return *g_active_ctx;
}

void bind_mode_adapter(core::IHardwareModeAdapter* adapter, core::VmContext&) noexcept {
    g_bound_adapter = adapter;
}

core::IHardwareModeAdapter* get_mode_adapter() noexcept {
    return g_bound_adapter;
}

bool init_mode_subsystems(core::VmContext&) {
    return true;
}

core::ModeEpochSnapshot current_snapshot(const core::VmContext&) noexcept {
    if (g_bound_adapter) {
        return g_bound_adapter->capture_snapshot(0);
    }
    return {};
}

void notify_mode_vsync(core::VmContext&) noexcept {}

bool reset_mode_subsystems(core::VmContext&) {
    return true;
}

void shutdown_mode_subsystems(core::VmContext&) noexcept {
    g_bound_adapter = nullptr;
}

std::string get_media_path() {
    return g_media_path;
}

std::string get_media_path(const core::VmContext&) {
    return g_media_path;
}

void set_media_path(std::string path, bool ready, core::VmContext&) {
    g_media_path = std::move(path);
    g_media_ready = ready;
}

bool is_media_ready() noexcept {
    return g_media_ready;
}

bool is_memcard_busy() noexcept {
    return false;
}

} // namespace pcsx2_ax::vm_bridge
