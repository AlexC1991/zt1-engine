// addition: themed celebrations (Features::celebrations; Celebrations.hpp).
// Look and sound only; nothing here reads or changes the calendar, goals,
// books or rating.
#include "Celebrations.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

#include "Sdl3Compat.hpp"

#include "Ambient.hpp"
#include "Features.hpp"
#include "Guests.hpp"
#include "PlacedObjects.hpp"
#include "World.hpp"

namespace {
// The flyers' cNameIDs and own sounds, as Ambient::guestArrived sends them
// (zoo.exe 0x4f7ebe): the biplane 5082, Santa 5083 (sounds/sleigh.wav), the
// witch 5084 (none)
constexpr int kBiplane = 5082, kSanta = 5083, kWitch = 5084;

// addition: a day's colours, all the game's own - guests' shirt, then the pick
// for a man or boy and for a woman or girl (zoo.exe 0x60739a, as the rename
// cheats and the Halloween start use it); buildings' two parts (0x6072e4).
// Halloween is zoo.exe 0x591426 (applyHolidayStart)'s own 10 / 2 / 11 and
// 7 / 5, here for the zoo's 31 October; Valentine's Day takes the "Mr. Pink"
// cheat's guest colours 8 / 0 / 0 (0x60739a, kColours in RenameCheats.cpp)
// and leaves the buildings alone. Put on over the saved colours and put
// back, never kept (suspend, before a save; switched off)
struct Look {
  bool guests = false, buildings = false;
  int shirt = 0, male = 0, female = 0, part0 = 0, part1 = 0;
  bool operator==(const Look &o) const {
    return guests == o.guests && buildings == o.buildings && shirt == o.shirt && male == o.male &&
           female == o.female && part0 == o.part0 && part1 == o.part1;
  }
  bool operator!=(const Look &o) const { return !(*this == o); }
};
const Look kNone{};
const Look kHalloween{true, true, 10, 2, 11, 7, 5};
const Look kValentine{true, false, 8, 0, 0, 0, 0};

struct GuestRec {
  int original[2], applied[2];
};
struct ObjectRec {
  std::vector<int> original, applied;
};
std::map<int, GuestRec> g_guests;   // by guest id
std::map<int, ObjectRec> g_objects; // by object id
Look g_look;                        // the colours in force
long g_lastKey = -1;                // the date last handled

// addition: the birthday's fireworks. Each burst is a ring of sparks thrown
// out from a point in the sky over the zoo, slowing and falling as it fades.
// Their own random numbers (never std::rand, which the game's rules use)
struct Burst {
  float x, y;     // where it bursts, as a share of the view (0-1)
  float age, life; // seconds
  float speed;    // how far the sparks are thrown, a share of the view's height a second
  int sparks;
  float turn;     // the ring's own turn
  SDL_Color colour;
};
std::vector<Burst> g_bursts;
constexpr float kRise = 0.45f; // (seconds the rocket climbs before it bursts)
bool g_fireworks = false;
float g_launchIn = 0;
uint32_t g_fireworkSeed = 0x2001u;
float fireworkRandom() { // 0..1
  g_fireworkSeed = g_fireworkSeed * 1664525u + 1013904223u;
  return static_cast<float>((g_fireworkSeed >> 8) & 0xffffff) / static_cast<float>(0x1000000);
}
// Bright colours a firework night wants, close to the game's own reds, golds,
// greens, blues and pinks
const SDL_Color kFireworkColours[] = {{255, 70, 60, 255},  {255, 200, 60, 255}, {120, 230, 90, 255},
                                      {90, 160, 255, 255}, {240, 110, 230, 255}, {255, 255, 230, 255}};

// The colours put back wherever they are still ours (a cheat or the player
// having changed one since leaves it alone)
void revertAll(World &world) {
  if (!g_guests.empty())
    for (Guests::Guest &g : world.getGuests().mutableGuests()) {
      auto it = g_guests.find(g.id);
      if (it == g_guests.end())
        continue;
      if (g.colours[0] == it->second.applied[0] && g.colours[1] == it->second.applied[1]) {
        g.colours[0] = it->second.original[0];
        g.colours[1] = it->second.original[1];
      }
    }
  if (!g_objects.empty()) {
    PlacedObjects &objects = world.objectsMutable();
    for (PlacedObjects::Object &o : objects.all()) {
      auto it = g_objects.find(o.id);
      if (it == g_objects.end() || o.colours != it->second.applied)
        continue;
      const PlacedObjects::Colouring *c = objects.colouringOf(PlacedObjects::fileOf(o));
      const size_t parts = c ? c->parts.size() : 0;
      for (size_t p = 0; p < parts; p++)
        objects.setColour(o, static_cast<int>(p), p < it->second.original.size() ? it->second.original[p] : -1);
      o.colours = it->second.original; // (exactly as it was: none picked stays none)
    }
  }
  g_guests.clear();
  g_objects.clear();
}

// The day's colours on every guest and building not yet wearing them
void applyLook(World &world, const Look &look) {
  if (look.guests) {
    Guests &guests = world.getGuests();
    for (Guests::Guest &g : guests.mutableGuests()) {
      if (g.type < 0 || g.type >= static_cast<int>(guests.types().size()) || g_guests.count(g.id))
        continue;
      GuestRec r{{g.colours[0], g.colours[1]}, {}};
      g.colours[0] = look.shirt;
      g.colours[1] = guests.types()[g.type].female ? look.female : look.male;
      r.applied[0] = g.colours[0];
      r.applied[1] = g.colours[1];
      g_guests[g.id] = r;
    }
  }
  if (look.buildings) {
    PlacedObjects &objects = world.objectsMutable();
    for (PlacedObjects::Object &o : objects.all()) {
      if (o.subClass != "building" || g_objects.count(o.id))
        continue;
      const PlacedObjects::Colouring *c = objects.colouringOf(PlacedObjects::fileOf(o));
      if (!c || c->parts.empty())
        continue;
      ObjectRec r;
      r.original = o.colours;
      objects.setColour(o, 0, look.part0);
      if (c->parts.size() > 1)
        objects.setColour(o, 1, look.part1);
      r.applied = o.colours;
      g_objects[o.id] = r;
    }
  }
}
} // namespace

