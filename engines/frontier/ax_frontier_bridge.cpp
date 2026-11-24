#include "ax_frontier_bridge.h"

#include <cstdlib>
#include <cstring>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

#if defined(AX_HAS_CORE_TARGET)
#include "ax/core/ax_core.hpp"
#endif

namespace {

uint64_t DomainMaskFor231(ax_frontier_domain_t domain) {
  switch (domain) {
    case AX_FRONTIER_DOMAIN_AUDIO_TIMING:
      return (1ULL << 0); // Core 0 (0x01)
    case AX_FRONTIER_DOMAIN_VULKAN_SUBMISSION:
      return (1ULL << 1); // Core 1 (0x02)
    case AX_FRONTIER_DOMAIN_GUEST_JIT:
      return ((1ULL << 2) | (1ULL << 3) | (1ULL << 4)); // Cores 2-4 (0x1C)
    case AX_FRONTIER_DOMAIN_HOUSEKEEPING:
    default:
      return (1ULL << 5); // Core 5 (0x20)
  }
}

} // namespace

extern "C" {

int ax_frontier_pin_thread(ax_frontier_domain_t domain, uintptr_t native_thread_handle) {
  const uint64_t mask = DomainMaskFor231(domain);
#if defined(__linux__)
  pthread_t target = (native_thread_handle != 0)
      ? reinterpret_cast<pthread_t>(native_thread_handle)
      : pthread_self();
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  for (int cpu = 0; cpu < 64; ++cpu) {
    if (mask & (1ULL << cpu)) {
      CPU_SET(cpu, &cpuset);
    }
  }
  return pthread_setaffinity_np(target, sizeof(cpu_set_t), &cpuset);
#else
  (void)native_thread_handle;
  (void)mask;
  return 0;
#endif
}

void ax_frontier_report_audio_metrics(uint32_t sample_rate, uint32_t buffer_frames, uint32_t underruns) {
  (void)sample_rate;
  (void)buffer_frames;
  (void)underruns;
}

int ax_frontier_fastmem_alloc(size_t size, uint32_t flags, ax_frontier_arena_t* out_arena) {
  if (!out_arena || size == 0) {
    return -1;
  }
  void* mem = std::aligned_alloc(64, ((size + 63u) / 64u) * 64u);
  if (!mem) {
    return -1;
  }
  std::memset(mem, 0, size);
  out_arena->ptr = mem;
  out_arena->size = size;
  out_arena->flags = flags;
  return 0;
}

void ax_frontier_fastmem_free(ax_frontier_arena_t* arena) {
  if (!arena || !arena->ptr) {
    return;
  }
  std::free(arena->ptr);
  arena->ptr = nullptr;
  arena->size = 0;
  arena->flags = 0;
}

} // extern "C"
