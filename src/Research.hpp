#ifndef RESEARCH_HPP
#define RESEARCH_HPP

#include <algorithm>
#include <string>
#include <vector>

class ResourceManager;

// ============================================================================
// RESEARCH AND CONSERVATION
// ============================================================================
// research.cfg [branches] names the two branches (research/branres.cfg,
// research/brancon.cfg). A freeform zoo also has the expansions' (Dinosaur
// Digs researd.cfg, Marine Mania researe.cfg): their branches' categories
// follow the base game's in the branch of the same name (measured: 13
// research and 9 conservation categories). A branch file's [branch] has
// its name, its categories (category=research/catexN.cfg, in order), its
// funding levels (funding=none/min/normal/max, each a section: name =
// the "%s normal" string, cost = dollars, work = progress a day). A category file's
// [category] names its programs (program=research/progexN.cfg); a program's
// [research] has its name, icon, cost (work to finish it), order (programs
// of a lower order come first), target (the cNameID it unlocks or improves)
// and effect (0 unlock target, 1 an exhibit for target's house, 2 train
// target staff, 4/5 zoo-wide).
//
// Which program is researched: one of those that can be researched now
// (each checked category's lowest order left), picked at random - the
// original picked differently in two new games (Swinging log toy and
// Lowland Gorilla, then Nutritional animal food and Thouarsus Cycad).
// Funding starts at none.
// ============================================================================
struct ResearchProgram {
  std::string file;
  std::string name;
  std::string icon;
  std::string entityIcon; // (an animal house's program: its button's picture)
  int helpId = 0;
  int cost = 0;
  int order = 0;
  int target = 0;
  int effect = 0;
  int effectVal[3] = {0, 0, 0}; // effectval1-3: which, how much, how (add / set / percent)
  float progress = 0; // work done
  bool done = false;
};

struct ResearchCategory {
  std::string file;
  std::string name;
  int expansion = 0; // 0 Zoo Tycoon, 1 Dinosaur Digs, 2 Marine Mania
  int helpId = 0;
  std::vector<ResearchProgram> programs;
};

struct ResearchFunding {
  std::string key; // none, min, normal, max
  std::string name; // "%s normal"
  int cost = 0;     // a day
  int work = 0;     // progress a day
};

struct ResearchBranch {
  std::string file;
  std::string name;
  std::string noProgramIcon;
  std::vector<ResearchCategory> categories;
  std::vector<ResearchFunding> funding;
  int fundingLevel = 0;
  std::vector<bool> enabled; // the categories' checkboxes (all on)
  int currentCategory = -1, currentProgram = -1;
};

class Research {
  friend class SaveGame; // (saving and loading a game)
public:
  static Research &get();

  // Reads research.cfg and its files once (later calls do nothing)
  void load(ResourceManager *resource_manager);

  std::vector<ResearchBranch> &branches() { return this->list; }
  ResearchBranch *branch(int index) {
    return index >= 0 && index < (int)this->list.size() ? &this->list[index]
                                                        : nullptr;
  }
  // The program a branch is working on (nullptr: none left)
  ResearchProgram *current(ResearchBranch &branch);
  // The next program of a category (lowest order not done), if any can be
  // researched now
  ResearchProgram *nextIn(ResearchCategory &category);
  // Picks what a branch researches next
  void pick(ResearchBranch &branch);
  // The research tick (zoo.exe 0x41f1ba): every 3 s of play each branch's
  // program gains its funding's work / 120 (the work a month); done at its
  // cost, the next one picked. Not while the zoo can't pay for the tick.
  // completed: each program finished; finishedBranch: a branch with none left
  struct Tick {
    std::vector<ResearchProgram *> completed;
    std::vector<std::string> finishedBranches;
  };
  Tick advance(float seconds, double cash);
  // An animal house's programs (research effect 1: its collections, the
  // free one from the start): those researched for a building of that
  // cNameID, by upkeep, cheapest first (zoo.exe 0x58ff14). effectVal: upkeep
  // a month (replacing its cUpkeep), adult and child happiness (replacing
  // its cAdultChange / cChildChange)
  std::vector<const ResearchProgram *> collectionFor(int nameId) {
    std::vector<const ResearchProgram *> out;
    for (ResearchBranch &b : this->list)
      for (ResearchCategory &c : b.categories)
        for (ResearchProgram &p : c.programs)
          if (p.effect == 1 && p.target == nameId && (p.done || p.cost <= 0))
            out.push_back(&p);
    std::stable_sort(out.begin(), out.end(), [](const ResearchProgram *a, const ResearchProgram *b) {
      return a->effectVal[0] < b->effectVal[0];
    });
    return out;
  }
  // A new game: nothing researched, funding none, programs picked afresh
  void restart() {
    for (ResearchBranch &b : this->list) {
      for (ResearchCategory &c : b.categories)
        for (ResearchProgram &p : c.programs) {
          p.progress = 0;
          p.done = false;
        }
      b.fundingLevel = 0;
      b.enabled.assign(b.categories.size(), true);
      this->pick(b);
    }
    this->tickClock = 0;
  }
  float tickClock = 0;

private:
  std::vector<ResearchBranch> list;
  bool loaded = false;
  bool canResearch(const ResearchProgram &program) const;
};

#endif // RESEARCH_HPP
