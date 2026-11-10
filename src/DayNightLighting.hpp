#ifndef DAY_NIGHT_LIGHTING_HPP
#define DAY_NIGHT_LIGHTING_HPP

#include <SDL3/SDL.h>
#include <map>
#include <set>
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
#include "GpuSurfacesD3D11.hpp"

class Animation;
class ResourceManager;
class PlacedObjects;
class WorldMap;
class WorldRenderer;
class World;
class Fences;

// addition: optional map lighting over solid volumes reconstructed from the
// original four sprite views. Static local irradiance uses 3D voxel rays;
// directional sun shadows project the reconstructed surface mesh.
class DayNightLighting {
public:
  // ours: a frame let go (Animation::trimSources): its mask forgotten, so
  // another frame made later at the same address isn't taken for it
  void forgetSurface(SDL_Surface *s) { nativeMasks.erase(s); }
  static constexpr int maxLights = 96, maxEmissives = 160;
  static constexpr int maxMapWidth = 640, maxModelFaces=80000, maxActorFaces=16000, maxActors=160, maxActorModels=256;
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
  // ours (for the checks): every lamp pool's kept box is its own grid's
  bool groundBoxesTrue(const WorldMap &map) const;
  // ours (for the checks): the shapes are over the budget (the ones nearest
  // the view chosen), and every projected face is on the shape it was
  // projected from
  bool shapesChosen() const { return !keptOwners.empty(); }
  bool facesOnTheirShapes() const;
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

