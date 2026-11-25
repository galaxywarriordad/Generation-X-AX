#if USE_ALSA
#include "audiostream.h"
#include <alsa/asoundlib.h>
#include <algorithm>
#include <array>
#include <cstring>
#include "cfg/cfg.h"
#include "cfg/option.h"

namespace {
constexpr snd_pcm_uframes_t kSilenceChunkFrames = 256;
alignas(16) static const std::array<int16_t, kSilenceChunkFrames * 2> kZeroStereoFrames{};
} // namespace

class AlsaAudioBackend : public AudioBackend
{
	snd_pcm_t *handle = nullptr;
	bool pcm_blocking = true;
	snd_pcm_uframes_t buffer_size = 0;
	snd_pcm_uframes_t period_size = 0;
	snd_pcm_t *handle_record = nullptr;

public:
	AlsaAudioBackend()
		: AudioBackend("alsa", "Advanced Linux Sound Architecture") {}

	bool init() override
	{
		snd_pcm_hw_params_t *params = nullptr;
		const std::string device = config::loadStr("alsa", "device");
		std::string active_device = device;
		int rc = -1;

		if (!active_device.empty() && active_device != "auto")
		{
			rc = snd_pcm_open(&handle, active_device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
			if (rc < 0)
			{
				ERROR_LOG(AUDIO, "ALSA: unable to open PCM device %s: %s", active_device.c_str(), snd_strerror(rc));
				return false;
			}
		}
		else
		{
			INFO_LOG(AUDIO, "ALSA: trying to determine audio device");
			static constexpr const char *const kCandidates[] = {
				"default", "plughw:0,0,0", "plughw:0,0", "plughw:1,0", "pulse"
			};
			for (const char *cand : kCandidates)
			{
				active_device = cand;
				rc = snd_pcm_open(&handle, active_device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
				if (rc >= 0)
					break;
			}
			if (rc < 0)
			{
				INFO_LOG(AUDIO, "ALSA: unable to automatically determine audio device.");
				ERROR_LOG(AUDIO, "ALSA: unable to open PCM device %s: %s", active_device.c_str(), snd_strerror(rc));
				return false;
			}
		}

		INFO_LOG(AUDIO, "ALSA: Successfully initialized \"%s\"", active_device.c_str());

		snd_pcm_hw_params_alloca(&params);

		rc = snd_pcm_hw_params_any(handle, params);
		if (rc < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Error:snd_pcm_hw_params_any %s", snd_strerror(rc));
			term();
			return false;
		}

		rc = snd_pcm_hw_params_set_rate_resample(handle, params, 0);
		if (rc < 0)
		{
			WARN_LOG(AUDIO, "ALSA: Unable to disable resampling: %s", snd_strerror(rc));
		}

		rc = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
		if (rc < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Error:snd_pcm_hw_params_set_access %s", snd_strerror(rc));
			term();
			return false;
		}

		rc = snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
		if (rc < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Error:snd_pcm_hw_params_set_format %s", snd_strerror(rc));
			term();
			return false;
		}

		rc = snd_pcm_hw_params_set_channels(handle, params, 2);
		if (rc < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Error:snd_pcm_hw_params_set_channels %s", snd_strerror(rc));
			term();
			return false;
		}

		rc = snd_pcm_hw_params_set_rate(handle, params, 44100, 0);
		if (rc < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Error:snd_pcm_hw_params_set_rate %s", snd_strerror(rc));
			term();
			return false;
		}

		period_size = std::min<snd_pcm_uframes_t>(SAMPLE_COUNT, static_cast<snd_pcm_uframes_t>(config::AudioBufferSize) / 4);
		if (period_size == 0)
			period_size = 64;
		rc = snd_pcm_hw_params_set_period_size_near(handle, params, &period_size, nullptr);
		if (rc < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Error:snd_pcm_hw_params_set_periods_near %s", snd_strerror(rc));
			term();
			return false;
		}
		INFO_LOG(AUDIO, "ALSA: period size set to %zu", static_cast<size_t>(period_size));

		buffer_size = static_cast<snd_pcm_uframes_t>(config::AudioBufferSize);
		rc = snd_pcm_hw_params_set_buffer_size_near(handle, params, &buffer_size);
		if (rc < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Error:snd_pcm_hw_params_set_buffer_size_near %s", snd_strerror(rc));
			term();
			return false;
		}
		INFO_LOG(AUDIO, "ALSA: buffer size set to %lu", static_cast<unsigned long>(buffer_size));

		rc = snd_pcm_hw_params(handle, params);
		if (rc < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Unable to set hw parameters: %s", snd_strerror(rc));
			term();
			return false;
		}

		pcm_blocking = true;
		return true;
	}

	bool initRecord(u32 sampling_freq) override
	{
		int err;
		if ((err = snd_pcm_open(&handle_record, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Cannot open default audio capture device: %s", snd_strerror(err));
			return false;
		}
		snd_pcm_hw_params_t *hw_params = nullptr;
		if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Cannot allocate hardware parameter structure: %s", snd_strerror(err));
			snd_pcm_close(handle_record);
			handle_record = nullptr;
			return false;
		}
		if ((err = snd_pcm_hw_params_any(handle_record, hw_params)) < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Cannot initialize hardware parameter structure: %s", snd_strerror(err));
			snd_pcm_hw_params_free(hw_params);
			snd_pcm_close(handle_record);
			handle_record = nullptr;
			return false;
		}
		if ((err = snd_pcm_hw_params_set_access(handle_record, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Cannot set access type: %s", snd_strerror(err));
			snd_pcm_hw_params_free(hw_params);
			snd_pcm_close(handle_record);
			handle_record = nullptr;
			return false;
		}
		if ((err = snd_pcm_hw_params_set_format(handle_record, hw_params, SND_PCM_FORMAT_S16_LE)) < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Cannot set sample format: %s", snd_strerror(err));
			snd_pcm_hw_params_free(hw_params);
			snd_pcm_close(handle_record);
			handle_record = nullptr;
			return false;
		}
		if ((err = snd_pcm_hw_params_set_rate(handle_record, hw_params, sampling_freq, 0)) < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Cannot set sample rate to %u Hz: %s", sampling_freq, snd_strerror(err));
			snd_pcm_hw_params_free(hw_params);
			snd_pcm_close(handle_record);
			handle_record = nullptr;
			return false;
		}
		if ((err = snd_pcm_hw_params_set_channels(handle_record, hw_params, 1)) < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Cannot set channel count: %s", snd_strerror(err));
			snd_pcm_hw_params_free(hw_params);
			snd_pcm_close(handle_record);
			handle_record = nullptr;
			return false;
		}
		if ((err = snd_pcm_hw_params(handle_record, hw_params)) < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Cannot set parameters: %s", snd_strerror(err));
			snd_pcm_hw_params_free(hw_params);
			snd_pcm_close(handle_record);
			handle_record = nullptr;
			return false;
		}
		snd_pcm_hw_params_free(hw_params);
		snd_pcm_nonblock(handle_record, 1);
		if ((err = snd_pcm_prepare(handle_record)) < 0)
		{
			ERROR_LOG(AUDIO, "ALSA: Cannot prepare device: %s", snd_strerror(err));
			snd_pcm_close(handle_record);
			handle_record = nullptr;
			return false;
		}
		INFO_LOG(AUDIO, "ALSA: Successfully initialized capture device");
		return true;
	}

	void termRecord() override
	{
		if (handle_record)
		{
			snd_pcm_close(handle_record);
			handle_record = nullptr;
		}
	}

	u32 record(void *frame, u32 samples) override
	{
		if (!handle_record)
			return 0;
		snd_pcm_sframes_t err = snd_pcm_readi(handle_record, frame, samples);
		if (err < static_cast<snd_pcm_sframes_t>(samples))
		{
			snd_pcm_uframes_t valid = 0;
			if (err < 0)
			{
				DEBUG_LOG(AUDIO, "ALSA: Recording error: %s", snd_strerror(static_cast<int>(err)));
				snd_pcm_prepare(handle_record);
			}
			else
			{
				valid = static_cast<snd_pcm_uframes_t>(err);
			}
			u8 *buffer = static_cast<u8 *>(frame) + (valid * 2);
			std::memset(buffer, 0, (samples - valid) * 2);
			return static_cast<u32>(valid);
		}
		return samples;
	}

	u32 push(const void *frame, u32 samples, bool wait) override
	{
		if (!handle)
			return 0;

		if (wait != pcm_blocking)
		{
			snd_pcm_nonblock(handle, wait ? 0 : 1);
			pcm_blocking = wait;
		}

		snd_pcm_sframes_t rc = snd_pcm_writei(handle, frame, samples);
		if (rc < 0)
		{
			rc = snd_pcm_recover(handle, static_cast<int>(rc), 1);
			if (rc < 0 && snd_pcm_state(handle) == SND_PCM_STATE_XRUN)
			{
				snd_pcm_prepare(handle);
				snd_pcm_uframes_t remaining = (buffer_size > samples) ? (buffer_size - samples) : 0;
				while (remaining > 0)
				{
					const snd_pcm_uframes_t chunk = std::min<snd_pcm_uframes_t>(remaining, kSilenceChunkFrames);
					snd_pcm_writei(handle, kZeroStereoFrames.data(), chunk);
					remaining -= chunk;
				}
				snd_pcm_writei(handle, frame, samples);
			}
		}
		return 1;
	}

	void term() override
	{
		if (handle)
		{
			snd_pcm_drop(handle);
			snd_pcm_close(handle);
			handle = nullptr;
		}
	}

	std::vector<std::string> getDeviceList()
	{
		std::vector<std::string> result;
		char **hints = nullptr;
		int err = snd_device_name_hint(-1, "pcm", reinterpret_cast<void ***>(&hints));
		if (err != 0 || hints == nullptr)
			return result;

		result.emplace_back("auto");
		for (char **n = hints; *n != nullptr; ++n)
		{
			char *type = snd_device_name_get_hint(*n, "IOID");
			char *name = snd_device_name_get_hint(*n, "NAME");

			if (name != nullptr && (type == nullptr || std::strcmp(type, "Output") == 0))
				result.emplace_back(name);

			std::free(type);
			std::free(name);
		}

		snd_device_name_free_hint(reinterpret_cast<void **>(hints));
		return result;
	}

	Option *getOptions(int *count) override
	{
		*count = 1;
		static Option result;
		result.name = "device";
		result.caption = "Device";
		result.type = Option::list;
		result.values = getDeviceList();
		return &result;
	}
};
static AlsaAudioBackend alsaBackend;

#endif
