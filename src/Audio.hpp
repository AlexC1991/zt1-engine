#ifndef AUDIO_HPP
#define AUDIO_HPP

#include <cstddef>

#include <SDL3_mixer/SDL_mixer.h>

// ours: the engine's sound as it was on SDL2_mixer - numbered channels for
// the effects (32, the first free one taken), one stream for the music, the
// same 0-128 volumes and 0-255 stereo levels - on SDL3_mixer's tracks. The
// numbers are SDL2_mixer's, so every sound is as loud and as far left or
// right as it was (Sound.cpp: zoo.exe's attenuation in hundredths of a
// decibel, made a volume).
namespace Audio {

using Clip = MIX_Audio; // a sound, decoded whole

constexpr int kMaxVolume = 128; // (SDL2_mixer's MIX_MAX_VOLUME)

// The mixer on the default playback device; false if there's no sound
bool open();
void close();
bool isOpen();
// What the device plays at (for the log)
bool spec(int *frequency, int *channels);

// A WAV (or any SDL3_mixer reads) from memory; 0 and a log line if not
Clip *load(const void *data, size_t size);
void free(Clip *clip);

// A free channel playing it (loops -1: for ever): its number, or -1
int play(Clip *clip, int loops);
// (channel -1: every channel)
void halt(int channel);
void pause(int channel);
void resume(int channel);
bool playing(int channel);
// 0-128, as SDL2_mixer's Mix_Volume
void volume(int channel, int volume);
// 0-255 a side, as SDL2_mixer's Mix_SetPanning
void pan(int channel, int left, int right);

// The music
void playMusic(Clip *clip, int loops);
bool musicPlaying();
void fadeOutMusic(int ms);
void haltMusic();
void pauseMusic();
void resumeMusic();
void musicVolume(int volume); // 0-128

} // namespace Audio

#endif // AUDIO_HPP
