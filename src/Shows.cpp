#include "Shows.hpp"
#include "World.hpp"
#include "IniReader.hpp"
#include "Research.hpp"
#include "LabTrace.hpp"
#include "Sound.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace {
std::string lowerShow(std::string s) {
  for (char &c : s) { if (c == '\\') c = '/'; else c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
  return s;
}
std::pair<float,float> sideOf(const Fences::Edge &e, bool positive) {
  return e.alongX ? std::make_pair(e.x+0.5f,e.y+(positive?0.5f:-0.5f))
                  : std::make_pair(e.x+(positive?0.5f:-0.5f),e.y+0.5f);
}
template<class T> bool showMove(T &m, float speed, float seconds) {
  float left = std::max(0.0f,speed*seconds);
  while (m.pathAt < m.path.size()) {
    auto [x,y] = m.path[m.pathAt];
    float dx=x-m.x, dy=y-m.y, distance=std::hypot(dx,dy);
    if(distance>0.00001f) { m.fx=dx/distance; m.fy=dy/distance; }
    if(distance>left) { m.x+=m.fx*left; m.y+=m.fy*left; return false; }
    m.x=x; m.y=y; left-=distance; ++m.pathAt;
  }
  m.path.clear(); m.pathAt=0; return true;
}
}

void Shows::init(World *w, ResourceManager *resources) {
  world=w; rm=resources;
  if (!rm) return;
  if (IniReader *ini=rm->getIniReader("shows.cfg")) {
    // zoo.exe 0x5167ca and 0x51f59b: the game's values, including mod overrides.
    settings.interval[0]=ini->getInt("timing","continuousShowInterval",65);
    settings.interval[1]=ini->getInt("timing","frequentShowInterval",90);
    settings.interval[2]=ini->getInt("timing","infrequentShowInterval",120);
    settings.returnToKeeper=std::max(1,ini->getInt("behavior","returnToKeeperThreshold",5));
    settings.maxLength=std::clamp(ini->getInt("show","maxLength",20),1,100);
    settings.idealMin=ini->getInt("show","minIdealLength",5);
    settings.idealMax=ini->getInt("show","maxIdealLength",7);
    settings.badTrick=ini->getInt("trickSatisfactionThresholds","badTrick",0);
    settings.goodTrick=ini->getInt("trickSatisfactionThresholds","goodTrick",4);
    settings.greatTrick=ini->getInt("trickSatisfactionThresholds","greatTrick",8);
    settings.badShow=ini->getInt("showSatisfactionThresholds","badShow",33);
    settings.goodShow=ini->getInt("showSatisfactionThresholds","goodShow",66);
    settings.greatShow=ini->getInt("showSatisfactionThresholds","greatShow",100);
    auto number=[&](const char *key,double fallback) { std::string v=ini->get("cost",key);return v.empty()?fallback:std::atof(v.c_str()); };
    settings.admission=number("defaultShowAdmission",5.5);
    settings.admissionMin=number("minShowAdmission",0);
    settings.admissionMax=number("maxShowAdmission",12);
    settings.admissionStep=number("showAdmissionIncrement",0.25);
    settings.startSound=ini->get("sounds","startSound");
    delete ini;
  }
}

void Shows::clear() { list.clear(); definitions.clear(); incomePending=0;messages.clear(); }
Shows::Show *Shows::show(int id) { auto i=list.find(id);return i==list.end()?nullptr:&i->second; }
const Shows::Show *Shows::show(int id) const { auto i=list.find(id);return i==list.end()?nullptr:&i->second; }

void Shows::refresh() {
  if(!world) return;
  auto &f=world->fences;
  for (const auto &ex : f.exhibits()) {
    if(!ex.tank) continue;
    int showEdges=0, ordinaryEdges=0;
    for(const auto &[edge,p]:f.pieces()) {
      auto a=sideOf(edge,false), b=sideOf(edge,true);
      bool inA=ex.tiles.count({static_cast<int>(std::floor(a.first)),static_cast<int>(std::floor(a.second))})!=0;
      bool inB=ex.tiles.count({static_cast<int>(std::floor(b.first)),static_cast<int>(std::floor(b.second))})!=0;
      if(inA==inB || p.type<0)continue;
      (f.types()[p.type].showFence?showEdges:ordinaryEdges)++;
    }
    auto *e=f.exhibit(ex.id);
    e->show=showEdges>ordinaryEdges;
    if(e->show && !list.count(ex.id)) {
      Show s;s.tank=ex.id;s.admission=settings.admission;list.emplace(ex.id,std::move(s));
      if(!e->named && e->name.rfind("Tank",0)==0)e->name="Show "+e->name;
    }
  }
  for(auto i=list.begin();i!=list.end();) {
    const auto *e=f.exhibit(i->first);
    if(!e || !e->tank || !e->show) { cancel(i->first,"The show tank was removed or drained.");i=list.erase(i); }
    else ++i;
  }
}

std::vector<std::pair<Fences::Edge,int>> Shows::links(int tank) const {
  std::vector<std::pair<Fences::Edge,int>> result;
  if(!world)return result;
  for(const auto &[edge,p]:world->fences.pieces()) {
    int s=-1,home=-1;
    if(world->fences.showPortal(edge,s,home) && s==tank)result.push_back({edge,home});
  }
  return result;
}

