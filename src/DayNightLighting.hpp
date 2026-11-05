#ifndef DAY_NIGHT_LIGHTING_HPP
#define DAY_NIGHT_LIGHTING_HPP

#include <SDL2/SDL.h>
#include <map>
#include <functional>
#include <string>
#include <tuple>
#include <vector>
#include <future>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include "CompassDirection.hpp"
#include "LightingAssets.hpp"
#include "LightingSun.hpp"
#include "GpuLighting.hpp"

class Animation;
class ResourceManager;
class PlacedObjects;
class WorldMap;
class WorldRenderer;
class World;

// addition: optional map lighting over solid volumes reconstructed from the
// original four sprite views. Static local irradiance uses 3D voxel rays;
// directional sun shadows project the reconstructed surface mesh.
class DayNightLighting {
public:
  static constexpr int maxLights = 96, maxEmissives = 160;
  static constexpr int maxMapWidth = 640, maxModelFaces=60000, maxActorFaces=16000, maxActors=160, maxActorModels=256;
  struct Sky { SDL_Color ambient; float lamps, sun, shadowLength; float dx, dy; float shadow=1; };
  struct Stats { int lights = 0, shadows = 0, rays = 0, width = 0, height = 0, emissives = 0, modelFaces=0,models=0, actors=0, actorFaces=0, actorBuilds=0, actorFallbacks=0, sunBuilds=0;size_t memoryBytes=0; };
  ~DayNightLighting();
  DayNightLighting() = default;
  DayNightLighting(const DayNightLighting &) = delete;
  DayNightLighting &operator=(const DayNightLighting &) = delete;

