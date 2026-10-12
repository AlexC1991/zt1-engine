#include "lab_api.h"

// zoo.exe 0x4378df animalUpdateHappiness: every tick, what the last checks
// found (ZTAnimal+0x2ac, "pending") is added to happiness (ZTAnimal+0x2a8),
// kept between the species' limits (+0x22c and +0x230: -100 and 100?).
// This checks the engine does exactly that, tick by tick.

LAB_EXPORT const char *lab_about() { return "Happiness tick (0x4378df): happiness += pending, kept to -100..100"; }

static int zebra = -1;

LAB_EXPORT void lab_start(Lab *lab) {
  lab->exhibit(5, 5, "chainlnk");
  zebra = lab->animal("zebra", false);
  lab->focusAnimal(zebra);
  lab->log("A zebra (%d) alone in a bare 5x5 exhibit: it should get unhappier (too few of its kind, a poor habitat)",
           zebra);
  lab->log("ZTAnimal+0x2a8 is mapped as: %d", lab->offsetOf("ZTAnimal", "happiness"));
}

static double clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

LAB_EXPORT bool lab_step(Lab *lab, int step) {
  double before = lab->get(zebra, "happiness");
  double pending = lab->get(zebra, "pending");
  lab->runEngine(1.0f); // one second: one tick of its checks
  double after = lab->get(zebra, "happiness");

  // My reading of 0x4378df
  double mine = clamp(before + pending, -100, 100);

  lab->watch("happiness (engine)", after, "ZTAnimal+0x2a8");
  lab->watch("pending", lab->get(zebra, "pending"), "ZTAnimal+0x2ac");
  lab->watch("my 0x4378df says", mine, "zoo.exe 0x4378df");
  lab->watch("suitability", lab->suitability(zebra), "zoo.exe 0x444827");
  lab->plot("happiness", after);
  lab->plot("pending", pending);
  bool same = after == mine;
  lab->expect("engine happiness == clamp(before + pending)", same);
  if (!same)
    lab->log("differs: before %.1f + pending %.1f -> mine %.1f, engine %.1f", before, pending, mine, after);
  else if (pending != 0)
    lab->log("%.1f %+.1f -> %.1f", before, pending, after);
  return step < 90;
}
