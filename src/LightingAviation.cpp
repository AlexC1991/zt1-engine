#include "LightingAssets.hpp"
#include <algorithm>
#include <cstring>

namespace LightingAssets {
namespace {
Region lens(const char *name,float l,float t,float r,float b,PixelMask mask) {
  return {name,Material::Glass,{{l,t},{r,t},{r,b},{l,b}},0,{255,255,255,255},true,false,mask};
}
Region bulbs(const char *name,std::initializer_list<SDL_FPoint> points,float width=3) {
  return {name,Material::Glass,points,width,{255,236,175,255},true,false,PixelMask::WhiteLamp};
}
Definition *named(std::vector<Definition> &a,const char *name) {
  for(auto &d:a)if(!std::strcmp(d.item,name))return &d;return nullptr;
}
}
// addition: functional fixture stencils, reviewed against all native used/idle
// frames in the four camera views. The helipad roof beacon and four blue guide
// lenses are distinct from windows, logos, metal rails and aircraft tail paint.
void appendAviationDefinitions(std::vector<Definition> &definitions) {
  if(auto *base=named(definitions,"helibase")) {
    const float blue[4][4][4]={
      {{58,-90,62,-86},{-9,-88,-5,-84},{59,-54,62,-50},{-15,-54,-11,-50}},
      {{11,-53,14,-50},{-61,-90,-57,-86},{7,-87,10,-83},{-60,-53,-57,-49}},
      {{11,-112,15,-109},{-61,-77,-58,-73},{5,-79,8,-75},{-62,-113,-59,-109}},
      {{57,-114,60,-110},{57,-76,60,-73},{-10,-80,-7,-76},{-15,-113,-11,-110}}};
    const float red[4][4]={{-56,-86,-50,-81},{48,-88,55,-81},{50,-36,58,-29},{-54,-35,-48,-28}};
    for(int v=0;v<4;++v) {
      auto &regions=base->views[v];
      regions.erase(std::remove_if(regions.begin(),regions.end(),[](const Region &r){return r.material==Material::Glass;}),regions.end());
      for(const auto &b:blue[v])regions.push_back(lens("native blue pad guide lens",b[0],b[1],b[2],b[3],PixelMask::BlueLens));
      const auto *b=red[v];regions.push_back(lens("native red roof beacon",b[0],b[1],b[2],b[3],PixelMask::RedLens));
    }
    base->animated=true;
  }
  if(auto *cinema=named(definitions,"cinema")) {
    for(int v:{1,2}) {
      auto &regions=cinema->views[v];
      // Keep the star and interior steady. Replace the old full marquee patch
      // with its steady lettering and separately sampled perimeter bulbs.
      if(regions.size()>1)regions.erase(regions.begin()+1);
    }
    cinema->views[1].push_back({"steady cinema lettering",Material::Sign,{{61,-22},{119,-45},{119,-33},{61,-9}},0,{255,236,205,255}});
    cinema->views[2].push_back({"steady cinema lettering",Material::Sign,{{-123,-45},{-64,-21},{-64,-9},{-123,-33}},0,{255,236,205,255}});
    cinema->views[1].push_back(bulbs("native marquee bulbs",{{59,-22},{122,-48},{122,-20},{59,7},{59,-22}},3.5f));
    cinema->views[2].push_back(bulbs("native marquee bulbs",{{-126,-48},{-62,-22},{-62,6},{-126,-21},{-126,-48}},3.5f));
    cinema->animated=true;cinema->nightFixtures=true;
  }
  if(auto *arcade=named(definitions,"dkarcade")) {
    arcade->views[0].push_back(bulbs("native roof rail bulbs",{{-75,-64},{-2,-28},{87,-66}},4.0f));
    arcade->views[0].push_back(bulbs("native roof rail bulbs",{{-75,-64},{-61,-84},{-42,-106},{-35,-109},{-26,-105}},4.0f));
    arcade->views[0].push_back(bulbs("native roof rail bulbs",{{22,-133},{37,-133},{44,-128},{54,-107},{63,-96},{73,-90},{77,-95},{74,-102},{68,-106},{66,-99},{71,-94},{84,-86},{88,-76},{87,-66},{69,-67},{49,-70},{34,-68},{-2,-28}},4.0f));
    arcade->views[1].push_back(bulbs("native roof rail bulbs",{{-94,-60},{-22,-25},{88,-65}},4.0f));
    arcade->views[1].push_back(bulbs("native roof rail bulbs",{{-94,-60},{-64,-75},{-54,-83},{-43,-84},{-35,-80}},4.0f));
    arcade->views[1].push_back(bulbs("native roof rail bulbs",{{-94,-60},{-72,-66},{-59,-67},{-44,-59},{-32,-47},{-24,-43},{-11,-44}},4.0f));
    arcade->views[1].push_back(bulbs("native roof rail bulbs",{{-22,-25},{-22,-48},{-12,-56},{-1,-62},{10,-86},{15,-89},{22,-81},{30,-78},{41,-85},{49,-91},{57,-91},{63,-86},{68,-76},{69,-63},{88,-65}},4.0f));
    arcade->views[1].push_back(bulbs("native roof rail bulbs",{{-22,-25},{-25,-18},{-21,-12},{-16,-18},{-20,-24},{-22,-25}},4.0f));
    arcade->views[1].push_back(bulbs("native roof rail bulbs",{{88,-65},{76,-56},{62,-49},{51,-43},{36,-34},{18,-29},{-2,-22},{-13,-25},{-22,-25}},4.0f));
    arcade->views[2].push_back(bulbs("native roof rail bulbs",{{-91,-65},{3,-25},{72,-59}},4.0f));
    arcade->views[2].push_back(bulbs("native roof rail bulbs",{{-91,-65},{-91,-80},{-86,-89},{-79,-92},{-72,-91},{-65,-84},{-65,-80},{-71,-77},{-77,-82},{-78,-86},{-72,-88}},4.0f));
    arcade->views[2].push_back(bulbs("native roof rail bulbs",{{-91,-65},{-72,-73},{-59,-80},{-51,-78},{-43,-84},{-37,-89},{-32,-86},{-26,-72},{-16,-57},{-6,-48},{3,-25}},4.0f));
    arcade->views[2].push_back(bulbs("native roof rail bulbs",{{-44,-99},{-35,-104}},4.0f));
    arcade->views[2].push_back(bulbs("native roof rail bulbs",{{72,-59},{54,-71},{45,-82},{38,-85},{32,-84}},4.0f));
    arcade->views[2].push_back(bulbs("native roof rail bulbs",{{3,-25},{1,-36},{9,-43},{19,-45},{31,-42},{43,-47},{46,-56},{52,-60},{60,-58},{72,-59}},4.0f));
    arcade->views[3].push_back(bulbs("native roof rail bulbs",{{-94,-64},{0,-29},{69,-66}},4.0f));
    arcade->views[3].push_back(bulbs("native roof rail bulbs",{{-94,-64},{-93,-83},{-83,-101},{-77,-106},{-72,-104},{-72,-98},{-79,-92},{-82,-94},{-77,-86},{-68,-83},{-58,-89},{-48,-114},{-45,-127},{-40,-133},{-33,-134},{-25,-130}},4.0f));
    arcade->views[3].push_back(bulbs("native roof rail bulbs",{{-94,-64},{-70,-69},{-51,-67},{-43,-60},{-30,-49},{0,-29}},4.0f));
    arcade->views[3].push_back(bulbs("native roof rail bulbs",{{69,-66},{51,-84},{34,-105},{27,-109},{19,-105}},4.0f));
    arcade->animated=true;arcade->nightFixtures=true;
  }
  // addition: keep independently powered bulb strings separate from the
  // guest-driven screen/mascot animation and the steady building interiors.
  // This costs one small atlas per visible fixture sequence, not a second zoo.
  std::vector<Definition> fixtures;
  for(auto &d:definitions)if(d.nightFixtures) {
    Definition lamps{d.id+3000,d.id==214?"__cinema_bulbs":"__arcade_bulbs"};lamps.animated=true;
    for(int v=0;v<4;++v) {
      auto &regions=d.views[v];for(const auto &r:regions)if(r.pixels==PixelMask::WhiteLamp)lamps.views[v].push_back(r);
      regions.erase(std::remove_if(regions.begin(),regions.end(),[](const Region &r){return r.pixels==PixelMask::WhiteLamp;}),regions.end());
      if(d.id==224)regions.erase(std::remove_if(regions.begin(),regions.end(),[](const Region &r){return r.material==Material::Sign;}),regions.end());
    }
    if(d.id==224) {
      d.views[0].push_back({"native active arcade screen",Material::Glass,{{3,-120},{27,-109},{27,-96},{3,-106}},0,{165,205,255,255},true,false,PixelMask::BlueLens});
      d.views[3].push_back({"native active arcade screen",Material::Glass,{{-29,-111},{-6,-121},{-6,-106},{-29,-96}},0,{165,205,255,255},true,false,PixelMask::BlueLens});
      d.animated=true;
    }else d.animated=false;
    fixtures.push_back(std::move(lamps));
  }
  for(auto &d:fixtures)definitions.push_back(std::move(d));
  // addition: small night marker attachments, not a claim of native navigation
  // bulbs. Authored on the solid airframe, clear of its red painted tail band,
  // yellow rotor tips and cockpit glass. Idle/parked has no powered markers.
  const float markers[4][3][2]={{{-39,-5},{19,-14},{-5,-31}},{{-32,-42},{21,-1},{4,-38}},{{32,-42},{-24,-4},{-3,-39}},{{39,-5},{-23,-15},{-5,-31}}};
  const char *names[]={"__helicopter_warmup_markers","__helicopter_warmup_beacon","__helicopter_fly_markers","__helicopter_fly_beacon","__helicopter_level_markers","__helicopter_level_beacon","__helicopter_fire_markers","__helicopter_fire_beacon"};
  for(int state=0;state<4;++state)for(int layer=0;layer<2;++layer) {
    Definition d{2000+state*2+layer,names[state*2+layer]};d.animated=true;
    for(int v=0;v<4;++v)for(int n=layer?2:0;n<(layer?3:2);++n) {
      const auto *p=markers[v][n];const float r=n==2?1.1f:n==0?.75f:1.0f;
      const SDL_Color colour=n==2?SDL_Color{255,50,35,255}:n==0?SDL_Color{240,240,219,255}:(v==0 || v==2)?SDL_Color{255,69,50,255}:SDL_Color{68,255,116,255};
      d.views[v].push_back({n==2?"added powered aircraft beacon":"added aircraft position marker",Material::Neon,{{p[0]-r,p[1]-r},{p[0]+r,p[1]-r},{p[0]+r,p[1]+r},{p[0]-r,p[1]+r}},0,colour,true});
    }
    definitions.push_back(std::move(d));
  }
}
int aircraftProfile(const std::string &state,int layer) {
  if(layer<0 || layer>1)return 0;
  if(state=="warmup")return 2000+layer;if(state=="fly")return 2002+layer;
  if(state=="level")return 2004+layer;if(state=="fire")return 2006+layer;return 0;
}
}