const std::vector<Shows::Trick> &Shows::tricks(const std::string &file) {
  auto found=definitions.find(file);if(found!=definitions.end())return found->second;
  std::vector<Trick> out;
  if(rm)if(IniReader *ai=rm->getIniReader(file)) {
    std::string sub=lowerShow(ai->get("global","defaultsubtype","m"));
    std::string base="m/tricks";
    if(ai->getList(base,"trick").empty())base=sub+"/tricks";
    for(const auto &key:ai->getList(base,"trick")) {
      std::string section=base+"/"+key;
      int id=ai->getInt(section,"id",0);if(!id)continue;
      Trick t;t.key=lowerShow(key);t.id=id;
      t.kind=ai->getInt(section,"type",0);t.behavior=lowerShow(ai->get(section,"name",key));
      t.name=rm->getString(id);if(t.name.empty())t.name=key;
      t.complexity=ai->getInt(section,"complexity",0);
      t.satisfaction=ai->getInt(section,"satisfaction",0);
      t.delta=ai->getInt(section,"satisfactiondelta",0);
      t.depth=ai->getInt(section,"minimumdepth",0);
      t.building=ai->getInt(section,"building",0);
      t.available=ai->getInt(section,"defaultavailable",1)!=0;
      t.visible=ai->getInt(section,"visible",1)!=0;
      t.icon=ai->get(section,"normalIcon");t.pre=lowerShow(ai->get(section,"keeperpretrick"));
      t.post=lowerShow(ai->get(section,"keeperposttrick"));out.push_back(std::move(t));
    }
    delete ai;
  }
  return definitions.emplace(file,std::move(out)).first->second;
}
const Shows::Trick *Shows::find(const std::string &file,const std::string &key) {
  for(const auto &t:tricks(file))if(t.key==lowerShow(key) || std::to_string(t.id)==key)return &t;
  return nullptr;
}

std::vector<std::string> Shows::species(int tank) const {
  std::vector<std::string> result;
  if(!world)return result;
  auto ports=links(tank);
  for(const auto &a:world->animals.members()) {
    if(a.baby || a.egg || a.dying || a.boxed)continue;
    const auto &t=world->animals.types()[a.type];
    if(!std::any_of(ports.begin(),ports.end(),[&](const auto &p){
      int home=a.showHome>=0?a.showHome:a.exhibit;
      return p.second==home || (!t.onlyUnderwater &&
        world->fences.habitatRoot(p.second,world->worldMap)==home);
    }))continue;
    if(t.showAnimal && std::find(result.begin(),result.end(),t.file)==result.end())result.push_back(t.file);
  }
  return result;
}

std::string Shows::unavailable(int tank,const std::string &file,const Trick &t) const {
  if(!world)return "No zoo.";
  const auto *e=world->fences.exhibit(tank);
  if(!e || !e->tank || !e->show)return "Requires a show tank.";
  if(world->fences.waterLevel(*e)<t.depth)return "Requires a deeper show tank ("+std::to_string(t.depth)+").";
  if(!t.available) {
    bool researched=false;
    for(const auto &b:Research::get().branches())for(const auto &c:b.categories)for(const auto &p:c.programs)
      if(p.effect==6 && p.done && p.effectVal[0]==t.id)
        for(const auto &type:world->animals.types())if(type.file==file && type.nameId==p.target)researched=true;
    if(!researched)return "Requires trick research.";
  }
  if(t.kind==1) {
    bool object=false;
    for(const auto &o:world->placedObjects.objects())
      if(o.nameId==t.building && world->fences.exhibitAt(static_cast<int>(std::floor(o.x)),static_cast<int>(std::floor(o.y)))==tank)object=true;
    if(!object)return "Requires its show object.";
  }
  (void)file;return {};
}

bool Shows::selectSpecies(int tank,const std::string &file) {
  refresh();auto *s=show(tank);if(!s || s->phase!=Phase::Waiting)return false;
  auto choices=species(tank);
  if(std::find(choices.begin(),choices.end(),file)==choices.end())return false;
  if(s->species!=file){s->species=file;s->script.clear();}return true;
}
bool Shows::add(int tank,const std::string &key) {
  auto *s=show(tank);if(!s || s->phase!=Phase::Waiting || static_cast<int>(s->script.size())>=settings.maxLength)return false;
  const Trick *t=find(s->species,key);if(!t || !t->visible || !unavailable(tank,s->species,*t).empty())return false;
  s->script.push_back(t->key);return true;
}
bool Shows::remove(int tank,int index) {
  auto *s=show(tank);if(!s || s->phase!=Phase::Waiting || index<0 || index>=static_cast<int>(s->script.size()))return false;
  s->script.erase(s->script.begin()+index);return true;
}
bool Shows::move(int tank,int index,int by) {
  auto *s=show(tank);if(!s || s->phase!=Phase::Waiting || index<0 || index>=static_cast<int>(s->script.size()) || index+by<0 || index+by>=static_cast<int>(s->script.size()))return false;
  std::swap(s->script[index],s->script[index+by]);return true;
}
bool Shows::setFrequency(int tank,int f) { auto *s=show(tank);if(!s || f<0 || f>2)return false;s->frequency=f;return true; }
bool Shows::setAdmission(int tank,double price) {
  auto *s=show(tank);if(!s || !std::isfinite(price))return false;
  s->admission=std::clamp(std::round(price/settings.admissionStep)*settings.admissionStep,settings.admissionMin,settings.admissionMax);return true;
}

std::vector<Shows::Trick> Shows::resolved(Show &s,std::vector<int> *benefits) {
  // zoo.exe 0x474826: repeated entries lose benefit; complexity triggers treats.
  std::vector<Trick> result;std::map<int,int> counts,scores;int previous=-1,complexity=0;
  const Trick *food=nullptr;for(const auto &t:tricks(s.species))if(t.kind==3){food=&t;break;}
  auto append=[&](const Trick &t) {
    int count=++counts[t.id];
    if(count==1)scores[t.id]=t.satisfaction;
    else if(count%3==0 || previous==t.id)scores[t.id]-=t.delta;
    result.push_back(t);if(benefits)benefits->push_back(scores[t.id]);previous=t.id;
  };
  for(const auto &key:s.script)if(const auto *t=find(s.species,key)) {
    append(*t);complexity+=t->complexity;
    if(food && complexity>=settings.returnToKeeper){append(*food);complexity=0;}
  }
  return result;
}
int Shows::benefit(int tank) { auto *s=show(tank);if(!s)return 0;std::vector<int> b;resolved(*s,&b);int sum=0;for(int n:b)sum+=n;return sum; }

