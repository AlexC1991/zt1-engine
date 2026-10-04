#ifndef AMBIENT_HPP
#define AMBIENT_HPP

#include <random>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "Animation.hpp"

class ResourceManager;
class WorldMap;
class WorldRenderer;

// The birds that fly over the zoo (ambient.cfg's ambient/*.ai: seagulls,
// crows, vultures, ...): now and then one crosses the map in a straight
// line at its cSpeed, flapping, its shadow (cForceShadowBlack: in black)
// on the ground under it. Which one comes is weighted by cFrequency; the
// ones with none (the witch, Santa, the biplane) come on their days.
class Ambient {
public:
  void load(ResourceManager *rm);
  void clear() { this->flyers.clear(); }
  // The game's date (the witch at Halloween, Santa at Christmas)
  void setDate(int day, int month) {
    this->day = day;
    this->month = month;
  }
  void update(float seconds, const WorldMap &map);
  // The shadows go on the ground (before the map's objects), the flyers
  // over everything
  void drawShadows(SDL_Renderer *renderer, const WorldRenderer &view, const WorldMap &map) const;
  void drawFlyers(SDL_Renderer *renderer, const WorldRenderer &view, const WorldMap &map) const;

  int count() const { return static_cast<int>(this->flyers.size()); }
  // Sends one across now (for tests); -1 any by frequency
  void spawn(const WorldMap &map, int kind = -1);
  // One at a place, going a way (tiles, tiles a second) for a while
  void spawnAt(int kind, float x, float y, float vx, float vy, float life) {
    if (kind >= 0 && kind < static_cast<int>(this->kinds.size()))
      this->flyers.push_back({kind, x, y, vx, vy, 0.0f, life});
  }
  int kindIndex(const std::string &key) const {
    for (size_t i = 0; i < this->kinds.size(); i++)
      if (this->kinds[i].key == key)
        return static_cast<int>(i);
    return -1;
  }

private:
  struct Kind {
    std::string key;
    float speed = 50;   // cSpeed: screen pixels a second
    int frequency = 0;  // cFrequency: how often, against the others
    bool blackShadow = true;
    Animation *idle = nullptr, *shadow = nullptr;
  };
  struct Flyer {
    int kind = 0;
    float x = 0, y = 0;   // tiles
    float vx = 0, vy = 0; // tiles a second
    float age = 0;
    float life = 0;       // seconds until it's off the map
  };
  std::vector<Kind> kinds;
  std::vector<Flyer> flyers;
  float nextIn = 8.0f;
  int day = 1, month = 0;
  bool specialFlown = false;
  std::mt19937 rng{12345};

  float groundAt(const WorldMap &map, float x, float y) const;
  CompassDirection facing(const WorldRenderer &view, const Flyer &f) const;
};

#endif // AMBIENT_HPP
