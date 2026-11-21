#ifndef AX_HATARI_HOOK_H
#define AX_HATARI_HOOK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ax_hatari_init(uint32_t sample_rate);
void ax_hatari_shutdown(void);

void ax_hatari_pin_jit_thread(void);
void ax_hatari_pin_housekeeping_thread(void);

void ax_hatari_begin_frame(uint64_t frame_index);
void ax_hatari_end_frame(uint64_t frame_index, double frame_time_ms);

/* Pushes interleaved 16-bit signed stereo PCM frames (L, R, L, R...) into LockFreeStereoRing */
void ax_hatari_push_audio_s16(const int16_t *stereo_s16, size_t frame_count);

#ifdef __cplusplus
}
#endif

#endif /* AX_HATARI_HOOK_H */
