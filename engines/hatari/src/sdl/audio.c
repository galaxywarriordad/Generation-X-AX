/*
  Hatari - audio.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains the routines which pass the audio data to the SDL library.
*/
const char Audio_fileid[] = "Hatari audio.c";

#include "main.h"

#if ENABLE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include "audio.h"
#include "configuration.h"
#include "log.h"
#include "sound.h"
#include "dmaSnd.h"
#include "crossbar.h"
#include "video.h"

#define DEFAULT_BUFFER_SAMPLES 1024

bool bSoundWorking = false;			/* Is sound OK */
static volatile bool bPlayingBuffer = false;	/* Is playing buffer? */
int SoundBufferSize = DEFAULT_BUFFER_SAMPLES;	/* Size of sound buffer (in samples) */
int SdlAudioBufferSize = 0;			/* in ms (0 = use default) */
int pulse_swallowing_count = 0;			/* Sound disciplined emulation rate controlled by  */
						/*  window comparator and pulse swallowing counter */

#if ENABLE_SDL3
static SDL_AudioStream *audio_stream;
static const int audio_silence = 0;
#else
static SDL_AudioDeviceID audio_device;
static int audio_silence;
#endif

static void Audio_CallBack(void *userdata, Uint8 *stream, int len)
{
	(void)userdata;
	Sint16 *pBuffer = (Sint16 *)stream;
	int i, window, nSamplesPerFrame;

	len = len / 4;  // Use length in samples (16 bit stereo), not in bytes
	if (len <= 0)
		return;

	pulse_swallowing_count = 0;	/* 0 = Unaltered emulation rate */

	if (ConfigureParams.Sound.bEnableSoundSync && nScreenRefreshRate > 0)
	{
		nSamplesPerFrame = nAudioFrequency / nScreenRefreshRate;
		window = (nSamplesPerFrame > SoundBufferSize) ? nSamplesPerFrame : SoundBufferSize;

		if (nGeneratedSamples < window + (window >> 1))
			pulse_swallowing_count = -5793 / nScreenRefreshRate;
		else if (nGeneratedSamples > (window << 1) + (window >> 2))
			pulse_swallowing_count = 5793 / nScreenRefreshRate;
	}

	if (nGeneratedSamples >= len)
	{
		for (i = 0; i < len; i++)
		{
			*pBuffer++ = AudioMixBuffer[(AudioMixBuffer_pos_read + i) & AUDIOMIXBUFFER_SIZE_MASK][0];
			*pBuffer++ = AudioMixBuffer[(AudioMixBuffer_pos_read + i) & AUDIOMIXBUFFER_SIZE_MASK][1];
		}
		AudioMixBuffer_pos_read += len;
		nGeneratedSamples -= len;
	}
	else
	{
		int avail = (nGeneratedSamples > 0) ? nGeneratedSamples : 0;
		if (avail > len)
			avail = len;
		for (i = 0; i < avail; i++)
		{
			*pBuffer++ = AudioMixBuffer[(AudioMixBuffer_pos_read + i) & AUDIOMIXBUFFER_SIZE_MASK][0];
			*pBuffer++ = AudioMixBuffer[(AudioMixBuffer_pos_read + i) & AUDIOMIXBUFFER_SIZE_MASK][1];
		}
		memset(pBuffer, audio_silence, (size_t)(len - avail) * 4u);
		AudioMixBuffer_pos_read += avail;
		nGeneratedSamples = 0;
	}

	AudioMixBuffer_pos_read = AudioMixBuffer_pos_read & AUDIOMIXBUFFER_SIZE_MASK;
}

#if ENABLE_SDL3
static void SDLCALL Audio_SDL3Callback(void *userdata, SDL_AudioStream *stream,
                                       int additional_amount, int total_amount)
{
	(void)total_amount;
	if (additional_amount > 0)
	{
		Uint8 *data = SDL_stack_alloc(Uint8, additional_amount);
		if (data)
		{
			Audio_CallBack(userdata, data, additional_amount);
			SDL_PutAudioStreamData(stream, data, additional_amount);
			SDL_stack_free(data);
		}
	}
}
#endif