bool Celebrations::newYear(int day, int month) { return (month == 11 && day == 31) || (month == 0 && day == 1); }
bool Celebrations::valentine(int day, int month) { return month == 1 && day == 14; }
bool Celebrations::birthday(int day, int month, int year) { return month == 0 && day == 1 && year >= 2; }
bool Celebrations::halloweenWeek(int day, int month) { return month == 9 && day >= 24 && day <= 31; }
bool Celebrations::halloweenDay(int day, int month) { return month == 9 && day == 31; }
bool Celebrations::christmasWeek(int day, int month) { return month == 11 && day >= 18 && day <= 25; }

std::string Celebrations::news(int day, int month, int year) {
  // (three days before each: 28 Dec, 29 Dec, 11 Feb, 21 Oct, 15 Dec)
  if (month == 11 && day == 28)
    return "New Year is three days away. Look to the skies as the old year ends.";
  if (month == 11 && day == 29)
    return "Your zoo turns " + std::to_string(year) + (year == 1 ? " year old" : " years old") +
           " in three days. There will be fireworks to mark it.";
  if (month == 1 && day == 11)
    return "Valentine's Day is three days away. Your guests are dressing for it.";
  if (month == 9 && day == 21)
    return "Halloween week starts in three days. Keep an eye out for witches.";
  if (month == 11 && day == 15)
    return "Christmas week starts in three days. Santa will be flying over the zoo.";
  return "";
}

