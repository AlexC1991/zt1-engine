#include "ZtdFile.hpp"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <zip.h>
#include <stdlib.h>
#include <cstdio>

#include "SDL_image.h"

#include "Utils.hpp"

// Archives stay open, with their files indexed by lower case name: opening
// one and searching it for every file read (thousands while a map and its
// panels load) was most of the loading time. (The game has 172 archives,
// well within the C runtime's 512 open files.) Listing reads the names
// straight from the archive's central directory: libzip opening all 172
// archives (377,000 files) just to list them took 3 seconds at start-up;
// libzip opens an archive when a file is first read from it.
namespace {
struct OpenArchive {
  bool listed = false;
  std::vector<std::string> names; // in the archive's order (libzip's index)
  std::unordered_map<std::string, zip_uint64_t> index; // lower case name
  zip_t *zip = nullptr;
  bool openTried = false;
};
std::mutex archivesMutex;
std::unordered_map<std::string, OpenArchive> archives;
std::unordered_map<std::string, bool> looseFiles; // name -> exists on disk

uint16_t le16(const unsigned char *p) { return p[0] | (p[1] << 8); }
uint32_t le32(const unsigned char *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

// The names in a zip's central directory; false if it can't be read this
// way (then libzip lists it)
bool readCentralDirectory(const std::string &path,
                          std::vector<std::string> &names) {
  FILE *f = fopen(path.c_str(), "rb");
  if (!f)
    return false;
  bool ok = false;
  std::vector<unsigned char> tail;
  if (fseek(f, 0, SEEK_END) == 0) {
    long size = ftell(f);
    long want = std::min<long>(size, 22 + 65535);
    tail.resize(static_cast<size_t>(want));
    if (want >= 22 && fseek(f, size - want, SEEK_SET) == 0 &&
        fread(tail.data(), 1, tail.size(), f) == tail.size()) {
      // End of central directory record, searched from the end
      for (long i = want - 22; i >= 0; i--) {
        const unsigned char *e = tail.data() + i;
        if (le32(e) != 0x06054b50)
          continue;
        uint16_t count = le16(e + 10);
        uint32_t cdSize = le32(e + 12), cdOffset = le32(e + 16);
        if (count == 0xFFFF || cdOffset == 0xFFFFFFFF ||
            (long)cdOffset + (long)cdSize > size)
          break; // zip64: leave it to libzip
        std::vector<unsigned char> cd(cdSize);
        if (fseek(f, (long)cdOffset, SEEK_SET) != 0 ||
            fread(cd.data(), 1, cd.size(), f) != cd.size())
          break;
        size_t at = 0;
        names.clear();
        names.reserve(count);
        while (names.size() < count && at + 46 <= cd.size() &&
               le32(cd.data() + at) == 0x02014b50) {
          uint16_t nameLen = le16(cd.data() + at + 28);
          uint16_t extraLen = le16(cd.data() + at + 30);
          uint16_t commentLen = le16(cd.data() + at + 32);
          if (at + 46 + nameLen > cd.size())
            break;
          names.emplace_back(reinterpret_cast<const char *>(cd.data() + at + 46),
                             nameLen);
          at += 46 + nameLen + extraLen + commentLen;
        }
        ok = names.size() == count;
        break;
      }
    }
  }
  fclose(f);
  return ok;
}

zip_t *openZip(OpenArchive &a, const std::string &path, int *error) {
  if (!a.openTried) {
    a.openTried = true;
    a.zip = zip_open(path.c_str(), ZIP_RDONLY, error);
  }
  return a.zip;
}

OpenArchive *listArchive(const std::string &path, int *error) {
  OpenArchive &a = archives[path];
  if (a.listed)
    return a.names.empty() && !a.zip ? nullptr : &a;
  a.listed = true;
  if (!readCentralDirectory(path, a.names)) {
    a.names.clear();
    if (zip_t *zip = openZip(a, path, error)) {
      zip_int64_t count = zip_get_num_entries(zip, 0);
      for (zip_int64_t i = 0; i < count; i++) {
        const char *name = zip_get_name(zip, static_cast<zip_uint64_t>(i), 0);
        a.names.push_back(name ? name : "");
      }
    } else {
      return nullptr;
    }
  }
  a.index.reserve(a.names.size());
  for (size_t i = 0; i < a.names.size(); i++)
    a.index.emplace(Utils::string_to_lower(a.names[i]), static_cast<zip_uint64_t>(i));
  return &a;
}

// An archive ready to read from: listed, and opened with libzip
OpenArchive *openArchive(const std::string &path, int *error) {
  OpenArchive *a = listArchive(path, error);
  if (!a || !openZip(*a, path, error))
    return nullptr;
  return a;
}
} // namespace

std::vector<std::string> ZtdFile::getFileList(const std::string &ztd_file) {
  std::lock_guard<std::mutex> lock(archivesMutex);
  int error = 0;
  if (OpenArchive *archive = listArchive(ztd_file, &error))
    return archive->names;
  return {};
}

void * ZtdFile::getFileContent(const std::string &ztd_file, const std::string &file_name, int * size) {
  void * content = NULL;
  int error = 0;
  std::lock_guard<std::mutex> lock(archivesMutex);

  // [PATCH] Check for loose file first (remembering which names have none)
  auto loose = looseFiles.find(file_name);
  FILE *loose_f = nullptr;
  if (loose == looseFiles.end() || loose->second) {
    loose_f = fopen(file_name.c_str(), "rb");
    looseFiles[file_name] = loose_f != nullptr;
  }
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

  if (OpenArchive *archive = openArchive(ztd_file, &error)) {
    auto entry = archive->index.find(Utils::string_to_lower(file_name));
    struct zip_stat finfo;
    zip_stat_init(&finfo);
    if (entry != archive->index.end() &&
        zip_stat_index(archive->zip, entry->second, 0, &finfo) == 0) {
      content = calloc(finfo.size + 1, sizeof(uint8_t));
      if (zip_file_t *fd = zip_fopen_index(archive->zip, entry->second, 0)) {
        zip_fread(fd, content, finfo.size);
        zip_fclose(fd);
      }
      if (size) {
        *size = finfo.size;
      }
    }
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
    ZT_TRACE("Could not load content of file %s in %s", file_name.c_str(), ztd_file.c_str());
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
    ZT_TRACE("Could not load content of file %s in %s", file_name.c_str(), ztd_file.c_str());
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
    ZT_TRACE("Could not load content of file %s in %s", file_name.c_str(), ztd_file.empty() ? "unknown ztd file" : ztd_file.c_str());
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
    ZT_TRACE("Encountered Zoo Tycoon format image file %s", file_name.c_str());
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
    ZT_TRACE("Could not load content of file %s in %s", file_name.c_str(), ztd_file.c_str());
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
    ZT_TRACE("Could not load content of file %s in %s", file_name.c_str(), ztd_file.c_str());
    return nullptr; // [PATCH] Don't crash on missing files
  }

  SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Could not load ini file %s", file_name.c_str());
  exit(8);
}

void ZtdFile::readFiles(
    const std::string &ztd_file, const std::set<std::string> &wanted,
    const std::function<void(const std::string &, const char *, int)> &got) {
  int error = 0;
  std::lock_guard<std::mutex> lock(archivesMutex);
  OpenArchive *archive = openArchive(ztd_file, &error);
  if (!archive)
    return;
  std::vector<char> buffer;
  for (const std::string &name : wanted) {
    auto entry = archive->index.find(name);
    if (entry == archive->index.end())
      continue;
    struct zip_stat finfo;
    zip_stat_init(&finfo);
    if (zip_stat_index(archive->zip, entry->second, 0, &finfo) != 0)
      continue;
    buffer.resize(finfo.size);
    zip_file_t *fd = zip_fopen_index(archive->zip, entry->second, 0);
    if (!fd)
      continue;
    zip_int64_t n = zip_fread(fd, buffer.data(), finfo.size);
    zip_fclose(fd);
    if (n >= 0)
      got(name, buffer.data(), static_cast<int>(n));
  }
}