static int Audio_GetBufferSize(int freq)
{
	int buf_size;
	const int max_safe = AUDIOMIXBUFFER_SIZE / 2;

	if (SdlAudioBufferSize <= 0 || freq <= 0)
	{
		buf_size = DEFAULT_BUFFER_SAMPLES;
	}
	else
	{
		int samples = (freq / 1000) * SdlAudioBufferSize;
		int power2 = 64;
		while (power2 < samples && power2 < max_safe)
			power2 *= 2;
		buf_size = power2;
	}

	if (buf_size > max_safe)
		buf_size = max_safe;

	return buf_size;
}

void Audio_Init(void)
{
	SDL_AudioSpec desiredAudioSpec =
	{
		.format = AUDIO_S16SYS,
		.channels = 2,
		.freq = nAudioFrequency
	};

	if (!ConfigureParams.Sound.bEnableSound)
	{
		Log_Printf(LOG_DEBUG, "Sound: Disabled\n");
		bSoundWorking = false;
		return;
	}

	if (SDL_WasInit(SDL_INIT_AUDIO) == 0)
	{
#if ENABLE_SDL3
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
#else
		if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
#endif
		{
			Log_Printf(LOG_WARN, "Could not init audio: %s\n", SDL_GetError());
			bSoundWorking = false;
			return;
		}
	}

#if ENABLE_SDL3
	char samples_str[11];
	SoundBufferSize = Audio_GetBufferSize(nAudioFrequency);
	snprintf(samples_str, sizeof(samples_str), "%u", (unsigned)SoundBufferSize);
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, samples_str);

	audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
						 &desiredAudioSpec,
						 Audio_SDL3Callback,
						 NULL);
	if (!audio_stream)
	{
		Log_Printf(LOG_WARN, "Can't use audio: %s\n", SDL_GetError());
		bSoundWorking = false;
		ConfigureParams.Sound.bEnableSound = false;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return;
	}
#else
	desiredAudioSpec.callback = Audio_CallBack;
	desiredAudioSpec.userdata = NULL;
	desiredAudioSpec.samples = Audio_GetBufferSize(desiredAudioSpec.freq);

	audio_device = SDL_OpenAudioDevice(NULL, 0, &desiredAudioSpec, NULL, 0);
	if (!audio_device)
	{
		Log_Printf(LOG_WARN, "Can't use audio: %s\n", SDL_GetError());
		bSoundWorking = false;
		ConfigureParams.Sound.bEnableSound = false;
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		return;
	}

	audio_silence = desiredAudioSpec.silence;
	SoundBufferSize = desiredAudioSpec.samples;
	if (SoundBufferSize > AUDIOMIXBUFFER_SIZE / 2)
	{
		Log_Printf(LOG_WARN, "Soundbuffer size is too big (%d > %d)!\n",
			   SoundBufferSize, AUDIOMIXBUFFER_SIZE / 2);
	}
#endif

	bSoundWorking = true;
	Audio_EnableAudio(true);
}

void Audio_UnInit(void)
{
	if (bSoundWorking)
	{
		Audio_EnableAudio(false);
#if ENABLE_SDL3
		SDL_DestroyAudioStream(audio_stream);
		audio_stream = NULL;
#else
		SDL_CloseAudioDevice(audio_device);
#endif
		bSoundWorking = false;
	}
}

void Audio_Lock(void)
{
#if ENABLE_SDL3
	SDL_LockAudioStream(audio_stream);
#else
	SDL_LockAudioDevice(audio_device);
#endif
}

void Audio_Unlock(void)
{
#if ENABLE_SDL3
	SDL_UnlockAudioStream(audio_stream);
#else
	SDL_UnlockAudioDevice(audio_device);
#endif
}

void Audio_EnableAudio(bool bEnable)
{
	if (bEnable && !bPlayingBuffer)
	{
#if ENABLE_SDL3
		SDL_ResumeAudioStreamDevice(audio_stream);
#else
		SDL_PauseAudioDevice(audio_device, false);
#endif
		bPlayingBuffer = true;
	}
	else if (!bEnable && bPlayingBuffer)
	{
#if ENABLE_SDL3
		SDL_PauseAudioStreamDevice(audio_stream);
#else
		SDL_PauseAudioDevice(audio_device, true);
#endif
		bPlayingBuffer = false;
	}
}
