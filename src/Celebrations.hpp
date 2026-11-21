#ifndef CELEBRATIONS_HPP
#define CELEBRATIONS_HPP

#include <functional>
#include <string>

#include <SDL3/SDL.h>

class World;

// addition (Features::celebrations; #314): themed celebrations through the
// zoo's year, in looks and sound only - the zoo's calendar (12 real seconds
// a day, real month lengths), its goals, books and rating are never read or
// changed here. Built only from the game's own art and sounds:
//   New Year (31 Dec, 1 Jan)       the biplane over the zoo, its own sound
//   Valentine's Day (14 Feb)       guests in the Mr. Pink cheat's shirt colours
//   the zoo's birthday (1 Jan,     fireworks over the zoo all day, and news of
//     year 2 on)                     its age (a zoo always starts on 1 Jan of
//                                    year 1, so it shares the day with New
//                                    Year: both are celebrated). The game has
//                                    no firework art or sound, so the bursts
//                                    are drawn by the engine, silent
//   Halloween week (24-31 Oct)     a witch each day; on the 31st guests and
//                                  buildings in the original's Halloween colours
//   Christmas week (18-25 Dec)     Santa's sleigh each day
// plus news on the message bar three days ahead of each. The original's own
// dated events (25 Dec Santa, Holiday Tree and Snowman; 31 Oct the witch per
// guest and the Jack-O'-Lantern; 15 Sep the biplane; the PC clock's
// Halloween start) are Ambient's and RenameCheats' and are not touched.
// Months are 0-based (0 January), days 1-based, years from 1, as ZooSim.
namespace Celebrations {
// What a date holds (pure: for the checks too)
bool newYear(int day, int month);
bool valentine(int day, int month);
bool birthday(int day, int month, int year); // 1 January of year 2 on
bool halloweenWeek(int day, int month);      // 24-31 October
bool halloweenDay(int day, int month);       // 31 October
bool christmasWeek(int day, int month);      // 18-25 December
// The news for a date three days before a celebration ("" none)
std::string news(int day, int month, int year);

// Called each frame with the zoo's date: on a new date, the news and the
// day's flyovers; while a colour day lasts, guests and buildings (new ones
// too) in its colours; with the switch off, anything it changed put back.
// postNews puts a line on the message bar.
void update(World &world, int day, int month, int year, const std::function<void(const std::string &)> &postNews);
// The colours put back as they were (before a save is written, so no colour
// of ours is kept in a file) and put on again by the next update
void suspend(World &world);
// A zoo started or loaded: nothing of the old one is remembered
void reset();
// (for the checks) how many guests and buildings wear a celebration's colours
int recolouredGuests();
int recolouredBuildings();

// addition: the zoo's birthday fireworks. On while the birthday lasts (set by
// update); tickFireworks moves them on with the game's seconds (so a paused
// or sped-up zoo pauses or speeds them, and they never touch the game's
// random numbers); drawFireworks draws them over the zoo, inside the world's
// view and under the HUD
bool fireworksOn();
int fireworkBursts(); // (for the checks) bursts in the sky now
void tickFireworks(float seconds);
void drawFireworks(SDL_Renderer *renderer, const SDL_Rect &view);

// Held while a save is written
struct SaveGuard {
  explicit SaveGuard(World &w) : world(w) { suspend(w); }
  World &world;
};
} // namespace Celebrations

#endif // CELEBRATIONS_HPP
