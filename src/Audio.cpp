#include "Audio.hpp"

// ours: see Audio.hpp

#include <algorithm>
#include <vector>

namespace Audio {
namespace {

MIX_Mixer *g_mixer = nullptr;
std::vector<MIX_Track *> g_channels; // (SDL2_mixer's 32 channels: Sound::init)
std::vector<int> g_volume;           // each channel's 0-128 (kept as SDL2_mixer kept it, across sounds)
std::vector<MIX_StereoGains> g_pan;
MIX_Track *g_music = nullptr;
int g_musicVolume = kMaxVolume;
constexpr int kChannels = 32;

MIX_Track *track(int channel) {
  return channel >= 0 && channel < static_cast<int>(g_channels.size()) ? g_channels[channel] : nullptr;
}
float gain(int volume) { return std::clamp(volume, 0, kMaxVolume) / static_cast<float>(kMaxVolume); }

} // namespace

bool open() {
  if (g_mixer)
    return true;
  if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
    return false;
  if (!MIX_Init())
    return false;
  // (as SDL2_mixer was opened: 44.1 kHz stereo)
  SDL_AudioSpec want{SDL_AUDIO_S16, 2, 44100};
  g_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want);
  if (!g_mixer)
    return false;
  for (int i = 0; i < kChannels; i++)
    g_channels.push_back(MIX_CreateTrack(g_mixer));
  g_volume.assign(kChannels, kMaxVolume);
  g_pan.assign(kChannels, MIX_StereoGains{1.0f, 1.0f});
  g_music = MIX_CreateTrack(g_mixer);
  return true;
}

void close() {
  if (!g_mixer)
    return;
  MIX_DestroyMixer(g_mixer); // (its tracks with it)
  g_mixer = nullptr;
  g_channels.clear();
  g_music = nullptr;
  MIX_Quit();
}

bool isOpen() { return g_mixer != nullptr; }

bool spec(int *frequency, int *channels) {
  SDL_AudioSpec s{};
  if (!g_mixer || !MIX_GetMixerFormat(g_mixer, &s))
    return false;
  *frequency = s.freq;
  *channels = s.channels;
  return true;
}

Clip *load(const void *data, size_t size) {
  if (!g_mixer || !data || !size)
    return nullptr;
  SDL_IOStream *io = SDL_IOFromConstMem(data, size);
  return io ? MIX_LoadAudio_IO(g_mixer, io, true, true) : nullptr;
}

void free(Clip *clip) {
  if (clip)
    MIX_DestroyAudio(clip);
}

int play(Clip *clip, int loops) {
  if (!g_mixer || !clip)
    return -1;
  // (the first channel neither playing nor held, as Mix_PlayChannel(-1))
  for (int i = 0; i < static_cast<int>(g_channels.size()); i++) {
    MIX_Track *t = g_channels[i];
    if (!t || MIX_TrackPlaying(t) || MIX_TrackPaused(t))
      continue;
    if (!MIX_SetTrackAudio(t, clip))
      return -1;
    MIX_SetTrackGain(t, gain(g_volume[i]));
    MIX_SetTrackStereo(t, &g_pan[i]);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
    const bool ok = MIX_PlayTrack(t, props);
    SDL_DestroyProperties(props);
    return ok ? i : -1;
  }
  return -1;
}

void halt(int channel) {
  if (!g_mixer)
    return;
  if (channel < 0) {
    for (MIX_Track *t : g_channels)
      MIX_StopTrack(t, 0);
  } else if (MIX_Track *t = track(channel)) {
    MIX_StopTrack(t, 0);
  }
}

void pause(int channel) {
  if (channel < 0) {
    for (MIX_Track *t : g_channels)
      MIX_PauseTrack(t);
  } else if (MIX_Track *t = track(channel)) {
    MIX_PauseTrack(t);
  }
}

void resume(int channel) {
  if (channel < 0) {
    for (MIX_Track *t : g_channels)
      MIX_ResumeTrack(t);
  } else if (MIX_Track *t = track(channel)) {
    MIX_ResumeTrack(t);
  }
}

bool playing(int channel) {
  if (channel < 0) {
    for (MIX_Track *t : g_channels)
      if (MIX_TrackPlaying(t) || MIX_TrackPaused(t))
        return true;
    return false;
  }
  MIX_Track *t = track(channel);
  return t && (MIX_TrackPlaying(t) || MIX_TrackPaused(t));
}

void volume(int channel, int v) {
  if (MIX_Track *t = track(channel)) {
    g_volume[channel] = std::clamp(v, 0, kMaxVolume);
    MIX_SetTrackGain(t, gain(v));
  }
}

void pan(int channel, int left, int right) {
  if (MIX_Track *t = track(channel)) {
    g_pan[channel] = MIX_StereoGains{std::clamp(left, 0, 255) / 255.0f, std::clamp(right, 0, 255) / 255.0f};
    MIX_SetTrackStereo(t, &g_pan[channel]);
  }
}

void playMusic(Clip *clip, int loops) {
  if (!g_music || !clip || !MIX_SetTrackAudio(g_music, clip))
    return;
  MIX_SetTrackGain(g_music, gain(g_musicVolume));
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, loops);
  MIX_PlayTrack(g_music, props);
  SDL_DestroyProperties(props);
}

bool musicPlaying() { return g_music && (MIX_TrackPlaying(g_music) || MIX_TrackPaused(g_music)); }
void fadeOutMusic(int ms) {
  if (g_music)
    MIX_StopTrack(g_music, MIX_TrackMSToFrames(g_music, ms));
}
void haltMusic() {
  if (g_music)
    MIX_StopTrack(g_music, 0);
}
void pauseMusic() {
  if (g_music)
    MIX_PauseTrack(g_music);
}
void resumeMusic() {
  if (g_music)
    MIX_ResumeTrack(g_music);
}
void musicVolume(int v) {
  g_musicVolume = std::clamp(v, 0, kMaxVolume);
  if (g_music)
    MIX_SetTrackGain(g_music, gain(g_musicVolume));
}

} // namespace Audio
