// addition: the buildings' painted signs lit at night (the optional day/night
// lighting): restroom signs, stand and shop signs. Each outline was traced
// from the original art, from one point on the sign, over its own colours to
// its dark outline (LightingSigns.inc, made by the night-audit signs tool).
// A sign shows its own daytime colours in the dark; nothing is added to it.
#include "LightingAssets.hpp"
#include <cstring>
namespace LightingAssets {
void appendSignDefinitions(std::vector<Definition> &result) {
  int next=320; // (buildings with no lights of their own yet)
  auto sign=[&](const char *item,int view,std::vector<SDL_FPoint> outline) {
    Definition *d=nullptr;
    for(auto &a:result)if(!std::strcmp(a.item,item)){d=&a;break;}
    if(!d){result.push_back(Definition{next++,item});d=&result.back();}
    d->views[view].push_back({"painted sign",Material::Sign,std::move(outline),0,{255,236,200,255}});
  };
#include "LightingSigns.inc"
}
}
