#ifndef GAME_BENCHMARK_HPP
#define GAME_BENCHMARK_HPP

#include <SDL3/SDL.h>
#include <fstream>
#include <string>
#include <vector>

class World;
class ResourceManager;

// ours: opt-in sustained measurement of the production main loop. This has
// no player-facing setting and is inactive without ZT_GAME_BENCH_OUT.
class GameBenchmark {
public:
  bool enabled() const { return !out.empty(); }
  bool start(World &, ResourceManager *, SDL_Renderer *);
  void beforeFrame(World &);
  void beforeSimulation();
  void afterSimulation();
  void beforePresent(World &, SDL_Renderer *);
  bool afterPresent(World &, double pending, double discarded);
  int rate() const { return speed; }
  // ours: ZT_GAME_BENCH_ZOO=<file> - a player's saved zoo (already loaded by
  // main) timed as the player sees it: maximised, its own view, day and
  // night lighting at noon and at midnight, held still and panned, with
  // frames captured mid-pan and held still at the same place
  bool startZoo(World &, SDL_Renderer *);
private:
  bool zoo=false;
  int zooPhase=-1, zooFrame=0, panDir=1;
  float panCarry=0;
  Uint64 zooPhaseStart=0;
  std::vector<double> phaseMs;
  std::vector<std::pair<int,int>> shotPos;
  void zooFrameStep(World &);
  void zooPresent(World &, SDL_Renderer *);
  bool zooAfter(World &);
  std::string out, name;
  std::ofstream frames, status, checks;
  Uint64 epoch=0, simulationStart=0, recordStart=0, lastPresent=0;
  double warmup=20, minimum=180, simulationMs=0;
  int speed=1, rotation=0, phase=0, sample=0, lastProgress=-1;
  bool control=false, lighting=false, cycle=false, smoke=false;
  double startClock=0;
  void shot(SDL_Renderer *, const char *);
};

#endif
