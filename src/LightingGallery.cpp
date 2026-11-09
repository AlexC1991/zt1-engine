// ours: a check for the optional day/night lighting (not part of the game).
// ZT_LIGHTING_GALLERY=1 with --panel-shots <out>: every building on flat
// ground, far apart, each captured close up in the morning, the late
// afternoon and at midnight, from all four sides (gallery/<name>-r<side>-<hour>.png),
// to look over its shadows and night lights. ZT_GALLERY_ONLY=<text> keeps the
// files containing it. The pictures hold the original art: never distribute.
#include "World.hpp"
#include "ItemCatalog.hpp"
#include "HdArt.hpp"
#include "Features.hpp"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <tuple>
#include <cstdio>

// ZT_LIGHTING_GALLERY=scene: trees, people, animals and a building side by
// side, captured hour by hour through the day (scene/<side>-<hour>.png), for
// looking at how their shadows change as the sun goes down.
static int runShadowScene(World &world, SDL_Renderer *renderer, const std::string &out) {
  namespace fs = std::filesystem;
  fs::create_directories(out + "/scene");
  if (!world.loadBlank(60, 60))
    return 1;
  world.setPaused(true);
  world.edgeScroll = false;
  world.getFences().setEditing(true);
  auto &objects = world.objectsMutable();
  for (const auto &[file, x, y] : std::vector<std::tuple<const char *, float, float>>{
           {"scenery/foliage/eleear.ai", 26.5f, 26.5f}, {"scenery/foliage/conifer.ai", 30.5f, 25.5f},
           {"scenery/foliage/acacia.ai", 25.5f, 31.5f}, {"scenery/foliage/bush1.ai", 33.5f, 29.5f},
           {"scenery/building/bathroom.ai", 34.5f, 34.5f}})
    objects.add(file, x, y, 0);
  auto &animals = world.getAnimals();
  int i = 0;
  for (const char *file : {"animals/elephant.ai", "animals/giraffe.ai", "animals/zebra.ai", "animals/trex.ai"}) {
    const int type = animals.typeOfFile(file);
    if (type < 0)
      continue;
    const int id = animals.adopt(type, false, 28.5f + i * 3, 33.5f, world.getMap(), world.getFences(), true);
    if (auto *m = animals.member(id)) {
      m->boxed = false;
      m->egg = false;
      m->anim = "idle";
      m->fx = 1;
      m->fy = 0;
    }
    i++;
  }
  auto &guests = world.getGuests();
  guests.setEntrance(30, 40);
  for (int k = 0; k < 6; k++)
    guests.admit(0);
  auto &people = const_cast<std::vector<Guests::Guest> &>(guests.guests());
  for (size_t k = 0; k < people.size(); k++) {
    auto &g = people[k];
    g.hidden = false;
    g.x = 28.5f + float(k);
    g.y = 30.0f + float(k % 2);
    g.anim = "walk";
    g.fx = 0;
    g.fy = 1;
  }
  world.getStaff().hire(world.getStaff().typeOfFile("staff/keeper.ai"), 31.5f, 27.5f, world.getMap(), world.getFences());
  world.reindex();
  world.getCamera().zoom = 1.5f;
  SDL_SetWindowSize(SDL_GetRenderWindow(renderer), 1280, 720);
  SDL_SetRenderVSync(renderer, 0);
  const bool lightingWas = Features::dayNightLighting;
  Features::dayNightLighting = true;
  auto &light = world.getLighting();
  auto draw = [&] {
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_SetRenderViewport(renderer, nullptr);
    SDL_SetRenderScale(renderer, 1, 1);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    world.setView({0, 0, 1280, 720}, 1);
    world.draw(renderer);
    SDL_FlushRenderer(renderer);
  };
  SDL_Surface *shot = SDL_CreateSurface(1280, 720, SDL_PIXELFORMAT_RGBA32);
  if (!shot)
    return 1;
  int saved = 0;
  // (each of the camera's four turns: a shadow keeps to its owner's body)
  for (int side = 0; side < 4; side++) {
    world.getRenderer().centreOnWorld(31, 31.5f, 0);
    for (double hour : {8.0, 12.0, 16.0}) {
      light.previewHour = hour;
      for (int k = 0; k < (saved ? 6 : 120); k++)
        draw();
      if (ZT_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGBA32, shot->pixels, shot->pitch) == 0) {
        char name[64];
        std::snprintf(name, sizeof name, "/scene/r%d-%04.1f.png", side, hour);
        if (IMG_SavePNG(shot, (out + name).c_str()))
          saved++;
      }
      SDL_PumpEvents();
    }
    world.rotateView(1);
  }
  SDL_DestroySurface(shot);
  light.previewHour = -1;
  Features::dayNightLighting = lightingWas;
  return saved ? 0 : 1;
}

