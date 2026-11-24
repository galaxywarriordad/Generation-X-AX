/***************************************************************************
    SDL Audio Code.
    
    This is the SDL specific audio code.
    If porting to a non-SDL platform, you would need to replace this class.
    
    It takes the output from the PCM and YM chips, mixes them and then
    outputs appropriately.
    
    In order to achieve seamless audio, when audio is enabled the framerate
    is adjusted to essentially sync the video to the audio output.
    
    This is based upon code from the Atari800 emulator project.
    Copyright (c) 1998-2008 Atari800 development team
***************************************************************************/

#include <atomic>
#include <iostream>
#include <cstring>
#include <SDL.h>

#include "sdl2/audio.hpp"
#include "frontend/config.hpp" // fps
#include "engine/audio/osoundint.hpp"

#ifdef COMPILE_SOUND_CODE

/* ----------------------------------------------------------------------------
   SDL Sound Implementation & Callback Function
   ----------------------------------------------------------------------------*/

// Note that these variables are accessed across audio producer and SDL callback contexts.
uint8_t* dsp_buffer = nullptr;
static int dsp_buffer_bytes = 0;
static std::atomic<int> dsp_write_pos{0};
static std::atomic<int> dsp_read_pos{0};
static std::atomic<Uint32> callbacktick{0};     // tick at which callback occured
static int bytes_per_sample = 4;               // Number of bytes per sample entry (usually 4 bytes if stereo and 16-bit sound)

// SDL Audio Callback Function
extern "C" void fill_audio(void *udata, Uint8 *stream, int len);

// ----------------------------------------------------------------------------

Audio::Audio()
{
    sound_enabled = false;
}

Audio::~Audio()
{
    stop_audio();
}

void Audio::init()
{
    if (config.sound.enabled)
        start_audio();
}

void Audio::start_audio()
{
    if (!sound_enabled)
    {
        std::string platform = SDL_GetPlatform();

        if (platform == "Linux")
        {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
                std::cout << "Error initializing audio subsystem: " << SDL_GetError() << std::endl;

            if (SDL_AudioInit("alsa") != 0)
            {
                std::cout << "Error initializing audio using ALSA: " << SDL_GetError() << std::endl;
                return;
            }
        }
        else
        {
            if (SDL_Init(SDL_INIT_AUDIO) == -1)
            {
                std::cout << "Error initializing audio: " << SDL_GetError() << std::endl;
                return;
            }
        }

        // SDL Audio Properties
        SDL_AudioSpec desired, obtained;
        std::memset(&desired, 0, sizeof(desired));
        std::memset(&obtained, 0, sizeof(obtained));

        desired.freq     = config.sound.rate;
        desired.format   = AUDIO_S16SYS;
        desired.channels = CHANNELS;
        desired.samples  = SAMPLES;
        desired.callback = fill_audio;
        desired.userdata = nullptr;

        dev = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
        if (dev == 0)
        {
            std::cout << "Error opening audio device: " << SDL_GetError() << std::endl;
            return;
        }

        if (desired.samples != obtained.samples)
        {
            std::cout << "Error initializing audio: number of samples not supported." << std::endl
                      << "Please compare desired vs obtained. Look at what audio driver SDL2 is using." << std::endl;
            SDL_CloseAudioDevice(dev);
            dev = 0;
            return;
        }

        bytes_per_sample = CHANNELS * (BITS / 8);

        // Start Audio
        sound_enabled = true;

        // how many fragments in the dsp buffer
        const int DSP_BUFFER_FRAGS = 5;
        int specified_delay_samps = (config.sound.rate * SND_DELAY) / 1000;
        int dsp_buffer_samps = SAMPLES * DSP_BUFFER_FRAGS + specified_delay_samps;
        dsp_buffer_bytes = CHANNELS * dsp_buffer_samps * (BITS / 8);
        dsp_buffer = new uint8_t[dsp_buffer_bytes];

        // Create Buffer For Mixing (use size_t to prevent uint16_t overflow)
        const size_t fps_denom = config.fps > 0 ? static_cast<size_t>(config.fps) : 60u;
        const size_t buffer_samples_per_tick = (static_cast<size_t>(config.sound.rate) / fps_denom) * static_cast<size_t>(CHANNELS) + static_cast<size_t>(SAMPLES) * 2u;
        mix_buffer = new uint16_t[buffer_samples_per_tick];

        clear_buffers();
        clear_wav();

        SDL_PauseAudioDevice(dev, 0);
    }
}

void Audio::clear_buffers()
{
    int specified_delay_samps = (config.sound.rate * SND_DELAY) / 1000;
    int initial_write_pos = (specified_delay_samps + SAMPLES) * bytes_per_sample;
    dsp_read_pos.store(0, std::memory_order_relaxed);
    dsp_write_pos.store(initial_write_pos, std::memory_order_release);
    avg_gap = 0.0;
    gap_est = 0;

    if (dsp_buffer && dsp_buffer_bytes > 0)
        std::memset(dsp_buffer, 0, static_cast<size_t>(dsp_buffer_bytes));

    const size_t fps_denom = config.fps > 0 ? static_cast<size_t>(config.fps) : 60u;
    const size_t buffer_samples_per_tick = (static_cast<size_t>(config.sound.rate) / fps_denom) * static_cast<size_t>(CHANNELS) + static_cast<size_t>(SAMPLES) * 2u;
    if (mix_buffer)
        std::memset(mix_buffer, 0, buffer_samples_per_tick * sizeof(uint16_t));

    callbacktick.store(0, std::memory_order_relaxed);
}

