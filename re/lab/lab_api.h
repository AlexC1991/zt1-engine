// ZT1 Lab scripts: what a script can do in the Lab (zt1-engine.exe --lab).
//
// A script is one .cpp in re/lab/scripts/. The Lab builds it (Build & Run)
// into a DLL and runs it in its live view: lab_start once, then lab_step
// over and over (Step, or Auto) until it returns false.
//
//   #include "lab_api.h"
//   // zoo.exe 0x4378df          <- the function it's about (Show in Lab finds it)
//   LAB_EXPORT const char *lab_about() { return "What this script checks"; }
//   LAB_EXPORT void lab_start(Lab *lab) { ... set up the scene ... }
//   LAB_EXPORT bool lab_step(Lab *lab, int step) { ... one step ...; return step < 200; }
//
// Strings are plain char pointers (the Lab and the script are built
// separately, so no std::string across). Everything a script logs, watches
// or plots shows in the Lab's panels.
#pragma once

#define LAB_EXPORT extern "C" __declspec(dllexport)

struct Lab {
  // --- Debug log and inspector
  // A line in the log (printf style)
  virtual void log(const char *fmt, ...) = 0;
  // A value in the inspector, refreshed each step; where: what it maps to
  // ("ZTAnimal+0x2a8", "zoo.exe 0x4378df")
  virtual void watch(const char *label, double value, const char *where) = 0;
  // A point on a graph (one line per series name), one per step
  virtual void plot(const char *series, double value) = 0;
  // A check: shown green or red in the inspector and counted
  virtual void expect(const char *what, bool ok) = 0;

  // --- The live view
  // What it shows: "scene" (the lab zoo), "heightmap" (the map's heights,
  // top down), "sprite" (an animation: what = "animals/zebra/m/walk/walk"),
  // "animal" (a species' look: what = "zebra"), "object" (what =
  // "scenery/other/lrock1.ai")
  virtual void view(const char *mode, const char *what) = 0;
  // The scene's camera onto an animal (its id), or a tile
  virtual void focusAnimal(int id) = 0;
  virtual void focusTile(float x, float y) = 0;

  // --- The lab zoo (the engine's real World: our code under test)
  // A fenced exhibit of w x h tiles on flat grass (fence: "chainlnk",
  // "atlwood", ...); its id. Each call clears the last one away
  virtual int exhibit(int w, int h, const char *fence) = 0;
  // An animal of a species ("zebra") in the exhibit; its id (-1: refused,
  // and the message id why in lastMessage)
  virtual int animal(const char *species, bool female) = 0;
  // An object in the exhibit, dx, dy tiles from its corner (-1: refused)
  virtual int object(const char *aiFile, float dx, float dy) = 0;
  virtual int lastMessage() = 0;
  // The engine's own simulation, run for so many seconds of game time
  // (an animal's checks tick once a second)
  virtual void runEngine(float seconds) = 0;
  // An animal's state in the engine, by name: happiness, pending, hunger,
  // food, health, energy, dirty, x, y, exhibit, type, female, baby, sick,
  // sleeping, escaped (0 if the name isn't one)
  virtual double get(int animal, const char *field) = 0;
  virtual void set(int animal, const char *field, double value) = 0;
  // An exhibit's suitability for an animal in it (the engine's 0-100)
  virtual int suitability(int animal) = 0;

  // --- The game's data and the binary
  // A species' .ai value ([<subtype>/Characteristics/Integers], over its
  // DefaultSubtype's): subtype "" for the default; found: 0 if absent
  virtual int speciesInt(const char *species, const char *subtype, const char *key, int *found) = 0;
  // Values in zoo.exe itself (its constants and tables), by address
  virtual unsigned exeU32(unsigned address) = 0;
  virtual float exeF32(unsigned address) = 0;
  virtual double exeF64(unsigned address) = 0;
  // The game's text (a message id)
  virtual const char *text(int id) = 0;
  // A mapped struct field's offset from re/knowledge.json ("ZTAnimal",
  // "happiness"): -1 if it isn't mapped
  virtual int offsetOf(const char *structName, const char *field) = 0;

  // --- Making things: mods, art packs, anything a program on your PC makes
  // A program started in the background (args as one command line; cwd:
  // where it runs, null for the engine's own folder); its job number, -1 if
  // it didn't start. It doesn't wait: poll running() in lab_step. Its output
  // comes to the log (not lines that are only a percentage)
  virtual int start(const char *program, const char *args, const char *cwd) = 0;
  // Whether it's still going (and its new output logged); exitCode once it's
  // done (-1 while it runs); stop ends it
  virtual bool running(int job) = 0;
  virtual int exitCode(int job) = 0;
  virtual void stop(int job) = 0;
  // The engine's own exe, to start() with its tools: --hd-export <folder>
  // [prefix...] writes map art's frames out as PNGs, --hd-finish <frames>
  // <upscaled> <out> tidies upscaled ones into a mod's hd/ folder,
  // --hd-survey [prefix...] logs how many frames and pixels there are
  virtual const char *engineExe() = 0;
  // Files and folders (removeDir never takes a drive or a top folder)
  virtual bool makeDir(const char *path) = 0;
  virtual bool removeDir(const char *path) = 0;
  virtual bool writeFile(const char *path, const char *text) = 0;
  virtual bool exists(const char *path) = 0;
  // How many files a folder has (extension ".png", or null for all)
  virtual int countFiles(const char *dir, const char *extension) = 0;
  // A mod's mod.json (name, author, about), unless it has one already
  virtual bool writeMod(const char *folder, const char *name, const char *author, const char *about) = 0;
  // The game's mods folder (mods.cfg folder=: the Mod Menu's), here and in
  // the game, so it finds what you made there
  virtual void useModsFolder(const char *folder) = 0;
  // The game's mods folder now
  virtual const char *modsFolder() = 0;
};