int runLightingGallery(World &world, ResourceManager *, SDL_Renderer *renderer, const std::string &out) {
  namespace fs = std::filesystem;
  if (std::string(std::getenv("ZT_LIGHTING_GALLERY")) == "scene")
    return runShadowScene(world, renderer, out);
  fs::create_directories(out + "/gallery");
  std::vector<std::string> files;
  const char *only = std::getenv("ZT_GALLERY_ONLY");
  const char *prefix = std::getenv("ZT_GALLERY_PREFIX");
  const std::string want = prefix && *prefix ? prefix : "scenery/building/";
  for (const auto &item : ItemCatalog::get().all())
    if (item.file.rfind(want, 0) == 0 && (!only || item.file.find(only) != std::string::npos))
      files.push_back(item.file);
  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  constexpr int spacing = 14, cols = 12;
  const int rows = std::max(1, (int(files.size()) + cols - 1) / cols);
  if (!world.loadBlank(cols * spacing + 28, rows * spacing + 28))
    return 1;
  world.setPaused(true);
  world.edgeScroll = false;
  world.getFences().setEditing(true);
  struct Spot { std::string file; float x, y; };
  std::vector<Spot> placed;
  std::ofstream list(out + "/gallery.csv");
  list << "file,placed\n";
  for (size_t i = 0; i < files.size(); i++) {
    const float x = 14.5f + float(i % cols) * spacing, y = 14.5f + float(i / cols) * spacing;
    const bool ok = world.objectsMutable().add(files[i], x, y, 0);
    list << files[i] << ',' << ok << '\n';
    if (ok)
      placed.push_back({files[i], x, y});
  }
  world.reindex();
  world.getCamera().zoom = 1.5f; // (close enough to see signs)
  SDL_SetWindowSize(SDL_GetRenderWindow(renderer), 1280, 720);
  SDL_SetRenderVSync(renderer, 0);
  const bool lightingWas = Features::dayNightLighting;
  Features::dayNightLighting = true;
  auto &light = world.getLighting();
  auto draw = [&] {
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_SetRenderViewport(renderer, nullptr);
    SDL_SetRenderScale(renderer, 1, 1);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    world.setView({0, 0, 1280, 720}, 1);
    world.draw(renderer);
    SDL_FlushRenderer(renderer);
  };
  SDL_Surface *shot = SDL_CreateSurface(560, 480, SDL_PIXELFORMAT_RGBA32);
  if (!shot)
    return 1;
  // (each one visited first, so its lighting shapes are made: a few a frame)
  light.previewHour = 9;
  for (const Spot &s : placed) {
    world.getRenderer().centreOnWorld(s.x, s.y, 0);
    for (int k = 0; k < 8; k++)
      draw();
    SDL_PumpEvents();
  }
  int saved = 0;
  for (int side = 0; side < 4; side++) {
    for (double hour : {9.0, 16.5, 0.0}) {
      light.previewHour = hour;
      for (const Spot &s : placed) {
        world.getRenderer().centreOnWorld(s.x, s.y, 0);
        for (int k = 0; k < 3; k++)
          draw();
        // (the building's foot at the middle: most of it is above)
        const SDL_Rect crop{640 - 280, 360 - 360, 560, 480};
        if (ZT_RenderReadPixels(renderer, &crop, SDL_PIXELFORMAT_RGBA32, shot->pixels, shot->pitch) == 0) {
          const std::string name = fs::path(s.file).stem().string() + "-r" + std::to_string(side) + "-" +
                                   (hour == 0 ? "night" : hour < 12 ? "morning" : "afternoon") + ".png";
          if (IMG_SavePNG(shot, (out + "/gallery/" + name).c_str()))
            saved++;
        }
        SDL_PumpEvents();
      }
    }
    world.rotateView(1);
  }
  SDL_DestroySurface(shot);
  light.previewHour = -1;
  Features::dayNightLighting = lightingWas;
  std::ofstream(out + "/gallery-checks.txt") << "buildings " << files.size() << " placed " << placed.size()
                                              << " pictures " << saved << "\n" << "HD art: " << HdArt::stats() << "\n";
  return placed.empty() ? 1 : 0;
}