void Celebrations::update(World &world, int day, int month, int year,
                          const std::function<void(const std::string &)> &postNews) {
  if (!Features::celebrations) {
    // (switched off: whatever it changed put back, and today handled afresh
    // if it's switched on again)
    if (g_look != kNone || !g_guests.empty() || !g_objects.empty()) {
      revertAll(world);
      g_look = kNone;
    }
    g_fireworks = false;
    g_bursts.clear();
    g_lastKey = -1;
    return;
  }
  const long key = (static_cast<long>(year) * 13 + month) * 40 + day;
  if (key != g_lastKey) {
    g_lastKey = key;
    const std::string line = news(day, month, year);
    if (!line.empty() && postNews)
      postNews(line);
    // The day's flyovers: New Year's biplane; the birthday (the same 1
    // January, from year 2) has fireworks all day as well
    Ambient &ambient = world.getAmbient();
    if (newYear(day, month))
      ambient.sendFlyer(world.getMap(), kBiplane, "sounds/biplane.wav", 1000);
    const bool fireworks = birthday(day, month, year);
    if (fireworks && !g_fireworks)
      g_launchIn = 0;
    g_fireworks = fireworks;
    if (halloweenWeek(day, month))
      ambient.sendFlyer(world.getMap(), kWitch, nullptr, 0);
    if (christmasWeek(day, month))
      ambient.sendFlyer(world.getMap(), kSanta, "sounds/sleigh.wav", 0);
  }
  const Look want = halloweenDay(day, month) ? kHalloween : valentine(day, month) ? kValentine : kNone;
  if (want != g_look) {
    revertAll(world);
    g_look = want;
  }
  if (g_look != kNone)
    applyLook(world, g_look);
}

void Celebrations::suspend(World &world) { revertAll(world); }

void Celebrations::reset() {
  g_guests.clear();
  g_objects.clear();
  g_look = kNone;
  g_lastKey = -1;
  g_fireworks = false;
  g_bursts.clear();
  g_launchIn = 0;
}

int Celebrations::recolouredGuests() { return static_cast<int>(g_guests.size()); }
int Celebrations::recolouredBuildings() { return static_cast<int>(g_objects.size()); }

bool Celebrations::fireworksOn() { return g_fireworks; }
int Celebrations::fireworkBursts() { return static_cast<int>(g_bursts.size()); }

// addition: a burst launched about every third of a second while the birthday
// lasts (a game day is 12 real seconds); the ones in the sky finish when it ends
void Celebrations::tickFireworks(float seconds) {
  if (seconds <= 0)
    return;
  for (Burst &b : g_bursts)
    b.age += seconds;
  g_bursts.erase(std::remove_if(g_bursts.begin(), g_bursts.end(), [](const Burst &b) { return b.age >= b.life; }),
                 g_bursts.end());
  if (!g_fireworks || !Features::celebrations)
    return;
  g_launchIn -= seconds;
  while (g_launchIn <= 0 && g_bursts.size() < 24) {
    Burst b;
    b.x = 0.12f + 0.76f * fireworkRandom();
    b.y = 0.08f + 0.30f * fireworkRandom();
    b.age = -g_launchIn; // (how long ago it was due, so a long step still spreads them out)
    b.life = kRise + 1.4f + 0.8f * fireworkRandom();
    b.speed = 0.20f + 0.12f * fireworkRandom();
    b.sparks = 34 + static_cast<int>(22 * fireworkRandom());
    b.turn = 6.2831853f * fireworkRandom();
    b.colour = kFireworkColours[static_cast<int>(fireworkRandom() * 6) % 6];
    g_bursts.push_back(b);
    g_launchIn += 0.25f + 0.25f * fireworkRandom();
  }
}