std::string Shows::ready(Show &s) const {
  if(!world)return "No zoo.";
  const auto *e=world->fences.exhibit(s.tank);
  if(!e || !e->tank || !e->show || world->fences.waterLevel(*e)<=0)return "Show tank has no water.";
  if(links(s.tank).empty())return "No home tank with a portal at the same water height.";
  auto choices=species(s.tank);
  if(s.species.empty() || std::find(choices.begin(),choices.end(),s.species)==choices.end())return "No adult show animal of the selected species in a linked home tank.";
  if(s.script.empty())return "The show script has no tricks.";
  bool assigned=false;
  for(const auto &m:world->staff.members()) {
    if(world->staff.types()[m.type].kind!=Staff::Kind::Trainer || m.id==world->staff.carried)continue;
    for(const auto &p:links(s.tank))if(std::find(m.exhibits.begin(),m.exhibits.end(),p.second)!=m.exhibits.end())assigned=true;
  }
  return assigned?std::string():"No marine specialist assigned to a linked home tank.";
}

void Shows::notice(Show &s,const std::string &text) { s.status=text;messages.push_back(text);SDL_Log("[Shows] tank %d: %s",s.tank,text.c_str()); }

bool Shows::start(int tank,std::string &why) {
  refresh();auto *s=show(tank);
  if(!s){why="Not a show tank.";return false;}
  if(s->phase!=Phase::Waiting){why="A show is already in progress.";return false;}
  why=ready(*s);if(!why.empty())return false;
  auto sequence=resolved(*s);
  for(const auto &t:sequence)if(!(why=unavailable(tank,s->species,t)).empty())return false;
  auto ports=links(tank);
  for(auto &m:world->staff.list) {
    if(world->staff.typeList[m.type].kind!=Staff::Kind::Trainer || m.id==world->staff.carried || m.showTank>=0 || m.water || m.climbing || m.job==Staff::Job::Visit)continue;
    if(!std::any_of(ports.begin(),ports.end(),[&](const auto &p){return std::find(m.exhibits.begin(),m.exhibits.end(),p.second)!=m.exhibits.end();}))continue;
    for(const auto &[edge,p]:world->fences.pieces()) {
      if(!p.gate || (p.tank!=tank && p.tank2!=tank))continue;
      auto a=sideOf(edge,false),b=sideOf(edge,true);
      bool aInside=world->fences.exhibitAt(static_cast<int>(std::floor(a.first)),static_cast<int>(std::floor(a.second)))==tank;
      auto outside=aInside?b:a;
      int x=static_cast<int>(std::floor(outside.first)),y=static_cast<int>(std::floor(outside.second));
      if(world->fences.exhibitAt(x,y)>=0)continue;
      if(!world->staff.walkTo(m.id,x,y,world->worldMap,world->fences))continue;
      m.showTank=tank;m.dropX=outside.first;m.dropY=outside.second;m.idleLeft=0;m.playsLeft=0;
      s->trainer=m.id;break;
    }
    if(s->trainer>=0)break;
  }
  if(s->trainer<0){why="Assigned specialist is busy or cannot reach the show platform.";return false;}
  s->actors.clear();
  for(auto &m:world->animals.list) {
    const auto &t=world->animals.typeList[m.type];
    if(t.file!=s->species || m.baby || m.egg || m.boxed || m.dying || m.sick || m.showTank>=0 || m.id==world->animals.carried)continue;
    auto p=std::find_if(ports.begin(),ports.end(),[&](const auto &port){
      return port.second==m.exhibit || (!t.onlyUnderwater &&
        world->fences.habitatRoot(port.second,world->worldMap)==m.exhibit);
    });
    if(p==ports.end())continue;
    Actor actor;actor.id=m.id;actor.home=p->second;actor.portal=p->first;
    resolved(*s,&actor.benefit);
    s->actors.push_back(std::move(actor));m.showTank=tank;m.showHome=p->second;
    m.stack.clear();m.path.clear();m.pathAt=0;m.stepping=false;m.showDone=false;m.showTrick=false;
    m.huntAnimal=m.fleeFrom=m.prey=-1;m.tag=false;m.foodItem=-1;
    m.moveAnim=t.anims[0].count("surfswim")?"surfswim":"swim";m.speed=t.slow;
  }
  if(s->actors.empty()) {world->staff.member(s->trainer)->showTank=-1;s->trainer=-1;why="No available adult show animals.";return false;}
  if(!planStaging(*s)) {
    for(auto &a:s->actors)if(auto *m=world->animals.member(a.id))m->showTank=m->showHome=-1;
    world->staff.member(s->trainer)->showTank=-1;s->trainer=-1;s->actors.clear();
    why="The show tank needs more clear space for its performers.";return false;
  }
  s->phase=Phase::Gathering;s->elapsed=0;s->clock=0;
  notice(*s,"A marine specialist will be starting a show shortly.");return true;
}

void Shows::trainerCue(Show &s,const std::string &behavior) {
  auto *m=world->staff.member(s.trainer);if(!m || behavior.empty())return;
  const auto &type=world->staff.typeList[m->type];
  if(IniReader *ini=rm->getIniReader(type.file)) {
    auto lines=ini->getList(std::string(m->female?"f":"m")+"/behaviorset/"+behavior,"f");
    if(lines.empty())lines=ini->getList("m/behaviorset/"+behavior,"f");
    for(auto line:lines) {
      line=lowerShow(line);auto a=line.find('('),b=line.find_first_of(",)",a);
      if(a!=std::string::npos && b!=std::string::npos && line.find("fplay")!=std::string::npos) {
        std::string anim=line.substr(a+1,b-a-1);
        anim.erase(std::remove(anim.begin(),anim.end(),' '),anim.end());
        if(type.anims[m->female?1:0].count(anim) || type.anims[0].count(anim)){m->anim=anim;m->animTime=0;break;}
      }
    }
    delete ini;
  }
}