  // Clock is existing saved play time: pause, speed and reloading follow it.
  static double hourAt(double clock);
  static std::string formatTime(double hour);
  static Sky skyAt(double hour);
  void prepare(World &,ResourceManager *rm, const PlacedObjects &objects, const WorldMap &map,
               WorldRenderer &view, double clock, int width, int height, SDL_Renderer *renderer);
  void collectActors(World &);
  void updateSun(const WorldMap &,const WorldRenderer &);
  void drawShadows(SDL_Renderer *,const WorldRenderer &,const WorldMap &,const std::function<bool(float,float)> &accept={});
  float sunShadow(LightingAssets::Point,LightingAssets::Point normal={0,0,1},uint32_t receiver=0xFFFFFFFFu)const;
  void composite(SDL_Renderer *renderer, WorldRenderer &view, const WorldMap &map, const SDL_Rect &rect);
  void drawEmission(SDL_Renderer *,WorldRenderer &,const SDL_Rect &);
  // addition: the HUD date bar displays the same hour used by sky and lamps.
  double displayedHour() const { return hour; }
  const Stats &stats() const { return counters; }
  const GpuLighting::Stats &gpuStats()const {return gpu.stats();}
  const std::string &gpuStatus()const {return gpu.reason();}
  size_t bufferBytes()const;
  bool verifyLocalLightIndex()const; // disposable check scenes only
  size_t mapTextureBytes()const {return size_t(textureW)*textureH*4+(maskAtlasSide==4096?(maskPages.empty()?0:4096ull*4096*4):maskPages.size()*2048ull*2048*4);}
  const auto &assetModels()const {return models;}
  // Developer preview only, never persisted to a player's settings or zoo.
  double previewHour = -1;
  double previewBeamTime=-1; // check-only; normal play uses saved world time
  float excitationAt(LightingAssets::Point,uint64_t receiver=uint64_t(-1))const;
  void drawFluorescence(SDL_Renderer *,Animation *,float,float,CompassDirection,int);
  int diagnosticLightLimit=maxLights; // check-mode source scaling; normal play keeps the default

private:
  friend int runLightingProjectionChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void()> &);
  friend int runLightingBeaconChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void()> &);
  friend int runLightingDetailChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void()> &);
  struct Type { float halfX = .25f, halfY = .25f; bool casts = false; int profile=0; bool foliage=false; };
  struct Light { float x, y, z, radius, lift; uint64_t owner; float distance;
    float sx=0,sy=0,energy=1,depth=0;SDL_FRect patch{};SDL_Texture *texture=nullptr;SDL_Color colour{255,218,154,255};SDL_RendererFlip flip=SDL_FLIP_NONE;int kind=0;LightingAssets::Point normal{},axis{};bool reflective=false;float cone=0,sweep=0; };
  std::map<std::string, Type> types;
  std::vector<Light> lights;
  std::vector<Light> emitters,transientLights;
  SDL_Renderer *frameRenderer=nullptr;
  const WorldRenderer *frameView=nullptr;
  const WorldMap *frameMap=nullptr;
  void actorEmission(Animation *,int,LightingAssets::Point,float,float,uint64_t,int,float intensity=1);
  struct Cover {uint64_t owner;Animation *art;CompassDirection side;float x,y,depth,l,t,r,b;int frame;};
  std::vector<Cover> covers;
  void finishEmitters();
  struct EmissionClip {size_t hash=0,used=0;std::vector<SDL_Rect> rectangles;};
  std::map<std::pair<uint64_t,SDL_Texture*>,EmissionClip> emissionClips;
  struct Instance {const LightingAssets::Model *model;const LightingAssets::Definition *asset;LightingAssets::Point position;float cosine,sine,distance;uint64_t owner;Animation *artwork=nullptr;int frame=0;float opacity=1;bool sprite=false;bool shaded=true;};
  std::vector<Instance> instances,actors;
  struct ActorModel {LightingAssets::Model model;size_t used=0;};
  std::map<std::pair<std::string,int>,ActorModel> actorModels;
  size_t actorTick=0;
  void addActor(Animation *,int,LightingAssets::Point,float fx,float fy,float halfX,float halfY,uint64_t id);

  std::map<std::tuple<Animation*,int,std::string>,LightingAssets::Model> models;
  struct Irradiance {LightingAssets::Point position;SDL_Color colour;float energy=0;};
  struct CachedGround {std::vector<Irradiance> samples;size_t used=0;};
  using SurfaceSamples=std::array<float,3>;
  using BeamSamples=std::array<float,4>;
  struct CachedSurface {std::vector<SurfaceSamples> samples;size_t used=0;std::vector<std::vector<BeamSamples>> beams;};
  std::map<uint64_t,CachedGround> groundIrradianceCache;
  std::map<uint64_t,CachedSurface> surfaceIrradianceCache;
  size_t irradianceTick=0;
  double fixtureClock=0;
  std::vector<size_t> beamLights;
  std::vector<std::vector<std::vector<BeamSamples>>> beamSurface;
  struct BeamShape {std::array<float,128> reach{};size_t stamp=0;};
  std::map<std::pair<uint64_t,size_t>,BeamShape> beamShapes;
  struct FluorescenceCue {Animation *art;float x,y,strength;int profile;};
  std::vector<FluorescenceCue> fluorescenceCues;
  std::unordered_set<Animation*> fluorescentArt;
  void addFluorescence(Animation *,float,float,float,int);
  std::map<const LightingAssets::Model*,std::vector<LightingAssets::Source>> facadeSources;
  const std::vector<LightingAssets::Source> &windowSources(const LightingAssets::Model &,const LightingAssets::Definition &);
  float sourceEnergy(const Light &,LightingAssets::Point,LightingAssets::Point,uint64_t,bool angular=true)const;
  float sourceDirection(const Light &,LightingAssets::Point)const;
  void prepareBeamShapes();
  void drawBeamHaze(SDL_Renderer *,WorldRenderer &,float);
  std::array<float,3> projectedIrradiance(size_t,size_t,int,LightingAssets::Point,std::array<float,3>)const;
  std::vector<std::vector<Irradiance>> groundLight;
  std::vector<std::vector<SurfaceSamples>> surfaceLight;
  // addition: exact voxel rays visit candidates from four-tile world cells.
  std::unordered_map<uint64_t,std::vector<size_t>> lightOccluders,lightSources;
  void indexLocalLights();
  struct ProjectedFace {
    SDL_FPoint p[4];LightingAssets::Point world[4];float height[4],depth;LightingAssets::Point normal,outward;
    SurfaceSamples local;float terrain[4]{1,1,1,1};float sun[2][4]{};size_t instance,localIndex=0;
    SDL_FRect bounds{};bool visible=true;
  };
  std::vector<ProjectedFace> projectedFaces,actorFaces;
  struct MaskPage {SDL_Texture *texture=nullptr;int x=4,y=4,row=0;size_t used=0;};
  struct MaskEntry {int page,x,y,w,h;};
  struct ReceiverMask {SDL_Texture *texture=nullptr;SDL_FRect bounds{},uv{};bool mirrored=false,required=false;};
  int maskAtlasSide=2048;
  std::vector<MaskPage> maskPages;
  std::map<SDL_Surface*,MaskEntry> nativeMasks;
  ReceiverMask receiverMask(const Instance &,WorldRenderer &);
  SDL_Texture *whiteMask();
  float terrainClearance(LightingAssets::Point,const WorldRenderer &)const;
  // addition: persistent submission storage; panning reuses allocated capacity.
  std::vector<SDL_Vertex> surfaceVertices,shadowVertices,fieldVertices;
  std::vector<int> surfaceIndices,shadowIndices,fieldIndices;
  LightingAssets::SunDepth sunMaps[2];float sunBlend=0;size_t sunHash=0;uint64_t projectedSunStamp[2]={},receiverSunStamp[2]={};
  struct Receiver {std::array<LightingAssets::Point,4> p;SDL_FPoint screen[4];LightingAssets::Point normal;int sample[4];};
  std::vector<Receiver> receivers;
  struct ReceiverPoint {LightingAssets::Point p,normal;float amount=0,sun[2]{};};
  std::vector<ReceiverPoint> receiverPoints;
  // ours: which ground points and face corners an actor's shadow may reach,
  // asked only in the sun maps' cells an actor reached; and what the map's
  // and each tank's ground pass share in a frame (each worked it all out)
  LightingAssets::ActorCells receiverCells[2],faceCells[2];uint64_t projectionSerial=0;
  std::vector<uint8_t> receiverNear,faceNear;std::vector<Uint8> shadowAlpha;std::vector<uint32_t> shadowShown;
  std::vector<ProjectedFace> faceScratch;std::vector<uint32_t> faceOrder;
  std::vector<float> casterOpacity; // addition: by sun-map caster (instance+1): foliage lets light through
  GpuLighting gpu;
  bool gpuSunReady=false;
  bool gpuAtlasReady=false;
  size_t gpuCasterScene=0;
  int gpuCasterRotation=-1;
  uint64_t gpuCasterGeneration=0;
  std::vector<GpuLighting::CasterTriangle> gpuStaticCasters,gpuActorCasters;
  uint64_t gpuReceiverGeneration=0;
  std::vector<GpuLighting::GroundPoint> gpuGroundPoints;
  std::vector<uint32_t> gpuGroundIndices;
  std::vector<GpuLighting::SurfaceFace> gpuSurfaceFaces;
  std::vector<GpuLighting::SurfaceBatch> gpuSurfaceBatches;
  bool gpuSurfaceFailed=false;
  size_t receiverFrame=0,receiverSampleFrame=size_t(-1);
  int receiverX0=0,receiverY0=0,receiverX1=-1,receiverY1=-1,receiverRotation=-1;uint32_t receiverGeneration=0;
  void prepareReceivers(const WorldMap &,const WorldRenderer &);
  // ours: the next sun map made ahead on a worker thread, with its faces' and
  // ground points' samples (made on the main thread when its time came, a
  // frame stalled every 0.6 s). Used only if nothing it was made from changed.
  std::unordered_map<const LightingAssets::Model*,float> modelTop; // (a shape's highest point)
  struct SunJob;struct SunScene;struct SunGround;struct SunTerrain;
  std::shared_ptr<const SunScene> sunScene;std::shared_ptr<const SunGround> sunGround;std::shared_ptr<const SunTerrain> sunTerrain;
  std::future<std::shared_ptr<SunJob>> sunJob;
  void startSunJob(const WorldMap &,double time,float left,float top,float extent);
  bool takeSunJob(int k,double time,float left,float top,float extent,const WorldMap &);
  bool spriteShadow(const Instance &,const WorldMap &,bool actor)const; // addition: cast from its sprite, not the sun's map

  size_t projectionHash=0;int projectionRotation=-1;float projectionUnit=0;
  void projectModels(const WorldRenderer &);
  size_t sceneHash=0,structureHash=0;
  std::map<std::string,LightingAssets::Model> structureModels;
  std::vector<Instance> structureInstances;
  size_t collectStructures(World &);
  LightingAssets::Point toWorld(const Instance &,LightingAssets::Point)const;
  bool blocked(LightingAssets::Point,LightingAssets::Point,uint64_t ignoreOwner,uint64_t receiverOwner=uint64_t(-1))const;
  std::array<float,3> irradiance(LightingAssets::Point,LightingAssets::Point normal,uint64_t receiverOwner,bool transientOnly=false,bool includeProjectors=true)const;
  void buildIrradiance(const WorldMap &);
  void drawSurfaceLighting(SDL_Renderer *,WorldRenderer &,float scale,float mapMiddle);
  float logicalW = 0, logicalH = 0;
  SDL_Texture *lightmap = nullptr;
  int textureW = 0, textureH = 0;
  double hour = 12;
  Sky sky{};
  Stats counters;
  bool warned = false;
  Type typeOf(ResourceManager *rm, const std::string &file, const std::string &kind);
  bool targets(SDL_Renderer *renderer, int w, int h);
};
#endif