void Celebrations::drawFireworks(SDL_Renderer *renderer, const SDL_Rect &view) {
  if (g_bursts.empty() || !renderer || view.w <= 0 || view.h <= 0 || !Features::celebrations)
    return;
  thread_local std::vector<ZT_Vertex> verts;
  thread_local std::vector<int> idx;
  verts.clear();
  idx.clear();
  // a square of one colour
  auto dot = [&](float x, float y, float half, SDL_Color col) {
    const int base = static_cast<int>(verts.size());
    verts.push_back({{x - half, y - half}, col, {0, 0}});
    verts.push_back({{x + half, y - half}, col, {0, 0}});
    verts.push_back({{x + half, y + half}, col, {0, 0}});
    verts.push_back({{x - half, y + half}, col, {0, 0}});
    for (int i : {0, 1, 2, 0, 2, 3})
      idx.push_back(base + i);
  };
  // a streak from (x0, y0) to (x1, y1), w wide, fading to nothing at its tail
  auto streak = [&](float x0, float y0, float x1, float y1, float w, SDL_Color head) {
    float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f)
      return;
    const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
    SDL_Color tail = head;
    tail.a = 0;
    const int base = static_cast<int>(verts.size());
    verts.push_back({{x0 + nx * 0.4f, y0 + ny * 0.4f}, tail, {0, 0}});
    verts.push_back({{x1 + nx, y1 + ny}, head, {0, 0}});
    verts.push_back({{x1 - nx, y1 - ny}, head, {0, 0}});
    verts.push_back({{x0 - nx * 0.4f, y0 - ny * 0.4f}, tail, {0, 0}});
    for (int i : {0, 1, 2, 0, 2, 3})
      idx.push_back(base + i);
  };
  auto alpha = [](SDL_Color c, float a) {
    c.a = static_cast<Uint8>(std::clamp(a, 0.0f, 1.0f) * 255);
    return c;
  };
  // a soft round glow: full colour in the middle, nothing at its edge
  auto glow = [&](float x, float y, float r, SDL_Color col) {
    SDL_Color edge = col;
    edge.a = 0;
    const int base = static_cast<int>(verts.size());
    verts.push_back({{x, y}, col, {0, 0}});
    constexpr int kSides = 12;
    for (int i = 0; i < kSides; i++) {
      const float a = 6.2831853f * i / kSides;
      verts.push_back({{x + std::cos(a) * r, y + std::sin(a) * r}, edge, {0, 0}});
    }
    for (int i = 0; i < kSides; i++) {
      idx.push_back(base);
      idx.push_back(base + 1 + i);
      idx.push_back(base + 1 + (i + 1) % kSides);
    }
  };
  // each spark's own speed (0.55 - 1 of the burst's), the same every frame
  auto sparkSpeed = [](const Burst &b, int s) {
    const float h = std::sin(s * 12.9898f + b.turn * 78.233f) * 43758.5453f;
    return 0.55f + 0.45f * (h - std::floor(h));
  };
  const float size = std::max(3.0f, view.h / 180.0f);
  const SDL_Color white{255, 250, 235, 255};
  for (const Burst &b : g_bursts) {
    const float cx = view.x + b.x * view.w, cy = view.y + b.y * view.h;
    if (b.age < kRise) {
      // the rocket climbing to where it bursts, a short bright trail behind it
      const float k = b.age / kRise;
      const float rise = 0.30f * view.h * (1.0f - k) * (1.0f - k);
      streak(cx, cy + rise + 0.06f * view.h, cx, cy + rise, size * 0.8f, alpha(white, 0.9f));
      dot(cx, cy + rise, size * 0.5f, alpha(white, 1.0f));
      continue;
    }
    const float t = b.age - kRise, life = b.life - kRise;
    const float fade = 1.0f - t / life;
    // (thrown out fast, slowing; then sagging as the sparks fall)
    const float reach = b.speed * view.h * (1.0f - std::exp(-3.0f * t)) / 3.0f;
    const float drop = 0.06f * view.h * t * t;
    // the flash as it bursts, and the burst's glow in the sky
    if (t < 0.25f)
      glow(cx, cy, size * (4.0f + 16.0f * t), alpha(white, 0.75f * (1.0f - t / 0.25f)));
    glow(cx, cy + drop * 0.5f, reach * 1.1f + size * 4, alpha(b.colour, 0.10f * fade));
    for (int s = 0; s < b.sparks; s++) {
      const float a = b.turn + 6.2831853f * s / b.sparks;
      const float ca = std::cos(a), sa = std::sin(a);
      const float r = reach * sparkSpeed(b, s);
      const float hx = cx + ca * r, hy = cy + sa * r + drop;
      // a streak back toward the middle, the spark at its head, a soft glow
      const float trail = std::min(r * 0.5f, 0.07f * view.h);
      streak(hx - ca * trail, hy - sa * trail - drop * 0.3f, hx, hy, size * 0.7f, alpha(b.colour, fade));
      glow(hx, hy, size * 1.6f, alpha(b.colour, 0.35f * fade));
      dot(hx, hy, size * 0.45f, alpha(t < 0.25f ? white : b.colour, std::min(1.0f, fade * 1.3f)));
      // a few sparks twinkle as they fade
      if (fade < 0.45f && ((s * 7 + static_cast<int>(b.age * 20)) % 5) == 0)
        dot(hx, hy, size * 0.6f, alpha(white, fade * 1.8f));
    }
  }
  if (verts.empty())
    return;
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  ZT_RenderGeometry(renderer, nullptr, verts.data(), static_cast<int>(verts.size()), idx.data(),
                    static_cast<int>(idx.size()));
}
