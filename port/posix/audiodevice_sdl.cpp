/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

/*
** The audio output for the POSIX build, in place of code/audio/audiodevice_ma.cpp. It plays
** through SDL, which reaches RISC OS sound through UnixLib's /dev/dsp. The factory keeps the
** miniaudio name so that the engine code calling it is unchanged.
*/

#include "audio/audiodevice.h"
#include "dbgprint.h"
#include "stackprime.h"
#include "riscosdesktop.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <vector>

namespace {

// The stream open for output, for Audio_SDL_Pause_For_Desktop.
SDL_AudioStream * OpenStream = nullptr;

class SDLAudioDeviceClass : public AudioDeviceClass
{
	public:
		~SDLAudioDeviceClass(void) { Close(); }

		bool Open(unsigned rate, unsigned channels, RenderCallback callback, void * context) override;
		void Close(void) override;
		bool Start(void) override;
		void Stop(void) override;

		bool Is_Open(void) const override { return(Stream != nullptr); }
		bool Is_Running(void) const override { return(Running.load(std::memory_order_acquire)); }
		bool Is_Lost(void) const override { return(false); }
		unsigned Rate(void) const override { return(Stream != nullptr ? RateValue : 0); }
		unsigned Channels(void) const override { return(Stream != nullptr ? ChannelCount : 0); }
		unsigned Period_Frames(void) const override { return(Stream != nullptr ? PeriodFrames : 0); }
		unsigned Periods(void) const override { return(Stream != nullptr ? 2 : 0); }
		char const * Name(void) const override { return(Stream != nullptr ? "SDL" : ""); }

	private:
		static void SDLCALL Feed(void * self, SDL_AudioStream * stream, int additional, int total);

		SDL_AudioStream * Stream = nullptr;
		RenderCallback Callback = nullptr;
		void * Context = nullptr;
		unsigned RateValue = 0;
		unsigned ChannelCount = 0;
		unsigned PeriodFrames = 0;
		std::vector<float> Buffer;
		std::atomic<bool> Running{false};
};


void SDLCALL SDLAudioDeviceClass::Feed(void * self, SDL_AudioStream * stream, int additional, int)
{
	static thread_local bool primed = false;
	if (!primed) {
		Stack_Prime(STACK_PRIME_THREAD);
		primed = true;
	}

	SDLAudioDeviceClass * device = (SDLAudioDeviceClass *)self;
	unsigned const framebytes = device->ChannelCount * sizeof(float);
	while (additional > 0) {
		unsigned frames = (unsigned)additional / framebytes;
		if (frames == 0) {
			frames = 1;
		}
		if (frames > device->PeriodFrames) {
			frames = device->PeriodFrames;
		}
		device->Buffer.assign((size_t)frames * device->ChannelCount, 0.0f);
		if (device->Running.load(std::memory_order_acquire) && device->Callback != nullptr) {
			device->Callback(device->Context, device->Buffer.data(), frames);
		}
		SDL_PutAudioStreamData(stream, device->Buffer.data(), (int)(frames * framebytes));
		additional -= (int)(frames * framebytes);
	}
}


bool SDLAudioDeviceClass::Open(unsigned rate, unsigned channels, RenderCallback callback, void * context)
{
	Close();

	if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		DebugString("Audio: SDL audio did not start: %s\n", SDL_GetError());
		return(false);
	}

	Callback = callback;
	Context = context;
	RateValue = rate;
	ChannelCount = channels;
	PeriodFrames = 1024;

	SDL_AudioSpec spec;
	spec.format = SDL_AUDIO_F32;
	spec.channels = (int)channels;
	spec.freq = (int)rate;
	Stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, this);
	if (Stream == nullptr) {
		DebugString("Audio: no output: %s\n", SDL_GetError());
		return(false);
	}
	OpenStream = Stream;
	DebugString("Audio: SDL output %s at %u Hz, %u channels\n", SDL_GetCurrentAudioDriver(), rate, channels);
	return(true);
}


void SDLAudioDeviceClass::Close(void)
{
	if (Stream != nullptr) {
		Running.store(false, std::memory_order_release);
		if (OpenStream == Stream) {
			OpenStream = nullptr;
		}
		SDL_DestroyAudioStream(Stream);
		Stream = nullptr;
	}
}


bool SDLAudioDeviceClass::Start(void)
{
	if (Stream == nullptr) {
		return(false);
	}
	Running.store(true, std::memory_order_release);
	return(SDL_ResumeAudioStreamDevice(Stream));
}


void SDLAudioDeviceClass::Stop(void)
{
	if (Stream == nullptr) {
		return;
	}
	Running.store(false, std::memory_order_release);
	SDL_PauseAudioStreamDevice(Stream);
}

}


std::unique_ptr<AudioDeviceClass> Audio_Create_Miniaudio_Device(void)
{
	return(std::make_unique<SDLAudioDeviceClass>());
}


/*
** For the desktop (RISC OS, riscosdesktop.h): the output paused while the game is away, and
** back as it was; a stream the game had paused itself stays paused.
*/
void Audio_SDL_Pause_For_Desktop(bool pause)
{
	static bool paused_here = false;
	if (OpenStream == nullptr) {
		paused_here = false;
		return;
	}
	if (pause) {
		paused_here = !SDL_AudioStreamDevicePaused(OpenStream);
		if (paused_here) {
			SDL_PauseAudioStreamDevice(OpenStream);
		}
	} else if (paused_here) {
		SDL_ResumeAudioStreamDevice(OpenStream);
		paused_here = false;
	}
}
