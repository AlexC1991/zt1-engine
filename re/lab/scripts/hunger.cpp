#include "lab_api.h"

// zoo.exe 0x438a79 animalCheckHunger: every cHungerCheck ticks (30, from
// animals.ai) an animal gets cHungerIncrement hungrier (its species' .ai,
// ZTAnimalType+0x214), up to 100. With no food in its exhibit it never eats.

LAB_EXPORT const char *lab_about() { return "Hunger (0x438a79): +cHungerIncrement every 30 ticks"; }

static int lion = -1, lastChangeStep = 0;
static double lastHunger = 0;
static int increment = 0;

LAB_EXPORT void lab_start(Lab *lab) {
  lab->exhibit(6, 6, "chainlnk");
  lion = lab->animal("lion", true);
  lab->focusAnimal(lion);
  int found = 0;
  increment = lab->speciesInt("lion", "", "cHungerIncrement", &found);
  lab->log("lion's cHungerIncrement (its .ai, mapped at ZTAnimalType+0x%x): %d%s", lab->offsetOf("ZTAnimalType", "cHungerIncrement"),
           increment, found ? "" : " (not found!)");
  lastHunger = lab->get(lion, "hunger");
}

LAB_EXPORT bool lab_step(Lab *lab, int step) {
  lab->runEngine(1.0f);
  double h = lab->get(lion, "hunger");
  lab->watch("hunger", h, "the engine's Member::hunger");
  lab->watch("cHungerIncrement", increment, "ZTAnimalType+0x214");
  lab->plot("hunger", h);
  if (h != lastHunger) {
    double want = lastHunger + increment > 100 ? 100 : lastHunger + increment;
    lab->expect("each rise is cHungerIncrement (capped at 100)", h == want);
    if (lastChangeStep > 0)
      lab->expect("rises are 30 ticks apart (cHungerCheck)", step - lastChangeStep == 30);
    lab->log("hunger %.0f -> %.0f after %d ticks", lastHunger, h, step - lastChangeStep);
    lastChangeStep = step;
    lastHunger = h;
  }
  return step < 200;
}
