#include "LightingAssets.hpp"
#include "CompassDirection.hpp"
#include <algorithm>
#include <deque>
#include <cmath>

namespace LightingAssets {
namespace {
using M=Material;
Region polygon(const char *name,M m,std::initializer_list<SDL_FPoint> points,SDL_Color colour={255,230,180,255},bool animated=false,bool cut=false) {
  return {name,m,points,0,colour,animated,cut};
}
Region line(const char *name,std::initializer_list<SDL_FPoint> points,float width,SDL_Color colour) {
  return {name,M::Neon,points,width,colour,false,false};
}
Region rect(const char *name,M material,float l,float t,float r,float b,SDL_Color colour,bool animated=false,bool cut=false) {
  return polygon(name,material,{{l,t},{r,t},{r,b},{l,b}},colour,animated,cut);
}
void panes(std::vector<Region> &out,SDL_FPoint a,SDL_FPoint b,SDL_FPoint c,SDL_FPoint d,int columns) {
  auto lerp=[](SDL_FPoint x,SDL_FPoint y,float t){return SDL_FPoint{x.x+(y.x-x.x)*t,x.y+(y.y-x.y)*t};};
  for(int i=0;i<columns;++i) {
    const float lo=(i+.12f)/columns,hi=(i+.88f)/columns;
    const auto p=lerp(a,b,lo),q=lerp(a,b,hi),r=lerp(d,c,hi),s=lerp(d,c,lo);
    out.push_back(polygon("window glass",M::Interior,{p,q,r,s},{255,206,136,255}));
  }
}
std::vector<Definition> definitions() {
  std::vector<Definition> result;
  Definition lamp{101,"lamp"};
  for(auto &v:lamp.views)v.push_back(polygon("lantern glass",M::Glass,{{-4,-65},{3,-65},{3,-60},{1,-58},{-3,-60}},{255,228,155,255}));
  lamp.sources.push_back({"lantern",{0,0,3.9f},{0,0,0},{255,226,159,255},1.8f,5});result.push_back(lamp);
  result.push_back({102,"bklamp"}); // Broken glass: metal only, no emission.
  Definition fire{103,"campfire"};
  for(auto &v:fire.views)v.push_back(polygon("flame",M::Fire,{{-11,-7},{-10,-15},{-4,-23},{2,-29},{6,-21},{12,-13},{11,-5},{3,-2},{-6,-3}},{255,151,58,255},true,true));
  fire.sources.push_back({"fire",{0,0,.8f},{0,0,0},{255,155,65,255},.8f,4});result.push_back(fire);

  Definition ride{104,"carousal"};
  // The rim belongs to the static canopy. Its material never samples the
  // moving horses, entrance tent or painted roof panels.
  for(int i=0;i<4;++i) {
    const float cx=i<2?-13.0f:14.0f,cy=(i==1 || i==2)?-66.0f:-52.0f;
    Region rim{"canopy bulbs",M::Glass,{},3,{255,228,165,255}};
    // Measured front edge of the bulb string in each native camera view.
    // Preserve the individual white bulbs and gaps instead of repainting a
    // continuous neon line across the coloured canopy fascia.
    for(int n=0;n<=24;++n) {
      const float a=n*3.14159265f/24;
      rim.outline.push_back({cx-87*std::cos(a),cy+47*std::sin(a)});
    }
    ride.views[i].push_back(rim);
    auto platform=line("platform trim",{{-65,12},{-54,21},{-35,28},{-12,32},{12,32},{36,27},{56,19},{68,10}},1.2f,{255,211,130,255});
    platform.material=M::Glass;
    for(auto &p:platform.outline){p.x+=cx;p.y+=(i==1 || i==2)?-14.0f:0.0f;}
    ride.views[i].push_back(platform);
  }
  for(int i=0;i<8;++i) {
    const float a=i*6.2831853f/8;
    ride.sources.push_back({"canopy bulbs",{.02f+1.7f*std::cos(a),.42f+1.7f*std::sin(a),3.7f},{0,0,0},{255,225,169,255},.16f,4.5f});
  }
  result.push_back(ride);

  Definition fountain{105,"fountain"};
  for(auto &v:fountain.views) {
    v.push_back(polygon("water jet",M::Water,{{-8,-130},{8,-130},{7,-17},{20,-8},{11,-1},{-13,-1},{-21,-8},{-6,-20}},{130,185,238,255},true,true));
    v.push_back(polygon("basin water",M::Water,{{-40,-13},{-21,-28},{8,-31},{38,-19},{43,-5},{20,5},{-17,5},{-39,-3}},{130,185,238,255},true));
  }
  result.push_back(fountain);
  Definition waterfall{106,"watfall1"};
  waterfall.views[0].push_back(polygon("falling water",M::Water,{{-18,-51},{0,-48},{18,-29},{30,-10},{35,2},{23,8},{11,-6},{-4,-25}},{130,185,238,255},true));
  waterfall.views[1].push_back(polygon("falling water",M::Water,{{-30,-83},{-18,-83},{-11,-62},{-2,-30},{13,-17},{39,-10},{42,1},{17,12},{-13,4},{-28,-7},{-33,-34}},{130,185,238,255},true));
  waterfall.views[2].push_back(polygon("falling water",M::Water,{{22,-91},{35,-86},{29,-59},{23,-34},{14,-23},{4,-17},{-3,-6},{22,-5},{37,-12},{40,2},{11,14},{-23,11},{-35,2},{-26,-14},{2,-44}},{130,185,238,255},true));
  waterfall.views[3].push_back(polygon("falling water",M::Water,{{13,-66},{24,-64},{9,-51},{-12,-33},{-21,-15},{-35,-6},{-45,3},{-52,-4},{-40,-19},{-26,-30},{-12,-43}},{130,185,238,255},true));
  result.push_back(waterfall);

  Definition burger{107,"bgrstnd"};
  burger.views[0].push_back(rect("steam",M::Steam,9,-85,31,-48,{},false,true));
  burger.views[1].push_back(rect("steam",M::Steam,-51,-89,-24,-52,{},false,true));
  burger.views[2].push_back(rect("steam",M::Steam,-38,-119,-14,-79,{},false,true));
  burger.views[3].push_back(rect("steam",M::Steam,15,-113,40,-76,{},false,true));
  burger.views[1].push_back(polygon("burger sign",M::Sign,{{8,-58},{20,-64},{31,-62},{39,-54},{40,-46},{35,-36},{22,-30},{9,-31},{3,-39},{3,-49}},{255,202,124,255}));
  burger.views[2].push_back(polygon("burger sign",M::Sign,{{-26,-68},{-14,-66},{-8,-58},{-8,-48},{-15,-40},{-28,-40},{-40,-47},{-42,-57},{-37,-64}},{255,202,124,255}));
  burger.views[1].push_back(polygon("service window",M::Interior,{{-15,-25},{6,-16},{6,1},{-15,-7}},{255,208,143,255}));
  burger.views[2].push_back(polygon("service window",M::Interior,{{-43,-29},{-22,-38},{-22,-19},{-43,-9}},{255,208,143,255}));
  burger.sources.push_back({"burger sign",{.05f,-.67f,3.8f},{0,-1,0},{255,196,116,255},.45f,4});
  burger.sources.push_back({"service window",{0,-.55f,.8f},{0,-1,0},{255,207,145,255},.3f,3});
  result.push_back(burger);

  Definition restaurant{108,"restrant"};
  restaurant.views[0].push_back(rect("steam",M::Steam,-12,-147,1,-110,{},false,true));
  restaurant.views[1].push_back(rect("steam",M::Steam,-47,-162,-32,-119,{},false,true));
  restaurant.views[2].push_back(rect("steam",M::Steam,-2,-179,13,-132,{},false,true));
  restaurant.views[3].push_back(rect("steam",M::Steam,24,-158,40,-116,{},false,true));
  restaurant.views[1].push_back(polygon("coffee sign",M::Sign,{{33,-99},{48,-94},{60,-85},{63,-70},{57,-53},{46,-40},{32,-38},{22,-44},{18,-58},{23,-78}},{193,167,255,255}));
  restaurant.views[2].push_back(polygon("coffee sign",M::Sign,{{-50,-96},{-35,-94},{-20,-78},{-8,-57},{-12,-39},{-25,-33},{-40,-41},{-52,-59},{-55,-80}},{193,167,255,255}));
  restaurant.views[0].push_back(line("red neon awning",{{-126,-51},{-127,-44},{-118,-37},{-100,-28},{-80,-18},{-61,-12},{-49,-12},{-43,-13}},1.4f,{255,65,91,255}));
  restaurant.views[1].push_back(line("red neon awning",{{126,-53},{126,-45},{119,-38},{105,-30},{84,-20},{64,-10},{53,-9}},1.4f,{255,65,91,255}));
  restaurant.views[2].push_back(line("red neon awning",{{-55,-14},{-55,-5},{-46,1},{-22,13},{1,24},{21,25},{37,20},{64,7},{91,-7},{116,-21},{126,-28},{127,-37}},1.4f,{255,65,91,255}));
  restaurant.views[3].push_back(line("red neon awning",{{-129,-32},{-129,-23},{-118,-14},{-92,-1},{-67,12},{-40,24},{-15,26},{9,15},{36,2},{52,-8},{53,-16}},1.4f,{255,65,91,255}));
  panes(restaurant.views[0],{-90,-85},{-34,-55},{-34,-33},{-90,-62},4);
  panes(restaurant.views[1],{-95,-10},{-31,20},{-31,43},{-95,12},4);
  panes(restaurant.views[2],{32,-41},{78,-65},{78,-43},{32,-19},4);
  panes(restaurant.views[3],{4,-55},{61,-84},{61,-61},{4,-32},4);
  restaurant.sources.push_back({"coffee sign",{-.2f,-1.17f,5.3f},{0,-1,0},{173,146,255,255},.65f,5});
  restaurant.sources.push_back({"front neon",{1.6f,-1.2f,1.0f},{0,0,0},{255,60,90,255},.25f,4});
  restaurant.sources.push_back({"side neon",{-1.7f,-.6f,1.0f},{0,0,0},{255,60,90,255},.25f,4});
  restaurant.sources.push_back({"interior windows",{-.1f,-1.75f,1.0f},{0,-1,0},{255,209,143,255},.4f,4});
  result.push_back(restaurant);
  // The electrified bars fence's two red warning beacons on its top rail
  // (fences/dzbar: lit in frames 2-6 of its 12, about 78-90 px above the
  // piece's ground point in every view). Only lit red lens pixels glow, so
  // the dark half of the blink gives none; a small red light under them,
  // only while they're lit (DayNightLighting: fence pieces)
  Definition beacons{1100,"dzbar"}; // (past 1000: a fence, not a catalogue object)
  for(auto &v:beacons.views)v.push_back({"warning beacons",M::Glass,{{-40,-112},{40,-112},{40,-66},{-40,-66}},0,{255,70,55,255},true,false,PixelMask::RedLens});
  beacons.sources.push_back({"warning beacons",{0,0,5.2f},{0,0,0},{255,60,45,255},.55f,3.0f});
  result.push_back(beacons);
  for(auto &a:result)a.animated=a.id==103 || a.id==105 || a.id==106 || a.id==1100;
  appendCatalogueDefinitions(result);
  appendWaterDefinitions(result);
  appendEffectDefinitions(result);
  appendAviationDefinitions(result);
  appendProjectionDefinitions(result);
  appendSignDefinitions(result);
  for(auto &a:result)if(a.animated)for(auto &v:a.views)for(auto &r:v)if(r.pixels==PixelMask::Bulb || r.pixels==PixelMask::Spark)r.animated=true;
  return result;
}
}
const std::vector<Definition> &all(){static const auto assets=definitions();return assets;}
namespace {
// ours: mod profiles, numbered from 9000 (past every catalogue id and its
// +3000 fixture set), kept for the session: a deque keeps their addresses
constexpr int kModProfiles=9000;
std::deque<Definition> &modDefinitions(){static std::deque<Definition> d;return d;}
std::deque<std::string> &modNames(){static std::deque<std::string> n;return n;}
}
int modProfile(const std::string &file,std::vector<Source> sources) {
  auto &defs=modDefinitions();auto &names=modNames();
  for(size_t i=0;i<defs.size();++i)if(names[i]==file){defs[i].sources=std::move(sources);return defs[i].id;}
  names.push_back(file);Definition d{kModProfiles+int(defs.size()),names.back().c_str()};
  // (animated: its glow follows the pose shown, as a flame's does)
  d.sources=std::move(sources);d.animated=true;d.lightMap=true;defs.push_back(std::move(d));
  return defs.back().id;
}
const Definition *find(const std::string &item) {
  for(const auto &a:all())if(item==a.item)return &a;
  return nullptr;
}
const Definition *profile(int id) {
  if(id>=kModProfiles){for(const auto &a:modDefinitions())if(a.id==id)return &a;return nullptr;}
  for(const auto &a:all())if(a.id==id)return &a;
  return nullptr;
}
int viewIndex(int side) {
  const auto d=static_cast<CompassDirection>(side);
  return d==CompassDirection::NE?0:d==CompassDirection::SE?1:d==CompassDirection::SW?2:3;
}
bool inside(const Region &r,float x,float y) {
  if(r.outline.size()<2)return false;
  if(r.stroke>0) {
    for(size_t i=1;i<r.outline.size();++i) {
      auto a=r.outline[i-1],b=r.outline[i];float dx=b.x-a.x,dy=b.y-a.y;
      float t=std::clamp(((x-a.x)*dx+(y-a.y)*dy)/std::max(.0001f,dx*dx+dy*dy),0.0f,1.0f);
      if(std::hypot(x-a.x-t*dx,y-a.y-t*dy)<=r.stroke*.5f)return true;
    }
    return false;
  }
  bool hit=false;
  for(size_t i=0,j=r.outline.size()-1;i<r.outline.size();j=i++) {
    auto a=r.outline[i],b=r.outline[j];
    if((a.y>y)!=(b.y>y) && x<(b.x-a.x)*(y-a.y)/(b.y-a.y)+a.x)hit=!hit;
  }
  return hit;
}
}
