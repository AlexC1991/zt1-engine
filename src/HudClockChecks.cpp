// ours: isolated native HUD checks, using the gameplay renderer and inputs.
// Only private test settings are written; no saves, shared Lab or HD worker.
#include "World.hpp"
#include "Features.hpp"
#include "RenderSettings.hpp"
#include "ui/UiGameScreen.hpp"
#include "ui/UiText.hpp"
#include <SDL_image.h>
#include <fstream>
#include <functional>
#include <tuple>

int runHudClockChecks(World &world,UiGameScreen &hud,ResourceManager *rm,SDL_Renderer *renderer,
                      const std::string &out,bool &widescreen,
                      const std::function<void(const char*)> &capture,const std::function<void(bool)> &pause) {
  std::ofstream log(out + "/clock-checks.txt");
  int failed = 0;
  auto check = [&](bool ok,const std::string &name) { log << (ok?"PASS ":"FAIL ") << name << '\n'; if(!ok)++failed; };
  auto label = [&] { auto *t=dynamic_cast<UiText*>(hud.getElementById(1030));return t?t->getText():std::string(); };
  auto box = [&](int id) { auto *e=hud.getElementById(id);return e?e->getLastRect():SDL_Rect{}; };
  auto click = [&](int id) {
    SDL_Rect r=box(id);Input in{};in.type=InputType::POSITIONED;in.event=InputEvent::LEFT_CLICK;
    in.x=r.x+12;in.y=r.y+r.h/2;in.position={in.x,in.y};std::vector<Input> inputs{in};
    return hud.handleInputs(inputs);
  };
  const bool wasLighting=Features::dayNightLighting,wasTime=Features::showGameTime,wasWide=widescreen,wasPaused=world.isPaused();
  const float wasScale=RenderSettings::uiScaleSetting;
  auto &lighting=world.getLighting();const double wasPreview=lighting.previewHour;
  SDL_Window *window=SDL_RenderGetWindow(renderer);int wasWidth,wasHeight;SDL_GetWindowSize(window,&wasWidth,&wasHeight);
  SDL_SetWindowSize(window,800,600);widescreen=false;RenderSettings::uiScaleSetting=1;
  Features::dayNightLighting=false;Features::showGameTime=false;pause(true);capture("clock-original-off");
  const std::string calendar=label();const SDL_Rect originalDate=box(1030),originalPlay=box(1072),originalMoney=box(1016);
  for(int id:{1030,1035,1037,1071,1072}) {
    auto r=box(id);log<<"RECT "<<id<<' '<<r.x<<' '<<r.y<<' '<<r.w<<' '<<r.h<<'\n';
  }
  if(auto *art=rm->getAnimation("ui/main/backgnd3/backgnd3"))
    art->forEachFrame([&](const std::string &,int frame,SDL_Surface *image) {
      if(frame==0)IMG_SavePNG(image,(out+"/native-date-art.png").c_str());
    });
  check(!calendar.empty() && calendar.find(':')==std::string::npos,"original calendar without clock when off");
  check(DayNightLighting::formatTime(0)=="00:00" && DayNightLighting::formatTime(23.999)=="23:59" &&
        DayNightLighting::formatTime(24)=="00:00","24-hour formatting and midnight rollover");
  Features::dayNightLighting=true;Features::showGameTime=true;
  for (double hour : {12.0,18.5,23.999,0.0}) {
    lighting.previewHour=hour;
    const std::string name="clock-"+DayNightLighting::formatTime(hour).substr(0,2)+"-"+DayNightLighting::formatTime(hour).substr(3);
    capture(name.c_str());
    check(label()==calendar+"   "+DayNightLighting::formatTime(lighting.displayedHour()),"HUD date/time matches the actual sky at "+DayNightLighting::formatTime(hour));
    SDL_Rect date=box(1030),money=box(1016),play=box(1072);
    check(date.x>=0 && date.y>=0 && date.y+date.h<=600 && date.x+date.w<=money.x,"clock stays on-screen and clear of money");
    check(date.y==originalDate.y && date.h==originalDate.h && money.y==originalMoney.y,
          "single-line clock and money retain the original baseline and height");
    check(hud.isOverPanel(date.x+date.w/2,date.y+3),"extended clock blocks map placement underneath it");
    check(play.x==originalPlay.x && play.y==originalPlay.y && play.w==originalPlay.w && play.h==originalPlay.h,
          "pause/play hit area is not resized");
  }
  // measured: the native upper lip is straight between the 12 px endcaps.
  // Comparing its pixels catches a curved corner accidentally stretched into
  // the extension even when the text and outer rectangles still fit.
  {
    auto load = [&](const char *name) {
      SDL_Surface *source=IMG_Load((out+"/"+name).c_str());
      SDL_Surface *image=source?SDL_ConvertSurfaceFormat(source,SDL_PIXELFORMAT_RGBA32,0):nullptr;
      if(source)SDL_FreeSurface(source);
      return image;
    };
    SDL_Surface *original=load("clock-original-off.png"),*extended=load("clock-00-00.png");
    auto pixel = [](SDL_Surface *image,int x,int y) {
      return reinterpret_cast<Uint32*>(static_cast<unsigned char*>(image->pixels)+y*image->pitch)[x];
    };
    bool straight=original && extended,corner=straight;
    // UiText reports the inner text box after applying the native 2 px
    // border. Compare the artwork at the outer box, not the glyph bounds.
    auto *dateIni=rm->getIniReader("ui/main.lyt");
    const int inset=dateIni?dateIni->getInt(hud.getElementById(1030)->getName(),"border",0):0;
    const int left=originalDate.x-inset,top=originalDate.y-inset;
    const int nativeRight=originalDate.x+originalDate.w+inset;
    const int right=box(1030).x+box(1030).w+inset;
    log<<"EXTENDED "<<box(1030).x<<' '<<box(1030).y<<' '<<box(1030).w<<' '<<box(1030).h<<'\n';
    // Five rows cover the upper corner before the time glyphs begin.
    if(straight)for(int y=top;y<top+5;++y) {
      // The outer olive shoulder has a native horizontal colour gradient;
      // compare the three flat, dark rows before the text for continuity.
      if(y>=top+1 && y<top+4)for(int x=left+16;x<right-12;++x)
        straight &= pixel(extended,x,y)==pixel(original,left+32,y);
      for(int x=0;x<12;++x)
        corner &= pixel(extended,right-12+x,y)==pixel(original,nativeRight-12+x,y);
    }
    check(straight,"extended upper lip remains straight with no stretched corner");
    check(corner,"extended right upper corner retains the native border pixels");
    if(original)SDL_FreeSurface(original);if(extended)SDL_FreeSurface(extended);
  }
  auto *cash=dynamic_cast<UiText*>(hud.getElementById(1016));
  if(cash) {
    const std::string saved=cash->getText();cash->setText("$100,000,000");capture("clock-large-balance");
    int width=0,height=0;rm->getTextSize(rm->getStringTexture(renderer,1017,cash->getText(),{83,219,83,255},1018),&width,&height);
    auto money=box(1016),date=box(1030);
    check(width<=money.w && date.x+date.w<=money.x,"large balance fits beside the clock without wrapping");
    cash->setText(saved);
  }
  check(click(1072)==UiAction::HUD_PLAY,"native Play input remains available with clock");
  pause(false);capture("clock-running");
  check(click(1071)==UiAction::HUD_PAUSE,"native Pause input remains available with clock");
  pause(true);lighting.previewHour=-1;double clock=world.getClock();world.update(SDL_GetKeyboardState(nullptr),1);
  capture("clock-paused");check(world.getClock()==clock,"paused zoo holds its visual clock");
  pause(false);world.update(SDL_GetKeyboardState(nullptr),.5f);capture("clock-live");
  check(world.getClock()>clock && label()==calendar+"   "+DayNightLighting::formatTime(DayNightLighting::hourAt(world.getClock())),
        "running clock follows saved world time");
  pause(true);
  // (the day/night cycle and the speed control show the clock whatever its
  // own switch says, and turning one off leaves it while the other needs it:
  // Features::gameTimeShown)
  Features::showGameTime=false;lighting.previewHour=0;capture("clock-kept-night");
  check(label()==calendar+"   "+DayNightLighting::formatTime(0) && Features::dayNightLighting && lighting.displayedHour()==0,
        "the day/night cycle keeps the clock shown with its own switch off");
  const bool wasSpeed=Features::fastForward;
  Features::fastForward=true;Features::dayNightLighting=false;capture("clock-kept-speed");
  check(label()==calendar+"   "+DayNightLighting::formatTime(DayNightLighting::hourAt(world.getClock())),
        "the speed control keeps the clock when the day/night cycle goes off");
  Features::fastForward=false;capture("clock-hidden");
  check(label()==calendar,"the clock goes with the last switch that showed it");
  Features::fastForward=wasSpeed;Features::dayNightLighting=true;
  Features::dayNightLighting=false;Features::showGameTime=true;capture("clock-without-lighting");
  check(label()==calendar+"   "+DayNightLighting::formatTime(DayNightLighting::hourAt(world.getClock())),
        "clock works independently with the original daylight rendering");
  pause(false);world.update(SDL_GetKeyboardState(nullptr),30);capture("clock-without-lighting-running");
  check(label()==calendar+"   "+DayNightLighting::formatTime(DayNightLighting::hourAt(world.getClock())),
        "clock advances with lighting off instead of using a stale prepared hour");
  pause(true);Features::showGameTime=false;
  Input away{};away.type=InputType::POSITIONED;away.event=InputEvent::CURSOR_MOVE;away.position={0,0};
  std::vector<Input> moved{away};hud.handleInputs(moved);
  capture("clock-restored-off");
  SDL_Rect restoredDate=box(1030),money=box(1016);
  check(label()==calendar && restoredDate.x==originalDate.x && restoredDate.y==originalDate.y &&
        restoredDate.w==originalDate.w && restoredDate.h==originalDate.h,"switching off restores original calendar and geometry");
  check(money.x==originalMoney.x && money.w==originalMoney.w,"money remains in its original position");
  SDL_Rect originalStrip{};SDL_UnionRect(&originalPlay,&originalDate,&originalStrip);
  auto pixels = [&](const std::string &file) {
    uint64_t hash=1469598103934665603ull;SDL_Surface *source=IMG_Load(file.c_str());
    SDL_Surface *image=source?SDL_ConvertSurfaceFormat(source,SDL_PIXELFORMAT_RGBA32,0):nullptr;
    if(source)SDL_FreeSurface(source);if(!image)return uint64_t(0);
    for(int y=originalStrip.y;y<originalStrip.y+originalStrip.h && y<image->h;++y)
      for(int x=originalStrip.x;x<originalStrip.x+originalStrip.w && x<image->w;++x)
        for(int c=0;c<4;++c){hash^=static_cast<unsigned char*>(image->pixels)[y*image->pitch+x*4+c];hash*=1099511628211ull;}
    SDL_FreeSurface(image);return hash;
  };
  check(pixels(out+"/clock-original-off.png")!=0 && pixels(out+"/clock-original-off.png")==pixels(out+"/clock-restored-off.png"),
        "feature off restores exact original pause/date pixels");
  Features::dayNightLighting=true;Features::showGameTime=true;lighting.previewHour=18.5;
  for(const auto [width,height,scale,wide,name] : {
        std::tuple{800,600,.75f,true,"clock-small-ui"},std::tuple{1280,720,1.0f,true,"clock-widescreen"},
        std::tuple{1600,1200,2.0f,true,"clock-double-ui"},std::tuple{1280,720,1.0f,false,"clock-centred"}}) {
    SDL_SetWindowSize(window,width,height);RenderSettings::uiScaleSetting=scale;widescreen=wide;
    capture(name);check(label()==calendar+"   18:30",std::string("clock at ")+name);
  }
  // Native pixel crops for direct review against the original bar, without
  // rescaling or mockup artwork hiding the edge/baseline differences.
  for(const auto &[source,dest] : {std::pair{"clock-original-off.png","original-strip.png"},
                                 std::pair{"clock-18-30.png","clock-strip.png"}}) {
    SDL_Surface *image=IMG_Load((out+"/"+source).c_str());
    SDL_Surface *strip=SDL_CreateRGBSurfaceWithFormat(0,320,40,32,SDL_PIXELFORMAT_RGBA32);
    SDL_Rect region{165,560,320,40};
    bool saved=image && strip && SDL_BlitSurface(image,&region,strip,nullptr)==0 && IMG_SavePNG(strip,(out+"/"+dest).c_str())==0;
    if(image)SDL_FreeSurface(image);if(strip)SDL_FreeSurface(strip);
    check(saved,std::string("native status strip capture ")+dest);
  }
  SDL_SetWindowSize(window,wasWidth,wasHeight);RenderSettings::uiScaleSetting=wasScale;widescreen=wasWide;
  lighting.previewHour=wasPreview;Features::dayNightLighting=wasLighting;Features::showGameTime=wasTime;pause(wasPaused);
  failed += UiOptions::updateChecks(renderer,rm,out)!=0;
  log<<"Rendered using native font "<<rm->getString(1017)<<". Only the private test settings were exercised; no saves written.\n";
  return failed?1:0;
}
