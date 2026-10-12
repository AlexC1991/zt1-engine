#ifndef LAB_HPP
#define LAB_HPP

// ZT1 Lab (zt1-engine.exe --lab [map]): a live view for what's found in
// zoo.exe. Pick a function or struct from re/knowledge.json (or send one from
// Ghidra), see the engine code that implements it, and Build & Run a script
// (re/lab/scripts/*.cpp, see re/lab/lab_api.h) that sets up a scene on our
// own engine and steps through it with a log, watched values and graphs.
#include <SDL2/SDL.h>
#include <functional>
#include <string>
#include <vector>

class ResourceManager;
class ScenarioManager;
class World;

// The game's dev console (its words in, its answer out): the Lab app's
// Scene console runs the same commands
using LabConsole = std::function<std::string(const std::vector<std::string> &)>;

// hostPort 0: the Lab's own window and panels. Else the Lab app's engine:
// no panels, its window kept out of sight, the live view sent as frames and
// commands taken over a connection on 127.0.0.1:hostPort (lab-app/)
int runLab(SDL_Window *window, SDL_Renderer *renderer, ResourceManager *rm, ScenarioManager *sm, World *world,
           const std::string &mapStem, int hostPort = 0, LabConsole console = nullptr);
// (--lab-host: the window off the screen and the taskbar from the start)
void labHostHide(SDL_Window *window);

#endif
