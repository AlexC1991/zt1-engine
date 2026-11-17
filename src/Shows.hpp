#ifndef SHOWS_HPP
#define SHOWS_HPP

#include <map>
#include <string>
#include <vector>
#include "Fences.hpp"
#include "Guests.hpp"

class World;
class ResourceManager;

// Marine Mania's linked tanks, species scripts and show actors. See
// docs/MARINE_SHOWS.md for original addresses and the recorded comparison.
class Shows {
  friend std::string runShowManagementChecks(class World &, ResourceManager *);
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
    // Show Finances by month (zoo.exe's show record, rolled by rollMonthlyB
    // 0x48b57e, shown by 0x473b61): attendance +0x7c this month, +0x80 last
    // (visitors is +0x84, the total); income +0x94, +0x98 (income is +0x9c);
    // upkeep +0x88 this month - its stands' monthly upkeep (0x59e8f0) -,
    // +0x8c last, +0x90 the total with this month's in it
    int visitorsNow = 0, visitorsLast = 0;
    double incomeNow = 0, incomeLast = 0;
    double upkeepNow = 0, upkeepLast = 0, upkeepTotal = 0;
    double openedDay = -1; // the zoo's day the show tank formed (-1: not yet known)
    std::string status;
    bool sent = false;       // transient: 11491 posted for this gathering (zoo.exe 0x5a3de4)
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
  int standShow(int object) const;
  bool isStand(int object) const {return standLinks.count(object)!=0;}
  bool setStandAdmission(int object,double price);
  struct AudienceStats { size_t admitted=0; } audienceStats;
  // zoo.ini [AI] maxDistToGoToShow (zoo.exe 0x440f0c, read once in init)
  int maxDistToGoToShow = 150;
  // A guest's show pick (zoo.exe 0x440f0c, chooseTask at 0x440eb7): true
  // when it's off to a grandstand
  bool pickShow(Guests::Guest &g);
  // A grandstand open to guests (its +0x19c): its show started and not over
  bool standOpen(int object) const;
  bool start(int tank, std::string &why);
  void cancel(int tank, const std::string &why);
  // A specialist taken out of the zoo (fired): the show it works stops
  // (zoo.exe keeperRemoved 0x458759)
  void specialistRemoved(int id);
  int benefit(int tank);
  std::string describe() const;
  std::string command(const std::vector<std::string> &words);
  std::string snapshot(bool actors = true) const;
  bool restore(const std::string &text);
  // A line for the message bar: the original's text, its kind (messageBarAdd
  // 0x49c6d7: kinds 0-2 dropped as repeats, colour kind % 3) and the animal
  // it's about (-1: none)
  struct Message { std::string text; int kind = 0; int animal = -1; };
  std::vector<Message> takeMessages() { auto out=messages;messages.clear();return out; }
  double takeIncome() { double v=incomePending;incomePending=0;return v; }
  // The month's end: each show's month rolled (zoo.exe rollMonthlyB 0x48b57e)
  void newMonth();
  // Months in operation and the averages' divisor (zoo.exe 0x474d1f)
  int monthsOpen(const Show &s) const;

private:
  World *world = nullptr;
  ResourceManager *rm = nullptr;
  std::map<int, Show> list;
  std::map<std::string, std::vector<Trick>> definitions;
  double incomePending = 0;
  float refreshIn = 0;
  std::map<int,int> standLinks; // derived from the stand's facing, never shared between shows
  std::map<std::string,double> standUpkeep; // a stand's cUpkeep by its file
  void countUpkeep();
  void linkStands();
  std::vector<Message> messages;
  const Trick *find(const std::string &species, const std::string &key);
  std::vector<std::pair<Fences::Edge, int>> links(int tank) const;
  std::vector<Trick> resolved(Show &s, std::vector<int> *benefits = nullptr);
  std::string ready(Show &s) const;
  bool planStaging(Show &s);
  bool trainerStep(Show &s, float seconds, bool leaving = false);
  void trainerCue(Show &s, const std::string &behavior);
  void actorStep(Show &s, Actor &actor, float seconds);
  void audiences(Show &s, float seconds);
  void finish(Show &s);
  void releaseAudience(Show &s, bool cancelled);
  void notice(Show &s, const std::string &text);
  void post(int id, int kind, int animal, const std::vector<std::string> &values);
};

#endif
