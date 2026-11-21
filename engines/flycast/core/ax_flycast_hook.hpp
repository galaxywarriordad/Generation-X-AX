#pragma once

#include <cstddef>
#include <cstdint>

namespace ax::flycast_hook {

void Init();
void Shutdown();

void PinHousekeepingThread();   // Core 5 (0x20) — UI, CHD/GDI I/O, HID
void PinVulkanThread();         // Core 1 (0x02) — Vulkan submission
void PinJitThread();            // Cores 2-4 (0x1C) — SH-4 / ARM7 JIT

void BeginFrame(std::uint64_t frame_index);
void EndFrame(std::uint64_t frame_index, double frame_time_ms);

// Converts Flycast's s16 stereo frames to float and pushes into LockFreeStereoRing
void PushAicaSamples(const std::int16_t* stereo_s16, std::size_t frame_count);

}  // namespace ax::flycast_hook
