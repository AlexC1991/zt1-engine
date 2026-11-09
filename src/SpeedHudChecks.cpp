// ours: exercise the production transport control through positioned inputs,
// including modal input, reversible layout, pause and the shared speed clock.
#include "Features.hpp"
#include "RenderSettings.hpp"
#include "World.hpp"
#include "ui/UiGameScreen.hpp"
#include "ui/UiText.hpp"
#include <SDL3_image/SDL_image.h>
#include <fstream>
#include <functional>
#include <cstdlib>
#include <string>

int runSpeedHudChecks(World &world,UiGameScreen &hud,ResourceManager *rm,SDL_Renderer *renderer,
    const std::string &out,bool &widescreen,const std::function<void(const char*)> &capture,
    const std::function<void(bool)> &pause,const std::function<float()> &syncSpeed) {
  std::ofstream log(out+"/speed-hud-checks.txt");
  int failed=0;
  auto check=[&](bool ok,const std::string &what){log<<(ok?"PASS ":"FAIL ")<<what<<'\n';if(!ok)++failed;};
  auto box=[&](int id){auto *e=hud.getElementById(id);return e?e->getLastRect():SDL_Rect{};};
  auto same=[](SDL_Rect a,SDL_Rect b){return a.x==b.x&&a.y==b.y&&a.w==b.w&&a.h==b.h;};
  auto send=[&](SDL_Point at,InputEvent event){Input in{};in.type=InputType::POSITIONED;in.event=event;in.position=at;in.x=at.x;in.y=at.y;std::vector<Input> v{in};return hud.handleInputs(v);};
  auto click=[&](SDL_Rect r){SDL_Point p{r.x+r.w/2,r.y+r.h/2};auto a=send(p,InputEvent::LEFT_CLICK);send(p,InputEvent::LEFT_RELEASE);return a;};
  auto away=[&]{send({0,0},InputEvent::CURSOR_MOVE);};
  auto hash=[&](const char *file,SDL_Rect r){
    SDL_Surface *loaded=IMG_Load((out+"/"+file+".png").c_str());
    SDL_Surface *s=loaded?ZT_ConvertSurfaceFormat(loaded,SDL_PIXELFORMAT_RGBA32,0):nullptr;
    if(loaded)SDL_DestroySurface(loaded);uint64_t h=1469598103934665603ull;
    if(!s)return uint64_t(0);
    for(int y=r.y;y<r.y+r.h;++y)for(int x=r.x;x<r.x+r.w;++x){
      if(x<0||y<0||x>=s->w||y>=s->h)continue;
      auto p=reinterpret_cast<Uint32*>(static_cast<Uint8*>(s->pixels)+y*s->pitch)[x];h^=p;h*=1099511628211ull;
    }
    SDL_DestroySurface(s);return h;
  };
  const bool oldFeature=Features::fastForward,oldTime=Features::showGameTime,
      oldLight=Features::dayNightLighting,oldWide=widescreen,oldPause=world.isPaused();
  const float oldScale=RenderSettings::uiScaleSetting;
  SDL_Window *window=SDL_GetRenderWindow(renderer);int oldW=0,oldH=0;SDL_GetWindowSize(window,&oldW,&oldH);
  SDL_SetWindowSize(window,800,600);widescreen=false;RenderSettings::uiScaleSetting=1;
  Features::fastForward=false;Features::showGameTime=false;Features::dayNightLighting=false;
  pause(false);away();syncSpeed();capture("speed-original-settle");away();capture("speed-original");
  const SDL_Rect originalDate=box(1030),originalMoney=box(1016),originalPause=box(1071);
  check(hud.speedButtonRect().w==0,"off has no speed hit target");
  Features::showGameTime=true;capture("speed-clock-original");Features::showGameTime=false;
  Features::fastForward=true;syncSpeed();capture("speed-1x");
  SDL_Rect speed=hud.speedButtonRect();
  check(speed.w>0&&speed.h>0&&speed.x>originalPause.x+originalPause.w/2&&speed.x<=originalPause.x+originalPause.w,
        "speed target attaches to Pause on its right");
  check(hud.isOverHud(speed.x+speed.w/2,speed.y+speed.h/2),"speed target blocks map placement");
  const SDL_Point speedCentre{speed.x+speed.w/2,speed.y+speed.h/2};
  away();send(speedCentre,InputEvent::CURSOR_MOVE);capture("speed-hover-from-world");
  const SDL_Rect pauseRect=box(1071);
  send({pauseRect.x+pauseRect.w/2,pauseRect.y+pauseRect.h/2},InputEvent::CURSOR_MOVE);capture("speed-pause-hover");
  send(speedCentre,InputEvent::CURSOR_MOVE);capture("speed-hover-from-pause");
  check(hash("speed-hover-from-world",originalPause)==hash("speed-hover-from-pause",originalPause),
        "moving from Pause onto Speed clears the native Pause hover");
  away();
  for(int rate:{2,5,1}) {
    const auto action=click(hud.speedButtonRect());
    check(action==UiAction::NONE&&syncSpeed()==rate,"positioned click reaches production "+std::to_string(rate)+"x without another HUD action");
    check(!world.isPaused(),"speed click does not pause a running zoo");
    away();const std::string name="speed-"+std::to_string(rate)+"x";capture(name.c_str());
  }
  const auto one=hash("speed-1x",speed),two=hash("speed-2x",speed),five=hash("speed-5x",speed);
  check(one&&two&&five&&one!=two&&two!=five&&one!=five,"three speed states have distinct visible icons");
  pause(true);click(hud.speedButtonRect());syncSpeed();away();capture("speed-paused-2x");
  check(world.isPaused()&&hud.gameSpeed()==2,"changing speed while paused keeps pause");
  check(click(box(1072))==UiAction::HUD_PLAY,"native Play still receives its own click");
  pause(false);check(syncSpeed()==2,"resuming keeps the selected speed");
  check(click(box(1071))==UiAction::HUD_PAUSE,"native Pause still receives its own click");
  pause(true);check(syncSpeed()==2,"pausing keeps the selected speed");
  hud.options->open(true,7);capture("speed-gameplay-options");
  click(hud.speedButtonRect());check(syncSpeed()==2,"modal options prevent clicking the speed control underneath");
  hud.options->close();capture("speed-options-closed");away();
  const SDL_Rect withSpeedDate=box(1030),withSpeedMoney=box(1016);
  check(withSpeedDate.y==originalDate.y&&withSpeedDate.h==originalDate.h&&withSpeedMoney.y==originalMoney.y,
        "calendar and cash retain their native baseline and text height");
  Features::showGameTime=true;away();capture("speed-clock");
  pause(false);away();capture("speed-clock-running");
  // ours: optional external golden artwork selected by the owner, independent
  // of the embedded UI atlas. Compare opaque pixels at the reference layout.
  if(const char *reference=std::getenv("ZT_SPEED_APPROVED_REFERENCE")) {
    SDL_Surface *ref=IMG_Load(reference),*actual=IMG_Load((out+"/speed-clock-running.png").c_str());
    SDL_Texture *golden=ref?SDL_CreateTextureFromSurface(renderer,ref):nullptr;
    SDL_Texture *target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,800,600);
    SDL_Surface *expected=ZT_CreateRGBSurfaceWithFormat(0,800,600,32,SDL_PIXELFORMAT_RGBA32);
    bool valid=golden&&target&&expected&&actual;
    int compared=0,maxDelta=0;
    if(valid) {
      SDL_Texture *previous=SDL_GetRenderTarget(renderer);
      SDL_Rect oldView{},oldClip{};SDL_GetRenderViewport(renderer,&oldView);SDL_GetRenderClipRect(renderer,&oldClip);
      const bool clipped=SDL_RenderClipEnabled(renderer)==true;
      float oldX=1,oldY=1;SDL_GetRenderScale(renderer,&oldX,&oldY);
      SDL_SetRenderTarget(renderer,target);SDL_SetRenderViewport(renderer,nullptr);SDL_SetRenderClipRect(renderer,nullptr);SDL_SetRenderScale(renderer,1,1);
      SDL_SetRenderDrawColor(renderer,0,0,0,0);SDL_RenderClear(renderer);
      SDL_SetTextureBlendMode(golden,SDL_BLENDMODE_NONE);SDL_SetTextureScaleMode(golden,SDL_SCALEMODE_LINEAR);
      // Measured: approved crop (304,1020,108,66) in its 1448x1086 screen.
      const SDL_FRect destination{167.9558f,563.5359f,59.6685f,36.4641f};
      ZT_RenderCopyF(renderer,golden,nullptr,&destination);
      valid=ZT_RenderReadPixels(renderer,nullptr,expected->format,expected->pixels,expected->pitch)==0;
      if(valid)IMG_SavePNG(expected,(out+"/speed-approved-reference-render.png").c_str());
      SDL_SetRenderTarget(renderer,previous);SDL_SetRenderScale(renderer,oldX,oldY);SDL_SetRenderViewport(renderer,&oldView);SDL_SetRenderClipRect(renderer,clipped?&oldClip:nullptr);
      if(valid)for(int y=566;y<600;++y)for(int x=170;x<226;++x) {
        Uint8 r,g,b,a,rr,gg,bb,aa;
        const auto e=reinterpret_cast<Uint32*>(static_cast<Uint8*>(expected->pixels)+y*expected->pitch)[x];
        const auto v=reinterpret_cast<Uint32*>(static_cast<Uint8*>(actual->pixels)+y*actual->pitch)[x];
        ZT_GetRGBA(e,expected->format,&r,&g,&b,&a);ZT_GetRGBA(v,actual->format,&rr,&gg,&bb,&aa);
        if(a<254)continue;
        maxDelta=std::max({maxDelta,std::abs(int(r)-rr),std::abs(int(g)-gg),std::abs(int(b)-bb)});++compared;
      }
    }
    log<<"REFERENCE pixels "<<compared<<" maximum channel difference "<<maxDelta<<'\n';
    check(valid&&compared>1000,"approved external component reference loads and covers the complete face");
    check(valid&&compared>1000&&maxDelta<=3,"rendered 2x face rim and arrows match approved source colours");
    if(golden)SDL_DestroyTexture(golden);if(target)SDL_DestroyTexture(target);
    if(ref)SDL_DestroySurface(ref);if(actual)SDL_DestroySurface(actual);if(expected)SDL_DestroySurface(expected);
  }
  send({speed.x+speed.w/2,speed.y+speed.h/2},InputEvent::LEFT_CLICK);capture("speed-pressed");
  send({speed.x+speed.w/2,speed.y+speed.h/2},InputEvent::LEFT_RELEASE);syncSpeed();away();capture("speed-clock-5x");
  auto *money=dynamic_cast<UiText*>(hud.getElementById(1016));
  if(money){
    const auto saved=money->getText();money->setText("$100,000,000");capture("speed-large-cash");
    int w=0,h=0;rm->getTextSize(rm->getStringTexture(renderer,1017,money->getText(),{83,219,83,255},1018),&w,&h);
    check(w<=box(1016).w,"large cash value fits the remaining money well");
    money->setText(saved);
  }
  const SDL_Rect clockDate=box(1030),clockMoney=box(1016);
  check(clockDate.x+clockDate.w<=clockMoney.x,"clock and cash do not overlap");
  check(hud.speedButtonRect().x==speed.x&&hud.speedButtonRect().y==speed.y,"clock switch does not move the transport controls");
  Features::fastForward=false;syncSpeed();Features::showGameTime=false;pause(false);away();capture("speed-restored");
  check(syncSpeed()==1&&hud.gameSpeed()==1,"disabling control restores production normal speed");
  check(same(box(1030),originalDate)&&same(box(1016),originalMoney)&&same(box(1071),originalPause),
        "disabling restores native date cash and Pause rectangles");
  const SDL_Rect strip{160,566,640,34};
  check(hash("speed-original",strip)==hash("speed-restored",strip),"off restores the full native transport bar pixel for pixel");
  for(int repeat=0;repeat<3;++repeat){Features::fastForward=true;syncSpeed();capture("speed-toggle-on");Features::fastForward=false;syncSpeed();capture("speed-toggle-off");}
  check(same(box(1030),originalDate)&&same(box(1016),originalMoney),"repeated switches do not accumulate layout offsets");
  SDL_SetWindowSize(window,1280,720);widescreen=true;Features::fastForward=true;Features::showGameTime=true;syncSpeed();away();capture("speed-widescreen");
  auto date=box(1030),cash=box(1016),button=hud.speedButtonRect();
  check(button.w>0&&date.x+date.w<=cash.x&&cash.x+cash.w<=1280,"widescreen clock cash and speed fit without overlap");
  SDL_SetWindowSize(window,1600,1200);widescreen=false;RenderSettings::uiScaleSetting=2;capture("speed-scale-2x");
  check(hud.speedButtonRect().w>0,"scaled HUD retains the speed target");
  failed+=UiOptions::updateChecks(renderer,rm,out)!=0;
  Features::fastForward=oldFeature;Features::showGameTime=oldTime;Features::dayNightLighting=oldLight;
  widescreen=oldWide;RenderSettings::uiScaleSetting=oldScale;SDL_SetWindowSize(window,oldW,oldH);pause(oldPause);syncSpeed();
  log<<"FAILURES "<<failed<<'\n';return failed?1:0;
}