bool Shows::planStaging(Show &s) {
  const auto *tank=world->fences.exhibit(s.tank);
  if(!tank)return false;
  auto &animals=world->animals;
  std::vector<std::pair<Animals::Member*,std::pair<float,float>>> used;
  for(auto &actor:s.actors) {
    auto *m=animals.member(actor.id);if(!m || actor.stage==5)continue;
    float best=-1;
    std::pair<float,float> chosen;
    for(auto [x,y]:tank->tiles) {
      float gx=x+0.5f,gy=y+0.5f;
      if(!animals.standable(x,y,animals.typeList[m->type],s.tank,world->worldMap,world->fences))continue;
      float portalX=actor.portal.x+(actor.portal.alongX?0.5f:0),portalY=actor.portal.y+(actor.portal.alongX?0:0.5f);
      float portalDistance=std::hypot(gx-portalX,gy-portalY);
      // Leave a full body-width lane between a waiting performer and the
      // portal wall, so the next animal can enter and pass it safely.
      float arriving=0,turning=0;
      for(const auto &other:s.actors)if(const auto *visitor=animals.member(other.id)) {
        arriving=std::max(arriving,animals.bodyRadius(*visitor));
        const auto &type=animals.typeList[visitor->type];
        turning=std::max(turning,std::hypot(static_cast<float>(type.footX),static_cast<float>(type.footY))*0.25f+0.12f);
      }
      float normal=actor.portal.alongX?std::abs(gy-portalY):std::abs(gx-portalX);
      if(normal<animals.bodyRadius(*m)+arriving+turning+0.5f)continue;
      float clearance=100;
      bool room=true;
      for(const auto &p:used) {
        float spacing=animals.bodyRadius(*m)+animals.bodyRadius(*p.first)+0.5f;
        float d=std::hypot(gx-p.second.first,gy-p.second.second);
        if(d<spacing){room=false;break;}
        clearance=std::min(clearance,d);
      }
      if(!room)continue;
      // Keep whole bodies away from the boundary, including the portal lane.
      int margin=static_cast<int>(std::ceil(animals.bodyRadius(*m)));
      for(int dx=-margin;dx<=margin && room;++dx)
        for(int dy=-margin;dy<=margin;++dy)
          if(!tank->tiles.count({x+dx,y+dy})){room=false;break;}
      if(!room)continue;
      float score=std::min(clearance,6.0f)-portalDistance*0.05f;
      if(score>best){best=score;chosen={gx,gy};}
    }
    if(best<0)return false;
    actor.stageX=chosen.first;actor.stageY=chosen.second;
    used.push_back({m,chosen});
  }
  return true;
}

bool Shows::trainerStep(Show &s,float seconds) {
  auto *m=world->staff.member(s.trainer);
  if(!m || m->id==world->staff.carried)return false;
  const auto *e=world->fences.exhibit(s.tank);if(!e)return false;
  m->showTank=s.tank;
  if(m->pathAt<m->path.size()) {
    m->anim="walk";showMove(*m,world->staff.typeList[m->type].speed,seconds);return false;
  }
  const auto *tile=world->worldMap.getTile(static_cast<int>(std::floor(m->x)),static_cast<int>(std::floor(m->y)));
  float height=e->wallTop()-(tile?tile->height:0);
  if(std::abs(m->z-height)>0.01f) {
    m->anim="climb";float step=seconds*2.0f;m->z+=std::clamp(height-m->z,-step,step);return false;
  }
  if(m->anim=="walk" || m->anim=="climb") {m->anim="idle";m->animTime=0;}
  m->dutyArg=e->name;
  return true;
}

