#ifndef LIGHTING_ASSETS_HPP
#define LIGHTING_ASSETS_HPP
#include <SDL3/SDL.h>
#include <array>
#include <memory>
#include <string>
#include <vector>
class Animation;

// addition: authored materials and solid geometry for reviewed game assets.
// Coordinates refer to the original sprite anchor, not its varying canvas.
namespace LightingAssets {
struct Point {float x=0,y=0,z=0;};
enum class Material {Glass, Neon, Sign, Interior, Fire, Water, Steam, Fluorescent};
enum class PixelMask {All,Flame,Lava,Bulb,Spark,RedSignal,GreenSignal,Camera,Shock,Water,Spray,Tar,BlueLens,RedLens,WhiteLamp,MorayTissue,CoralTissue};
struct Region {
  const char *name;Material material;std::vector<SDL_FPoint> outline;
  float stroke=0;SDL_Color colour{255,230,180,255};
  bool animated=false,shadowCut=false;
  PixelMask pixels=PixelMask::All;
};
struct Source {
  const char *name;Point position,normal;SDL_Color colour;
  float power=1,radius=5;
  float cone=0,sweep=0; // addition: outer cone cosine and radians/second; zero is the ordinary fixture
};
struct Definition {
  int id;const char *item;std::array<std::vector<Region>,4> views;
  std::vector<Source> sources;
  bool animated=false;
  bool nightFixtures=false; // addition: native active lamps also run while empty at night
  bool lightMap=false; // addition: a mod item's own light maps (modProfile)
};
void appendCatalogueDefinitions(std::vector<Definition> &);
void appendWaterDefinitions(std::vector<Definition> &);
bool pixelAllowed(PixelMask,Uint8,Uint8,Uint8);
void appendProjectionDefinitions(std::vector<Definition> &);
int fluorescenceProfile(const std::string &resource);
void appendAviationDefinitions(std::vector<Definition> &);
int aircraftProfile(const std::string &state,int layer);
void appendEffectDefinitions(std::vector<Definition> &);
void appendSignDefinitions(std::vector<Definition> &);
int actorEffect(const std::string &resource,const std::string &state,int frame);
const std::vector<Definition> &all();
// Views: NE, SE, SW, NW. Catalogued light-bearing surfaces are explicitly mapped;
// an unknown item has no inferred lights or stretched-sprite shadow.
const Definition *find(const std::string &item);
const Definition *profile(int id);
// addition: a mod item's own night lights, drawn in the Lab (Mods > the Night
// light layer): its glow from light-map PNGs beside its art, <art folder>/
// light/<direction>_<frame>.png (the ground point at the picture's middle),
// and the lights it casts. The item's file asks for it: [lighting] lightmap = 1
int modProfile(const std::string &file,std::vector<Source> sources);
int viewIndex(int compassDirection);
bool inside(const Region &,float x,float y);

struct Face {std::array<Point,4> p;Point normal, outward;};
struct Model {
  Point minimum,step;int nx=0,ny=0,nz=0;
  std::vector<unsigned char> solid;
  std::vector<Face> faces;
  int occupied=0;
  bool contains(int x,int y,int z)const;
  bool ray(Point from,Point to)const;
};
// Reconstruct the 3D solid from all four original views. Steam, flames and
// the fountain's water jet are explicitly non-solid. No game art or derived
// mesh is distributed; the model is built from the player's local assets.
Model build(Animation &,const Definition &,float halfX,float halfY,int frame=-1,bool actor=false);
// ours: build in three steps, so the slow middle one can run on a worker
// thread: what it's made from read from the art (and the model from the disk
// cache when it's there: hit), on the main thread; the solid and its faces,
// on any thread; the cache written, on the main thread again. build is the
// three in a row.
struct BuildJob;
std::shared_ptr<const BuildJob> prepareBuild(Animation &,const Definition &,float halfX,float halfY,int frame,bool actor,Model &cached,bool &hit);
Model runBuild(const BuildJob &);
// runBuild on a worker thread below the game's priority (put back after: the
// pool's threads are reused) - on a laptop the builds' cores share the power
// the game's thread runs on, and slowed every frame while they ran
Model runBuildInBackground(const BuildJob &);
void finishBuild(const BuildJob &,const Model &);
}
#endif
