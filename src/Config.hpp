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
  // [user] uiScale: menu scale (0 or missing = fit the window)
  float getUiScale();
  // [user] artupscale (fsr default|mmpx|off), artupscalefactor (0 = from the
  // desktop size), artsharpness (FSR sharpening in stops, 0 = sharpest)
  std::string getArtUpscale();
  int getArtUpscaleFactor();
  float getArtSharpness();
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