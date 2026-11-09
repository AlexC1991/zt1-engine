#include "World.hpp"
#include "ItemCatalog.hpp"
#include "SpriteDatabase.hpp"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
namespace {
std::string cell(const std::string &s){std::string v="\"";for(char c:s){if(c=='\"')v+='\"';v+=c;}return v+'\"';}
std::string lower(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});std::replace(s.begin(),s.end(),'\\','/');return s;}
std::string token(std::string s){for(char &c:s)if(!std::isalnum(static_cast<unsigned char>(c)) && c!='-' && c!='_')c='_';return s;}
std::string join(const std::map<std::string,std::string> &values){std::string s;for(const auto &[k,v]:values){if(!s.empty())s+=';';s+=k+'='+v;}return s;}
std::string category(const CatalogItem &i){if(i.registrySection=="animals")return "animals";if(i.members.count("staff"))return "staff";if(i.members.count("foliage"))return "foliage";if(i.members.count("rocks"))return "rocks";if(i.file.starts_with("scenery/building/"))return "buildings";if(i.registrySection=="fences" || i.registrySection=="tankwall")return "fences";if(i.members.count("paths"))return "paths";return "other";}
}
// addition: read-only native-art inventory for deciding which surfaces need
// authored emission/transmission maps. It applies no new lighting profiles.
int runLightingAssetAudit(World &world,ResourceManager *rm,SDL_Renderer *renderer,const std::string &out) {
 namespace fs=std::filesystem;fs::create_directories(out+"/art");
 std::ofstream meta(out+"/assets.csv"),images(out+"/images.csv");
 meta<<"resource,name,category,registry_key,members,integers,animations,archive,mod\n";
 images<<"resource,state,direction,frame,total_frames,anchor_x,anchor_y,mirrored,image\n";
 int saved=0,missing=0;const char *names[]={"NE","SE","SW","NW"};
 const CompassDirection sides[]={CompassDirection::NE,CompassDirection::SE,CompassDirection::SW,CompassDirection::NW};
 auto save=[&](const std::string &resource,const std::string &state,Animation *art) {
  if(!art){++missing;return;}
  const auto dir=fs::path(out)/"art"/token(resource)/token(state);fs::create_directories(dir);
  std::set<int> poses{0};if(art->frameCount()>1){poses.insert(art->frameCount()/2);poses.insert(art->frameCount()-1);}
  // addition: brief flashes can fall between first/middle/last samples.
  if(state=="camera" || state=="fencezap" || state=="animal_test_fence" || resource.ends_with("/photobth.ai") || resource.ends_with("/helibase.ai") || resource.ends_with("/helicopt.ai") || resource.ends_with("/buoy.ai") || resource.ends_with("/trchest.ai") || resource.ends_with("/xmastree.ai"))
   for(int frame=0;frame<art->frameCount();++frame)poses.insert(frame);
  for(int k=0;k<4;++k)for(int frame:poses) {
   const auto native=art->lightingFrame(sides[k],frame);if(!native.surface)continue;
   const std::string relative="art/"+token(resource)+"/"+token(state)+"/"+names[k]+"-"+std::to_string(frame)+".png";
   if(!IMG_SavePNG(native.surface,(fs::path(out)/relative).string().c_str())){++missing;continue;}
   images<<cell(resource)<<','<<cell(state)<<','<<names[k]<<','<<frame<<','<<art->frameCount()<<','<<native.anchor.x<<','<<native.anchor.y<<','<<native.mirrored<<','<<cell(relative)<<'\n';++saved;
  }
  SDL_PumpEvents();
 };
 auto texture=[&](const std::string &resource,const std::string &state,SDL_Texture *tex) {
  if(!tex){++missing;return;}int w,h;if(ZT_QueryTexture(tex,nullptr,nullptr,&w,&h)!=0)return;
  auto *target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_TARGET,w,h);
  auto *surface=ZT_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_RGBA32);if(!target || !surface){SDL_DestroyTexture(target);SDL_DestroySurface(surface);return;}
  auto *previous=SDL_GetRenderTarget(renderer);SDL_Rect viewport,clip;float sx,sy;SDL_GetRenderViewport(renderer,&viewport);SDL_GetRenderClipRect(renderer,&clip);const bool clipped=SDL_RenderClipEnabled(renderer);SDL_GetRenderScale(renderer,&sx,&sy);
  SDL_SetRenderTarget(renderer,target);SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderScale(renderer,1,1);SDL_SetRenderClipRect(renderer,nullptr);SDL_SetRenderDrawColor(renderer,0,0,0,0);SDL_RenderClear(renderer);ZT_RenderCopy(renderer,tex,nullptr,nullptr);
  if(ZT_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,surface->pixels,surface->pitch)==0) {
   const std::string relative="art/"+token(resource)+"/"+token(state)+".png";fs::create_directories((fs::path(out)/relative).parent_path());
   if(IMG_SavePNG(surface,(fs::path(out)/relative).string().c_str())){images<<cell(resource)<<','<<cell(state)<<",tile,0,1,0,0,0,"<<cell(relative)<<'\n';++saved;}
  }
  SDL_SetRenderTarget(renderer,previous);SDL_SetRenderScale(renderer,sx,sy);SDL_SetRenderViewport(renderer,&viewport);SDL_SetRenderClipRect(renderer,clipped?&clip:nullptr);
  SDL_DestroyTexture(target);SDL_DestroySurface(surface);
 };
 auto record=[&](const std::string &resource,const std::string &name,const std::string &group,const std::string &key,const std::string &members) {
  std::unique_ptr<IniReader> ai{rm->hasResource(resource)?rm->getIniReader(resource):nullptr};std::map<std::string,std::string> ints,anims;
  if(ai){ints=ai->getSection("characteristics/integers");anims=ai->getSection("animations");
   if(anims.empty())for(const char *s:{"m/animations","f/animations","y/animations"})for(const auto &[k,v]:ai->getSection(s))anims.emplace(k,v);}
  meta<<cell(resource)<<','<<cell(name)<<','<<cell(group)<<','<<cell(key)<<','<<cell(members)<<','<<cell(join(ints))<<','<<cell(join(anims))<<','<<cell(fs::path(rm->archiveOf(resource)).filename().string())<<','<<rm->fromMod(resource)<<'\n';return anims;
 };
 for(const auto &i:ItemCatalog::get().all()) {
  std::string members;for(const auto &m:i.members){if(!members.empty())members+=';';members+=m;}
  const auto anims=record(i.file,i.name,category(i),i.registryKey,members);
  if(i.file.starts_with("scenery/")) {
   save(i.file,"idle",world.objectsMutable().artOfFile(i.file));
   for(const auto &[state,value]:anims)if(state!="idle") {
    std::string path=lower(value);if(path.ends_with(".ani"))path.resize(path.size()-4);
    if(path.find('/')==std::string::npos) {
      const std::string leaf=path;path.clear();
      for(const char *root:{"objects","food"})for(const auto &type:{fs::path(i.file).stem().string(),i.type}) {
        const auto candidate=std::string(root)+"/"+type+"/"+leaf+"/"+leaf;
        if(path.empty() && rm->hasResource(candidate+".ani"))path=candidate;
      }
    }
    if(!path.empty() && rm->hasResource(path+".ani"))save(i.file,state,rm->getAnimation(path));
   }
  } else if(i.file.starts_with("ambient/")) {
   std::string path="ambient/"+i.type+"/idle/idle";
   for(const auto &[state,value]:anims){path=lower(value);if(path.ends_with(".ani"))path.resize(path.size()-4);if(path.find('/')==std::string::npos)path="ambient/"+i.type+"/"+path+"/"+path;if(rm->hasResource(path+".ani"))save(i.file,state,rm->getAnimation(path));}
  }
 }
 auto &animals=world.getAnimals();
 for(size_t n=0;n<animals.types().size();++n) {
  const auto &t=animals.types()[n];Animals::Member m;m.type=int(n);m.id=-10000-int(n);
  for(const char *state:{"stand","walk","subswim","test_fence"})if(t.anims[0].count(state))if(auto *art=animals.art(m,state))save(t.file,std::string("animal_")+state,art);
 }
 auto &staff=world.getStaff();
 for(size_t n=0;n<staff.types().size();++n) {
  const auto &t=staff.types()[n];Staff::Member m;m.type=int(n);
  for(const char *state:{"stand","walk"})save(t.file,std::string("staff_")+state,staff.art(m,state));
  if(t.base){record(t.baseFile,rm->getString(t.nameId),"staff_base",t.key,"");save(t.baseFile,"base",t.base);save(t.baseFile,"used",t.baseUsed);}
 }
 auto &guests=world.getGuests();
 for(size_t n=0;n<guests.types().size();++n) {
  const auto &t=guests.types()[n];Guests::Guest g;g.type=int(n);const std::string file="guests/"+t.key;
  record(file,rm->getString(t.nameId),"guests",t.key,"");
  for(const char *state:{"stand","walk","camera","fencezap"})if(t.anims.count(state))save(file,state,guests.art(g,state));
 }
 std::ofstream runtime(out+"/runtime.csv");runtime<<"resource,key,electrified,tank,see_through,show_wall,height,has_gate\n";
 for(const auto &t:world.getFences().types()) {
  runtime<<cell(t.file)<<','<<cell(t.key)<<','<<t.electrified<<','<<t.tank<<','<<t.seeThrough<<','<<t.showFence<<','<<t.height<<','<<t.hasGate<<'\n';
  save(t.file,"fence",t.art[0][0]);if(t.hasGate)save(t.file,"gate",t.art[1][0]);save(t.file,"decayed",t.det[0]);save(t.file,"broken",t.broke[0]);
  if(t.tank){save(t.file,"tank_top",t.tankArt[1][2]);save(t.file,"platform",t.platform);save(t.file,"ladder",t.ladder);save(t.file,"portal",t.portal);}
 }
 for(const char *path:{"objects/poo/idle/idle","objects/trash/idle/idle","objects/dustcld/idle/idle","objects/watcloud/idle/idle"})
  if(rm->hasResource(std::string(path)+".ani")){meta<<cell(path)<<','<<cell(path)<<",runtime_effect,,,,,,0\n";save(path,"effect",rm->getAnimation(path));}
 auto &sprites=SpriteDatabase::get();
 for(int n=0;n<32;++n) {
  const auto name=sprites.getTerrainName(n);if(name.empty())continue;
  meta<<cell("terrain-id:"+std::to_string(n))<<','<<cell(name)<<",terrain,"<<n<<",,,,,0\n";
  texture("terrain-id:"+std::to_string(n),"texture",sprites.getTerrainTexture(renderer,n));
 }
 for(const auto &i:ItemCatalog::get().all())if(i.members.count("paths"))for(int frame:{1,5,20}) {
  const auto &piece=sprites.getPathSprite(renderer,fs::path(i.file).stem().string(),frame);texture(i.file,"path_"+std::to_string(frame),piece.texture);
 }
 std::ofstream(out+"/audit-status.txt")<<"Catalogue entries: "<<ItemCatalog::get().all().size()<<"\nExported views: "<<saved<<"\nMissing optional animation lookups: "<<missing<<"\nNo lighting profiles or game assets modified.\n";
 return 0;
}
