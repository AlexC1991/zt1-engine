#include "Sound.hpp"

#include <algorithm>
#include <cmath>

#include "ResourceManager.hpp"
#include "Utils.hpp"

namespace {
constexpr int kInaudible = 6000;   // more attenuation than this: not played
constexpr int kMaxDistance = 10000;
constexpr float kPerPixel = 2.75f; // attenuation a screen pixel away
constexpr float kPanPerPixel = 6.0f;
} // namespace

Sound &Sound::get() {
  static Sound instance;
  return instance;
}

void Sound::init(ResourceManager *rm) {
  this->rm = rm;
  // Room for the many that can play at once (animals, guests, the UI)
  Mix_AllocateChannels(32);
}

Mix_Chunk *Sound::chunk(const std::string &raw) {
  std::string path = Utils::string_to_lower(raw);
  if (path.size() < 4 || path.compare(path.size() - 4, 4, ".wav") != 0)
    path += ".wav";
  auto it = this->chunks.find(path);
  if (it != this->chunks.end())
    return it->second;
  Mix_Chunk *c = nullptr;
  if (this->rm) {
    int size = 0;
    void *data = this->rm->getFileBytes(path, &size);
    if (data && size > 0) {
      if (SDL_RWops *rw = SDL_RWFromConstMem(data, size))
        c = Mix_LoadWAV_RW(rw, 1);
      if (!c)
        SDL_Log("[Sound] can't read %s: %s", path.c_str(), Mix_GetError());
    }
    free(data);
  }
  this->chunks[path] = c;
  if (c)
    SDL_Log("[Sound] loaded %s", path.c_str());
  return c;
}

int Sound::start(Mix_Chunk *c, int atten, float pan, int loops) {
  if (!c || !this->enabled)
    return -1;
  int total = std::max(1, this->master + atten);
  if (total > kInaudible)
    return -1;
  int channel = Mix_PlayChannel(-1, c, loops);
  if (channel < 0)
    return -1;
  // Hundredths of a decibel to a volume
  float gain = std::pow(10.0f, -total / 2000.0f);
  Mix_Volume(channel, std::clamp(static_cast<int>(std::lround(gain * MIX_MAX_VOLUME)), 0, MIX_MAX_VOLUME));
  float p = std::clamp(pan, -1.0f, 1.0f);
  Mix_SetPanning(channel, static_cast<Uint8>(std::lround(p > 0 ? 255 * (1 - p) : 255)),
                 static_cast<Uint8>(std::lround(p < 0 ? 255 * (1 + p) : 255)));
  return channel;
}

void Sound::play(const std::string &path, int atten) {
  if (!path.empty())
    this->start(this->chunk(path), atten, 0.0f, 0);
}

void Sound::playAt(const std::string &path, float dx, float dy, int atten) {
  if (path.empty())
    return;
  int away = std::min(kMaxDistance, static_cast<int>(std::hypot(dx, dy) * kPerPixel));
  if (atten + away + this->master > kInaudible)
    return;
  float pan = std::clamp(dx * kPanPerPixel, -10000.0f, 10000.0f) / 10000.0f;
  this->start(this->chunk(path), atten + away, pan, 0);
}

int Sound::loop(const std::string &path, int atten) {
  return path.empty() ? -1 : this->start(this->chunk(path), atten, 0.0f, -1);
}

bool Sound::place(int channel, float dx, float dy, int atten) {
  if (channel < 0)
    return false;
  int away = std::min(kMaxDistance, static_cast<int>(std::hypot(dx, dy) * kPerPixel));
  int total = std::max(1, this->master + atten + away);
  if (total > 9000) { // (zoo.exe: over 9000 a positional sound stops)
    Mix_HaltChannel(channel);
    return false;
  }
  float gain = std::pow(10.0f, -total / 2000.0f);
  Mix_Volume(channel, std::clamp(static_cast<int>(std::lround(gain * MIX_MAX_VOLUME)), 0, MIX_MAX_VOLUME));
  float p = std::clamp(dx * kPanPerPixel, -10000.0f, 10000.0f) / 10000.0f;
  Mix_SetPanning(channel, static_cast<Uint8>(std::lround(p > 0 ? 255 * (1 - p) : 255)),
                 static_cast<Uint8>(std::lround(p < 0 ? 255 * (1 + p) : 255)));
  return true;
}

int Sound::loopAt(const std::string &path, float dx, float dy, int atten) {
  int away = std::min(kMaxDistance, static_cast<int>(std::hypot(dx, dy) * kPerPixel));
  if (path.empty() || atten + away + this->master > kInaudible)
    return -1;
  float pan = std::clamp(dx * kPanPerPixel, -10000.0f, 10000.0f) / 10000.0f;
  return this->start(this->chunk(path), atten + away, pan, -1);
}

void Sound::stop(int channel) {
  if (channel >= 0)
    Mix_HaltChannel(channel);
}

void Sound::setPaused(bool paused) {
  if (paused == this->paused)
    return;
  this->paused = paused;
  if (paused)
    Mix_Pause(-1);
  else
    Mix_Resume(-1);
}
