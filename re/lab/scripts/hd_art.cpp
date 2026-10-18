#include "lab_api.h"

#include <chrono>
#include <string>

// HD art: an AI-upscaled copy of the map's art (animals, people, fences,
// objects) drawn when the map is zoomed in, made as a mod - the game shows
// the original art until a player switches it on (Mod Menu, or Game
// Options > Mods & Updates), and switching it works at once.
//
// It's an ordinary experiment on the Lab's toolkit (lab_api.h, "Making
// things"): copy this file to make your own - another upscaler, another
// model, a different part of the art - without changing the Lab.
//   1. the engine writes the original frames out (engineExe --hd-export),
//      their clear edges filled so an upscaler doesn't draw dark fringes
//   2. an upscaler on your PC makes them 4x: Real-ESRGAN ncnn-vulkan
//      (github.com/xinntao/Real-ESRGAN, BSD-3; any Vulkan graphics card)
//   3. the engine tidies them into the mod's hd/ folder (--hd-finish): sizes
//      checked, the original's outline kept, its texture detail put back
//
// Time and size: about 30 small frames a second on an RX 6600 (the
// scientist's 1,785 frames took 64 s); every animal, person, fence and
// object comes to about a million frames.

// ---- Settings: change these to yours
// The mod's name, and its folder's (made in the game's mods folder)
static const char *kModName = "ZT1 HD Art (AI)";
static const char *kModFolder = "ZT1-HD-Art-AI";
// Where the game's mods are ("": wherever the game looks now - the Mod Menu's)
static const char *kModsFolder = "B:/zt1-mods";
// The map art to upscale: folders, a space between ("": all of it - hours)
static const char *kArt = "animals/lion";
// The upscaler, and its model: realesrgan-x4plus keeps the most detail;
// realesrgan-x4plus-anime the cleanest outlines; realesr-animevideov3 is
// about 4x faster
static const char *kUpscaler = "E:/zt1-shots/tools/realesrgan/realesrgan-ncnn-vulkan.exe";
static const char *kModel = "realesrgan-x4plus";

LAB_EXPORT const char *lab_about() { return "HD art: the map's art upscaled 4x by an AI upscaler, as a mod"; }

namespace {
enum Stage { kExport, kUpscale, kFinish };
Stage stage = kExport;
int job = -1, total = 0;
std::string mods, mod, work, frames, up;
std::chrono::steady_clock::time_point began;

std::string inQuotes(const std::string &s) { return "\"" + s + "\""; }
std::string folderOf(const std::string &file) {
  size_t slash = file.find_last_of("/\\");
  return slash == std::string::npos ? "." : file.substr(0, slash);
}
} // namespace

LAB_EXPORT void lab_start(Lab *lab) {
  began = std::chrono::steady_clock::now();
  mods = *kModsFolder ? kModsFolder : lab->modsFolder();
  mod = mods + "/" + kModFolder;
  // (the work folders - the frames as they go - beside the mod, gone after)
  work = mods + "/.work/" + kModFolder;
  frames = work + "/frames";
  up = work + "/up";
  stage = kExport;
  total = 0;
  job = -1;
  if (!lab->exists(kUpscaler)) {
    lab->log("No upscaler at %s - put Real-ESRGAN ncnn-vulkan there, or change kUpscaler", kUpscaler);
    return;
  }
  lab->removeDir(work.c_str());
  lab->makeDir(frames.c_str());
  lab->makeDir(up.c_str());
  lab->log("1. Writing out the original frames: %s", *kArt ? kArt : "all the map art");
  job = lab->start(lab->engineExe(), ("--hd-export " + inQuotes(frames) + " " + kArt).c_str(), nullptr);
}

LAB_EXPORT bool lab_step(Lab *lab, int step) {
  (void)step;
  if (job < 0)
    return false;
  if (lab->running(job)) {
    // (the upscaler's progress: its frames written so far)
    if (stage == kUpscale && total) {
      int done = lab->countFiles(up.c_str(), ".png");
      lab->watch("Frames upscaled", done, "of the frames written out");
      lab->plot("upscaled %", 100.0 * done / total);
    }
    return true;
  }
  if (lab->exitCode(job) != 0) {
    lab->log("Stopped: %s ended with %d", stage == kUpscale ? "the upscaler" : "the engine", lab->exitCode(job));
    lab->removeDir(work.c_str());
    return false;
  }
  if (stage == kExport) {
    total = lab->countFiles(frames.c_str(), ".png");
    lab->watch("Frames", total, "written out");
    if (!total) {
      lab->log("No frames matched %s", kArt);
      lab->removeDir(work.c_str());
      return false;
    }
    lab->log("2. Upscaling %d frames 4x (%s)", total, kModel);
    std::string args = "-i " + inQuotes(frames) + " -o " + inQuotes(up) + " -n " + kModel + " -s 4 -f png -m " +
                       inQuotes(folderOf(kUpscaler) + "/models");
    job = lab->start(kUpscaler, args.c_str(), folderOf(kUpscaler).c_str());
    stage = kUpscale;
    return true;
  }
  if (stage == kUpscale) {
    lab->log("3. Putting them in the mod");
    job = lab->start(lab->engineExe(),
                     ("--hd-finish " + inQuotes(frames) + " " + inQuotes(up) + " " + inQuotes(mod + "/hd")).c_str(), nullptr);
    stage = kFinish;
    return true;
  }
  // Done: its mod.json (unless it has one), the game pointed at the mods
  // folder, the work folders gone
  lab->writeMod(mod.c_str(), kModName, "ZT1 Lab",
                "The map's art - animals, people, fences, objects - upscaled 4x by an AI upscaler, drawn when "
                "you zoom in. Off, the original art.");
  lab->useModsFolder(mods.c_str());
  lab->removeDir(work.c_str());
  auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - began).count();
  lab->log("Done in %lld s: %s", static_cast<long long>(secs), mod.c_str());
  return false;
}
