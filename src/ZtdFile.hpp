#ifndef ZTD_FILE_HPP
#define ZTD_FILE_HPP

#include <vector>
#include <functional>
#include <set>
#include <string>

#include <SDL.h>
#include "SDL_mixer.h"

#include "IniReader.hpp"

class ZtdFile {
public:
    static std::vector<std::string> getFileList(const std::string &ztd_file);
    static void * getFileContent(const std::string &ztd_file, const std::string &file_name, int * size);
  // Several files from one archive in a single pass: wanted holds lower
  // case names; got(name, data, size) is called for each one found
  static void readFiles(const std::string &ztd_file,
                        const std::set<std::string> &wanted,
                        const std::function<void(const std::string &,
                                                 const char *, int)> &got);
    static SDL_Surface * getImageSurface(const std::string &ztd_file, const std::string &file_name);
    static Mix_Music * getMusic(const std::string &ztd_file, const std::string &file_name);
    static IniReader * getIniReader(const std::string &ztd_file, const std::string &file_name);
private:
    static SDL_Surface * getImageSurfaceBmp(const std::string &ztd_file, const std::string &file_name);
    static SDL_Surface * getImageSurfaceTga(const std::string &ztd_file, const std::string &file_name);
    static SDL_Surface * getImageSurfaceZt1(const std::string &ztd_file, const std::string &file_name);
};

#endif // ZTD_FILE_HPP