  // ours: the lights this frame, for the Test Zoo console (lighting lights)
  std::string describeLights() const;
private:
  friend int runLightingProjectionChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void()> &);
  friend int runLightingBeaconChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void()> &);
  friend int runLightingDetailChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void()> &);
  friend int runLightingChecks(World &,ResourceManager *,SDL_Renderer *,const std::string &,const std::function<void(const char*)> &,const SDL_Rect &);
  struct Type { float halfX = .25f, halfY = .25f; bool casts = false; int profile=0; bool foliage=false; };
  struct Light { float x, y, z, radius, lift; uint64_t owner; float distance;
    float sx=0,sy=0,energy=1,depth=0;SDL_FRect patch{};SDL_Texture *texture=nullptr;SDL_Color colour{255,218,154,255};SDL_FlipMode flip=SDL_FLIP_NONE;int kind=0;LightingAssets::Point normal{},axis{};bool reflective=false;float cone=0,sweep=0;
    // addition: a light on the ground only (not on surfaces), shown this much
    // this frame - a blinking beacon's - without its cached light changing
    bool groundOnly=false;float shown=1;
    SDL_Texture *glow=nullptr;int glowPad=6; }; // (an emitter's soft light round it: Animation::EmissionPatch::glow)
  std::map<std::string, Type> types;
  std::vector<Light> lights;
  std::vector<Light> emitters,transientLights;
  float glowStrength=120; // (the soft light round emitters: its alpha at full night)
  SDL_Renderer *frameRenderer=nullptr;
  const WorldRenderer *frameView=nullptr;
  const WorldMap *frameMap=nullptr;
  void actorEmission(Animation *,int,LightingAssets::Point,float,float,uint64_t,int,float intensity=1);
  struct Cover {uint64_t owner;Animation *art;CompassDirection side;float x,y,depth,l,t,r,b;int frame;};
  std::vector<Cover> covers;
  void finishEmitters();
  struct EmissionClip {size_t hash=0,used=0;std::vector<SDL_Rect> rectangles;};
  std::map<std::pair<uint64_t,SDL_Texture*>,EmissionClip> emissionClips;
  struct Instance {const LightingAssets::Model *model;const LightingAssets::Definition *asset;LightingAssets::Point position;float cosine,sine,distance;uint64_t owner;Animation *artwork=nullptr;int frame=0;float opacity=1;bool sprite=false;bool shaded=true;
    // addition: lit by lamps and beams once the sun's shadows have gone (a
    // fence, a solid tank wall: by day their own shadow blackened their art)
    bool nightReceiver=false;
    // ours: over the shapes' budget, one of the shapes chosen to be lit (the
    // others stay in the list, so each keeps its place - the sun maps' and
    // the lamps' name for it - and they still cast shadows and block lamps)
    bool kept=true;};
  std::vector<Instance> instances,actors;
  struct ActorModel {LightingAssets::Model model;size_t used=0;};
  std::map<std::pair<std::string,int>,ActorModel> actorModels;
  // ours: a pose's shape being made on a worker thread (LightingAssets::
  // runBuild) - a new animal's or guest's first sight of a pose cost up to
  // half a second on a weak laptop - its nearest made pose shown meanwhile
  struct PendingActorModel {std::shared_ptr<const LightingAssets::BuildJob> job;std::future<LightingAssets::Model> result;};
  std::map<std::pair<std::string,int>,PendingActorModel> pendingActorModels;
  size_t actorTick=0;
  void addActor(Animation *,int,LightingAssets::Point,float fx,float fy,float halfX,float halfY,uint64_t id);

  std::map<std::tuple<Animation*,int,std::string>,LightingAssets::Model> models;
  // (the same for the scenery's shapes: a big building's took up to a tenth
  // of a second on a weak laptop; it has none until it's made)
  std::map<std::tuple<Animation*,int,std::string>,PendingActorModel> pendingModels;
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
  // addition: light on a tank's water - its surface's height there, or
  // -1e9 for none (Fences' tanks); the light mostly taken in by the water,
  // and glints coming and going on its surface (drawEmission)
  const Fences *frameFences=nullptr;
  float waterLevelAt(float x,float y)const;
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
    SurfaceSamples local;float terrain[4]{1,1,1,1};size_t instance,localIndex=0;
    SDL_FRect bounds{};bool visible=true;
  };
  std::vector<ProjectedFace> projectedFaces,actorFaces;
  // ours: each scenery face's corners' sun in sun map 0 and 1 (projectedFaces'
  // order; 0 for a face not drawn), kept apart from the faces: a new map's
  // taken whole, and the two swapped whole as the hour moves on
  std::vector<std::array<float,4>> projectedSun[2];
  std::vector<uint64_t> projectedOwners; // ours: each face's shape's owner as projected (facesOnTheirShapes)
  // ours: each scenery face's depth, screen box, shape and facing, kept beside
  // projectedFaces (remade when projectionSerial moves on), so a frame's test
  // of what's on the screen reads 28 bytes a face rather than the whole face
  struct FaceCull {float depth,left,top,right,bottom;uint32_t instance,visible;};
  std::vector<FaceCull> faceCull;uint64_t faceCullSerial=0;
  // ours: a frame's lists for the surface lighting on the graphics card, kept
  // so their memory is reused, not asked of Windows again (and faulted in) each frame
  std::vector<GpuSurfacesD3D11::Draw> surfaceOrder;std::vector<GpuSurfacesD3D11::Face> surfaceMoving,surfaceFacesSent;std::vector<GpuSurfacesD3D11::FaceLight> surfaceLightSent;
  std::vector<uint32_t> surfaceActorAt,surfaceActorRank;std::vector<uint8_t> surfaceActorOn,surfaceHasMask;
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
  std::vector<ZT_Vertex> surfaceVertices,shadowVertices,fieldVertices;
  std::vector<int> surfaceIndices,shadowIndices,fieldIndices;
  float sunLeft=0,sunTop=0,sunExtent=0; // ours: where the sun maps are (updateSun)
  uint64_t irradianceHash=0,irradianceSerial=0;
  uint64_t casterHash=0; // ours: every shape that casts (the scene's shapes before the budget's choice): the sun maps' scene // ours: the scene the lamps' light was made for (prepare), and a count of its makings
  LightingAssets::SunDepth sunMaps[2];float sunBlend=0;size_t sunHash=0;uint64_t projectedSunStamp[2]={},receiverSunStamp[2]={};
  struct Receiver {std::array<LightingAssets::Point,4> p;SDL_FPoint screen[4];LightingAssets::Point normal;int sample[4];};
  std::vector<Receiver> receivers;
  struct ReceiverPoint {LightingAssets::Point p,normal;float amount=0,sun[2]{};};
  std::vector<ReceiverPoint> receiverPoints;
  std::vector<ReceiverPoint> receiverPrevious; // ours: the points before they were made again (prepareReceivers)
  std::vector<int> receiverMatch,receiverLattice,receiverChain;std::vector<uint32_t> receiverMissed;
  void sampleMissed(int k); // ours: sun map k asked at receiverMissed's points
  // ours: which ground points and face corners an actor's shadow may reach,
  // asked only in the sun maps' cells an actor reached; and what the map's
  // and each tank's ground pass share in a frame (each worked it all out)
  LightingAssets::ActorCells receiverCells[2],faceCells[2];uint64_t projectionSerial=0;
  std::vector<uint8_t> receiverNear,faceNear;std::vector<Uint8> shadowAlpha;std::vector<uint32_t> shadowShown;
  struct GroundBox{const void *data=nullptr;size_t count=0;LightingAssets::Point lo{},hi{};};std::vector<GroundBox> groundBoxes; // (composite's: each lamp's pool's box; cleared as buildIrradiance makes the grids)
  std::vector<ProjectedFace> faceScratch;std::vector<uint64_t> faceKeys,faceKeysSpare;
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
  // ours: the ground's shadow on Direct3D 11 (GpuSurfacesD3D11): the frame
  // its points were last brought up to date, the quads on the screen, a pass's
  size_t d3dGroundFrame=size_t(-1);bool d3dGroundReady=false;std::vector<uint32_t> d3dGroundShown,d3dGroundQuads,d3dGroundLive;
  // (the quads with a shadow in either map; the quads at each point)
  uint64_t d3dGroundLitKey=~0ull,d3dGroundAtKey=~0ull;std::vector<uint32_t> d3dGroundLit,d3dGroundAtStart,d3dGroundAt,d3dGroundSeen;
  std::vector<uint8_t> d3dGroundLitFlag;uint32_t d3dGroundSerial=0;
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
  // ours: a map made on a worker for a time and place (startSunJob's), and a
  // made one taken as sun map k when it fits what's there now
  std::future<std::shared_ptr<SunJob>> launchSunJob(const WorldMap &,double time,float left,float top,float extent);
  bool adoptSunJob(int k,const std::shared_ptr<SunJob> &job,double time,float left,float top,float extent,const WorldMap &);
  // ours: both maps made ahead for where the view is heading, before it
  // leaves the ones it has (they were made again at once, the frame waiting);
  // and the made-ahead ones for a place since left, kept until done (a
  // future from std::async waits for its work when let go)
  std::future<std::shared_ptr<SunJob>> recentreJobs[3]; // (this step's, the next's and the one after: the hour may move on meanwhile)
 float recentreLeft=0,recentreTop=0,recentreExtent=0;bool recentring=false;
  std::vector<std::future<std::shared_ptr<SunJob>>> retiredSunJobs;
  void retireRecentre();
  bool spriteShadow(const Instance &,const WorldMap &,bool actor)const; // addition: cast from its sprite, not the sun's map

  size_t projectionHash=0;int projectionRotation=-1;float projectionUnit=0;
  // ours: each shape's projected faces kept (by its owner, model, place and
  // turn) while the view's turn and height scale, the map and day or night
  // stay: chosen again in a big zoo, only the shapes new to the choice are
  // projected; dropped when unused for four projections
  // (and the faces' sun in each sun map as last asked, with the map's stamp:
  // taken again when the shape is chosen again while that map stands)
  struct ProjectedShape {std::vector<ProjectedFace> faces;uint64_t used=0;std::vector<std::array<float,4>> sun[2];uint64_t sunStamp[2]{};};
  std::unordered_map<uint64_t,ProjectedShape> projectedShapes;uint64_t projectedShapesKey=0,projectedShapesTick=0;
  std::vector<uint64_t> projectedKeys,projectedKeysSpare;std::vector<uint32_t> projectedShapeAt; // (projectModels' sort)
  std::vector<ProjectedShape*> projectedFaceShape;std::vector<uint32_t> projectedFaceAt;std::vector<uint64_t> projectedTakenKeys;std::vector<ProjectedShape*> projectedTakenShapes; // (each projected face's shape, and its place among the shapes projected; their keys)
  void keepProjectedSun();    // ours: each face's sun written back to its shape (before the faces are projected again)
  void carryProjectedSun(int k); // ours: projectedSun[k] from the shapes kept with sun map k's stamp; the rest asked of the map
  // ours: sceneHash's shapes part only (and whether night's on), not its
  // lights - what projectModels' faces depend on
  size_t geometryHash=0;
  // ours: prepare's choice of the shapes nearest the middle, kept until the
  // view moves a quarter of the screen, turns, zooms or the shapes change
  std::set<std::pair<uint64_t,const LightingAssets::Model*>> keptOwners;
  float keptCentre[2]={0,0},keptZoom=0;int keptRotation=-1;size_t keptCandidates=0;
  void projectModels(const WorldRenderer &);
  size_t sceneHash=0,structureHash=0;
  std::map<std::string,LightingAssets::Model> structureModels;
  std::vector<Instance> structureInstances;
  size_t collectStructures(World &);
  // ours: the land gates as moving shapes, with the animals and guests (in
  // actors, at the pose of their swing), not the scenery: as scenery, a gate
  // swinging open or shut remade the whole zoo's lighting
  std::map<std::string,LightingAssets::Model> gateModels;
  void collectGates(World &);
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
