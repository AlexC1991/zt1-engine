#include "ZtdFile.hpp"

#include <vector>

#include <zip.h>
#include <stdlib.h>
#include <cstdio>

#include "SDL_image.h"

#include "Utils.hpp"

std::vector<std::string> ZtdFile::getFileList(const std::string &ztd_file) {
  std::vector<std::string> files = std::vector<std::string>();

  if(zip_t * file = zip_open(ztd_file.c_str(), 0, NULL)) {
    size_t index = 0;
    struct zip_stat finfo;
    zip_stat_init(&finfo);
    while ((zip_stat_index(file, index, 0, &finfo)) == 0) {
      files.push_back(std::string(finfo.name));
      index++;
    }
    zip_close(file);
  }
  return files;
}

void * ZtdFile::getFileContent(const std::string &ztd_file, const std::string &file_name, int * size) {
  void * content = NULL;
  int error = 0;

  // [PATCH] Check for loose file first
  FILE *loose_f = fopen(file_name.c_str(), "rb");
  if (loose_f) {
      fseek(loose_f, 0, SEEK_END);
      long fsize = ftell(loose_f);
      fseek(loose_f, 0, SEEK_SET);
      
      content = calloc(fsize + 1, 1);
      if (content) {
          fread(content, 1, fsize, loose_f);
          if (size) *size = (int)fsize;
          fclose(loose_f);
          SDL_Log("ZtdFile: Loaded loose file override: %s", file_name.c_str());
          return content;
      }
      fclose(loose_f);
  }

  if(zip_t * file = zip_open(ztd_file.c_str(), 0, &error)) {
    int index = 0;
    struct zip_stat finfo;
    zip_stat_init(&finfo);
    while ((zip_stat_index(file, index, 0, &finfo)) == 0) {
      if(Utils::string_to_lower(std::string(finfo.name)) == Utils::string_to_lower(file_name)) {
        content = calloc(finfo.size + 1, sizeof(uint8_t));
        zip_file_t * fd = zip_fopen_index(file, index, 0);
        zip_fread(fd, content, finfo.size);
        zip_fclose(fd);
        if (size) {
          *size =  finfo.size;
        }
        break;
      }
      index++;
    }
    zip_close(file);
  }
  if (error != 0) {
    SDL_Log("Could not open file %s, got error %i", ztd_file.c_str(), error);
    exit(1);
  }
  return content;
}

SDL_Surface * ZtdFile::getImageSurfaceBmp(const std::string &ztd_file, const std::string &file_name) {
  SDL_Surface * surface = NULL;
  int file_size = 0;

  void * file_content = ZtdFile::getFileContent(ztd_file, file_name, &file_size);
  if (file_content) {
    SDL_RWops * rw = SDL_RWFromMem(file_content, file_size);
    surface = IMG_LoadTyped_RW(rw, 1, "BMP");
    free(file_content);
  } else {
    SDL_Log("Could not load content of file %s in %s", file_name.c_str(), ztd_file.c_str());
    return nullptr; // [PATCH] Don't crash on missing files
  }

  return surface;
}

SDL_Surface * ZtdFile::getImageSurfaceTga(const std::string &ztd_file, const std::string &file_name) {
  SDL_Surface * surface = NULL;
  int file_size = 0;

  void * file_content = ZtdFile::getFileContent(ztd_file, file_name, &file_size);
  if (file_content) {
    SDL_RWops * rw = SDL_RWFromMem(file_content, file_size);
    surface = IMG_LoadTyped_RW(rw, 1, "TGA");
    free(file_content);
  } else {
    SDL_Log("Could not load content of file %s in %s", file_name.c_str(), ztd_file.c_str());
    return nullptr; // [PATCH] Don't crash on missing files
  }

  return surface;
}

SDL_Surface * ZtdFile::getImageSurfaceZt1(const std::string &ztd_file, const std::string &file_name) {
  SDL_Surface * surface = NULL;
  int file_size = 0;

  void * file_content = ZtdFile::getFileContent(ztd_file, file_name, &file_size);
  if (file_content) {
    SDL_RWops * rw = SDL_RWFromMem(file_content, file_size);
    surface = IMG_Load_RW(rw, 1);
    free(file_content);
  } else {
    SDL_Log("Could not load content of file %s in %s", file_name.c_str(), ztd_file.empty() ? "unknown ztd file" : ztd_file.c_str());
    return nullptr; // [PATCH] Don't crash on missing files
  }

  return surface;
}

SDL_Surface * ZtdFile::getImageSurface(const std::string &ztd_file, const std::string &file_name) {
  SDL_Surface * surface = nullptr;

  std::string file_extension = Utils::getFileExtension(file_name);
  if(file_extension == "BMP"){
    surface = ZtdFile::getImageSurfaceBmp(ztd_file, file_name);
  } else if (file_extension == "TGA") {
    surface = ZtdFile::getImageSurfaceTga(ztd_file, file_name);
  } else if (file_extension.empty()){
    SDL_Log("Encountered Zoo Tycoon format image file %s", file_name.c_str());
    surface = ZtdFile::getImageSurfaceZt1(ztd_file, file_name);
  } else {
    SDL_Log("Unkown image file extension %s for file %s, returning nullptr", file_extension.c_str(), file_name.c_str());
  }

  return surface;
}

Mix_Music * ZtdFile::getMusic(const std::string &ztd_file, const std::string &file_name) {
  Mix_Music * music = NULL;
  int file_size = 0;
  Mix_MusicType music_type = MUS_WAV;

  void * file_content = ZtdFile::getFileContent(ztd_file, file_name, &file_size);
  if (file_content) {
    SDL_RWops * rw = SDL_RWFromMem(file_content, file_size);
    music = Mix_LoadMUSType_RW(rw, MUS_WAV, 1);
  } else {
    SDL_Log("Could not load content of file %s in %s", file_name.c_str(), ztd_file.c_str());
    return nullptr; // [PATCH] Don't crash on missing files
  }

  return music;
}

IniReader * ZtdFile::getIniReader(const std::string &ztd_file, const std::string &file_name)
{
  int file_size = 0;
  void * file_content = ZtdFile::getFileContent(ztd_file, file_name, &file_size);
  if (file_content) {
    return new IniReader(file_content, file_size);
  } else {
    SDL_Log("Could not load content of file %s in %s", file_name.c_str(), ztd_file.c_str());
    return nullptr; // [PATCH] Don't crash on missing files
  }

  SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Could not load ini file %s", file_name.c_str());
  exit(8);
}

void ZtdFile::readFiles(
    const std::string &ztd_file, const std::set<std::string> &wanted,
    const std::function<void(const std::string &, const char *, int)> &got) {
  int error = 0;
  zip_t *file = zip_open(ztd_file.c_str(), 0, &error);
  if (!file)
    return;
  zip_int64_t count = zip_get_num_entries(file, 0);
  std::vector<char> buffer;
  for (zip_int64_t index = 0; index < count; index++) {
    struct zip_stat finfo;
    zip_stat_init(&finfo);
    if (zip_stat_index(file, index, 0, &finfo) != 0)
      continue;
    std::string name = Utils::string_to_lower(std::string(finfo.name));
    if (!wanted.count(name))
      continue;
    buffer.resize(finfo.size);
    zip_file_t *fd = zip_fopen_index(file, index, 0);
    if (!fd)
      continue;
    zip_int64_t n = zip_fread(fd, buffer.data(), finfo.size);
    zip_fclose(fd);
    if (n >= 0)
      got(name, buffer.data(), static_cast<int>(n));
  }
  zip_close(file);
}
