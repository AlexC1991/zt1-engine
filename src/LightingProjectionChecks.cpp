#include "World.hpp"
#include "Features.hpp"
#include "HdArt.hpp"
#include "Mods.hpp"
#include <filesystem>
#include <memory>
#include "RenderSettings.hpp"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
namespace LightingDiagnostics {extern bool omitReceivers,fullReceiverClip;}
// addition: analytic cone/excitation checks and native, disposable map footage.
int runLightingProjectionChecks(World &world,ResourceManager *rm,SDL_Renderer *renderer,const std::string &out,const std::function<void()> &draw) {
  using namespace LightingAssets;int failed=0;std::ostringstream log;
  auto check=[&](const std::string &name,bool ok){log<<(ok?"PASS ":"FAIL ")<<name<<'\n';failed+=!ok;};
  auto capture=[&](const std::string &name){draw();int w,h;SDL_GetCurrentRenderOutputSize(renderer,&w,&h);auto *s=SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);if(!s){++failed;return;}
    if(ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,s->pixels,s->pitch) || !IMG_SavePNG(s,(out+"/"+name+".png").c_str()))++failed;SDL_DestroySurface(s);};
  auto pixels=[&]{int w,h;SDL_GetCurrentRenderOutputSize(renderer,&w,&h);std::vector<Uint32> data(size_t(w)*h);if(ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,data.data(),w*4))data.clear();return data;};
  auto coverage=[&]{int w,h;SDL_GetCurrentRenderOutputSize(renderer,&w,&h);std::vector<Uint8> mask(size_t(w)*h);for(const auto &c:world.getLighting().covers){const auto f=c.art->lightingFrame(c.side,c.frame);auto *s=f.surface;if(!s)continue;
    std::vector<Uint8> shadow(size_t(s->w)*s->h);if(f.shadowRuns)for(auto run:*f.shadowRuns)for(size_t i=size_t(run>>32);i<size_t(run>>32)+uint32_t(run);++i)shadow[i]=1;
    const float left=f.mirrored?c.x+f.anchor.x-s->w:c.x-f.anchor.x,top=c.y-f.anchor.y;
    for(int y=0;y<s->h;++y)for(int x=0;x<s->w;++x){int sx=f.mirrored?s->w-1-x:x;const auto pixel=reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(s->pixels)+y*s->pitch)[sx];int px=int(std::floor(left+x+.5f)),py=int(std::floor(top+y+.5f));
      if(px>=0 && py>=0 && px<w && py<h && (pixel&SDL_GetPixelFormatDetails(s->format)->Amask) && !shadow[size_t(y)*s->w+sx])mask[size_t(py)*w+px]=1;}}
    return mask;};
  auto changed=[](Uint32 a,Uint32 b){for(int shift:{0,8,16})if(std::abs(int((a>>shift)&255)-int((b>>shift)&255))>1)return true;return false;};
  const std::string mode=std::getenv("ZT_LIGHTING_BENCH");


  if(mode=="hd-review") {


    namespace fs=std::filesystem;extern std::string hdReviewMaterialKey(int);
    const bool was=Features::dayNightLighting;Features::dayNightLighting=true;
    world.loadBlank(96,96);world.setPaused(true);auto &light=world.getLighting();light.previewHour=0;
    world.getCamera().zoom=1.5f;world.getRenderer().centreOnWorld(48,48,0);
    auto &objects=world.objectsMutable();objects.add("scenery/building/restrant.ai",47.5f,47.5f,0);
    objects.add("scenery/other/streetlt.ai",44.5f,47.5f,0);world.reindex();draw();
    check("isolated HD review pack is active",HdArt::active());
    auto atlas=[&](const std::vector<Animation::EmissionPatch> &patches,int w,int h,int scale) {
      std::vector<Uint8> alpha(size_t(w)*h*scale*scale);auto *target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,w*scale,h*scale);
      auto *surface=SDL_CreateSurface(w*scale, h*scale, SDL_PIXELFORMAT_RGBA32);auto *previous=SDL_GetRenderTarget(renderer);
      if(!target || !surface){SDL_DestroyTexture(target);SDL_DestroySurface(surface);++failed;return alpha;}
      SDL_SetRenderTarget(renderer,target);SDL_SetRenderScale(renderer,1,1);SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderClipRect(renderer,nullptr);SDL_SetRenderDrawColor(renderer,0,0,0,0);SDL_RenderClear(renderer);
      for(const auto &patch:patches){SDL_BlendMode mode;Uint8 a;SDL_GetTextureBlendMode(patch.texture,&mode);SDL_GetTextureAlphaMod(patch.texture,&a);SDL_SetTextureBlendMode(patch.texture,SDL_BLENDMODE_NONE);SDL_SetTextureAlphaMod(patch.texture,255);
        SDL_Rect rect{patch.rect.x*scale,patch.rect.y*scale,patch.rect.w*scale,patch.rect.h*scale};ZT_RenderCopy(renderer,patch.texture,nullptr,&rect);SDL_SetTextureBlendMode(patch.texture,mode);SDL_SetTextureAlphaMod(patch.texture,a);}
      if(ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,surface->pixels,surface->pitch))++failed;
      for(int y=0;y<h*scale;++y)for(int x=0;x<w*scale;++x)alpha[size_t(y)*w*scale+x]=static_cast<Uint8*>(surface->pixels)[y*surface->pitch+x*4+3];
      SDL_SetRenderTarget(renderer,previous);SDL_DestroySurface(surface);SDL_DestroyTexture(target);return alpha;
    };
    struct Case {const char *item,*state;};
    const Case cases[]={{"streetlt","idle"},{"lightho","idle"},{"restrant","idle"},{"restrant","used"},{"carousal","idle"},{"carousal","used"},{"helibase","idle"},{"helibase","used"}};
    std::ofstream rows(out+"/hd-poses.csv");rows<<"item,state,view,frame,native_active,baked,texture_pixels,uncropped_pixels,alpha_mismatches\n";
    long long allPoses=0,alphaCompared=0,texturePixels=0,fullPixels=0;
    for(const auto &c:cases){const auto *a=find(c.item);const std::string path=std::string("objects/")+c.item+"/"+c.state+"/"+c.state;
      std::unique_ptr<Animation> art{rm->getAnimation(path)};bool loaded=art && a,paired=loaded,dimensions=loaded,footprint=loaded;int inspected=0,active=0;
      if(loaded)for(int side=0;side<4;++side)for(int frame=0;frame<art->frameCount();++frame){
        const auto view=CompassDirection(side);const auto native=art->lightingFrame(view,frame);if(!native.surface)continue;
        RenderSettings::worldZoomedIn=false;const auto &nativePatches=art->emissionPatches(renderer,view,frame,a->id);const bool lit=!nativePatches.empty();
        const int w=native.surface->w,h=native.surface->h;const bool sample=frame==0 || frame==art->frameCount()/2 || frame==art->frameCount()-1;
        std::vector<Uint8> nativeAlpha;if(sample)nativeAlpha=atlas(nativePatches,w,h,1);
        RenderSettings::worldZoomedIn=true;const auto &hd=art->emissionPatches(renderer,view,frame,a->id);bool baked=!hd.empty();size_t patchPixels=0;
        for(const auto &p:hd){int tw=0,th=0;ZT_QueryTexture(p.texture,nullptr,nullptr,&tw,&th);baked &= std::string(p.material)=="baked HD material";
          dimensions &= tw==p.rect.w*4 && th==p.rect.h*4 && p.rect.x>=0 && p.rect.y>=0 && p.rect.x+p.rect.w<=w && p.rect.y+p.rect.h<=h;patchPixels+=size_t(tw)*th;}
        paired &= lit?baked:hd.empty();active+=lit;int mismatches=0;
        if(sample && lit && baked){auto alpha=atlas(hd,w,h,4);
          // The bake's mask is the native material footprint intersected with
          // the finished HD frame's alpha; no bright pixel discovery is allowed.
          const int selected=a->animated?frame:0;const char *dirs[]={"NE","SE","SW","NW"};
          std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> colour{HdArt::load(art->lightingKey()+"/"+dirs[side]+"_"+std::to_string(selected)),SDL_DestroySurface};
          if(!colour || colour->w!=w*4 || colour->h!=h*4)footprint=false;
          else {auto *rgba=SDL_ConvertSurface(colour.get(), SDL_PIXELFORMAT_RGBA32);
            for(int y=0;y<h*4;++y)for(int x=0;x<w*4;++x){const int expected=std::min(int(nativeAlpha[size_t(y/4)*w+x/4]),int(static_cast<Uint8*>(rgba->pixels)[y*rgba->pitch+x*4+3]));mismatches+=std::abs(int(alpha[size_t(y)*w*4+x])-expected)>1;}
            SDL_DestroySurface(rgba);alphaCompared+=size_t(w)*h*16;}
          footprint &= mismatches==0;
        }
        rows<<c.item<<','<<c.state<<','<<side<<','<<frame<<','<<lit<<','<<baked<<','<<patchPixels<<','<<(lit?size_t(w)*h*16:0)<<','<<mismatches<<'\n';
        texturePixels+=patchPixels;fullPixels+=lit?size_t(w)*h*16:0;++inspected;++allPoses;
      }
      const std::string label=std::string(c.item)+" "+c.state;
      check(label+" native animation is available",loaded && inspected>0);
      check(label+" active and inactive poses retain their material gates",paired && active>0);
      check(label+" baked layers keep four-times resolution and native bounds",dimensions);
      check(label+" sampled alpha stays within the approved material and HD body",footprint);
    }
    // The following mutations are restricted to the isolated review fixture.
    const auto roots=Mods::enabledPaths();fs::path root;for(const auto &p:roots)if(fs::path(p).filename()=="review-pack")root=fs::weakly_canonical(p);
    const auto review=fs::weakly_canonical(fs::path(out).parent_path()/"mods/review-pack");
    check("temporary file probes are inside the isolated review pack",!root.empty() && root==review);
    if(!root.empty() && root==review){
      const auto *a=find("streetlt");auto *art=objects.artOfFile("scenery/other/streetlt.ai");
      const auto file=root/"hd/_lighting"/hdReviewMaterialKey(a->id)/std::to_string(a->id)/"0/objects/streetlt/idle/NE_0.png",backup=fs::path(file.string()+".review-backup");
      auto baked=[&]{RenderSettings::worldZoomedIn=true;const auto &patches=art->emissionPatches(renderer,CompassDirection::NE,0,a->id);return !patches.empty() && std::string(patches[0].material)=="baked HD material";};
      std::error_code ec;fs::rename(file,backup,ec);check("review layer can be temporarily withheld",!ec);
      if(!ec){HdArt::refresh();check("missing HD map falls back to the native material",!baked());
        auto *tiny=SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);SDL_FillSurfaceRect(tiny,nullptr,0xffffffff);const bool saved=IMG_SavePNG(tiny,file.string().c_str());SDL_DestroySurface(tiny);
        HdArt::refresh();check("incorrect HD map dimensions fall back without replacing the sprite",saved && !baked());fs::remove(file,ec);fs::rename(backup,file,ec);HdArt::refresh();check("restoring a map reloads the baked layer",!ec && baked());
      }
      world.getCamera().zoom=1.5f;Features::dayNightLighting=true;light.previewHour=0;draw();const auto on=pixels();
      const bool disabled=Mods::setEnabled("review-pack",false);HdArt::refresh();draw();check("HD mod can be switched off in its private session",disabled && !HdArt::active());const auto off=pixels();
      const bool enabled=Mods::setEnabled("review-pack",true);HdArt::refresh();draw();check("switching HD back on restores identical lit pixels",enabled && HdArt::active() && on==pixels() && on!=off);
    }
    for(int v=0;v<4;++v){light.previewHour=0;capture("restaurant_hd_night_"+std::to_string(v));light.previewHour=12;capture("restaurant_hd_day_"+std::to_string(v));world.rotateView(1);}

    draw();const auto beforeCacheReset=pixels();HdArt::setResources(rm);draw();
    check("resetting palette references rebuilds the same HD recolours",beforeCacheReset==pixels());
    world.loadBlank(96,96);world.setPaused(true);Features::dayNightLighting=true;light.previewHour=0;
    objects.add("scenery/building/carousal.ai",48.5f,48.5f,0);world.getRenderer().centreOnWorld(48.5f,48.5f,1);world.getCamera().zoom=1.5f;
    auto *ride=objects.last();
    for(int v=0;v<4;++v){for(int f:{0,6}){ride->inUse=true;ride->useTime=f*std::max(1u,ride->used?ride->used->frameTimeMs():100u)/1000.0;capture("carousel_hd_"+std::to_string(v)+"_"+std::to_string(f));}world.rotateView(1);}
    world.loadBlank(96,96);world.setPaused(true);Features::dayNightLighting=true;light.previewHour=0;world.getFences().setEditing(true);world.reindex();auto &staff=world.getStaff();int type=-1;
    for(size_t i=0;i<staff.types().size();++i)if(staff.types()[i].kind==Staff::Kind::Helicopter){type=int(i);break;}
    check("HD helipad capture uses the real helicopter team",type>=0);
    if(type>=0){const auto id=staff.hire(type,48.5f,48.5f,world.getMap(),world.getFences());auto *team=staff.member(id);check("HD helipad team can be hired",team!=nullptr);
      if(team){const auto &t=staff.types()[type];team->heliStage=Staff::HeliStage::Chase;team->anim="fly";team->heliX=70;team->heliY=70;team->heliZ=5;
        world.getRenderer().centreOnWorld(team->x,team->y,2);world.getCamera().zoom=1.5f;
        for(int v=0;v<4;++v){for(int f:{0,6}){team->baseAnimTime=f*std::max(1u,t.baseUsed?t.baseUsed->frameTimeMs():100u)/1000.0;capture("helipad_hd_"+std::to_string(v)+"_"+std::to_string(f));}world.rotateView(1);}
      }
    }
    std::ofstream(out+"/hd-review-checks.txt")<<log.str()<<"Pose/view combinations: "<<allPoses<<"\nAlpha samples compared: "<<alphaCompared<<"\nCropped texture pixels: "<<texturePixels<<"\nUncropped texture pixels: "<<fullPixels<<"\nFailures: "<<failed<<'\n';
    RenderSettings::worldZoomedIn=false;light.previewHour=-1;Features::dayNightLighting=was;return failed?1:0;
  }
  if(mode=="hd-materials") {
    auto &light=world.getLighting();const bool was=Features::dayNightLighting;Features::dayNightLighting=true;
    world.loadBlank(96,96);world.setPaused(true);light.previewHour=0;
    world.objectsMutable().add("scenery/other/streetlt.ai",47.5f,47.5f,0);
    auto *art=world.objectsMutable().artOfFile("scenery/other/streetlt.ai");const auto *a=find("streetlt");
    world.getCamera().zoom=2;world.getRenderer().centreOnWorld(47.5f,47.5f,0);draw();
    check("HD sample mod is loaded only in the private test session",HdArt::active());
    bool maps=art && a,scale=maps;
    for(int v=0;v<4 && art && a;++v) {
      const auto side=CompassDirection(v);const auto native=art->lightingFrame(side,0);RenderSettings::worldZoomedIn=true;
      const auto &patches=art->emissionPatches(renderer,side,0,a->id);
      maps &= patches.size()==1 && std::string(patches[0].material)=="baked HD material";
      if(!patches.empty()){int w=0,h=0;ZT_QueryTexture(patches[0].texture,nullptr,nullptr,&w,&h);scale &= native.surface && w==patches[0].rect.w*4 && h==patches[0].rect.h*4 && patches[0].rect.x>=0 && patches[0].rect.y>=0 && patches[0].rect.x+patches[0].rect.w<=native.surface->w && patches[0].rect.y+patches[0].rect.h<=native.surface->h;}
      capture("hd_night_"+std::to_string(v));world.rotateView(1);
    }
    check("four native views load their matching baked emission layer",maps);
    check("HD emission has four-times texture resolution with native placement",scale);
    world.getCamera().zoom=.8f;draw();bool nativeOnly=true;
    if(art && a)for(const auto &p:art->emissionPatches(renderer,CompassDirection::NE,0,a->id))nativeOnly &= std::string(p.material)!="baked HD material";
    check("zooming out returns to native material patches",nativeOnly);
    world.getCamera().zoom=2;light.previewHour=12;capture("hd_day");
    Features::dayNightLighting=false;draw();const auto base=pixels();Features::dayNightLighting=true;draw();Features::dayNightLighting=false;draw();
    check("lighting switch off restores the same HD daylight pixels",base==pixels());
    std::ofstream(out+"/hd-material-checks.txt")<<log.str()<<"Failures: "<<failed<<'\n';
    light.previewHour=-1;Features::dayNightLighting=was;return failed?1:0;
  }
  if(mode=="fixtures" || mode=="fixtures-video") {
    auto &light=world.getLighting();const bool was=Features::dayNightLighting;world.loadBlank(96,96);world.setPaused(true);Features::dayNightLighting=true;light.previewHour=0;light.previewBeamTime=0;
    auto &objects=world.objectsMutable();auto &view=world.getRenderer();
    for(int i=0;i<6;++i)objects.add("scenery/other/streetlt.ai",37.5f+(i%3)*8,58.5f+(i/3)*10,(i%4)*2);
    objects.add("scenery/building/asitheat.ai",61.5f,62.5f,0);
    objects.add("scenery/other/lamp.ai",36.5f,73.5f,0);
    objects.add("scenery/other/lightho.ai",20.5f,20.5f,0);
    objects.add("scenery/other/srock2.ai",40.5f,60.5f,0);
    auto &tankWalls=world.getFences();auto &map=world.getMap();tankWalls.setEditing(true);int tankType=tankWalls.typeIndex("tankwal2");if(tankType<0)tankType=tankWalls.typeIndex("tankwall");
    if(tankType>=0){for(int x=55;x<61;++x){tankWalls.place({true,x,67},tankType,1,map);tankWalls.place({true,x,73},tankType,2,map);}for(int y=67;y<73;++y){tankWalls.place({false,55,y},tankType,3,map);tankWalls.place({false,61,y},tankType,4,map);}
      for(int id:tankWalls.updateExhibits(map,1,0,1))tankWalls.finishExhibit(id,"Street-light visibility tank",map);const int id=tankWalls.exhibitAt(56,68);if(id>=0)tankWalls.fillExhibit(id,true);}
    world.reindex();world.getCamera().zoom=.7f;view.centreOnWorld(47.5f,63.5f,0);for(int i=0;i<48;++i)draw();
    const auto *street=find("streetlt"),*theater=find("asitheat"),*lantern=find("lamp");
    check("street-light head has a stationary downward cone",street && street->sources.size()==1 && street->sources[0].cone>0 && street->sources[0].normal.z<-.99f && street->sources[0].sweep==0);
    bool aligned=true;const auto p=street->sources[0].position;const float sx[]={32*(p.x-p.y),-32*(p.x+p.y),32*(-p.x+p.y),32*(p.x+p.y)};
    const float sy[]={16*(p.x+p.y-p.z),16*(p.x-p.y-p.z),16*(-p.x-p.y-p.z),16*(-p.x+p.y-p.z)};
    for(int r=0;r<4;++r){const auto &region=street->views[r][0];float x=0,y=0;for(auto q:region.outline){x+=q.x/region.outline.size();y+=q.y/region.outline.size();}aligned &= std::hypot(x-sx[r],y-sy[r])<1;}
    check("source aligns with the native lamp head within one pixel in four views",aligned);
    bool noPots=theater && theater->sources.empty();for(const auto &v:theater->views)for(const auto &r:v)noPots &= r.material!=Material::Glass;
    check("theater planters no longer have glass emission or a floor lamp source",noPots);
    check("ordinary lantern keeps its diffuse light",lantern && lantern->sources[0].cone==0);
    int fixed=0;bool energy=true,grids=true,directed=true;
    for(size_t i=0;i<light.lights.size();++i){const auto &l=light.lights[i];if(l.cone<=0 || l.sweep!=0)continue;++fixed;
      const Point under{l.x,l.y,0};energy &= light.sourceEnergy(l,under,{0,0,1},uint64_t(-1))>0;
      directed &= light.sourceDirection(l,{l.x,l.y,l.z+2})==0 && light.sourceDirection(l,{l.x+7,l.y,0})==0;
      grids &= light.groundLight[i].size()==33*33;
      const auto &sample=light.groundLight[i][16*33+22];const float raw=light.sourceEnergy(l,sample.position,{0,0,1},uint64_t(-1));
      energy &= std::abs(raw-sample.energy)<.00001f;
    }
    check("six fixed spotlights coexist without consuming the four sweeping slots",fixed==6 && light.beamLights.size()==1);
    check("fixed-cone ground samples cache the angular falloff exactly once",energy && grids);
    check("street lighting rejects receivers above and outside its cone",directed);
    const auto stamp=light.sceneHash,tick=light.irradianceTick;light.previewBeamTime=3.5;draw();
    check("fixed spots and sweeping lighthouse reuse static irradiance as time advances",stamp==light.sceneHash && tick==light.irradianceTick);
    for(int r=0;r<4;++r){capture("fixtures_night_"+std::to_string(r));light.previewHour=12;capture("fixtures_day_"+std::to_string(r));light.previewHour=0;world.rotateView(1);}
    Features::dayNightLighting=false;draw();const auto original=pixels();Features::dayNightLighting=true;draw();Features::dayNightLighting=false;draw();
    check("fixture showcase restores exact original pixels when switched off",original==pixels());Features::dayNightLighting=true;
    log<<"Failures: "<<failed<<'\n';std::ofstream(out+"/fixture-checks.txt")<<log.str();SDL_Log("[FixtureChecks] %s",log.str().c_str());
    if(mode=="fixtures-video" && !failed){constexpr int fps=24,frames=48*fps;int w,h;SDL_GetCurrentRenderOutputSize(renderer,&w,&h);if(auto *window=SDL_GetRenderWindow(renderer))SDL_HideWindow(window);
#ifdef _WIN32
      _setmode(_fileno(stdout),_O_BINARY);
#endif
      std::printf("\nZT_LIGHTING_FRAMES %d %d %d %d\n",w,h,fps,frames);std::fflush(stdout);std::vector<Uint32> data(size_t(w)*h);int rotation=0;SDL_Texture *caption=nullptr;std::string last;
      for(int f=0;f<frames;++f){const int r=f/(12*fps);if(r!=rotation){world.rotateView(r-rotation);rotation=r;}light.previewBeamTime=float(f)/fps;draw();const std::string label="Stationary street-light cones - view "+std::to_string(r+1)+" of 4";
        if(label!=last){caption=rm->getStringTexture(renderer,25001,label,{255,232,172,255},25002);last=label;}if(caption){int tw,th;ZT_QueryTexture(caption,nullptr,nullptr,&tw,&th);SDL_Rect bg{195,h-90,tw+24,th+18},dst{207,h-81,tw,th};SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(renderer,20,35,28,235);ZT_RenderFillRect(renderer,&bg);ZT_RenderCopy(renderer,caption,nullptr,&dst);}
        if(ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,data.data(),w*4) || std::fwrite(data.data(),4,data.size(),stdout)!=data.size()){failed=1;break;}}
      std::fflush(stdout);
    }
    light.previewHour=light.previewBeamTime=-1;Features::dayNightLighting=was;return failed?1:0;
  }
  auto &light=world.getLighting();const bool was=Features::dayNightLighting;world.loadBlank(96,96);world.setPaused(true);Features::dayNightLighting=true;light.previewHour=0;draw();
  DayNightLighting::Light lamp{0,0,4,12,0,7,0};lamp.energy=4;lamp.cone=.94f;lamp.normal=lamp.axis={1,0,0};lamp.colour={220,240,255,255};
  check("projector illuminates its forward axis",light.sourceDirection(lamp,{6,0,4})>.99f);
  check("cone rejects points behind and beside the fixture",light.sourceDirection(lamp,{-6,0,4})==0 && light.sourceDirection(lamp,{0,6,4})==0);
  const auto clear=light.sourceEnergy(lamp,{4,0,4},{-1,0,0},8);check("projector lights a facing receiver",clear>0);
  Model wall;wall.minimum={2,-3,0};wall.step={.01f,6,8};wall.nx=wall.ny=wall.nz=1;
  wall.faces.push_back({{Point{2,-3,0},Point{2,3,0},Point{2,3,8},Point{2,-3,8}},{-1,0,0},{-1,0,0}});
  light.instances.push_back({&wall,nullptr,{0,0,0},1,0,0,9});light.indexLocalLights();
  check("an opaque wall blocks projected illumination",light.sourceEnergy(lamp,{4,0,4},{-1,0,0},8)==0);
  check("a building can obstruct its own recessed receiving surface",light.sourceEnergy(lamp,{4,0,4},{-1,0,0},9)==0);
  check("surface offset prevents a clear facade from shadowing itself",light.sourceEnergy(lamp,{2,0,4},{-1,0,0},9)>0);
  light.instances.clear();light.indexLocalLights();lamp.cone=0;lamp.normal=lamp.axis={0,0,0};lamp.x=0;lamp.y=0;lamp.z=2;lamp.colour={0,0,255,255};light.lights={lamp};
  const float blue=light.excitationAt({1,0,2});lamp.colour={255,0,0,255};light.lights={lamp};const float red=light.excitationAt({1,0,2});light.lights.clear();const float dark=light.excitationAt({1,0,2});
  check("blue excitation produces a tissue response",blue>.2f);
  lamp.colour={0,0,255,255};light.lights={lamp};light.instances.push_back({&wall,nullptr,{0,0,0},1,0,0,9});light.indexLocalLights();
  check("opaque geometry also blocks fluorescence excitation",light.excitationAt({4,0,2})==0);light.instances.clear();light.lights.clear();light.indexLocalLights();
  check("red light and darkness do not energize the fluorescent layer",red==0 && dark==0);
  check("the fluorescence mapping selects only supported reference materials",fluorescenceProfile("animals/morayeel.ai")==5001 && fluorescenceProfile("scenery/foliage/anemone.ai")==5002 && fluorescenceProfile("animals/orca.ai")==0);
  auto &map=world.getMap();auto &fences=world.getFences();auto &objects=world.objectsMutable();fences.setEditing(true);
  objects.add("scenery/other/lightho.ai",35.5f,34.5f,0);
  objects.add("scenery/building/restrant.ai",38.5f,42.5f,0);
  objects.add("scenery/building/atlshop.ai",43.5f,44.5f,0);
  objects.add("scenery/other/lamp.ai",42.5f,39.5f,0);
  int type=fences.typeIndex("tankwal2");if(type<0)type=fences.typeIndex("tankwall");
  if(type>=0){const int x=46,y=28,w=14,h=12;for(int i=x;i<x+w;++i){fences.place({true,i,y},type,1,map);fences.place({true,i,y+h},type,2,map);}for(int j=y;j<y+h;++j){fences.place({false,x,j},type,3,map);fences.place({false,x+w,j},type,4,map);}
    for(int id:fences.updateExhibits(map,1,0,1))fences.finishExhibit(id,"Fluorescence tank",map);const int tank=fences.exhibitAt(x+1,y+1);if(tank>=0)fences.fillExhibit(tank,true);
  }
  objects.add("scenery/foliage/anemone.ai",51.5f,34.5f,0);objects.add("scenery/foliage/dtcoral.ai",55.5f,34.5f,0);
  auto &animals=world.getAnimals();const int eel=animals.typeOfFile("animals/morayeel.ai");
  for(int i=0;i<3 && eel>=0;++i){int id=animals.adopt(eel,false,50.5f+i*3,33.5f,map,fences,true);if(auto *m=animals.member(id)){m->boxed=false;m->swimZ=1.0f;m->anim="subswim";if(!animals.art(*m,m->anim))m->anim="walk";m->fx=0;m->fy=-1;}}
  map.touch();world.reindex();world.getCamera().zoom=1;world.getRenderer().centreOnWorld(44,36,0);light.previewBeamTime=0;
  for(int i=0;i<40;++i)draw();
  check("native lighthouse has a projector at its lantern",light.beamLights.size()==1 && light.lights[light.beamLights[0]].z>9);
  bool varying=false;for(const auto &beam:light.beamSurface)for(const auto &object:beam)for(const auto &face:object)for(int k=1;k<4;++k)varying|=std::abs(face[k]-face[0])>.01f;
  check("native projected illumination varies across receiver corners",varying);
  check("projector ground uses its bounded 48-cell receiver grid",!light.beamLights.empty() && light.groundLight[light.beamLights[0]].size()==49*49);
  bool outward=false;for(const auto &l:light.lights)outward|=l.cone<=0 && std::abs(l.normal.x)+std::abs(l.normal.y)>.1f;
  check("building openings cast outward illumination",outward);
  const auto sources=light.lights;DayNightLighting::Light excitation{53,34,0,20,0,uint64_t(-2),0};excitation.energy=60;excitation.colour={0,120,255,255};light.lights.push_back(excitation);light.collectActors(world);
  bool eelCue=false,anemoneCue=false,coralCue=false;for(const auto &cue:light.fluorescenceCues){eelCue|=cue.profile==5001;anemoneCue|=cue.profile==5002;coralCue|=cue.profile==5003;}
  check("blue excitation activates native eel and both coral reference materials",eelCue && anemoneCue && coralCue);light.lights=sources;draw();
  std::ofstream diagnostics(out+"/projection-source.txt");for(size_t li:light.beamLights){const auto &l=light.lights[li];const auto &shape=light.beamShapes.at({l.owner,li});const auto range=std::minmax_element(shape.reach.begin(),shape.reach.end());diagnostics<<"origin="<<l.x<<","<<l.y<<","<<l.z<<" axis="<<l.axis.x<<","<<l.axis.y<<","<<l.axis.z<<" reach="<<*range.first<<","<<*range.second<<"\n";}

  const auto stamp=light.sceneHash;const auto cache=light.irradianceTick;light.previewBeamTime=2.5;draw();
  check("a sweeping projector reuses its static obstruction samples",light.sceneHash==stamp && light.irradianceTick==cache);
  check("projectors stay within the four-projector budget",light.beamLights.size()<=4);
  const size_t originalObjects=objects.all().size();const auto originalSurface=light.surfaceLight;const auto originalBeams=light.beamSurface;
  for(int i=0;i<4;++i)objects.add("scenery/other/lightho.ai",70.5f+i*3,72.5f,0);world.reindex();draw();
  check("five native lighthouses obey the four-projector limit",light.beamLights.size()==4);
  objects.all().resize(originalObjects);world.reindex();draw();
  check("projector insertion and removal restores identical cached receivers",light.surfaceLight==originalSurface && light.beamSurface==originalBeams);
  bool sameClips=true;extern void tankAnimalFastPaths(bool);
  for(int r=0;r<4;++r){light.previewBeamTime=0;tankAnimalFastPaths(false);draw();const auto reference=pixels();tankAnimalFastPaths(true);draw();sameClips &= !reference.empty() && reference==pixels();world.rotateView(1);}
  check("fluorescent marine bodies match exact tank clipping in four views",sameClips);
  const auto savedCues=light.fluorescenceCues;light.fluorescenceCues.clear();light.fluorescentArt.clear();
  if(!savedCues.empty())for(int i=0;i<100;++i){const auto &c=savedCues[0];light.addFluorescence(c.art,c.x,c.y,1,c.profile);}
  check("fluorescent materials obey the 64-visible-cue budget",!savedCues.empty() && light.fluorescenceCues.size()==64);
  Features::dayNightLighting=false;draw();const auto original=pixels();Features::dayNightLighting=true;draw();Features::dayNightLighting=false;draw();
  check("switching lighting off restores the exact original pixels",!original.empty() && original==pixels());Features::dayNightLighting=true;
  std::ofstream probes(out+"/buried-probes.csv");probes<<"rotation,depth,x,y,changed_pixels\n";bool soilHidden=true;int soilCases=0;bool cameraHides=true,fastParity=true;int parityFrames=0;
  for(int r=0;r<4;++r){light.previewBeamTime=0;capture("projection_night_"+std::to_string(r));const auto lit=pixels();const auto support=coverage();
    LightingDiagnostics::fullReceiverClip=true;draw();fastParity &= lit==pixels();++parityFrames;LightingDiagnostics::fullReceiverClip=false;
    LightingDiagnostics::omitReceivers=true;capture("diagnostic_no_receivers_"+std::to_string(r));const auto bare=pixels();LightingDiagnostics::omitReceivers=false;
    const float gx[]={-1,-1,1,1};const int rotation=world.getRenderer().getRotation();int width,height;SDL_GetCurrentRenderOutputSize(renderer,&width,&height);
    for(float z:{-.5f,-1.0f,-2.0f})for(int y:{30,32,34,36,38}){const Point p{gx[rotation]>0?60.025f:45.975f,float(y),z};cameraHides &= light.terrainClearance(p,world.getRenderer())<0;
      const Point back{gx[rotation]>0?46.15f:59.85f,float(y),z};cameraHides &= light.terrainClearance(back,world.getRenderer())>=0;
      float x,sy,d;world.getRenderer().worldToScreenF(p.x,p.y,p.z,x,sy,d);int px=int(std::floor(x)),py=int(std::floor(sy));if(px<40 || py<10 || px>=width-10 || py>=height-80)continue;
      bool native=false;for(int j=-4;j<=4;++j)for(int i=-4;i<=4;++i)native|=support[size_t(py+j)*width+px+i]!=0;if(native)continue;
      int n=0;for(int j=-1;j<=1;++j)for(int i=-1;i<=1;++i)n+=changed(lit[size_t(py+j)*width+px+i],bare[size_t(py+j)*width+px+i]);
      probes<<r<<","<<z<<","<<px<<","<<py<<","<<n<<"\n";soilHidden &= n==0;++soilCases;}
    light.previewBeamTime=2.5;capture("projection_turn_"+std::to_string(r));const auto turnPixels=pixels();LightingDiagnostics::fullReceiverClip=true;draw();fastParity &= turnPixels==pixels();++parityFrames;LightingDiagnostics::fullReceiverClip=false;
    if(r==2){const auto lit=pixels();const auto support=coverage();LightingDiagnostics::omitReceivers=true;capture("diagnostic_without_proxy_surfaces");const auto bare=pixels();LightingDiagnostics::omitReceivers=false;
      const auto &o=objects.objects()[1];auto *a=PlacedObjects::displayArt(o);const auto side=world.getRenderer().screenSide(0,-1);auto f=a->lightingFrame(side,PlacedObjects::animationFrame(o,a));float sx,sy,d;world.getRenderer().worldToScreenF(o.x,o.y,0,sx,sy,d);
      const int left=int(f.mirrored?sx+f.anchor.x-f.surface->w:sx-f.anchor.x),top=int(sy-f.anchor.y);int outside=0,protectedPixels=0,inside=0;std::ofstream stencil(out+"/outline-probes.csv");stencil<<"index\n";
      for(int y=std::max(5,top-6);y<std::min(height-80,top+f.surface->h+6);++y)for(int x=std::max(40,left-6);x<std::min(width-5,left+f.surface->w+6);++x){
        bool body=false;for(int j=-3;j<=3 && !body;++j)for(int i=-3;i<=3 && !body;++i)body=support[size_t(y+j)*width+x+i]!=0;
        const size_t index=size_t(y)*width+x;if(!body){stencil<<index<<"\n";++protectedPixels;outside+=changed(lit[index],bare[index]);}else if(support[index])inside+=changed(lit[index],bare[index]);}
      check("native building stencil does not paint a proxy outline onto surrounding pixels",protectedPixels>50 && outside==0);
      check("visible native building surfaces still receive projected illumination",inside>50);
      std::ofstream(out+"/outline-result.txt")<<"protected="<<protectedPixels<<" changed_outside="<<outside<<" changed_inside="<<inside<<"\n";
    }world.rotateView(1);}
  check("fast receiver path matches complete clipping in eight four-view captures",fastParity && parityFrames==8);
  check("terrain hides buried wall receivers but keeps exposed inner wall receivers in four views",cameraHides);
  check("buried tank posts do not change visible grass pixels in four views",soilCases>=4 && soilHidden);
  check("native receiver mask storage remains within four atlas pages",!light.maskPages.empty() && light.maskPages.size()<=4);
  light.previewHour=12;capture("projection_day");Features::dayNightLighting=false;capture("projection_original");Features::dayNightLighting=true;light.previewHour=0;
  log<<"Failures: "<<failed<<'\n';std::ofstream(out+"/projection-checks.txt")<<log.str();SDL_Log("[ProjectionChecks] %s",log.str().c_str());
  // addition: timed presentation of this native projector/tank fixture. Separate
  // from offline recording; no screenshot readback inside the measured loop.
  std::ofstream timing(out+"/projection-frames.csv");timing<<"mode,frame,frame_ms\n";
  for(int mode=0;mode<2;++mode){Features::dayNightLighting=mode!=0;for(int f=0;f<144;++f){light.previewBeamTime=f/24.0;world.getRenderer().centreOnWorld(44+std::sin(f*.03f)*2,36+std::cos(f*.03f)*2,0);const auto start=SDL_GetPerformanceCounter();draw();SDL_RenderPresent(renderer);if(f>=24)timing<<(mode?"night":"off")<<","<<f-24<<","<<1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency()<<"\n";}}
  Features::dayNightLighting=true;world.getRenderer().centreOnWorld(44,36,0);
  const bool video=std::string(std::getenv("ZT_LIGHTING_BENCH"))=="projection-video";
  if(video && !failed){constexpr int fps=24,seconds=48;int w,h;SDL_GetCurrentRenderOutputSize(renderer,&w,&h);if(auto *window=SDL_GetRenderWindow(renderer))SDL_HideWindow(window);
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    std::printf("\nZT_LIGHTING_FRAMES %d %d %d %d\n",w,h,fps,seconds*fps);std::fflush(stdout);std::vector<Uint32> pixels(size_t(w)*h);int rotation=0;SDL_Texture *caption=nullptr;std::string last;
    for(int f=0;f<fps*seconds;++f){float time=float(f)/fps;int rot=int(time/12);if(rot!=rotation){world.rotateView(rot-rotation);rotation=rot;}light.previewBeamTime=time;
      for(const auto &member:animals.members())if(auto *m=animals.member(member.id))m->animTime=time;
      objects.tick(1.0f/fps);draw();std::string label="Projected lighthouse light and fluorescence - view "+std::to_string(rot+1)+" of 4";
      if(label!=last){rm->setTextScale(1);caption=rm->getStringTexture(renderer,25001,label,{255,232,172,255},25002);last=label;}
      if(caption){int tw,th;ZT_QueryTexture(caption,nullptr,nullptr,&tw,&th);SDL_Rect bg{195,h-90,tw+24,th+18},dst{207,h-81,tw,th};SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);SDL_SetRenderDrawColor(renderer,20,35,28,235);ZT_RenderFillRect(renderer,&bg);ZT_RenderCopy(renderer,caption,nullptr,&dst);}
      if(ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,pixels.data(),w*4) || std::fwrite(pixels.data(),4,pixels.size(),stdout)!=pixels.size()){failed=1;break;}if(f%fps==0)std::fflush(stdout);
    }std::fflush(stdout);
  }
  light.previewBeamTime=-1;light.previewHour=-1;Features::dayNightLighting=was;return failed?1:0;
}

// addition: expose the material fingerprint to the isolated graphics check.
namespace HdArt {std::string materialKey(int);}
std::string hdReviewMaterialKey(int kind){return HdArt::materialKey(kind);}