void Audio::stop_audio()
{
    if (sound_enabled)
    {
        sound_enabled = false;

        if (dev != 0)
        {
            SDL_PauseAudioDevice(dev, 1);
            SDL_CloseAudioDevice(dev);
            dev = 0;
        }

        delete[] dsp_buffer;
        dsp_buffer = nullptr;
        delete[] mix_buffer;
        mix_buffer = nullptr;
    }
}

void Audio::pause_audio()
{
    if (sound_enabled && dev != 0)
    {
        SDL_PauseAudioDevice(dev, 1);
    }
}

void Audio::resume_audio()
{
    if (sound_enabled && dev != 0)
    {
        clear_buffers();
        SDL_PauseAudioDevice(dev, 0);
    }
}

// Called every frame to update the audio
void Audio::tick()
{
    if (!sound_enabled || !dsp_buffer || dsp_buffer_bytes <= 0) return;

    // Update audio streams from PCM & YM Devices
    osoundint.pcm->stream_update();
    osoundint.ym->stream_update();

    // Get the audio buffers we've just output
    int16_t *pcm_buffer = osoundint.pcm->get_buffer();
    int16_t *ym_buffer  = osoundint.ym->get_buffer();
    int16_t *wav_buffer = wavfile.data;

    int samples_written = osoundint.pcm->buffer_size;

    // Mix them into the mix_buffer
    for (int i = 0; i < samples_written; i++)
    {
        int32_t mix_data = static_cast<int32_t>(wav_buffer[wavfile.pos]) +
                           static_cast<int32_t>(pcm_buffer[i]) +
                           static_cast<int32_t>(ym_buffer[i]);

        if (mix_data > INT16_MAX) mix_data = INT16_MAX;
        else if (mix_data < INT16_MIN) mix_data = INT16_MIN;

        mix_buffer[i] = static_cast<uint16_t>(mix_data);

        if (++wavfile.pos >= wavfile.length)
            wavfile.pos = 0;
    }

    const uint8_t* mbuf8 = reinterpret_cast<const uint8_t*>(mix_buffer);
    const double bytes_per_ms = static_cast<double>(bytes_per_sample) * (static_cast<double>(config.sound.rate) / 1000.0);
    const int bytes_written = (BITS == 8 ? samples_written : samples_written * 2);

    int write_pos = dsp_write_pos.load(std::memory_order_relaxed);
    int read_pos  = dsp_read_pos.load(std::memory_order_acquire);
    int gap = write_pos - read_pos;

    const Uint32 last_cb = callbacktick.load(std::memory_order_acquire);
    if (last_cb != 0)
    {
        const Uint32 now = SDL_GetTicks();
        gap_est = static_cast<int>(static_cast<double>(gap) - bytes_per_ms * static_cast<double>(now - last_cb));
    }

    // Non-blocking overflow guard: advance read_pos rather than sleeping on the main tick
    if (gap + bytes_written > dsp_buffer_bytes)
    {
        const int overflow = (gap + bytes_written) - dsp_buffer_bytes;
        read_pos += overflow;
        dsp_read_pos.store(read_pos, std::memory_order_release);
        gap = write_pos - read_pos;
    }

    const int newpos = write_pos + bytes_written;
    if (newpos / dsp_buffer_bytes == write_pos / dsp_buffer_bytes)
    {
        std::memcpy(dsp_buffer + (write_pos % dsp_buffer_bytes), mbuf8, static_cast<size_t>(bytes_written));
    }
    else
    {
        const int first_part_size = dsp_buffer_bytes - (write_pos % dsp_buffer_bytes);
        std::memcpy(dsp_buffer + (write_pos % dsp_buffer_bytes), mbuf8, static_cast<size_t>(first_part_size));
        std::memcpy(dsp_buffer, mbuf8 + first_part_size, static_cast<size_t>(bytes_written - first_part_size));
    }

    if (last_cb == 0)
    {
        read_pos += bytes_written;
    }

    while (read_pos > dsp_buffer_bytes)
    {
        write_pos -= dsp_buffer_bytes;
        read_pos  -= dsp_buffer_bytes;
    }

    dsp_read_pos.store(read_pos, std::memory_order_release);
    dsp_write_pos.store(newpos, std::memory_order_release);
}

