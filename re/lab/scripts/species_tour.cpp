#include "lab_api.h"

// The species data (ZTAnimalType): each species shown, with its .ai values
// at the offsets mapped in the knowledge base. And the map's heights to end.

LAB_EXPORT const char *lab_about() { return "Species data: each species' look and its mapped values"; }

static const char *species[] = {"zebra", "lion", "elephant", "gorilla", "giraffe", "hippo", "panda", "polarbr"};
static const char *keys[] = {"cHungerThreshold", "cHungerIncrement", "cInitialHappiness", "cHabitatPreference",
                             "cNumberAnimalsMin", "cNumberAnimalsMax", "cPurchaseCost"};

LAB_EXPORT void lab_start(Lab *lab) { lab->log("A species a step: its look, and its values at their mapped offsets"); }

LAB_EXPORT bool lab_step(Lab *lab, int step) {
  const int n = sizeof species / sizeof *species;
  if (step > n) {
    lab->view("heightmap", "");
    lab->log("The lab map's heights, top down: the lab exhibit in green");
    return false;
  }
  const char *sp = species[step - 1];
  lab->view("animal", sp);
  lab->log("== %s", sp);
  for (const char *k : keys) {
    int found = 0;
    int v = lab->speciesInt(sp, "", k, &found);
    int off = lab->offsetOf("ZTAnimalType", k);
    lab->watch(k, v, off >= 0 ? "ZTAnimalType (mapped)" : "not mapped yet");
    if (found && off >= 0)
      lab->log("  %-20s %6d  at ZTAnimalType+0x%x", k, v, off);
    else if (found)
      lab->log("  %-20s %6d  (no offset mapped for it yet)", k, v);
  }
  return true;
}
lab->log(fmt, ...);lab->plot(series, value);lab->view(mode, what);lab->view(mode, what);lab->focusTile(x, y);