#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <vector>
#include <string>

#include <SDL2/SDL.h>

#include "IniReader.hpp"

class Config {
public:
  Config(const std::string &filename = "zoo.ini");
  ~Config();

  std::vector<std::string> getResourcePaths();
  // [user] uiScale: a fixed scale for the menus, panels and zoo (0 or missing:
  // 1:1 as the original, whole steps up on a big window with the uiscale
  // extra, shrunk to fit a window under 800x600)
  float getUiScale();
  // [user] artupscale (fsr default|mmpx|off), artupscalefactor (0 = from the
  // desktop size), artsharpness (FSR sharpening in stops, 0 = sharpest)
  std::string getArtUpscale();
  int getArtUpscaleFactor();
  float getArtSharpness();
  // [user] widescreen: 1 (default) = the in-game screen fills the window, its
  // bars and panels pinned to the edges, as the original at any size; 0 =
  // our old way, its 800x600 screen scaled to fit and centred
  bool getWidescreen();
  // [user] timedunlocks: 1 = freeform items come out month by month as in
  // the original (freeform/unlock*.scn); 0 (default) = all of them from
  // the start
  bool getTimedUnlocks();
  std::string getMenuMusic();
  bool getPlayMenuMusic();
  int getScreenWidth();
  int getScreenHeight();
  std::string getLangDllName();
  std::string getResDllName();
  SDL_Color getProgressColor();
  SDL_Rect getProgressPosition();
private:
  IniReader * reader = NULL;
};

#endif // CONFIG_HPP