// Adjust the speed of the emulator, based on audio streaming performance.
double Audio::adjust_speed()
{
    if (!sound_enabled)
        return 1.0;

    const double alpha = 2.0 / (1.0 + 40.0);
    static bool inited = false;

    if (!inited)
    {
        inited = true;
        avg_gap = gap_est;
    }
    else
    {
        avg_gap = avg_gap + alpha * (static_cast<double>(gap_est) - avg_gap);
    }

    const int gap_too_small = (SND_DELAY * config.sound.rate * bytes_per_sample) / 1000;
    const int gap_too_large = ((SND_DELAY + SND_SPREAD) * config.sound.rate * bytes_per_sample) / 1000;

    if (avg_gap < gap_too_small)
    {
        return 0.9;
    }
    if (avg_gap > gap_too_large)
    {
        return 1.1;
    }
    return 1.0;
}

// Empty Wav Buffer
static int16_t EMPTY_BUFFER[] = {0, 0, 0, 0};

void Audio::load_wav(const char* filename)
{
    if (sound_enabled)
    {
        clear_wav();

        SDL_AudioSpec wave;
        uint8_t *data = nullptr;
        uint32_t length = 0;

        pause_audio();

        if (SDL_LoadWAV(filename, &wave, &data, &length) == nullptr)
        {
            wavfile.loaded = 0;
            resume_audio();
            std::cout << "Could not load wav: " << filename << std::endl;
            return;
        }

        uint8_t* data_vol = new uint8_t[length];
        SDL_MixAudioFormat(data_vol, data, wave.format, length, SDL_MIX_MAXVOLUME / 2);

        if (wave.format != AUDIO_S16 || wave.channels != 2 || wave.freq != config.sound.rate)
        {
            SDL_AudioCVT cvt;
            SDL_BuildAudioCVT(&cvt, wave.format, wave.channels, wave.freq,
                              AUDIO_S16, CHANNELS, config.sound.rate);

            cvt.buf = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(length) * static_cast<size_t>(cvt.len_mult)));
            std::memcpy(cvt.buf, data_vol, length);
            cvt.len = static_cast<int>(length);
            SDL_ConvertAudio(&cvt);
            SDL_FreeWAV(data);
            delete[] data_vol;

            wavfile.data = reinterpret_cast<int16_t*>(cvt.buf);
            wavfile.length = cvt.len_cvt / 2;
            wavfile.pos = 0;
            wavfile.loaded = 1;
        }
        else
        {
            SDL_FreeWAV(data);
            wavfile.data = reinterpret_cast<int16_t*>(data_vol);
            wavfile.length = length / 2;
            wavfile.pos = 0;
            wavfile.loaded = 2;
        }

        resume_audio();
    }
}

void Audio::clear_wav()
{
    if (wavfile.loaded)
    {
        if (wavfile.loaded == 1)
            std::free(wavfile.data);
        else
            delete[] wavfile.data;
    }

    wavfile.length = 1;
    wavfile.data   = EMPTY_BUFFER;
    wavfile.pos    = 0;
    wavfile.loaded = false;
}

// SDL Audio Callback Function
extern "C" void fill_audio(void *udata, Uint8 *stream, int len)
{
    (void)udata;
    if (!dsp_buffer || dsp_buffer_bytes <= 0 || len <= 0)
    {
        if (len > 0) std::memset(stream, 0, static_cast<size_t>(len));
        return;
    }

    int underflow_amount = 0;
#define MAX_SAMPLE_SIZE 4
    static char last_bytes[MAX_SAMPLE_SIZE];

    const int write_pos = dsp_write_pos.load(std::memory_order_acquire);
    int read_pos = dsp_read_pos.load(std::memory_order_relaxed);
    int gap = write_pos - read_pos;

    if (gap < 0) gap = 0;

    if (gap < len)
    {
        underflow_amount = len - gap;
        len = gap;
    }

    const int newpos = read_pos + len;

    if (len > 0)
    {
        if (newpos / dsp_buffer_bytes == read_pos / dsp_buffer_bytes)
        {
            std::memcpy(stream, dsp_buffer + (read_pos % dsp_buffer_bytes), static_cast<size_t>(len));
        }
        else
        {
            const int first_part_size = dsp_buffer_bytes - (read_pos % dsp_buffer_bytes);
            std::memcpy(stream, dsp_buffer + (read_pos % dsp_buffer_bytes), static_cast<size_t>(first_part_size));
            std::memcpy(stream + first_part_size, dsp_buffer, static_cast<size_t>(len - first_part_size));
        }

        if (len >= bytes_per_sample && bytes_per_sample <= MAX_SAMPLE_SIZE)
        {
            std::memcpy(last_bytes, stream + len - bytes_per_sample, static_cast<size_t>(bytes_per_sample));
        }
    }

    if (underflow_amount > 0 && bytes_per_sample > 0)
    {
        const int num_samples = underflow_amount / bytes_per_sample;
        for (int i = 0; i < num_samples; i++)
        {
            std::memcpy(stream + len + i * bytes_per_sample, last_bytes, static_cast<size_t>(bytes_per_sample));
        }
    }

    dsp_read_pos.store(newpos, std::memory_order_release);
    callbacktick.store(SDL_GetTicks(), std::memory_order_release);
}

#endif
