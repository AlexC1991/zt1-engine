#ifndef GOALS_HPP
#define GOALS_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class ResourceManager;
class IniReader;

// The scenario goal engine (zoo.exe 0x58de8a loads, 0x4240f5 checks,
// 0x4fc783 fires): a map's .scn [goals] goal= and each extragoals= file's
// (freeform: donation.scn, awards.scn, unlock.scn). Every 1.5 s of game
// time each goal measures its rule (rulea, ruleb, arga, argb) against
// value by type; when its state changes to met it fires its trigger
// (trulea, truleb, targa, targb): a popup, a UI element disabled or enabled,
// cash, an award. Sticky goals fire once, ever.
class Goals {
public:
  struct Goal {
    std::string name, file;
    int rulea = 0, ruleb = 0, type = 0, value = 0, arga = 0, argb = 0;
    bool sticky = false, hidden = false, optional = false;
    int text = 0;
    int trulea = 0, truleb = 0, targa = 0, targb = 0;
    int state = 0;
    int depth = 0; // (a composite's child: 1, its children 2...)
    // (kept by our saves before composites and extragoals patterns were
    // read: a goal of a file named outright, not a composite's)
    bool legacy = false;
  };
  // The goal tree (zoo.exe makeGoal 0x58e066): a goal, or a composite of
  // child= goals - rtype 1 (0x59364c, class 0x6352f8) all of them, rtype 2
  // (0x593793, class 0x6352cc) them in order
  struct Node {
    int kind = 0;  // 0 a goal (goals[goal]), 1 all of, 2 in order
    int goal = -1;
    bool optional = false;
    int state = 0;
    std::string file, name;
    std::vector<Node> children;
  };
  // What the engine asks of the game
  struct Hooks {
    // A rule's measure (zoo.exe 0x41d665)
    std::function<int(const Goal &)> measure;
    // Whether measure knows the goal's rule (rules it doesn't know read 0)
    std::function<bool(const Goal &)> measures;
    // A popup: its picture and text; pause=1: the game waits on it
    std::function<void(const std::string &image, const std::string &text, bool pause)> popup;
    // A UI element disabled (true) or enabled; true when that changed it
    std::function<bool(int element, bool disable)> disable;
    std::function<void(int element, bool hide)> hide;
    // Cash: 0 booked and added, 1 added, 2 set to
    std::function<void(int amount, int mode)> cash;
    std::function<void(int stringId)> message;
    std::function<void(int award)> award;
    // What can be bought (goalFire 4, 7): kind 0 the types of those name
    // ids, 1 every animal, 2 every type; true when it changed anything
    std::function<bool(int kind, const std::vector<int> &ids, bool available)> available;
    // A scenario's reward kept for freeform (4/10: 0x598cdc)
    std::function<void(int nameId)> reward;
    // A message naming a type (4/2, 4/3: string stringId with its name)
    std::function<void(int stringId, int nameId)> messageAbout;
    // The months played (the game manager's month + year * 12, 0x42425d)
    std::function<int()> months;
    // The scenario over (allGoalsMet 0x48a726): 1 won, 2 lost
    std::function<void(int outcome)> ended;
  };

  void setHooks(Hooks hooks) { this->hooks = std::move(hooks); }
  // A map's goals (and its [start] triggers= fired once)
  void load(ResourceManager *rm, const std::string &scnPath);
  void clear();
  void update(float seconds);
  // Every goal checked now (tests)
  void evaluateAll();
  const std::vector<Goal> &all() const { return this->goals; }
  std::vector<Goal> &mutableAll() { return this->goals; }
  // Awards received, in order
  std::vector<int> awards;
  // A saved game's states put back: what they did done again, silently
  // (no popups, no cash: zoo.exe 0x595083)
  void restore(const std::vector<int> &states);

  // The script the goals were loaded from ("" when none)
  const std::string &script() const { return this->scnPath; }
  // One saved goal state of the original's list (goalsLoad 0x58de8a): the
  // scenario's own goal (ctor 0x58e218) first; then each [goals] goal= of
  // the script, then of every file each [start] extragoals= entry finds
  // (split at its '.': every resource starting with the part before and
  // ending with the part after, 0x4b41f2, so freeform/unlock.scn brings
  // unlock01..unlock20 too); a composite (rtype 1, 2) has its child= goals
  // straight after it. goal: a normal goal (rtype 0), with its rules.
  struct Slot {
    std::string file, name;
    bool goal = false, composite = false;
    int depth = 0; // (a composite's child: 1, its children 2...)
    Goal rules;
  };
  static std::vector<Slot> originalOrder(ResourceManager *rm, const std::string &scnPath);
  // The states an original saved game holds for this script, in that order:
  // each goal we track has its own; the rest keep base's (the zoo's saved
  // states, by index as zoo.exe reads them, 0x5950ab), or, for a goal of
  // the list's own (not a composite's child) still at 0 there whose rule we
  // measure, what it measures now. A saved zoo of version 85 or before
  // (version: its own) holds no composite's states: a composite reads
  // nothing then (0x5950fd), so its states stop at the first composite
  std::vector<int> originalStates(const std::vector<int> &base, uint32_t version) const;
  // An original saved game's states put back by the same order (silently,
  // as restore)
  void restoreOriginal(const std::vector<int> &states, uint32_t version);

  // The scenario's time limit ([duration] nummonths, ScenarioGoal::ctor
  // 0x58e218): none (0) met at once; -1 never over
  int durationMonths = 0;
  int durationState = 1;
  // The outcome, once every goal's settled (allGoalsMet 0x48a726, its
  // +0x30): 0 playing, 1 won, 2 lost
  int outcome = 0;
  const std::vector<Node> &tree() const { return this->roots; }

private:
  ResourceManager *rm = nullptr;
  std::string scnPath;
  std::vector<Goal> goals;
  std::vector<Node> roots;
  Node makeNode(IniReader *ini, const std::string &file, const std::string &sec, int depth);
  void updateNode(Node &n);
  int nodeState(const Node &n) const;
  void checkDeadline();
  int topState(int i) const;
  void updateTop(int i);
  Hooks hooks;
  float clock = 0;
  void readGoals(const std::string &file, bool fromList);
  void evaluate(Goal &g);
  int result(const Goal &g) const;
  void fire(const Goal &g, bool quiet);
  void showPopup(const std::string &file, int section);
};

#endif // GOALS_HPP