void Shows::actorStep(Show &s,Actor &a,float seconds) {
  if(a.stage==5)return; // A returned animal is released, including on later return ticks.
  auto &animals=world->animals;auto &fences=world->fences;auto *m=animals.member(a.id);
  if(!m || m->boxed || m->dying || m->baby || m->egg || m->id==animals.carried){
    if(auto *o=world->placedObjects.byId(a.object))o->showHidden=false;
    a.object=-1;
    if(m){m->showTank=m->showHome=-1;m->showTrick=m->showDone=false;}a.stage=5;return;
  }
  m->showTank=s.tank;m->showHome=a.home;
  auto *home=fences.exhibit(a.home);auto *tank=fences.exhibit(s.tank);
  if(!home || !home->tank || !tank || !tank->tank || !home->filling || !tank->filling) {animals.box(m->id);m->showTank=m->showHome=-1;a.stage=5;return;}
  auto first=sideOf(a.portal,false),second=sideOf(a.portal,true);
  bool firstHome=home->tiles.count({static_cast<int>(std::floor(first.first)),static_cast<int>(std::floor(first.second))})!=0;
  auto hp=firstHome?first:second,sp=firstHome?second:first;
  auto walk=[&](std::pair<float,float> target) {
    if(std::hypot(m->x-target.first,m->y-target.second)<0.12f)return true;
    if(m->pathAt<m->path.size())return false;
    const auto &type=animals.typeList[m->type];
    m->moveAnim=type.anims[m->female?1:0].count("surfswim")||type.anims[0].count("surfswim")?"surfswim":"swim";
    m->speed=type.slow;
    const auto *current=fences.exhibit(fences.exhibitAt(static_cast<int>(m->x),static_cast<int>(m->y)));
    if(!current || !current->tank)current=home;
    m->swimTo=current?std::max(0.0f,fences.waterLevel(*current)-2.0f):0;
    m->stack.clear();m->stepping=false;
    if(!animals.walkTo(*m,target.first,target.second,world->worldMap,fences))a.elapsed+=seconds;
    return false;
  };
  a.elapsed+=seconds;
  if(a.elapsed>180) {
    if(auto *o=world->placedObjects.byId(a.object))o->showHidden=false;
    a.object=-1;animals.box(m->id);m->showTank=m->showHome=-1;a.stage=5;return;}
  if(a.stage==0) { if(walk(hp)){a.stage=1;a.elapsed=0;}return; }
  if(a.stage==1) { if(walk({a.stageX,a.stageY})){a.stage=2;a.elapsed=0;m->exhibit=s.tank;}return; }
  if(a.stage==3) {
    if(m->inside)animals.leaveShelter(*m);
    if(walk(sp)){a.stage=4;a.elapsed=0;}return;
  }
  if(a.stage==4) {
    auto landing=hp;
    float clearance=animals.bodyRadius(*m)*2+0.5f;
    if(a.portal.alongX)landing.second+=(hp.second<sp.second?-clearance:clearance);
    else landing.first+=(hp.first<sp.first?-clearance:clearance);
    if(walk(landing)) {m->showTank=m->showHome=-1;m->showTrick=false;m->showDone=false;m->exhibit=a.home;a.stage=5;m->stack.clear();m->stepping=false;a.elapsed=0;}
    return;
  }
  if(a.stage==5 || s.phase!=Phase::Performing)return;
  auto sequence=resolved(s);
  if(a.index>=static_cast<int>(sequence.size()))return;
  const Trick &trick=sequence[a.index];
  if(m->showDone) {
    m->showDone=false;m->showTrick=false;
    if(auto *o=world->placedObjects.byId(a.object))o->showHidden=false;
    a.object=-1;m->building=-1;
    if(trick.kind!=3) {
      int score=a.index<static_cast<int>(a.benefit.size())?a.benefit[a.index]:trick.satisfaction;
      a.score+=score;++a.completed;++s.tricksDone;
      for(auto &g:world->guests.list)if(g.showTank==s.tank && g.showSeat>=0) {
        const char *anim=score>=settings.greatTrick?"gsovat":score>=settings.goodTrick?"gsclap":score<=settings.badTrick?"gsboo":"gssit";
        if(world->guests.typeList[g.type].anims.count(anim)){g.anim=anim;g.animTime=0;}
        if(score<=settings.badTrick)world->guests.think(g,10610);
      }
    }
    trainerCue(s,trick.post);++a.index;a.elapsed=0;return;
  }
  if(m->showTrick)return;
  std::string refusal=unavailable(s.tank,s.species,trick);
  const auto &type=animals.typeList[m->type];
  if(!refusal.empty() || m->sick) {++a.skipped;++s.tricksSkipped;++a.index;a.elapsed=0;return;}
  std::string set="b"+trick.behavior;
  if(trick.kind==1) {
    const PlacedObjects::Object *object=nullptr;
    for(const auto &o:world->placedObjects.objects())if(o.nameId==trick.building && fences.exhibitAt(static_cast<int>(std::floor(o.x)),static_cast<int>(std::floor(o.y)))==s.tank){object=&o;break;}
    if(!object){++a.skipped;++s.tricksSkipped;++a.index;return;}
    set="b"+lowerShow(object->typeName)+trick.behavior+"0";
    if(std::hypot(m->x-object->x,m->y-object->y)>1.1f) {walk({object->x,object->y});return;}
    if(std::any_of(s.actors.begin(),s.actors.end(),[&](const Actor &other){return other.id!=a.id && other.object==object->id;}))return;
    a.object=object->id;m->building=object->id;
  }
  // Roll once when the animal is ready to perform, not on every route tick.
  bool skipped=trick.kind!=3 && m->happiness<=type.skipTrickHappiness &&
    std::uniform_int_distribution<int>(0,99)(animals.rng)<type.skipTrickChance;
  if(skipped || !animals.set(*m,set)) {a.object=-1;m->building=-1;++a.skipped;++s.tricksSkipped;++a.index;a.elapsed=0;return;}
  m->stack.clear();m->path.clear();m->pathAt=0;m->stepping=false;
  m->showTrick=true;m->showDone=false;m->swimZ=m->swimTo=static_cast<float>(fences.waterLevel(*tank));
  animals.pushSet(*m,set);trainerCue(s,trick.pre);
  ZT_STEP("0x5a66c0","Performs a show trick",{"animal",m->id},{"trick",trick.key},{"show",s.tank});
}

void Shows::cancel(int tank,const std::string &why) {
  auto *s=show(tank);if(!s || !world)return;
  if(s->phase!=Phase::Waiting)++s->cancelled;
  for(const auto &a:s->actors) {
    if(auto *o=world->placedObjects.byId(a.object))o->showHidden=false;
    if(auto *m=world->animals.member(a.id)) {
    if(m->inside)world->animals.leaveShelter(*m);
    m->showTank=m->showHome=-1;m->showTrick=m->showDone=false;m->stack.clear();m->path.clear();m->stepping=false;
    if(m->exhibit!=a.home && !m->boxed)world->animals.box(m->id);
  }
  }
  if(auto *m=world->staff.member(s->trainer)) {m->showTank=-1;m->z=0;m->anim="idle";m->workCheck=0;m->dutyArg.clear();}
  releaseAudience(*s,true);s->actors.clear();s->trainer=-1;s->phase=Phase::Waiting;s->clock=0;notice(*s,why);
}

void Shows::finish(Show &s) {
  s.lastScore=0;for(const auto &a:s.actors)s.lastScore+=a.score;
  ++s.completed;releaseAudience(s,false);
  s.phase=Phase::Returning;s.elapsed=0;for(auto &a:s.actors)if(a.stage!=5){a.stage=3;a.elapsed=0;}
  notice(s,"The show has ended.");
}

