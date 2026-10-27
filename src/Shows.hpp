#ifndef SHOWS_HPP
#define SHOWS_HPP

#include <map>
#include <string>
#include <vector>
#include "Fences.hpp"

class World;
class ResourceManager;

// Marine Mania's linked tanks, species scripts and show actors. See
// docs/MARINE_SHOWS.md for original addresses and the recorded comparison.
class Shows {
  friend class SaveGame;
public:
  struct Trick {
    std::string key, behavior, name, icon, pre, post;
    int id = 0, kind = 0, complexity = 0, satisfaction = 0, delta = 0;
    int depth = 0, building = 0;
    bool available = true, visible = true;
  };
  enum class Phase { Waiting, Gathering, Performing, Returning };
  struct Actor {
    int id = -1, home = -1, stage = 0, index = 0;
    int score = 0, completed = 0, skipped = 0, object = -1;
    float elapsed = 0;
    float stageX = 0, stageY = 0; // derived gathering positions, rebuilt on load
    Fences::Edge portal;
    std::vector<int> benefit;
  };
  struct Show {
    int tank = -1;
    std::string species;
    std::vector<std::string> script;
    int frequency = 1;
    double admission = 5.5;
    Phase phase = Phase::Waiting;
    float clock = 0, elapsed = 0;
    int trainer = -1;
    std::vector<Actor> actors;
    int completed = 0, cancelled = 0, lastScore = 0, tricksDone = 0, tricksSkipped = 0;
    int visitors = 0;
    double income = 0;
    std::string status;
  };
  struct Settings {
    int interval[3] = {65, 90, 120};
    int returnToKeeper = 5, maxLength = 20, idealMin = 5, idealMax = 7;
    int badTrick = 0, goodTrick = 4, greatTrick = 8;
    int badShow = 33, goodShow = 66, greatShow = 100;
    double admission = 5.5, admissionMin = 0, admissionMax = 12, admissionStep = 0.25;
    std::string startSound;
  } settings;

  void init(World *world, ResourceManager *rm);
  void clear();
  void refresh();
  void update(float seconds);
  Show *show(int tank);
  const Show *show(int tank) const;
  const std::map<int, Show> &all() const { return this->list; }
  const std::vector<Trick> &tricks(const std::string &species);
  std::vector<std::string> species(int tank) const;
  std::string unavailable(int tank, const std::string &species, const Trick &trick) const;
  bool selectSpecies(int tank, const std::string &file);
  bool add(int tank, const std::string &key);
  bool remove(int tank, int index);
  bool move(int tank, int index, int by);
  bool setFrequency(int tank, int frequency);
  bool setAdmission(int tank, double price);
  bool start(int tank, std::string &why);
  void cancel(int tank, const std::string &why);
  int benefit(int tank);
  std::string describe() const;
  std::string command(const std::vector<std::string> &words);
  std::string snapshot(bool actors = true) const;
  bool restore(const std::string &text);
  std::vector<std::string> takeMessages() { auto out=messages;messages.clear();return out; }
  double takeIncome() { double v=incomePending;incomePending=0;return v; }

private:
  World *world = nullptr;
  ResourceManager *rm = nullptr;
  std::map<int, Show> list;
  std::map<std::string, std::vector<Trick>> definitions;
  double incomePending = 0;
  std::vector<std::string> messages;
  const Trick *find(const std::string &species, const std::string &key);
  std::vector<std::pair<Fences::Edge, int>> links(int tank) const;
  std::vector<Trick> resolved(Show &s, std::vector<int> *benefits = nullptr);
  std::string ready(Show &s) const;
  bool planStaging(Show &s);
  bool trainerStep(Show &s, float seconds);
  void trainerCue(Show &s, const std::string &behavior);
  void actorStep(Show &s, Actor &actor, float seconds);
  void audiences(Show &s, float seconds);
  void finish(Show &s);
  void releaseAudience(Show &s, bool cancelled);
  void notice(Show &s, const std::string &text);
};

#endif
