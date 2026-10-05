#ifndef SOUND_HPP
#define SOUND_HPP

#include <map>
#include <string>

#include "SDL_mixer.h"

class ResourceManager;

// The game's sound effects, played the way zoo.exe does (DirectSound, read
// in its disassembly): each sound has an attenuation in hundredths of a
// decibel (a sound file's own, the .ai [Sounds] second value, the options'
// master); over 6000 in all it isn't played. A sound on the map is heard
// from the middle of the view: 2.75 more a screen pixel away (10000 at
// most), panned 6 a pixel left or right. Loaded the first time they're
// played and kept.
class Sound {
public:
  static Sound &get();
  void init(ResourceManager *rm);

  // A sound file (with or without .wav), not on the map
  void play(const std::string &path, int atten = 0);
  // On the map, this many screen pixels from the middle of the view
  void playAt(const std::string &path, float dxPixels, float dyPixels, int atten = 0);
  // Looped (the world's ambience): its channel, or -1
  int loop(const std::string &path, int atten = 0);
  void stop(int channel);
  // A playing loop heard from somewhere else now (screen pixels from the
  // middle of the view, as playAt); false: out of earshot (stopped)
  bool place(int channel, float dxPixels, float dyPixels, int atten);
  // A sound on the map started looped at a point (building ambience)
  int loopAt(const std::string &path, float dxPixels, float dyPixels, int atten);
  // Everything holds while the game is paused
  void setPaused(bool paused);

  bool enabled = true;
  int master = 0; // the options' attenuation ([UI] userAttenuation)

private:
  ResourceManager *rm = nullptr;
  std::map<std::string, Mix_Chunk *> chunks;
  bool paused = false;
  Mix_Chunk *chunk(const std::string &path);
  int start(Mix_Chunk *c, int atten, float pan, int loops);
};

#endif // SOUND_HPP
