/*
  Hatari - audio.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains the routines which pass the audio data to libretro.
*/
const char Audio_fileid[] = "Hatari audio.c";

#include <libretro.h>

#include "main.h"
#include "audio.h"
#include "configuration.h"
#include "log.h"
#include "sound.h"
#include "dmaSnd.h"
#include "crossbar.h"
#include "video.h"

bool bSoundWorking = false;			/* Is sound OK */
static volatile bool bPlayingBuffer = false;	/* Is playing buffer? */
int SoundBufferSize = 1024 / 4;			/* Size of sound buffer (in samples) */
int SdlAudioBufferSize = 0;			/* in ms (0 = use default) */
int pulse_swallowing_count = 0;			/* Sound disciplined emulation rate controlled by  */
						/*  window comparator and pulse swallowing counter */

static retro_audio_sample_t audio_sample_cb;
static retro_audio_sample_batch_t audio_sample_batch_cb;

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
	audio_sample_cb = cb;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
	audio_sample_batch_cb = cb;
}

void Audio_Init(void)
{
	bSoundWorking = true;
	Audio_EnableAudio(true);
}

void Audio_UnInit(void)
{
	Audio_EnableAudio(false);
	bSoundWorking = false;
}

void Audio_Lock(void)
{
}

void Audio_Unlock(void)
{
	int count = nGeneratedSamples;
	if (count > 0)
	{
		if (count > AUDIOMIXBUFFER_SIZE)
			count = AUDIOMIXBUFFER_SIZE;

		size_t read_idx = (size_t)(AudioMixBuffer_pos_read & AUDIOMIXBUFFER_SIZE_MASK);
		if (bPlayingBuffer && audio_sample_batch_cb)
		{
			if (read_idx + (size_t)count <= (size_t)AUDIOMIXBUFFER_SIZE)
			{
				audio_sample_batch_cb(&AudioMixBuffer[read_idx][0], (size_t)count);
			}
			else
			{
				size_t samples_at_end = (size_t)AUDIOMIXBUFFER_SIZE - read_idx;
				audio_sample_batch_cb(&AudioMixBuffer[read_idx][0], samples_at_end);
				audio_sample_batch_cb(&AudioMixBuffer[0][0], (size_t)count - samples_at_end);
			}
		}

		AudioMixBuffer_pos_read = (int)((read_idx + (size_t)count) & (size_t)AUDIOMIXBUFFER_SIZE_MASK);
	}
	nGeneratedSamples = 0;
}

void Audio_EnableAudio(bool bEnable)
{
	bPlayingBuffer = bEnable;
}