void Shows::update(float seconds) {
  if(!world || seconds<=0)return;
  refresh();
  for(auto &[id,s]:list) {
    s.elapsed+=seconds;
    audiences(s,seconds);
    if(s.phase==Phase::Waiting) {
      s.clock+=seconds;std::string why=ready(s);s.status=why.empty()?"Waiting for next show.":why;
      if(s.clock>=std::max(1,settings.interval[std::clamp(s.frequency,0,2)]))start(id,why);
      continue;
    }
    auto *trainer=world->staff.member(s.trainer);
    const auto *tank=world->fences.exhibit(id);
    auto portals=links(id);
    bool assigned=trainer && std::any_of(portals.begin(),portals.end(),[&](const auto &p){return std::find(trainer->exhibits.begin(),trainer->exhibits.end(),p.second)!=trainer->exhibits.end();});
    if(!tank || !tank->filling || world->fences.waterLevel(*tank)<=0 || portals.empty() || !assigned) {cancel(id,"The show was cancelled: linked tanks or specialist assignment unavailable.");continue;}
    if(!trainer || trainer->id==world->staff.carried || !world->fences.exhibit(id)) {cancel(id,"The show was cancelled: specialist or tank unavailable.");continue;}
    bool atPlatform=trainerStep(s,seconds);
    if(s.phase==Phase::Gathering && !atPlatform) {
      if(s.elapsed>180)cancel(id,"The specialist cannot reach the show platform.");
      continue;
    }
    for(auto &actor:s.actors)actorStep(s,actor,seconds);
    if(s.phase==Phase::Gathering && std::all_of(s.actors.begin(),s.actors.end(),[](const Actor &a){return a.stage==2 || a.stage==5;})) {
      if(std::all_of(s.actors.begin(),s.actors.end(),[](const Actor &a){return a.stage==5;})){cancel(id,"No show animal could reach the tank.");continue;}
      s.phase=Phase::Performing;s.elapsed=0;notice(s,"A show is starting.");
      if(!settings.startSound.empty())Sound::get().play(settings.startSound);
    }
    else if(s.phase==Phase::Performing) {
      int count=static_cast<int>(resolved(s).size());
      if(std::all_of(s.actors.begin(),s.actors.end(),[&](const Actor &a){return a.stage==5 || a.index>=count;}))finish(s);
    }
    else if(s.phase==Phase::Returning && std::all_of(s.actors.begin(),s.actors.end(),[](const Actor &a){return a.stage==5;})) {
      trainer->showTank=-1;trainer->z=0;trainer->anim="idle";trainer->workCheck=0;trainer->dutyArg.clear();
      s.trainer=-1;s.actors.clear();s.phase=Phase::Waiting;s.clock=0;s.elapsed=0;
    }
  }
}

void Shows::audiences(Show &s,float seconds) {
  auto &guests=world->guests;
  auto leave=[&](Guests::Guest &g) {
    if(g.showSeat>=0)if(auto *stand=world->placedObjects.byId(g.showStand))stand->visitorsNow=std::max(0,stand->visitorsNow-1);
    g.x=g.showDoorX;g.y=g.showDoorY;g.showTank=g.showStand=g.showSeat=-1;g.showZ=0;g.showExit=0;
    g.building=-1;g.hidden=false;g.path.clear();g.pathAt=0;g.need.clear();g.state=Guests::State::Walking;
    guests.play(g,"stand",1.0f);
  };
  for(auto &g:guests.list)if(g.showTank==s.tank) {
    auto *stand=world->placedObjects.byId(g.showStand);
    if(!stand){leave(g);continue;}
    if(g.showExit>0) {
      g.showExit-=seconds;
      const char *anim=g.showExit>g.showExitLength-2 || g.showExit<1?"gssit":g.showRating==0?"gsboo":"gsovat";
      if(guests.typeList[g.type].anims.count(anim) && g.anim!=anim){g.anim=anim;g.animTime=0;}
      if(g.showExit<=0)leave(g);
      continue;
    }
    if(g.showSeat<0) {
      if(!showMove(g,g.speed,seconds))continue;
      std::set<int> seats;
      for(const auto &other:guests.list)if(other.showStand==g.showStand && other.showSeat>=0)seats.insert(other.showSeat);
      int slot=0;while(seats.count(slot))++slot;
      std::vector<int> positions;
      int capacity=12;
      if(IniReader *ini=rm->getIniReader(PlacedObjects::fileOf(*stand))) {
        positions=ini->getIntList(ini->get("slot","cHasDrawSlots","drawslots"),"p");
        capacity=ini->getInt("characteristics/integers","cCapacity",12);delete ini;
      }
      if(slot>=capacity || static_cast<size_t>(slot*3+2)>=positions.size()){leave(g);continue;}
      g.showDoorX=g.x;g.showDoorY=g.y;
      float x=positions[slot*3]/64.0f,y=positions[slot*3+1]/64.0f;
      // Draw slots use the same across/forward axes as the entrance slots.
      float angle=stand->facing*3.14159265f/4.0f;
      g.x=stand->x-x*std::cos(angle)+y*std::sin(angle);
      g.y=stand->y-x*std::sin(angle)-y*std::cos(angle);
      g.showZ=positions[slot*3+2]/16.0f;
      const float dx[4]={0,1,0,-1},dy[4]={-1,0,1,0};
      g.fx=dx[(stand->facing&6)/2];g.fy=dy[(stand->facing&6)/2];
      g.showSeat=slot;g.state=Guests::State::Using;g.hidden=false;g.anim="gssit";g.animTime=0;
      ++s.visitors;s.income+=s.admission;incomePending+=s.admission;
      stand->income+=s.admission;++stand->visitorsNow;++stand->visitorsTotal;
      g.visited.insert(stand->id);
    }
    else if(g.anim!="gssit" && g.animTime>7){g.anim="gssit";g.animTime=0;}
  }
  bool admitting=s.phase==Phase::Gathering || s.phase==Phase::Performing ||
    (s.phase==Phase::Waiting && s.clock>=settings.interval[std::clamp(s.frequency,0,2)]-30 && ready(s).empty());
  if(!admitting)return;
  for(auto &g:guests.list) {
    if(g.showTank>=0 || g.leaving || g.guide>=0 || g.hidden || g.hunger>80 || g.thirst>80 ||
       (g.state!=Guests::State::Walking && g.state!=Guests::State::Gawking))continue;
    for(size_t i=0;i<world->placedObjects.all().size();++i) {
      auto &stand=world->placedObjects.all()[i];
      const auto &info=guests.infoOf(PlacedObjects::fileOf(stand));
      if(!info.satisfies.count("grandstand") || g.visited.count(stand.id))continue;
      // Original 0x59e349 checks the view from 3/4/5 tiles in the stand's facing.
      const int dx[4]={0,1,0,-1},dy[4]={-1,0,1,0};int side=(stand.facing&6)/2;
      bool facesTank=false;
      for(int distance:{3,4,5}) {
        int tx=static_cast<int>(std::floor(stand.x+dx[side]*distance));
        int ty=static_cast<int>(std::floor(stand.y+dy[side]*distance));
        facesTank=facesTank || world->fences.exhibitAt(tx,ty)==s.tank;
      }
      if(!facesTank)continue;
      int occupied=0;for(const auto &other:guests.list)occupied+=other.showStand==stand.id;
      if(occupied>=info.capacity)continue;
      bool routed=false;
      for(auto [x,y]:guests.doorTiles(stand))if(guests.walkable(x,y) &&
        ((static_cast<int>(std::floor(g.x))==x && static_cast<int>(std::floor(g.y))==y) || guests.walkTo(g,x,y,guests.typeList[g.type].speed))) {
        if(static_cast<int>(std::floor(g.x))==x && static_cast<int>(std::floor(g.y))==y){g.path.clear();g.pathAt=0;}
        g.showTank=s.tank;g.showStand=stand.id;g.showSeat=-1;g.showExit=0;g.building=static_cast<int>(i);
        g.showDoorX=x+0.5f;g.showDoorY=y+0.5f;g.need="show";g.target=s.tank;g.state=Guests::State::Walking;routed=true;break;
      }
      if(routed)break;
    }
  }
}

