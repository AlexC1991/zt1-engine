#ifndef AMBIENT_HPP
#define AMBIENT_HPP

#include <random>
#include <string>
#include <functional>
#include <vector>

#include <SDL3/SDL.h>

#include "Animation.hpp"

class ResourceManager;
class WorldMap;
class WorldRenderer;

// The birds that fly over the zoo (ambient.cfg's ambient/*.ai: seagulls,
// crows, vultures, ...; zoo.exe ZTAmbientAnimal): the kinds the scenario
// lists in its [ambients] (id = cNameID) come one each every cFrequency
// seconds, each from a map edge straight across in one of eight ways, at
// its cSpeed, flapping, its shadow (cForceShadowBlack: in black) on the
// ground under it. The ones with none (the witch, Santa, the biplane) come
// on their days.
class Ambient {
  friend class DayNightLighting; // addition: same flying poses/altitude for optional sun shadows.
public:
  void load(ResourceManager *rm);
  // A new zoo or a zoo loaded (zoo.exe 0x4c6a3e: the sleigh's and the
  // biplane's sounds stopped, their once-a-day marks cleared)
  void clear();
  // The scenario's [ambients] ids (cNameID); empty: every kind (ours, for a
  // zoo loaded without its scenario)
  void setAllowed(const std::vector<int> &ids) { this->allowed = ids; }
  // One more kind on the list (zoo.exe 0x59148e: no check for one there)
  // (ours: an empty list already lets every kind come)
  void allow(int nameId) {
    if (!this->allowed.empty())
      this->allowed.push_back(nameId);
  }
  bool allows(int kind) const;
  // The game's date (the witch at Halloween, Santa at Christmas)
  void setDate(int day, int month) {
    this->day = day;
    this->month = month;
  }
  void update(float seconds, const WorldMap &map);
  // A guest come in at the gate (zoo.exe 0x4f6e3c -> 0x4f7ebe): the special
  // days' flyers
  void guestArrived(const WorldMap &map);
  // The shadows go on the ground (before the map's objects), the flyers
  // over everything
  void drawShadows(SDL_Renderer *renderer, const WorldRenderer &view, const WorldMap &map) const;
  void drawFlyers(SDL_Renderer *renderer, const WorldRenderer &view, const WorldMap &map) const;

