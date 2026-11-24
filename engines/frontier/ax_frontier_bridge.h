#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

typedef enum ax_frontier_domain_e {
  AX_FRONTIER_DOMAIN_AUDIO_TIMING = 0,
  AX_FRONTIER_DOMAIN_VULKAN_SUBMISSION = 1,
  AX_FRONTIER_DOMAIN_GUEST_JIT = 2,
  AX_FRONTIER_DOMAIN_HOUSEKEEPING = 3
} ax_frontier_domain_t;

typedef struct ax_frontier_arena_s {
  void* ptr;
  size_t size;
  uint32_t flags;
} ax_frontier_arena_t;

int ax_frontier_pin_thread(ax_frontier_domain_t domain, uintptr_t native_thread_handle);
void ax_frontier_report_audio_metrics(uint32_t sample_rate, uint32_t buffer_frames, uint32_t underruns);
int ax_frontier_fastmem_alloc(size_t size, uint32_t flags, ax_frontier_arena_t* out_arena);
void ax_frontier_fastmem_free(ax_frontier_arena_t* arena);

} // extern "C"