void Shows::releaseAudience(Show &s,bool cancelled) {
  for(auto &g:world->guests.list)if(g.showTank==s.tank) {
    if(!cancelled && g.showSeat>=0) {
      g.showRating=s.lastScore<=settings.badShow?0:s.lastScore<settings.goodShow?1:s.lastScore<settings.greatShow?2:3;
      world->guests.think(g,10287+g.showRating);
      g.showExitLength=g.showRating==0?6.0f:g.showRating==1?5.0f:g.showRating==2?7.0f:9.0f;
      g.showExit=g.showExitLength;g.anim="gssit";g.animTime=0;
    } else {
      if(cancelled && g.showSeat>=0)world->guests.think(g,10287);
      if(g.showSeat>=0)if(auto *stand=world->placedObjects.byId(g.showStand))stand->visitorsNow=std::max(0,stand->visitorsNow-1);
      g.x=g.showDoorX;g.y=g.showDoorY;g.showTank=g.showStand=g.showSeat=-1;g.showZ=0;g.showExit=0;
      g.path.clear();g.pathAt=0;g.building=-1;g.need.clear();g.hidden=false;g.state=Guests::State::Walking;
      world->guests.play(g,"stand",1.0f);
    }
  }
}

std::string Shows::describe() const {
  std::ostringstream out;
  static const char *phases[]={"waiting","gathering","performing","returning"};
  for(const auto &[id,s]:list) {
    out<<"Show "<<id<<" "<<phases[static_cast<int>(s.phase)]<<" species="<<s.species<<" script="<<s.script.size()
       <<" staff="<<s.trainer<<" actors="<<s.actors.size()<<" clock="<<s.clock<<" completed="<<s.completed
       <<" cancelled="<<s.cancelled<<" tricks="<<s.tricksDone<<" skipped="<<s.tricksSkipped<<" score="<<s.lastScore
       <<" visitors="<<s.visitors<<" income="<<s.income
       <<" reserved="<<std::count_if(world->animals.members().begin(),world->animals.members().end(),[&](const auto &m){return m.showTank==id;})
       <<" status="<<s.status<<"\n";
    for(const auto &a:s.actors)out<<"  animal="<<a.id<<" home="<<a.home<<" stage="<<a.stage<<" trick="<<a.index<<" score="<<a.score<<"\n";
  }
  if(list.empty())out<<"No show tanks.\n";
  return out.str();
}

std::string Shows::command(const std::vector<std::string> &w) {
  refresh();
  if(w.size()<2 || w[1]=="list")return describe();
  int id=std::atoi(w[1].c_str());auto *s=show(id);
  if(!s)return "No such show tank.\n";
  if(w.size()==2)return describe();
  const std::string &op=w[2];
  if(op=="species") {
    if(w.size()>3)return selectSpecies(id,w[3])?"Species selected.\n":"Species unavailable.\n";
    std::string out;for(const auto &file:species(id))out+=file+"\n";return out;
  }
  if(op=="portals") {
    std::ostringstream out;
    for(const auto &[edge,home]:links(id)) {
      const auto *piece=world->fences.at(edge);
      out<<"Portal home="<<home<<" x="<<edge.x<<" y="<<edge.y<<" alongX="<<edge.alongX
         <<" openFrame="<<(piece?piece->open:0)<<" art="<<(world->fences.portalType(edge)!=nullptr)<<"\n";
    }
    return out.str().empty()?"No valid connecting portal.\n":out.str();
  }
  if(op=="tricks") {
    std::ostringstream out;for(const auto &t:tricks(s->species))out<<t.key<<" id="<<t.id<<" type="<<t.kind<<" depth="<<t.depth<<" visible="<<t.visible<<" "<<unavailable(id,s->species,t)<<"\n";return out.str();
  }
  if(op=="add" && w.size()>3)return add(id,w[3])?"Trick added.\n":"Trick unavailable or script full.\n";
  if(op=="remove" && w.size()>3)return remove(id,std::atoi(w[3].c_str()))?"Trick removed.\n":"Cannot remove trick.\n";
  if(op=="frequency" && w.size()>3)return setFrequency(id,std::atoi(w[3].c_str()))?"Frequency set.\n":"Invalid frequency.\n";
  if(op=="price" && w.size()>3)return setAdmission(id,std::atof(w[3].c_str()))?"Admission set.\n":"Invalid admission.\n";
  if(op=="run"){std::string why;return start(id,why)?"Show preparation started.\n":why+"\n";}
  if(op=="cancel"){cancel(id,"Cancelled by the test console.");return "Show cancelled.\n";}
  if(op=="script") {std::string out;for(const auto &key:s->script)out+=key+"\n";return out;}
  return "shows [tank] [species [file]|tricks|add key|remove index|script|frequency 0..2|price amount|run|cancel]\n";
}