  int count() const { return static_cast<int>(this->flyers.size()); }
  int sent() const { return this->sentCount; } // (sent since the last clear)
  // (for the checks: one in flight's kind, place and way)
  bool flyer(int i, int &kind, float &x, float &y, float &vx, float &vy) const {
    if (i < 0 || i >= static_cast<int>(this->flyers.size()))
      return false;
    const Flyer &f = this->flyers[i];
    kind = f.kind; x = f.x; y = f.y; vx = f.vx; vy = f.vy;
    return true;
  }
  // Sends one across now (for tests); -1 any by frequency; its index in
  // the flyers, -1 none
  int spawn(const WorldMap &map, int kind = -1);
  // (for the checks: one in flight's own sound - the sleigh's, the
  // biplane's - and its channel, -1 none or stopped)
  bool ownSound(int i, std::string &path, int &atten, int &channel) const {
    if (i < 0 || i >= static_cast<int>(this->flyers.size()))
      return false;
    const Flyer &f = this->flyers[i];
    path = f.own; atten = f.ownAtten; channel = f.ownChannel;
    return true;
  }
  int frequencyOf(int kind) const {
    return kind >= 0 && kind < static_cast<int>(this->kinds.size()) ? this->kinds[kind].frequency : 0;
  }
  int kindOf(int nameId) const {
    for (size_t i = 0; i < this->kinds.size(); i++)
      if (this->kinds[i].nameId == nameId)
        return static_cast<int>(i);
    return -1;
  }
  // Saved and loaded (ours: the engine's part of a save, version 21; the
  // original's part writes them as its ambient records, zoo.exe
  // ZTAmbientAnimal::save 0x47a7fd)
  struct Saved {
    std::string key;
    float x = 0, y = 0, vx = 0, vy = 0, age = 0, life = 0;
    std::string own;
    int ownAtten = 0;
    // (for the original's part: its kind's cNameID, cSpeed, its way 0-7)
    int nameId = 0, way = 0;
    float speed = 0;
    // (its place in whole 64ths and the step left over, +0x158)
    int32_t x64 = 0, y64 = 0, stepLeft = 0;
  };
  // One back from the original's record: its kind, place (tiles), way (0-7,
  // +0x12c), cSpeed (+0x154) and the step left over (+0x158, thousandths of
  // a 64th); false: no such kind
  bool resume(const WorldMap &map, const std::string &key, float x, float y, int way, float speed,
              int stepLeft = 0);
  // The scenario's [ambients] ids as set (saved with the zoo: the original
  // makes its list again from the scenario a saved zoo names)
  const std::vector<int> &allowedIds() const { return this->allowed; }
  std::vector<Saved> saveFlyers() const;
  void loadFlyers(const std::vector<Saved> &saved);
  double worldClock() const { return this->clock; }
  void setWorldClock(double c) { this->clock = c; }
  // Each kind's last (by key) and the once-a-day marks
  std::vector<std::pair<std::string, double>> lasts() const;
  void setLasts(const std::vector<std::pair<std::string, double>> &l);
  // A saved zoo loaded: every kind's last back to 0, so each comes at once
  // (zoo.exe 0x59148e adds each again with last 0)
  void dueNow();
  bool santaSent = false, biplaneSent = false; // zoo.exe 0x63e256, 0x63e25c
  // One at a place, going a way (tiles, tiles a second) for a while
  void spawnAt(int kind, float x, float y, float vx, float vy, float life) {
    if (kind >= 0 && kind < static_cast<int>(this->kinds.size())) {
      Flyer f;
      f.kind = kind; f.x = x; f.y = y; f.vx = vx; f.vy = vy; f.life = life;
      this->flyers.push_back(f);
    }
  }
  std::function<bool(float x, float y, float &dx, float &dy)> viewOffset;
  int kindIndex(const std::string &key) const {
    for (size_t i = 0; i < this->kinds.size(); i++)
      if (this->kinds[i].key == key)
        return static_cast<int>(i);
    return -1;
  }

private:
  struct Kind {
    std::string key;
    float speed = 50;   // cSpeed: 64ths of a tile a second on each axis
    int nameId = 0;     // cNameID: the scenario's [ambients] id
    int frequency = 0;  // cFrequency: seconds between one and the next (0: never)
    double last = 0;    // when the last one came (the world's seconds)
    bool blackShadow = true;
    Animation *idle = nullptr, *shadow = nullptr;
    // Its call (cSoundName: hawk.cfg etc., a 1 in chance try a second),
    // or a loop while it flies (cSoundLoop: the biplane, the witch)
    int chance = 0;
    bool loop = false;
    std::vector<std::pair<std::string, int>> calls;
  };
  struct Flyer {
    int kind = 0;
    float x = 0, y = 0;   // tiles
    float vx = 0, vy = 0; // tiles a second
    float age = 0;
    float life = 0;       // seconds until it's off the map
    float soundClock = 0;
    int channel = -1;
    // Its own sound loop, given by the special day that sent it (zoo.exe
    // 0x4f7ebe: sounds/sleigh.wav, sounds/biplane.wav 1000 quieter)
    std::string own;
    int ownAtten = 0, ownChannel = -1;
  };
  std::vector<Kind> kinds;
  std::vector<Flyer> flyers;
  double clock = 0; // the world's seconds (zoo.exe world +0x204)
  int sentCount = 0;
  std::vector<int> allowed;
  int day = 1, month = 0;
  std::mt19937 rng{12345};

  float groundAt(const WorldMap &map, float x, float y) const;
  void ended(Flyer &f);
  static void originalPlace(const Flyer &f, int32_t &x64, int32_t &y64, int32_t &stepLeft);
  CompassDirection facing(const WorldRenderer &view, const Flyer &f) const;
};

#endif // AMBIENT_HPP