std::string Shows::snapshot(bool actors) const {
  std::ostringstream out;out<<"SHOWS2 "<<list.size()<<' '<<std::setprecision(17)<<incomePending<<' ';
  for(const auto &[id,s]:list) {
    out<<id<<' '<<std::quoted(s.species)<<' '<<s.frequency<<' '<<s.admission<<' '<<(actors?s.clock:0)<<' '
       <<s.completed<<' '<<s.cancelled<<' '<<s.lastScore<<' '<<s.tricksDone<<' '<<s.tricksSkipped<<' '<<s.visitors<<' '<<s.income<<' '
       <<s.script.size()<<' ';
    for(const auto &key:s.script)out<<std::quoted(key)<<' ';
    out<<(actors?static_cast<int>(s.phase):0)<<' '<<(actors?s.trainer:-1)<<' '<<s.elapsed<<' '<<(actors?s.actors.size():0)<<' ';
    if(actors)for(const auto &a:s.actors)
      out<<a.id<<' '<<a.home<<' '<<a.stage<<' '<<a.index<<' '<<a.score<<' '<<a.completed<<' '<<a.skipped<<' '
         <<a.portal.alongX<<' '<<a.portal.x<<' '<<a.portal.y<<' '<<a.elapsed<<' '; // The trick restarts after loading; its object is acquired again.
    const auto *trainer=actors?world->staff.member(s.trainer):nullptr;
    out<<(trainer?trainer->dropX:0)<<' '<<(trainer?trainer->dropY:0)<<' ';
  }
  size_t visitors=0;if(actors)for(const auto &g:world->guests.list)if(g.showTank>=0)++visitors;
  out<<visitors<<' ';
  if(actors)for(const auto &g:world->guests.list)if(g.showTank>=0)
    out<<g.id<<' '<<g.showTank<<' '<<g.showStand<<' '<<g.showSeat<<' '<<g.showDoorX<<' '<<g.showDoorY<<' '
       <<g.showZ<<' '<<g.showExit<<' '<<g.showExitLength<<' '<<g.showRating<<' ';
  return out.str();
}
bool Shows::restore(const std::string &text) {
  clear();if(text.empty()){refresh();return true;}
  std::istringstream in(text);std::string magic;size_t count=0;in>>magic>>count>>incomePending;
  if(magic!="SHOWS2" || count>1000 || !std::isfinite(incomePending))return false;
  for(size_t i=0;i<count && in;++i) {
    Show s;size_t n=0;int phase=0;float dropX=0,dropY=0;
    in>>s.tank>>std::quoted(s.species)>>s.frequency>>s.admission>>s.clock>>s.completed>>s.cancelled>>s.lastScore>>s.tricksDone>>s.tricksSkipped>>s.visitors>>s.income>>n;
    if(n>100)return false;
    for(size_t j=0;j<n;++j){std::string key;in>>std::quoted(key);s.script.push_back(key);}
    in>>phase>>s.trainer>>s.elapsed>>n;if(phase<0 || phase>3 || n>10000)return false;s.phase=static_cast<Phase>(phase);
    for(size_t j=0;j<n;++j) {
      Actor a;in>>a.id>>a.home>>a.stage>>a.index>>a.score>>a.completed>>a.skipped>>a.portal.alongX>>a.portal.x>>a.portal.y>>a.elapsed;
      if(a.stage<0 || a.stage>5 || a.index<0 || a.index>200 || !std::isfinite(a.elapsed))return false;
      resolved(s,&a.benefit);s.actors.push_back(a);
    }
    in>>dropX>>dropY;
    if(!std::isfinite(s.admission) || !std::isfinite(s.clock) || !std::isfinite(s.elapsed) || !std::isfinite(s.income) ||
       !std::isfinite(dropX) || !std::isfinite(dropY))return false;
    s.frequency=std::clamp(s.frequency,0,2);s.admission=std::clamp(s.admission,settings.admissionMin,settings.admissionMax);
    if(!in || list.count(s.tank))return false;
    int id=s.tank;list.emplace(id,std::move(s));auto &saved=list.at(id);
    if(saved.phase==Phase::Gathering && !planStaging(saved))return false;
    if(saved.phase!=Phase::Waiting) {
      for(auto &a:saved.actors)if(auto *m=world->animals.member(a.id)) {
        if(a.stage==5)continue;
        m->showTank=id;m->showHome=a.home;m->showTrick=m->showDone=false;
        const auto *tank=world->fences.exhibit(m->exhibit);
        m->swimZ=m->swimTo=tank?static_cast<float>(world->fences.waterLevel(*tank)):0;
      }
      if(auto *m=world->staff.member(saved.trainer)) {
        m->showTank=id;m->dropX=dropX;m->dropY=dropY;
        world->staff.walkTo(m->id,static_cast<int>(std::floor(dropX)),static_cast<int>(std::floor(dropY)),world->worldMap,world->fences);
      }
    }
  }
  size_t visitors=0;in>>visitors;if(visitors>100000)return false;
  for(size_t i=0;i<visitors && in;++i) {
    int id=-1,tank=-1,stand=-1,seat=-1,rating=0;float x=0,y=0,z=0,exit=0,length=0;
    in>>id>>tank>>stand>>seat>>x>>y>>z>>exit>>length>>rating;
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(exit)||!std::isfinite(length))return false;
    auto *g=const_cast<Guests::Guest*>(world->guests.guest(id));if(!g || !show(tank))continue;
    g->showTank=tank;g->showStand=stand;g->showSeat=seat;g->showDoorX=x;g->showDoorY=y;
    g->showZ=z;g->showExit=exit;g->showExitLength=length;g->showRating=std::clamp(rating,0,3);
    g->hidden=false;g->anim="gssit";g->state=seat<0?Guests::State::Walking:Guests::State::Using;
    if(seat<0)world->guests.walkTo(*g,static_cast<int>(std::floor(x)),static_cast<int>(std::floor(y)),g->speed);
  }
  if(!in)return false;
  refresh();return true;
}
