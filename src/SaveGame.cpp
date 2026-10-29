#include "SaveGame.hpp"
#include "Goals.hpp"
#include "Features.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <set>
#include <type_traits>
#include <vector>
#include <zlib.h>

#include "Research.hpp"
#include "World.hpp"
#include "ZooSim.hpp"
#include "ui/UiGameScreen.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#undef near
#undef far
#endif

namespace {
constexpr uint32_t kMagic = 0x5A543153; // "S1TZ"
constexpr uint32_t kMapMagic = 0x504D3153; // "S1MP": a map design (saveMap)
constexpr uint32_t kVersion = 14; // 14: file-based guest favourites and recent exhibits. // 12: imported identity, facing, inventory and status/check timers. // 11: retain tranquilized animals across save/load. // 9: filter definition paths persist per placed filter. // (7: before tanks kept the ground they sank from; 1: before visitors and exhibit books; 2: research progress whole; 3: no shared tank walls; 4: tank walls in height units; 5: no view turn; 6: tank walls in half units)

// A little binary writer and reader: numbers as they are, strings with
// their length, vectors with their count
struct Out {
  std::vector<char> b;
  template <class T> void raw(const T &v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const char *p = reinterpret_cast<const char *>(&v);
    b.insert(b.end(), p, p + sizeof(T));
  }
  void str(const std::string &s) {
    raw(static_cast<uint32_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
  }
};
struct In {
  const std::vector<char> &b;
  size_t at = 0;
  bool bad = false;
  template <class T> T raw() {
    T v{};
    if (at + sizeof(T) > b.size()) {
      bad = true;
      return v;
    }
    std::memcpy(&v, b.data() + at, sizeof(T));
    at += sizeof(T);
    return v;
  }
  uint32_t count(uint32_t limit = 1000000, size_t minimumBytes = 1) {
    const auto n = raw<uint32_t>();
    if (bad || n > limit || n > (b.size() - at) / minimumBytes) { bad = true; return 0; }
    return n;
  }
  template <class T> void get(T &v) { v = raw<T>(); }
  template <class T, size_t N> void get(T (&v)[N]) {
    for (size_t k = 0; k < N; k++)
      v[k] = raw<T>();
  }
  std::string str() {
    uint32_t n = raw<uint32_t>();
    if (bad || at + n > b.size()) {
      bad = true;
      return {};
    }
    std::string s(b.data() + at, n);
    at += n;
    return s;
  }
};

// A normal TZFB stream followed by our full native snapshot and this footer.
// zoo.exe stops at the end of its managers. Engine readers validate both
// streams before selecting the native snapshot; extensions never nest.
constexpr char kEngineFooter[8]={'Z','T','1','E','X','T','R','A'};
constexpr size_t kFooterSize=36;
std::string saveFailure;
void describeMissing(const std::string &file) {
  saveFailure = "Missing content: " + std::filesystem::path(file).filename().string() +
                ". Enable its mod and try again.";
  SDL_Log("[Save] required resource unavailable: %s", file.c_str());
}
void missingContent(In &in, const std::string &file) {
  if (in.bad) return;
  describeMissing(file);
  in.bad = true;
}
bool requireContent(In &in, ResourceManager *rm, const std::string &file) {
  if (in.bad) return false;
  if (!rm->hasResource(file)) { missingContent(in, file); return false; }
  return true;
}
uint32_t saveCrc(const char *data,size_t size) {
  return uint32_t(crc32(0,reinterpret_cast<const Bytef *>(data),uInt(size)));
}
bool hasEngineSection(const std::vector<char> &b);
bool nativeStream(std::vector<char> &b) {
  if(b.size()<kFooterSize || std::memcmp(b.data()+b.size()-kFooterSize,kEngineFooter,8)!=0)return !hasEngineSection(b);
  In footer{b};footer.at=b.size()-kFooterSize+8;
  const auto version=footer.raw<uint32_t>();
  const auto offset=footer.raw<uint64_t>(),length=footer.raw<uint64_t>();
  const auto nativeCrc=footer.raw<uint32_t>(),originalCrc=footer.raw<uint32_t>();
  if(footer.bad || version!=1 || offset<8 || offset>b.size()-kFooterSize ||
     length!=b.size()-kFooterSize-offset || length<8 || length>128*1024*1024 ||
     std::memcmp(b.data(),"TZFB",4)!=0 || std::memcmp(b.data()+offset,"S1TZ",4)!=0 ||
     saveCrc(b.data(),size_t(offset))!=originalCrc ||
     saveCrc(b.data()+offset,size_t(length))!=nativeCrc)return false;
  std::vector<char> native(b.begin()+offset,b.begin()+offset+length);
  b.swap(native);return true;
}
bool atomicSave(const std::string &path,const std::vector<char> &bytes) {
  const auto temporary=path+".writing";
  FILE *file=std::fopen(temporary.c_str(),"wb");
  if(!file)return false;
  const bool written=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
  const bool closed=std::fclose(file)==0;
  if(!written||!closed){std::error_code ec;std::filesystem::remove(temporary,ec);return false;}
#ifdef _WIN32
  const bool replaced=MoveFileExA(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
  std::error_code ec;std::filesystem::rename(temporary,path,ec);const bool replaced=!ec;
#endif
  if(!replaced){std::error_code ec;std::filesystem::remove(temporary,ec);}
  return replaced;
}

bool readSaveBytes(const std::string &path,std::vector<char> &out,bool native=true) {
  FILE *fp=std::fopen(path.c_str(),"rb");if(!fp)return false;
  if(std::fseek(fp,0,SEEK_END)!=0){std::fclose(fp);return false;}
  const long n=std::ftell(fp);std::fseek(fp,0,SEEK_SET);
  if(n<=0 || n>256*1024*1024){std::fclose(fp);return false;}
  out.resize(size_t(n));
  const bool ok=std::fread(out.data(),1,out.size(),fp)==out.size();std::fclose(fp);
  return ok && (!native || nativeStream(out));
}
bool hasEngineSection(const std::vector<char> &b) {
  if(b.size()<8 || std::memcmp(b.data(),"TZFB",4)!=0)return false;
  const size_t start=b.size()>kFooterSize?b.size()-kFooterSize:0;
  for(size_t i=start;i+8<=b.size();++i)
    if(std::memcmp(b.data()+i,kEngineFooter,8)==0)return true;
  // Also recognize a truncated/missing footer. Do not silently import the
  // reduced original stream and discard the player's engine-only features.
  AssetBuffer bytes;bytes.data=const_cast<char *>(b.data());bytes.size=b.size();
  ZooReader z;
  return z.load(bytes) && z.hasGame() && z.originalBytes().size()+8<=b.size() &&
         std::memcmp(b.data()+z.originalBytes().size(),"S1TZ",4)==0;
}

void putEdge(Out &o, const Fences::Edge &e) {
  o.raw(e.alongX);
  o.raw(e.x);
  o.raw(e.y);
}
Fences::Edge getEdge(In &i) {
  Fences::Edge e;
  i.get(e.alongX);
  i.get(e.x);
  i.get(e.y);
  return e;
}
} // namespace

const std::string &SaveGame::lastError() { return saveFailure; }

std::string SaveGame::folder() {
  std::string base = ".";
#ifdef _WIN32
  char docs[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs)))
    base = docs;
#endif
  std::filesystem::path p = std::filesystem::path(base) / "zt1-engine" / "Saved Games";
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  return p.string();
}

// The original's dialogs: "Save a zoo..." / "Load a zoo...", Zoo Tycoon
#ifdef _WIN32
static UINT_PTR CALLBACK saveFormatHook(HWND dialog,UINT message,WPARAM,LPARAM data) {
  if(message==WM_NOTIFY) {
    const auto *notice=reinterpret_cast<const OFNOTIFYA *>(data);
    if(notice->hdr.code==CDN_TYPECHANGE && notice->lpOFN) {
      const char *extension=notice->lpOFN->nFilterIndex==2?"zt1save":"zoo";
      notice->lpOFN->lpstrDefExt=extension;
      SendMessageA(GetParent(dialog),CDM_SETDEFEXT,0,reinterpret_cast<LPARAM>(extension));
    }
  }
  return 0;
}
#endif

// Saved Game files, in the Saved Games folder
static std::string fileDialog(bool saving) {
#ifdef _WIN32
  char file[MAX_PATH] = {};
  std::string dir = SaveGame::folder();
  OPENFILENAMEA ofn{};
  ofn.lStructSize = sizeof(ofn);
  // (owned by the game's window: on top of it, the game waiting - with no
  // owner it could open behind the window, the game seeming to hang)
  ofn.hwndOwner = GetActiveWindow() ? GetActiveWindow() : GetForegroundWindow();
  // (loading: the original game's own saved zoos too, .zoo)
  ofn.lpstrFilter = saving ? "Shared zoo (*.zoo)\0*.zoo\0"
                             "Engine-only save (*.zt1save)\0*.zt1save\0"
                           : "All saved zoos (*.zt1save, *.zoo)\0*.zt1save;*.zoo\0"
                             "Zoo Tycoon Saved Game (*.zt1save)\0*.zt1save\0"
                             "The original game's saved zoos (*.zoo)\0*.zoo\0"
                             "All files (*.*)\0*.*\0";
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrInitialDir = dir.c_str();
  ofn.lpstrDefExt = saving ? "zoo" : "zt1save";
  ofn.lpstrTitle = saving ? "Save a zoo..." : "Load a zoo...";
  ofn.Flags = OFN_NOCHANGEDIR | (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  if(saving) {
    ofn.Flags|=OFN_EXPLORER|OFN_ENABLEHOOK|OFN_ENABLESIZING;
    ofn.lpfnHook=saveFormatHook;
  }
  BOOL ok = saving ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
  if(ok&&saving)SDL_Log("[Save] dialog selected format %lu",ofn.nFilterIndex);
  return ok ? std::string(file) : std::string();
#else
  (void)saving;
  return "";
#endif
}

std::string SaveGame::askSavePath() { return fileDialog(true); }
std::string SaveGame::askLoadPath() { return fileDialog(false); }

// ----------------------------------------------------------------------------
// The original game's own saved zoos (.zoo, read by ZooReader in zoo.exe's
// order: loadZoo 0x453000, worldLoad 0x452e13)
// ----------------------------------------------------------------------------
bool SaveGame::isOriginal(const std::string &path) {
  std::vector<char> bytes;
  return readSaveBytes(path,bytes,false) && bytes.size()>=8 &&
         std::memcmp(bytes.data(),"TZFB",4)==0 && !hasEngineSection(bytes);
}

// Filters need the saved zoo's live tank exhibits, not the temporary
// map pockets produced by Fences::load. Run this after habitat restoration.
static int restoreOriginalFilters(const ZooReader &reader, Fences &fences, const WorldMap &map) {
  int missed = 0;
  const std::string previous = fences.filterType().file;
  for (const auto &obj : reader.getObjects()) {
    if (!obj.decoded || obj.className != "filter") continue;
    bool installed = false;
    for (const auto &file : {
           "scenery/" + obj.subClass + "/" + obj.typeName + ".ai",
           "scenery/other/" + obj.subClass + ".ai",
           "scenery/other/" + obj.typeName + ".ai"})
      if (fences.selectFilter(file)) { installed = true; break; }
    if (!installed) {
      describeMissing(obj.subClass + ".ai");
      SDL_Log("[Save] original filter %s/%s is not installed", obj.subClass.c_str(), obj.typeName.c_str());
      ++missed;
      continue;
    }
    const auto &kind = fences.filterType();
    int x = static_cast<int>(std::floor(obj.x / 64.0f - kind.footprintX * 0.5f + 0.001f));
    int y = static_cast<int>(std::floor(obj.y / 64.0f - kind.footprintY * 0.5f + 0.001f));
    if (fences.placeFilter(x, y, map)) {
      auto *filter = fences.filter(static_cast<int>(fences.filters().size()) - 1);
      if (obj.filterHealth >= 0)
        filter->health = static_cast<float>(std::clamp(obj.filterHealth, 0, kind.startHealth));
      if (!obj.name.empty()) filter->name = obj.name;
      filter->upkeepLast = obj.filterCosts[0];
      filter->upkeepCurrent = obj.filterCosts[1];
      filter->upkeepTotal = obj.filterCosts[2];
#ifdef _WIN32
      FILETIME built{uint32_t(obj.filterBuilt), uint32_t(obj.filterBuilt >> 32)};
      SYSTEMTIME placed{};
      if (obj.filterBuilt && FileTimeToSystemTime(&built, &placed) && placed.wYear >= 2001)
        filter->placedMonth = (placed.wYear - 2001) * 12 + placed.wMonth - 1;
#endif
    } else {
      SDL_Log("[Save] original filter %s could not be restored at %d,%d", obj.name.c_str(), x, y);
      ++missed;
    }
  }
  fences.selectFilter(previous);
  return missed;
}

// Original dates are FILETIMEs in the zoo calendar, not wall-clock play time.
static double originalElapsed(const ZooReader &z, uint64_t then) {
  const auto *d = z.getDate();
  const int year = d[0], month = d[1], day = d[3];
  if (!then || year < 1601 || year > 9999 || month < 1 || month > 12 ||
      day < 1 || day > ZooSim::daysInMonth(month - 1, year - 2000)) return 0;
  auto leaps = [](int y) { return y / 4 - y / 100 + y / 400; };
  int64_t days = int64_t(year - 1601) * 365 + leaps(year - 1) - leaps(1600);
  for (int m = 1; m < month; ++m) {
    static const int lengths[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    days += lengths[m - 1] + (m == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)));
  }
  const uint64_t seconds = uint64_t(days + day - 1) * 86400 + d[4] * 3600 + d[5] * 60 + d[6];
  const uint64_t now = seconds * 10000000ULL + uint64_t(d[7]) * 10000;
  return then <= now ? double(now - then) / 10000000.0 / 86400.0 * ZooSim::kSecondsPerDay : 0;
}
static void originalFacing(uint32_t direction, float &x, float &y) {
  const float angle = (direction & 7) * 3.14159265358979323846f / 4;
  x = std::sin(angle); y = -std::cos(angle);
}

void SaveGame::restoreOriginalManagers(World &world, UiGameScreen &hud) {
  const auto &z = world.zooReader;
  if (!z.hasGame()) return;
  Research &research = Research::get();
  research.restart();
  // researchMgr::load (0x593c3c) matches the config's name IDs. The
  // kind-2 payload is the bit representation of a float, not an integer.
  for (const auto &record : z.getResearch())
    for (auto &branch : research.branches()) {
      if (record.kind == 0 && branch.nameId == record.id)
        branch.fundingLevel = std::clamp(static_cast<int>(record.value), 0,
                                       std::max(0, static_cast<int>(branch.funding.size()) - 1));
      for (size_t c = 0; c < branch.categories.size(); ++c) {
        auto &category = branch.categories[c];
        if (record.kind == 1 && category.nameId == record.id)
          branch.enabled[c] = record.value != 0;
        if (record.kind == 2)
          for (auto &program : category.programs)
            if (program.nameId == record.id) {
              float progress = 0;
              std::memcpy(&progress, &record.value, sizeof progress);
              if (std::isfinite(progress) && progress >= 0) {
                program.progress = progress;
                program.done = progress >= program.cost;
              }
            }
      }
    }
  for (auto &branch : research.branches()) research.pick(branch);
  hud.marketingLevel = std::clamp(z.getMarketing(), 0,
                                std::max(0, static_cast<int>(hud.marketing.size()) - 1));
  // awardsLoad stores the same target IDs used by the award goal action.
  std::vector<int> awards = z.getAwards();
  std::sort(awards.begin(), awards.end());
  awards.erase(std::unique(awards.begin(), awards.end()), awards.end());
  if (goals) goals->awards = awards;
  hud.setAwards(awards);
}

void SaveGame::restoreOriginalBooks(World &world, ZooSim &sim) {
  const auto &z = world.zooReader;
  if (!z.hasGame()) return;
  const auto &books = z.getBooks();
  const int month = std::clamp(static_cast<int>(z.getDate()[1]) - 1, 0, 11);
  const int year = std::clamp(static_cast<int>(z.getDate()[0]) - 2000, 1, 9999);
  sim.months.clear();
  sim.importedAnnual = books.yearly; sim.importedTotals = books.total;
  // The original retains a rolling 12 months, in January..December slots.
  // Its finance panel (0x52f820, tables at 0x635a90) combines the rows below.
  const std::vector<std::vector<int>> rows = {
      {4}, {5}, {8,20}, {6,7}, {30}, {9,10},
      {3}, {2}, {0,1,28}, {11,12,13}, {19}, {29}};
  for (int ago = std::min(12, (year - 1) * 12 + month + 1) - 1; ago >= 0; --ago) {
    const int serial = (year - 1) * 12 + month - ago;
    ZooSim::Month entry; entry.year = serial / 12 + 1; entry.month = serial % 12;
    for (size_t line = 0; line < rows.size(); ++line)
      for (int row : rows[line])
        if (row < static_cast<int>(books.monthly.size())) {
          const float value = books.monthly[row][entry.month];
          if (std::isfinite(value))
            entry.lines[line] += line >= ZooSim::Construction ? -value : value;
        }
    // Row 14 is the net total, already represented by these income/expense
    // rows; adding it would count every transaction twice.
    entry.rating = sim.zooRating;
    sim.months.push_back(entry);
  }
  const auto *d = z.getDate();
  sim.day = std::clamp(static_cast<int>(d[3]) - 1, 0, ZooSim::daysInMonth(month, year) - 1) +
            (d[4] * 3600 + d[5] * 60 + d[6]) / 86400.0;
  // Compute elapsed calendar days for monthly unlocks without replaying
  // charges, admissions or notifications from the original zoo.
  sim.dayCount = 0;
  for (int y = 1; y < year; ++y) for (int m = 0; m < 12; ++m) sim.dayCount += ZooSim::daysInMonth(m, y);
  for (int m = 0; m < month; ++m) sim.dayCount += ZooSim::daysInMonth(m, year);
  sim.dayCount += static_cast<long>(sim.day);
  sim.dirty = true;
}

bool SaveGame::loadOriginal(World &world, ZooSim &sim, UiGameScreen &hud) {
  if (!world.zooReader.hasGame()) {
    saveFailure = "This original save has an unsupported or incomplete game-state section.";
    return false;
  }
  if (!loadOriginalWorld(world))
    return false;
  const ZooReader &z = world.zooReader;
  restoreOriginalManagers(world, hud);
  // The books: the cash (gameMgr +0xc), the date (+0x1194), the admission,
  // the rating
  if (z.hasGame()) {
    sim.money = z.getCash();
    sim.zooRating = z.getRating();
    restoreOriginalBooks(world, sim);
    UiGameScreen::ZooInfo info = hud.zooInfo();
    info.admission = z.getAdmission();
    info.cash = static_cast<long>(sim.money);
    hud.setZooInfo(info);
  }
  return true;
}

// A mod may rename a stock species, and a new species may reuse an old name id.
// Resolve original species references from stock definitions, never mod ids.
static int originalSpeciesName(ResourceManager *rm,const Animals::Type &type) {
  if(!rm->availableInGame(type.file))return 0;
  if(rm->modOf(type.file).empty())return type.nameId;
  const auto text=rm->gameTextWithoutMods(type.file);
  if(text.empty())return 0;
  IniReader stock(const_cast<char*>(text.data()),text.size());
  const auto subtype=stock.get("global","defaultsubtype","m");
  return stock.getInt(subtype+"/characteristics/integers","cnameid",
                      stock.getInt("characteristics/integers","cnameid",0));
}

// A replaced stock species may add engine-only tricks. Only project a script
// when every behavior and required item still matches the original definition.
static bool originalShowScript(ResourceManager *rm,Shows &shows,const Shows::Show &show) {
  if(show.script.empty()||!rm->availableInGame(show.species))return false;
  if(rm->modOf(show.species).empty())return true;
  const auto text=rm->gameTextWithoutMods(show.species);
  if(text.empty())return false;
  IniReader stock(const_cast<char*>(text.data()),text.size());
  std::string base="m/tricks";
  if(stock.getList(base,"trick").empty())base=Utils::string_to_lower(stock.get("global","defaultsubtype","m"))+"/tricks";
  const auto keys=stock.getList(base,"trick");
  const auto &definitions=shows.tricks(show.species);
  for(const auto &key:show.script) {
    if(std::none_of(keys.begin(),keys.end(),[&](const auto &value){return Utils::string_to_lower(value)==key;}))return false;
    const auto found=std::find_if(definitions.begin(),definitions.end(),[&](const auto &t){return t.key==key;});
    if(found==definitions.end())return false;
    const auto &t=*found;const auto section=base+"/"+key;
    if(stock.getInt(section,"id",0)!=t.id||stock.getInt(section,"type",0)!=t.kind||
       stock.getInt(section,"building",0)!=t.building||
       stock.getInt(section,"minimumdepth",0)!=t.depth||
       Utils::string_to_lower(stock.get(section,"name",key))!=t.behavior||
       Utils::string_to_lower(stock.get(section,"keeperpretrick"))!=t.pre||
       Utils::string_to_lower(stock.get(section,"keeperposttrick"))!=t.post)return false;
  }
  return true;
}

static std::string originalThoughtText(const ZooReader &z,const ZooReader::Thought &thought,ResourceManager *rm) {
  std::string text=rm->getString(int(thought.text));
  const auto at=text.find("%s");
  if(at!=std::string::npos) {
    const auto &objects=z.getObjects();
    const auto subject=std::find_if(objects.begin(),objects.end(),[&](const auto &o){return o.id==thought.subject;});
    std::string argument=subject==objects.end()?std::string():subject->name;
    if(argument.empty()&&thought.habitatX>=0&&thought.habitatY>=0)
      for(const auto &habitat:z.getHabitats())if(habitat.x==thought.habitatX&&habitat.y==thought.habitatY) {
        argument=habitat.name;break;
      }
    if(argument.empty())return {}; // never show an unexpanded placeholder
    text.replace(at,2,argument);
  }
  if(text.find('%')!=std::string::npos)return {};
  return text;
}

bool SaveGame::loadOriginalWorld(World &world) {
  saveFailure.clear();
  const ZooReader &z = world.zooReader;
  for(auto &object:world.placedObjects.list)
    for(const auto &record:z.getObjects())if(record.id==uint32_t(object.id) && record.className=="building" &&
        record.commonEnd && record.commonEnd+20<=record.payload.size()) {
      const auto *p=record.payload.data()+record.commonEnd;
      auto word=[&](size_t at){uint32_t n=0;if(record.commonEnd+at+4<=record.payload.size())std::memcpy(&n,p+at,4);return n;};
      const size_t tail=20+size_t(word(16))*8;
      if(tail+28>record.payload.size()-record.commonEnd)break;
      object.visitorsTotal=int(word(4));object.income=word(8);
      std::memcpy(&object.price,p+12,4);object.fill=int(word(tail));
      object.visitorsNow=int(word(tail+12));object.visitorsLast=int(word(tail+16));
      if(z.getVersion()>=68)object.upkeep=word(tail+28);
      break;
    }
  if (!z.isExact()) return false;
  for (const auto &record : z.getObjects()) {
    if (!record.decoded) continue;
    std::string required;
    if (record.className == "fences" || record.className == "tankwall") {
      if (world.fences.typeIndex(record.subClass) < 0) required = record.subClass + ".ai";
    } else if (record.className == "paths") {
      required = "paths/" + record.typeName + ".ai";
      if (world.resourceManager->hasResource(required)) required.clear();
    } else if (record.className == "building" || record.className == "objects") {
      // Registry classes need not match their definition's resource folder
      // (the original gate is building/building/aqgate but lives in other).
      const bool livePile=record.typeName=="poo"||record.typeName=="dinopoo"||record.typeName=="trash";
      if (!livePile && !world.placedObjects.byId(int(record.id)))
        required = record.typeName + ".ai";
    }
    if (!required.empty()) { describeMissing(required); return false; }
  }
  WorldMap &map = world.worldMap;
  Fences &f = world.fences;
  // The habitats: the map path keeps fenced ground as pockets; a saved zoo's
  // are exhibits (habitatMgrLoad 0x4c7e6c names each by a tile in it). Its
  // tanks' ground is sunk already: the heights are kept as the save has them
  std::vector<std::array<int, 6>> heights;
  for (int y = 0; y < map.height; y++)
    for (int x = 0; x < map.width; x++) {
      const MapTile &t = map.tiles[y][x];
      heights.push_back({t.height, t.cornerHeight[0], t.cornerHeight[1], t.cornerHeight[2], t.cornerHeight[3], t.terrainType});
    }
  f.exhibitList.clear();
  f.made = 0;
  f.updateExhibits(map, 1, 0, 1, false);
  for (int y = 0, k = 0; y < map.height; y++)
    for (int x = 0; x < map.width; x++, k++) {
      MapTile &t = map.tiles[y][x];
      t.height = heights[k][0];
      t.terrainType = uint8_t(heights[k][5]);
      for (int c = 0; c < 4; c++)
        t.cornerHeight[c] = heights[k][1 + c];
    }
  map.touch();
  int named = 0;
  for (Fences::Exhibit &ex : f.exhibitList) {
    const ZooReader::Habitat *hab = nullptr;
    for (const ZooReader::Habitat &h : z.getHabitats())
      if (ex.tiles.count({h.x, h.y}))
        hab = &h;
    if (!hab) {
      // (no habitat of the save's: ground the walls shut off)
      ex.pocket = true;
      ex.named = false;
      continue;
    }
    ex.name = hab->name;
    ex.named = true;
    named++;
    if (ex.tank) {
      // The floor as the save has it; the ground round it; its walls and
      // water (in half steps over the floor: 5 and 4 is a new tank's)
      int floor = INT32_MAX, ground = INT32_MIN;
      for (auto [x, y] : ex.tiles)
        if (const MapTile *t = map.getTile(x, y))
          for (int c = 0; c < 4; c++)
            floor = std::min(floor, static_cast<int>(t->cornerHeight[c]));
      for (auto [x, y] : ex.tiles)
        for (auto [dx, dy] : {std::make_pair(1, 0), std::make_pair(-1, 0), std::make_pair(0, 1), std::make_pair(0, -1)})
          if (!ex.tiles.count({x + dx, y + dy}))
            if (const MapTile *t = map.getTile(x + dx, y + dy))
              for (int c = 0; c < 4; c++)
                ground = std::max(ground, static_cast<int>(t->cornerHeight[c]));
      // makeTank saw already excavated original terrain. It cannot supply
      // the pre-tank snapshot: use restoreGround's documented level-grass
      // fallback, rather than recording the sunken floor as the old ground.
      ex.before.clear();
      ex.floorHeight = floor;
      ex.groundHeight = ground > INT32_MIN ? ground : floor + 2;
      ex.wallSub = std::max(1, hab->tankHeight);
      ex.water = hab->tankHeight > 1 ? std::clamp(hab->waterLevel / static_cast<float>(hab->tankHeight - 1), 0.0f, 1.0f) : 0.0f;
      ex.salt = hab->salt;
      ex.purity = static_cast<float>(std::clamp(hab->purity, 0, 100));
      ex.filling = true;
      ex.level = -1e9f;
    }
  }
  const int filterMisses = restoreOriginalFilters(z, f, map);
  // Food and dung down where they lie, with what's left of them
  ZooItems &items = world.items;
  int piles = 0;
  for (const ZooReader::ZooObject &o : z.getObjects()) {
    if (!o.decoded || (o.className != "food" && o.typeName != "poo" && o.typeName != "dinopoo" && o.typeName != "trash"))
      continue;
    float x = o.x / 64.0f, y = o.y / 64.0f;
    int ex = f.exhibitAt(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
    bool dung = o.typeName == "poo" || o.typeName == "dinopoo";
    const bool litter=o.typeName=="trash";
    items.add(dung ? ZooItems::Kind::Dung : litter ? ZooItems::Kind::Litter : ZooItems::Kind::Food, ex, x, y);
    ZooItems::Item &it = items.list.back();
    if (!dung && !litter) {
      it.food = o.subClass == "food" ? o.typeName : o.subClass;
      it.units = it.full = static_cast<float>(std::max(0, o.food));
    }
    piles++;
  }
  // The animals, as they were (ZTAnimal::load 0x455533: their status)
  Animals &an = world.animals;
  std::vector<int> stockSpecies;
  for(const auto &type:an.typeList)stockSpecies.push_back(originalSpeciesName(world.resourceManager,type));
  int animals = 0, staffCount = 0, missed = filterMisses;
  for (const ZooReader::ZooObject &o : z.getObjects()) {
    if (!o.hasAnimal)
      continue;
    int type = an.typeOfFile("animals/" + o.subClass + ".ai");
    int id = type >= 0 ? an.adopt(type, o.animal.female, o.x / 64.0f, o.y / 64.0f, map, f, true) : -1;
    if (id < 0) {
      if (type < 0) describeMissing(o.subClass + ".ai");
      SDL_Log("[Save] the original's %s \"%s\" couldn't be put back at %.1f, %.1f", o.subClass.c_str(), o.name.c_str(),
              o.x / 64.0f, o.y / 64.0f);
      missed++;
      continue;
    }
    for (Animals::Member &m : an.list)
      if (m.id == id) {
        if (!o.name.empty())
          m.name = o.name;
        if (o.id > 0 && o.id < static_cast<uint32_t>(INT32_MAX)) {
          m.id = static_cast<int>(o.id); an.nextId = std::max(an.nextId, m.id + 1);
        }
        originalFacing(o.direction, m.fx, m.fy);
        m.baby = o.typeName == "y";
        m.dirty = static_cast<float>(o.animal.dung);
        m.sick = o.animal.sick; m.sleeping = o.animal.sleeping;
        m.dying = o.animal.dying;
        if (o.animal.hasLife) m.life = o.animal.life;
        m.grow = o.animal.grow; m.ready = o.animal.ready;
        m.hungerCheck = o.animal.checks[0]; m.healthCheck = o.animal.checks[1];
        m.energyCheck = o.animal.checks[2]; m.otherCheck = o.animal.checks[3];
        m.captivityCheck = o.animal.checks[5]; m.habitatCheck = o.animal.checks[6];
        m.boredCheck = o.animal.checks[7]; m.socialCheck = o.animal.checks[8];
        m.reproductionCheck = o.animal.checks[9];
        m.preyCheck = m.huntCheck = o.animal.chaseCheck;
        m.lastAte = an.now - originalElapsed(z, o.animal.lastAte);
        m.lastSlept = an.now - originalElapsed(z, o.animal.lastSlept);
        m.rampaging = o.animal.rampaging; m.rampageTicks = o.animal.rampageTicks;
        m.happiness = static_cast<float>(std::clamp(o.animal.happiness, -100, 100));
        m.hunger = static_cast<float>(o.animal.hunger);
        m.health = static_cast<float>(o.animal.health);
        m.energy = static_cast<float>(o.animal.energy);
        m.food = 100 - m.hunger;
        m.boxed = o.animal.crated;
        // (loose: the save's escape - its flag, or standing in no exhibit;
        // still an egg only if the save says so)
        m.escaped = o.animal.escaped || m.exhibit < 0;
        if (o.animal.egg && an.typeList[m.type].laysEggs) {
          an.becomeEgg(m); m.hatchIn = o.animal.hatchIn;
          m.health = static_cast<float>(o.animal.health); m.grow = o.animal.grow;
        }
        if (m.dying) {
          m.stack.clear(); m.path.clear(); m.stepping = false;
          m.dieAfter = an.set(m, "bdie") ? 1 : 0;
          if (m.dieAfter) an.pushSet(m, "bdie");
        }
      }
    animals++;
  }
  an.markDirty();
  // The staff, where they stood (ZTStaff 0x455370 and its kinds)
  Staff &st = world.staff;
  for (const ZooReader::ZooObject &o : z.getObjects()) {
    if (!o.decoded || (o.className != "keeper" && o.className != "maint" && o.className != "tour" &&
                       o.className != "staff" && o.className != "helicopter"))
      continue;
    int type = st.typeOfFile("staff/" + o.subClass + ".ai");
    int id = type >= 0 ? st.hire(type, o.x / 64.0f, o.y / 64.0f, map, f) : -1;
    if (id < 0 && type >= 0) {
      // An original save may catch a specialist halfway up a ladder, or a
      // keeper inside an exhibit. Its private task stack cannot be resumed;
      // restart on accessible ground, preferably beside that exhibit's gate.
      // New-hire placement rules must not discard an existing saved worker.
      std::vector<std::pair<float, float>> candidates;
      const int occupied = f.exhibitAt(o.tileX(), o.tileY());
      for (const auto &[edge, piece] : f.pieces()) if (piece.gate) {
        const int ax = edge.alongX ? edge.x : edge.x-1;
        const int ay = edge.alongX ? edge.y-1 : edge.y;
        if (occupied >= 0 && f.exhibitOf(edge) != occupied) continue;
        for (auto point : {std::make_pair(ax,ay),std::make_pair(edge.x,edge.y)})
          if (f.exhibitAt(point.first,point.second) < 0)
            candidates.emplace_back(point.first+0.5f,point.second+0.5f);
      }
      auto distance = [&](const auto &point) {
        const float dx=point.first-o.x/64.0f,dy=point.second-o.y/64.0f;
        return dx*dx+dy*dy;
      };
      auto nearest = [&] {
        std::stable_sort(candidates.begin(),candidates.end(),[&](const auto &a,const auto &b) {return distance(a)<distance(b);});
        for (auto [x,y] : candidates)
          if ((id=st.hire(type,x,y,map,f)) >= 0) break;
      };
      nearest();
      if (id < 0) {
        candidates.clear();
        for (int y=0;y<map.height;++y) for (int x=0;x<map.width;++x)
          if (st.canPlaceType(type,x+0.5f,y+0.5f,map,f)==Fences::Fit::Ok)
            candidates.emplace_back(x+0.5f,y+0.5f);
        nearest();
      }
    }
    if (id < 0) {
      if (type < 0) describeMissing(o.subClass + ".ai");
      SDL_Log("[Save] the original's %s \"%s\" couldn't be put back at %.1f, %.1f", o.subClass.c_str(), o.name.c_str(),
              o.x / 64.0f, o.y / 64.0f);
      missed++;
      continue;
    }
    if (Staff::Member *m = st.member(id)) {
      if (!o.name.empty()) m->name = o.name;
      if (o.id > 0 && o.id < static_cast<uint32_t>(INT32_MAX)) {
        m->id = static_cast<int>(o.id); st.nextId = std::max(st.nextId, m->id + 1);
      }
      originalFacing(o.direction, m->fx, m->fy);
      m->female = o.typeName == "f";
      if (!o.colours.empty()) m->hair = o.colours[0];
      if (o.colours.size() > 1) m->skin = o.colours[1];
      if (o.hasStaff) {
        std::copy(o.staff.duties.begin(), o.staff.duties.end(), m->duties);
        m->workCheck = static_cast<float>(o.staff.work);
        m->crowdClock = static_cast<float>(o.staff.crowd);
        for (auto [x, y] : o.staff.exhibits) {
          const int ex = f.exhibitAt(x, y);
          if (ex >= 0 && std::find(m->exhibits.begin(), m->exhibits.end(), ex) == m->exhibits.end())
            m->exhibits.push_back(ex);
        }
      }
    }
    staffCount++;
  }
  // Original guest records were decoded but discarded. Restore visitors
  // without calling admit(): they have already paid their entrance fee.
  // Original task/route pointers belong to zoo.exe; let our AI choose a
  // fresh task after a short stand, as for native save restoration.
  Guests &gs = world.guests;
  gs.clear();
  int guestCount = 0;
  for (const ZooReader::ZooObject &o : z.getObjects()) {
    if (!o.decoded || !o.hasGuest)
      continue;
    int type = -1;
    for (size_t k = 0; k < gs.typeList.size(); ++k)
      if (gs.typeList[k].key == o.typeName || gs.typeList[k].key == o.subClass)
        type = static_cast<int>(k);
    if (type < 0 || o.id >= static_cast<uint32_t>(INT32_MAX) ||
        !map.getTile(o.tileX(), o.tileY())) {
      SDL_Log("[Save] original guest %s/%s id %u could not be restored", o.subClass.c_str(), o.typeName.c_str(), o.id);
      missed++;
      continue;
    }
    Guests::Guest g;
    g.id = static_cast<int>(o.id);
    g.type = type;
    g.name = o.name.empty() ? "Guest " + std::to_string(g.id) : o.name;
    g.x = o.x / 64.0f;
    g.y = o.y / 64.0f;
    g.speed = gs.typeList[type].speed;
    g.arrived = gs.clock - originalElapsed(z, o.guest.arrived);
    originalFacing(o.direction, g.fx, g.fy);
    g.hungerCheck = o.guest.checks[0]; g.thirstCheck = o.guest.checks[1];
    g.bathroomCheck = o.guest.checks[2]; g.leaveCheck = o.guest.checks[3];
    g.souvenirCheck = o.guest.checks[4]; g.energyCheck = o.guest.checks[5];
    g.trashCheck = o.guest.checks[6]; g.viewCheck = o.guest.checks[8];
    g.chaseCheck = o.guest.chaseCheck;
    g.leaving = o.guest.leaving; g.sawEscape = o.guest.afraid; g.sick = o.guest.sick;
    std::copy(o.guest.items.begin(), o.guest.items.end(), g.inventory);
    for (size_t k = 0; k < an.typeList.size(); ++k)
      if (stockSpecies[k]>0&&stockSpecies[k] == o.guest.favourite)
        g.favourite = static_cast<int>(k);
    // The recent-species list holds cNameIDs (0x42e527/0x425b6d),
    // while the separate visit list holds grid coordinates (0x42281c).
    for(auto [x,y]:o.guest.visited) {
      bool building=false;
      for(const auto &record:z.getObjects())if(record.tileX()==x&&record.tileY()==y&&
          record.className=="building"&&world.placedObjects.byId(int(record.id))) {
        g.visited.insert(int(record.id));building=true;
      }
      if(!building) {const int exhibit=f.exhibitAt(x,y);if(exhibit>=0)g.seen.insert(exhibit);}
    }
    g.happiness = static_cast<float>(std::clamp(o.guest.happiness, -100, 100));
    g.hunger = static_cast<float>(std::clamp(o.guest.hunger, 0, 100));
    g.thirst = static_cast<float>(std::clamp(o.guest.thirst, 0, 100));
    g.tired = static_cast<float>(std::clamp(o.guest.energy, 0, 100));
    g.bathroom = static_cast<float>(std::clamp(o.guest.bathroom, 0, 100));
    for (size_t k = 0; k < 4 && k < o.colours.size(); ++k)
      g.colours[k] = o.colours[k];
    for(const auto &thought:z.getThoughts())if(thought.owner==o.id) {
      const auto text=originalThoughtText(z,thought,world.resourceManager);
      if(text.empty()||std::find(g.thoughts.begin(),g.thoughts.end(),text)!=g.thoughts.end())continue;
      if(g.thoughts.empty())g.lastThought=int(thought.text);
      g.thoughts.push_back(text);
      if(g.thoughts.size()==10)break;
    }
    g.playFor = 0.5f;
    gs.nextId = std::max(gs.nextId, g.id + 1);
    gs.list.push_back(std::move(g));
    guestCount++;
  }
  // Saved programs are shared by id in ZTShowMgr; habitats select one per
  // species. Rebuild schedules, while original in-flight tasks return safely.
  world.shows.clear();world.shows.refresh();
  for(const auto &hab:z.getHabitats())if(hab.show) {
    const int tank=f.exhibitAt(hab.x,hab.y);
    auto *show=world.shows.show(tank);if(!show)continue;
    const ZooReader::ShowProgram *program=nullptr;
    const ZooReader::Habitat::ShowAssignment *selected=nullptr;
    for(uint32_t species:hab.showSpecies)
      for(const auto &assignment:hab.showAssignments)if(assignment.species==species)
        for(const auto &candidate:z.getShowPrograms())if(candidate.id==assignment.program) {
          program=&candidate;selected=&assignment;break;
        }
    if(!program)continue;
    for(const auto &type:an.typeList)if(stockSpecies[&type-an.typeList.data()]>0&&stockSpecies[&type-an.typeList.data()]==program->species) {
      show->species=type.file;show->script.clear();
      for(const auto &saved:program->tricks)
        for(const auto &trick:world.shows.tricks(type.file))if(trick.id==saved.id) {
          show->script.push_back(trick.key);break;
        }
      show->frequency=-1;
      if(hab.showInterval>0) {
        show->frequency=0;
        for(int k=1;k<3;++k)
          if(std::abs(int64_t(hab.showInterval)-world.shows.settings.interval[k])<
             std::abs(int64_t(hab.showInterval)-world.shows.settings.interval[show->frequency]))show->frequency=k;
      }
      if(selected&&std::isfinite(selected->admission))
        show->admission=std::clamp(double(selected->admission),world.shows.settings.admissionMin,world.shows.settings.admissionMax);
      show->clock=0;
      break;
    }
  }
  world.shows.linkStands();
  world.reindex();
  SDL_Log("[Save] the original's saved zoo: %d habitats named, %d piles, %d animals, %d staff, %d guests (%d couldn't be put back)",
          named, piles, animals, staffCount, guestCount, missed);
  if (missed && saveFailure.empty()) saveFailure = "A saved animal, staff member or filter could not be restored. Your current zoo is unchanged.";
  return missed == 0;
}

std::string SaveGame::checkOriginalImport(World &world) {
  const ZooReader &z = world.zooReader;
  if (!z.isExact())
    return "Load an original saved zoo in the Test Zoo first.";
  int passed = 0, total = 0;
  std::string out;
  auto check = [&](const std::string &name, bool ok) {
    ++total;
    if (ok) ++passed;
    out += std::string(ok ? "PASS " : "FAIL ") + name + "\n";
  };
  const Guests &gs = world.guests;
  int expected = 0, maxId = 0;
  std::set<int> ids;
  for (const auto &o : z.getObjects()) {
    if (!o.decoded || !o.hasGuest) continue;
    ++expected;
    const auto *g = gs.guest(static_cast<int>(o.id));
    check("guest " + o.name + " restored with original id", g != nullptr);
    if (!g) continue;
    maxId = std::max(maxId, g->id);
    ids.insert(g->id);
    check(o.name + " kind and name", gs.typeList[g->type].key == (o.typeName.empty() ? o.subClass : o.typeName) && g->name == o.name);
    check(o.name + " position", g->x == o.x / 64.0f && g->y == o.y / 64.0f);
    check(o.name + " five status values",
          g->happiness == std::clamp(o.guest.happiness, -100, 100) &&
          g->hunger == std::clamp(o.guest.hunger, 0, 100) &&
          g->thirst == std::clamp(o.guest.thirst, 0, 100) &&
          g->tired == std::clamp(o.guest.energy, 0, 100) &&
          g->bathroom == std::clamp(o.guest.bathroom, 0, 100));
    bool colours = true;
    for (size_t k = 0; k < 4 && k < o.colours.size(); ++k)
      colours = colours && g->colours[k] == o.colours[k];
    check(o.name + " saved clothing colours", colours);
    check(o.name + " inventory and leaving state", std::equal(o.guest.items.begin(), o.guest.items.end(), g->inventory) &&
          g->leaving == o.guest.leaving && g->sick == o.guest.sick);
    check(o.name + " original arrival date", std::abs((gs.clock - g->arrived) - originalElapsed(z, o.guest.arrived)) < 0.0001);
    std::vector<std::string> thoughts;
    for(const auto &thought:z.getThoughts())if(thought.owner==o.id) {
      const auto text=originalThoughtText(z,thought,world.resourceManager);
      if(!text.empty()&&std::find(thoughts.begin(),thoughts.end(),text)==thoughts.end())thoughts.push_back(text);
      if(thoughts.size()==10)break;
    }
    check(o.name+" original thoughts restore newest first",g->thoughts==thoughts);
    std::set<int> visited,seenExhibits;
    for(auto [x,y]:o.guest.visited) {
      bool building=false;
      for(const auto &record:z.getObjects())if(record.className=="building"&&record.tileX()==x&&record.tileY()==y&&
          world.placedObjects.byId(int(record.id))) {visited.insert(int(record.id));building=true;}
      if(!building) {const int ex=world.fences.exhibitAt(x,y);if(ex>=0)seenExhibits.insert(ex);}
    }
    check(o.name+" recent visit coordinates map to existing targets",g->visited==visited&&g->seen==seenExhibits);
    check(o.name + " saved need/check timers", g->hungerCheck == o.guest.checks[0] &&
          g->thirstCheck == o.guest.checks[1] && g->bathroomCheck == o.guest.checks[2] &&
          g->leaveCheck == o.guest.checks[3] && g->viewCheck == o.guest.checks[8]);
    if (o.guest.favourite>0)
      check(o.name + " favourite species", g->favourite >= 0 && gs.typeList.size() > 0 &&
            originalSpeciesName(world.resourceManager,world.animals.typeList[g->favourite]) == o.guest.favourite);
  }
  check("every original guest restored exactly once",
        static_cast<int>(gs.list.size()) == expected && static_cast<int>(ids.size()) == expected);
  check("future guest ids cannot collide", gs.nextId > maxId);
  check("restored guests are not charged admission again", gs.income == 0 && gs.arrivals == 0);
  for (const auto &o : z.getObjects()) {
    if (!o.decoded || !o.hasAnimal) continue;
    const Animals::Member *found = nullptr;
    for (const auto &m : world.animals.members())
      if (m.name == o.name) found = &m;
    check("animal " + o.name + " age category",
          found && found->baby == (o.typeName == "y"));
    check("animal " + o.name + " original identity/status", found && found->id == static_cast<int>(o.id) &&
          found->sick == o.animal.sick && found->rampaging == o.animal.rampaging &&
          found->dirty == o.animal.dung && found->grow == o.animal.grow && found->ready == o.animal.ready);
    if (o.animal.hasLife) check("animal " + o.name + " remaining lifetime", found && found->life == o.animal.life);
    if (o.animal.egg) check("animal " + o.name + " hatch timer", found && found->egg && found->hatchIn == o.animal.hatchIn);
  }
  for (const auto &o : z.getObjects()) if (o.hasStaff) {
    const auto *worker = world.staff.member(int(o.id));
    check("original worker " + o.name + " identity and safe task restart", worker &&
          worker->name == o.name && worker->job == Staff::Job::None &&
          worker->showTank < 0 && !worker->climbing && worker->tank < 0);
  }
  // A saved damaged fence must not become new, and a saved pump must
  // become serviceable equipment rather than a decorative sprite.
  ZooReader fixture = z;
  auto &records = const_cast<std::vector<ZooReader::ZooObject> &>(fixture.getObjects());
  records.erase(std::remove_if(records.begin(), records.end(),
                [](const auto &o) { return o.className == "filter"; }), records.end());
  for (auto &o : records) if (o.hasCondition) o.life = 1;
  WorldMap ground;
  ground.makeBlank(z.getMapWidth(), z.getMapHeight(), 0);
  Fences imported;
  imported.loadTypes(world.resourceManager);
  imported.load(fixture, ground);
  imported.exhibitList.clear();
  imported.made = 0;
  imported.updateExhibits(ground, 1, 0, 1, false);
  auto markHabitats = [&](Fences &fn) {
    for (auto &ex : fn.exhibitList) {
      bool saved = false;
      for (const auto &habitat : z.getHabitats())
        saved = saved || ex.tiles.count({habitat.x, habitat.y}) != 0;
      ex.pocket = !saved; // ordinary zoo grounds are not an exhibit
      ex.named = saved;
    }
  };
  markHabitats(imported);
  bool condition = true;
  int fencesChecked = 0;
  for (const auto &[edge, piece] : imported.pieces()) {
    condition = condition && piece.life == 1;
    ++fencesChecked;
  }
  check("saved fence wear is retained", fencesChecked > 0 && condition);
  int pumpX = -1, pumpY = -1;
  imported.selectFilter("scenery/other/filter1.ai");
  for (int y = 0; y < ground.height && pumpX < 0; ++y)
    for (int x = 0; x < ground.width && pumpX < 0; ++x)
      if (imported.filterFit(x, y, ground) == Fences::Fit::Ok) pumpX = x, pumpY = y;
  const bool hasTank = std::any_of(z.getHabitats().begin(), z.getHabitats().end(),
                                 [](const auto &habitat) { return habitat.tank; });
  if (hasTank) check("filter import fixture has a valid tank-side tile", pumpX >= 0);
  else out += "SKIP tank filter fixture: this original map has no tank\n";

  if (pumpX >= 0) {
    ZooReader::ZooObject pump;
    pump.className = "filter"; pump.subClass = "filter1"; pump.typeName = "f";
    pump.decoded = true; pump.filterHealth = 1; pump.name = "Imported worn filter";
    pump.filterCosts = {50, 100, 350};
    pump.x = pumpX * 64 + 32; pump.y = pumpY * 64 + 32;
    records.push_back(pump);
    WorldMap pumpGround;
    pumpGround.makeBlank(z.getMapWidth(), z.getMapHeight(), 0);
    imported.load(fixture, pumpGround);
    imported.exhibitList.clear();
    imported.made = 0;
    imported.updateExhibits(pumpGround, 1, 0, 1, false);
    markHabitats(imported);
    restoreOriginalFilters(fixture, imported, pumpGround);
    check("original filter becomes live equipment", imported.filters().size() == 1);
    if (!imported.filters().empty()) {
      auto &filter = *imported.filter(0);
      check("filter centre converts to correct grid tile", filter.x == pumpX && filter.y == pumpY);
      check("filter health and name retained", filter.health == 1 && filter.name == pump.name);
      check("filter last/current/lifetime costs retained", filter.upkeepLast == 50 && filter.upkeepCurrent == 100 && filter.upkeepTotal == 350);
      const auto &type = imported.filterType(filter.file);
      filter.decayClock = type.decayTime * 0.75f;
      filter.filterClock = type.filterDelay * 0.5f;
      const auto decay=filter.decayClock,clean=filter.filterClock;
      imported.serviceFilter(0);
      check("servicing a filter preserves its decay and cleaning clocks",
            filter.health==type.startHealth && filter.decayClock==decay && filter.filterClock==clean);
      filter.health = 1;
      if (auto *tank = imported.exhibit(filter.tank)) tank->water = 1;
      check("imported worn filter has decayed running state",
            imported.filterState(filter) == Fences::FilterState::Decayed);
    }
  }
  // Native world snapshots must load over a different-size stock/blank
  // bootstrap, without needing the original zoo file or its old dimensions.
  Out snapshot;
  putWorld(&snapshot, world);
  World bootstrap(world.resourceManager);
  const bool blank = bootstrap.loadBlank(32, 32);
  In snapshotIn{snapshot.b};
  const bool restored = blank && getWorld(&snapshotIn, bootstrap, kVersion);
  check("native world snapshot loads over a different-size bootstrap", restored);
  if (restored) {
    check("native snapshot keeps original dimensions", bootstrap.worldMap.width == world.worldMap.width && bootstrap.worldMap.height == world.worldMap.height);
    check("native snapshot keeps objects and boundary pieces", bootstrap.placedObjects.objects().size() == world.placedObjects.objects().size() && bootstrap.fences.pieces().size() == world.fences.pieces().size());
    check("native snapshot keeps inside/outside zoo boundaries", bootstrap.fences.inside == world.fences.inside);
    bool levels=true;
    for(const auto &ex:world.fences.exhibitList) if(ex.tank) {
      const auto *copy=bootstrap.fences.exhibit(ex.id);
      levels=levels&&copy&&copy->floorHeight==ex.floorHeight&&copy->wallSub==ex.wallSub&&copy->water==ex.water;
    }
    check("native tank floor, wall and water levels survive",levels);
    const bool restoreGround=Features::tankGroundBack;Features::tankGroundBack=true;
    for(auto &ex:bootstrap.fences.exhibitList)if(ex.tank&&!ex.tiles.empty()) {
      // An original-only file has no pre-excavation snapshot.
      const auto tiles=ex.tiles;const int ground=ex.groundHeight;
      check("original tank has no invented pre-excavation ground",ex.before.empty());
      Fences::Edge edge{};bool found=false;
      for(const auto &[at,piece]:bootstrap.fences.edges)if(piece.tank==ex.id||piece.tank2==ex.id) {edge=at;found=true;break;}
      if(found)bootstrap.fences.remove(edge,bootstrap.worldMap);
      bool restoredGround=found;
      for(auto [x,y]:tiles) {
        const auto *tile=bootstrap.worldMap.getTile(x,y);
        restoredGround=restoredGround&&tile&&tile->terrainType==0&&tile->height==ground;
        if(tile)for(int height:tile->cornerHeight)restoredGround=restoredGround&&height==ground;
      }
      check("demolishing imported tank uses level surrounding-ground fallback",restoredGround);
      break; // remove invalidates exhibit references; reload the untouched snapshot below.
    }
    Features::tankGroundBack=restoreGround;
    In reset{snapshot.b};check("native world reloads after tank-demolition probe",getWorld(&reset,bootstrap,kVersion));
  }

  // Exercise the real full-file v13 writer/reader, including non-default
  // values that the short marine fixtures may not happen to contain.
  if (restored) {
    UiGameScreen testHud(world.resourceManager, "", "");
    ZooSim testBooks; testBooks.start(12345, 73);
    restoreOriginalBooks(world, testBooks);
    const auto savedMonths = testBooks.months;
    const auto savedAnnual = testBooks.importedAnnual;
    const auto savedTotals = testBooks.importedTotals;
    check("original monthly history ends at its saved calendar month",
          !z.hasGame() || (!savedMonths.empty() && savedMonths.back().month == z.getDate()[1]-1 &&
                          savedMonths.back().year == z.getDate()[0]-2000));
    if (z.hasGame() && z.getBooks().monthly.size() >= 31) {
      const int month = z.getDate()[1]-1;
      const auto &rows = z.getBooks().monthly;
      const auto &lines = savedMonths.back().lines;
      check("original finance income rows match the original panel",
            lines[ZooSim::Admissions] == rows[4][month] &&
            lines[ZooSim::AdmissionsIncome] == rows[5][month] &&
            lines[ZooSim::Concessions] == double(rows[6][month])+rows[7][month] &&
            lines[ZooSim::ShowIncome] == rows[30][month]);
      check("original finance expenses use the original panel signs",
            lines[ZooSim::Construction] == -double(rows[3][month]) &&
            lines[ZooSim::AnimalPurchase] == -double(rows[2][month]) &&
            lines[ZooSim::Wages] == -double(rows[11][month])-rows[12][month]-rows[13][month] &&
            lines[ZooSim::Research] == -double(rows[19][month]) &&
            lines[ZooSim::Marketing] == -double(rows[29][month]));
    }
    const auto previousResearch = Research::get().branches();
    const auto previousAwards = goals ? goals->awards : std::vector<int>{};
    restoreOriginalManagers(world, testHud);
    for (const auto &record : z.getResearch()) {
      bool found = false, matches = true;
      for (const auto &branch : Research::get().branches()) {
        if (record.kind == 0 && branch.nameId == record.id) {
          found = true;
          matches = matches && branch.fundingLevel == std::clamp(static_cast<int>(record.value), 0,
                                   std::max(0, static_cast<int>(branch.funding.size()) - 1));
        }
        for (size_t ci = 0; ci < branch.categories.size(); ++ci) {
          const auto &category = branch.categories[ci];
          if (record.kind == 1 && category.nameId == record.id) {
            found = true; matches = matches && branch.enabled[ci] == (record.value != 0);
          }
          for (const auto &program : category.programs)
            if (record.kind == 2 && program.nameId == record.id) {
              float progress = 0; std::memcpy(&progress, &record.value, sizeof progress);
              found = true;
              matches = matches && program.progress == progress && program.done == (progress >= program.cost);
            }
        }
      }
      check("research record " + std::to_string(record.kind) + "/" + std::to_string(record.id),
            found && matches);
    }
    // Deliberately exercise disabled categories and partial progress even in
    // a fixture whose original manager was left at its defaults.
    for (auto &branch : Research::get().branches()) {
      if (!branch.enabled.empty()) branch.enabled[0] = false;
      branch.fundingLevel = std::min(1, std::max(0, static_cast<int>(branch.funding.size()) - 1));
      for (auto &category : branch.categories)
        for (auto &program : category.programs)
          if (program.cost > 1 && !program.done) { program.progress = 0.5f; break; }
    }
    const auto savedResearch = Research::get().branches();
    const auto temp = std::filesystem::temp_directory_path() /
        ("zt1-native-import-check-" + std::to_string(SDL_GetTicks64()) + ".zt1save");
    bool written = save(temp.string(), world, testBooks, testHud, "freeform/ff01.scn");
    bool loaded = written && load(temp.string(), bootstrap, testBooks, testHud);
    check("full native v"+std::to_string(kVersion)+" file round-trip", loaded);
    if (loaded) {
      std::vector<char> native;
      readSaveBytes(temp.string(), native, false);
      const auto badFile = temp.string() + ".invalid";
      auto rejectDefinition = [&](const std::string &label, const std::string &value) {
        if (value.empty()) return;
        Out key; key.str(value);
        auto bytes = native;
        auto at = std::search(bytes.begin(), bytes.end(), key.b.begin(), key.b.end());
        bool rejected = false;
        if (at != bytes.end()) {
          *(at + 4) = '!';
          if (atomicSave(badFile, bytes)) {
            rejected = !load(badFile, bootstrap, testBooks, testHud) &&
                       lastError().find("Missing content:") == 0;
          }
        }
        check("missing " + label + " rejects a native load", rejected);
      };
      if (!world.animals.list.empty())
        rejectDefinition("animal", world.animals.typeList[world.animals.list.front().type].file);
      if (!world.staff.list.empty())
        rejectDefinition("staff", world.staff.typeList[world.staff.list.front().type].file);
      if (!world.fences.edges.empty())
        rejectDefinition("fence", world.fences.typeList[world.fences.edges.begin()->second.type].key);
      for (int16_t path : world.worldMap.pathTile) if (path >= 0) {
        rejectDefinition("path", world.worldMap.pathTypes[path]); break;
      }
      for (const auto &object : world.placedObjects.list) if (!object.fence) {
        rejectDefinition("object", object.typeName); break;
      }
      auto rejectExtra = [&](const std::string &label, const std::function<void()> &insert) {
        const auto filters = world.fences.filterList;
        const auto items = world.items.list;
        const auto decks = world.walkways.decks;
        insert();
        const bool wrote = save(badFile, world, testBooks, testHud, "freeform/ff01.scn");
        world.fences.filterList = filters; world.items.list = items; world.walkways.decks = decks;
        check("missing " + label + " rejects a native load", wrote &&
              !load(badFile, bootstrap, testBooks, testHud) && lastError().find("Missing content:") == 0);
      };
      rejectExtra("filter", [&] {
        Fences::Filter filter; filter.file = "scenery/other/!missingfilter.ai";
        world.fences.filterList.push_back(filter);
      });
      rejectExtra("food", [&] {
        ZooItems::Item item; item.kind = ZooItems::Kind::Food; item.food = "!missingfood";
        world.items.list.push_back(item);
      });
      rejectExtra("walkway", [&] {
        Walkways::Deck deck; deck.type = "!missingpath"; world.walkways.set(10, 10, deck);
      });
      auto malformed = native;
      In header{malformed};header.raw<uint32_t>();header.raw<uint32_t>();header.str();
      const size_t months = header.at + 28;
      if (months + 4 <= malformed.size()) {
        const uint32_t count = UINT32_MAX;
        std::memcpy(malformed.data() + months, &count, 4);
        check("invalid month allocation count is rejected",
              atomicSave(badFile, malformed) && !load(badFile, bootstrap, testBooks, testHud));
      }
      malformed = native;malformed.resize(malformed.size()/2);
      check("truncated native state is rejected",
            atomicSave(badFile, malformed) && !load(badFile, bootstrap, testBooks, testHud));
      const auto blocked = badFile + ".blocked";
      std::error_code removeError;
      std::filesystem::create_directory(blocked, removeError);
      const auto marker = blocked + "/keep";
      const std::vector<char> sentinel = {'k','e','e','p'};
      const bool markerWritten = atomicSave(marker, sentinel);
      std::vector<char> preserved;
      check("failed replacement preserves an existing destination",
            markerWritten && !atomicSave(blocked, native) &&
            readSaveBytes(marker, preserved, false) && preserved == sentinel);
      std::filesystem::remove(marker, removeError);
      std::filesystem::remove(blocked, removeError);
      std::filesystem::remove(badFile, removeError);
      loaded = load(temp.string(), bootstrap, testBooks, testHud);
      check("valid native state still loads after rejected files", loaded);
    }
    if (loaded) {
      bool history = savedMonths.size() == testBooks.months.size();
      for (size_t m = 0; m < savedMonths.size() && history; ++m)
        history = savedMonths[m].month == testBooks.months[m].month &&
                  savedMonths[m].year == testBooks.months[m].year && savedMonths[m].lines == testBooks.months[m].lines;
      check("native save retains imported monthly and annual books",
            history && savedAnnual == testBooks.importedAnnual && savedTotals == testBooks.importedTotals);
      bool researchEqual = savedResearch.size() == Research::get().branches().size();
      for (size_t b = 0; b < savedResearch.size() && researchEqual; ++b) {
        const auto &before = savedResearch[b], &after = Research::get().branches()[b];
        researchEqual = before.enabled == after.enabled && before.fundingLevel == after.fundingLevel &&
                        before.currentCategory == after.currentCategory && before.currentProgram == after.currentProgram;
        for (size_t c = 0; c < before.categories.size() && researchEqual; ++c)
          for (size_t p = 0; p < before.categories[c].programs.size(); ++p)
            researchEqual = researchEqual &&
                before.categories[c].programs[p].progress == after.categories[c].programs[p].progress &&
                before.categories[c].programs[p].done == after.categories[c].programs[p].done;
      }
      check("native research funding, categories and progress", researchEqual);
      for (const auto &g : world.guests.list) {
        const auto *copy = bootstrap.guests.guest(g.id);
        check("native guest state " + std::to_string(g.id), copy &&
              copy->arrived == g.arrived && copy->favourite == g.favourite &&
              copy->leaving == g.leaving && copy->sick == g.sick &&
              std::equal(std::begin(g.inventory), std::end(g.inventory), copy->inventory) &&
              copy->hungerCheck == g.hungerCheck && copy->visited == g.visited && copy->seen==g.seen && copy->thoughts==g.thoughts);
      }
      if(!world.guests.list.empty()&&world.animals.typeList.size()>1) {
        World shifted(world.resourceManager);shifted.loadBlank(32,32);
        std::rotate(shifted.animals.typeList.begin(),shifted.animals.typeList.begin()+1,shifted.animals.typeList.end());
        const bool okay=load(temp.string(),shifted,testBooks,testHud);
        bool same=okay;
        for(const auto &g:world.guests.list) {
          const auto *copy=shifted.guests.guest(g.id);
          const std::string file=g.favourite>=0&&g.favourite<int(world.animals.typeList.size())?
              world.animals.typeList[g.favourite].file:std::string();
          same=same&&copy&&(file.empty()?copy->favourite<0:
              copy->favourite>=0&&shifted.animals.typeList[copy->favourite].file==file);
        }
        check("guest favourite species survives a reordered animal catalogue",same);
      }
      for (const auto &a : world.animals.list) {
        const auto *copy = bootstrap.animals.member(a.id);
        check("native animal state " + std::to_string(a.id), copy && copy->life == a.life &&
              copy->grow == a.grow && copy->ready == a.ready && copy->egg == a.egg &&
              copy->hatchIn == a.hatchIn && copy->sick == a.sick &&
              copy->rampaging == a.rampaging && copy->reproductionCheck == a.reproductionCheck);
      }
      for (const auto &s : world.staff.list) {
        const auto *copy = bootstrap.staff.member(s.id);
        check("native staff state " + std::to_string(s.id), copy && copy->female == s.female &&
              copy->hair == s.hair && copy->skin == s.skin && copy->exhibits == s.exhibits &&
              std::equal(std::begin(s.duties), std::end(s.duties), copy->duties) &&
              copy->workCheck == s.workCheck);
      }
      std::set<int> objectIds;
      bool ids = true;
      for (const auto &object : world.placedObjects.objects())
        ids = ids && object.id > 0 && objectIds.insert(object.id).second &&
              bootstrap.placedObjects.byId(object.id);
      check("original object identities remain unique through native save", ids);
      check("native save preserves the original compatibility archive", bootstrap.zooReader.originalBytes()==world.zooReader.originalBytes());
      const auto hybrid=temp.string()+".zoo";
      const auto decks=world.walkways.decks;
      Walkways::Deck engineDeck;engineDeck.type="path";
      for(int &h:engineDeck.h)h=7;
      world.walkways.set(10,10,engineDeck);
      const bool combinedSaved=save(hybrid,world,testBooks,testHud,"freeform/ff01.scn");
      check("combined .zoo saves successfully",combinedSaved);
      world.walkways.decks=decks;
      if(combinedSaved) {
        std::vector<char> combined;
        const bool read=readSaveBytes(hybrid,combined,false);
        check("combined save begins with original TZFB",read && combined.size()>8 && std::memcmp(combined.data(),"TZFB",4)==0);
        check("combined save selects the native engine loader",!isOriginal(hybrid) && mapOf(hybrid)=="freeform/ff01.scn");
        AssetBuffer originalPart;originalPart.data=combined.data();originalPart.size=combined.size();ZooReader originalDecoded;
        check("compatible stream parses completely",read && originalDecoded.load(originalPart) && originalDecoded.hasGame());
        const auto cameraBefore = world.worldRenderer.getCamera();
        const int turnBefore = world.worldRenderer.getRotation();
        for (float zoom : {0.5f,1.0f,2.0f}) for (int turn=0;turn<4;++turn) {
          world.worldRenderer.rotateView(turn-world.worldRenderer.getRotation());
          auto &cam=world.worldRenderer.getCamera();
          cam.zoom=zoom;cam.x=-1273;cam.y=-837;cam.screenCenterX=640;cam.screenCenterY=360;
          std::vector<uint8_t> encoded;std::string error;ZooReader decoded;
          bool ok=encodeOriginal(world,testBooks,testHud,encoded,error);
          AssetBuffer viewBytes;viewBytes.data=encoded.data();viewBytes.size=encoded.size();
          ok=ok&&decoded.load(viewBytes)&&decoded.hasGame();
          WorldRenderer importedView;
          importedView.getCamera().screenCenterX=640;importedView.getCamera().screenCenterY=360;
          const auto *v=decoded.getView();
          ok=ok&&importedView.startViewSaved(world.worldMap,v[1],v[2],v[3],v[0]);
          importedView.applyPendingView();
          const auto &after=importedView.getCamera();
          check("current camera export/import turn "+std::to_string(turn)+" zoom "+std::to_string(zoom),
                ok&&importedView.getRotation()==turn&&after.zoom==zoom&&
                std::abs(after.x-cam.x)<=1&&std::abs(after.y-cam.y)<=1);
        }
        world.worldRenderer.rotateView(turnBefore-world.worldRenderer.getRotation());
        world.worldRenderer.setCamera(cameraBefore);
        bool showPrograms=true;
        for(const auto &[id,show]:world.shows.all()) {
          const auto *ex=world.fences.exhibit(id);
          const ZooReader::Habitat *hab=nullptr;
          for(const auto &h:originalDecoded.getHabitats())if(ex&&ex->tiles.count({h.x,h.y})){hab=&h;break;}
          int species=0;for(const auto &type:world.animals.typeList)if(type.file==show.species)species=originalSpeciesName(world.resourceManager,type);
          const ZooReader::ShowProgram *program=nullptr;
          if(hab)for(const auto &a:hab->showAssignments)if(a.species==uint32_t(species))
            for(const auto &p:originalDecoded.getShowPrograms())if(p.id==a.program)program=&p;
          const bool compatible=originalShowScript(world.resourceManager,world.shows,show);
          showPrograms=showPrograms&&hab&&hab->show&&
            (compatible?(program&&program->tricks.size()==show.script.size()):!program);
          if(program)for(size_t k=0;k<show.script.size()&&k<program->tricks.size();++k) {
            const auto *trick=world.shows.find(show.species,show.script[k]);
            showPrograms=showPrograms&&trick&&program->tricks[k].id==trick->id&&
              program->tricks[k].raw.size()>1&&program->tricks[k].raw[1]==uint8_t(trick->visible);
          }
        }
        check("original stream retains supported show scripts and omits custom programs",showPrograms);
        const auto sourceArchive=world.zooReader;
        world.zooReader=originalDecoded;
        std::vector<uint8_t> cycle;std::string issue;ZooReader afterCycle;
        bool stablePrograms=encodeOriginal(world,testBooks,testHud,cycle,issue);
        if(stablePrograms){AssetBuffer input;input.data=cycle.data();input.size=cycle.size();stablePrograms=afterCycle.load(input)&&afterCycle.hasGame();}
        world.zooReader=sourceArchive;
        check("an original reimport does not accumulate unused show programs",
              stablePrograms&&afterCycle.getShowPrograms().size()==originalDecoded.getShowPrograms().size());
        bool recentSpecies=true,thoughtReferences=true;
        std::set<uint32_t> originalUnitIds;
        for(const auto &o:originalDecoded.getObjects())if(o.id)originalUnitIds.insert(o.id);
        for(const auto &o:originalDecoded.getObjects())if(o.hasGuest) {
          recentSpecies=recentSpecies&&o.guest.seen.size()<=5&&o.guest.visited.size()<=10;
          for(uint32_t id:o.guest.seen)recentSpecies=recentSpecies&&
            std::any_of(world.animals.typeList.begin(),world.animals.typeList.end(),[&](const auto &type){return originalSpeciesName(world.resourceManager,type)>0&&id==uint32_t(originalSpeciesName(world.resourceManager,type));});
        }
        for(const auto &t:originalDecoded.getThoughts()) {
          thoughtReferences=thoughtReferences&&(!t.owner||originalUnitIds.count(t.owner))&&(!t.subject||originalUnitIds.count(t.subject));
          if(t.habitatX>=0&&t.habitatY>=0)thoughtReferences=thoughtReferences&&
            std::any_of(originalDecoded.getHabitats().begin(),originalDecoded.getHabitats().end(),[&](const auto &h){return h.x==t.habitatX&&h.y==t.habitatY;});
        }
        check("original recent species and visits use their correct bounded lists",recentSpecies);
        check("original thought references point to current actors and habitats",thoughtReferences);
        // Old custom map designs may have zero object ids. Never mistake one
        // for a zero-body cache declaration in an original export template.
        if(!world.placedObjects.list.empty()) {
          const auto savedSource=world.zooReader.objects;
          auto probe=world.placedObjects.list.front();probe.id=0;probe.label="Zero identity save probe";
          probe.x=8.5f;probe.y=12.5f;
          ZooReader::ZooObject declaration;declaration.className=probe.className;
          declaration.subClass="!cache";declaration.typeName="!cache";
          world.zooReader.objects.insert(world.zooReader.objects.begin(),declaration);
          world.placedObjects.list.push_back(probe);
          std::vector<uint8_t> data;std::string issue;ZooReader decoded;
          bool valid=encodeOriginal(world,testBooks,testHud,data,issue);
          if(valid){AssetBuffer input;input.data=data.data();input.size=data.size();valid=decoded.load(input)&&decoded.hasGame();}
          bool found=false;
          for(const auto &o:decoded.getObjects())if(o.name==probe.label)
            found=o.decoded&&!o.payload.empty()&&o.id&&o.x==544&&o.y==800;
          world.placedObjects.list.pop_back();world.zooReader.objects=savedSource;
          check("zero-id map objects export as current units instead of empty cache bodies",valid&&found);
        }
        ZooSim changedBooks=testBooks;
        const bool marineFinance=originalDecoded.getBooks().monthly.size()>30;
        const int incomeRow=marineFinance?30:5;
        const auto incomeLine=marineFinance?ZooSim::ShowIncome:ZooSim::AdmissionsIncome;
        changedBooks.earn(incomeLine,37);changedBooks.spend(ZooSim::Construction,29);
        auto decodeExport=[&](ZooSim &books,ZooReader &reader) {
          std::vector<uint8_t> data;std::string error;
          if(!encodeOriginal(world,books,testHud,data,error))return false;
          AssetBuffer buffer;buffer.data=data.data();buffer.size=data.size();
          return reader.load(buffer)&&reader.hasGame();
        };
        ZooReader changed;
        bool financial=decodeExport(changedBooks,changed);
        if(financial) {
          const auto &before=originalDecoded.getBooks(),&after=changed.getBooks();
          const int m=testBooks.month(),y=int(before.year);
          auto near=[](float a,float b){return std::abs(a-b)<0.02f;};
          financial=near(after.monthly[incomeRow][m],before.monthly[incomeRow][m]+37)&&
            near(after.monthly[3][m],before.monthly[3][m]+29)&&
            near(after.yearly[incomeRow][y],before.yearly[incomeRow][y]+37)&&
            near(after.yearly[3][y],before.yearly[3][y]+29)&&
            near(after.total[incomeRow],before.total[incomeRow]+37)&&near(after.total[3],before.total[3]+29)&&
            near(after.total[14],before.total[14]+8);
        }
        check("original monthly, annual and lifetime books include current transactions",financial);
        while(changedBooks.month()!=0)changedBooks.newMonth();
        changedBooks.newMonth();
        for(int k=0;k<12;++k)changedBooks.newMonth();
        changedBooks.earn(incomeLine,19);
        ZooReader rolled;const bool rollover=decodeExport(changedBooks,rolled);
        check("original finance calendar and annual ring advance with the native date",
          rollover&&rolled.getBooks().month==uint32_t(changedBooks.month()+1)&&
          rolled.getBooks().year==uint32_t((int(originalDecoded.getBooks().year)+changedBooks.year()-testBooks.year())%20)&&
          std::abs(rolled.getBooks().monthly[incomeRow][changedBooks.month()]-19)<0.02f);
        const bool loadedHybrid=load(hybrid,bootstrap,testBooks,testHud);
        check("combined save restores full native world",loadedHybrid && bootstrap.animals.list.size()==world.animals.list.size() &&
              bootstrap.guests.list.size()==world.guests.list.size());
        const auto *deck=bootstrap.walkways.at(10,10);
        check("engine-only elevated deck survives in the same .zoo",loadedHybrid && deck && deck->h[0]==7);
        check("combined save retains native show schedules",loadedHybrid &&
              bootstrap.shows.snapshot(false)==world.shows.snapshot(false));
        check("compatibility archive excludes the engine extension",
              loadedHybrid && bootstrap.zooReader.originalBytes()==world.zooReader.originalBytes());
        auto damaged=combined;
        damaged[12]^=1;check("damage to compatible stream is detected",!nativeStream(damaged));
        damaged=combined;damaged[damaged.size()-kFooterSize-1]^=1;
        check("damage to native stream is detected",!nativeStream(damaged));
        damaged=combined;damaged.resize(damaged.size()-4);
        check("truncated footer cannot silently discard engine features",hasEngineSection(damaged) && !nativeStream(damaged));
        damaged=combined;damaged.resize(damaged.size()-kFooterSize);
        check("missing footer cannot silently discard engine features",hasEngineSection(damaged) && !nativeStream(damaged));
        auto second=hybrid+".zoo";
        const bool savedAgain=loadedHybrid && save(second,bootstrap,testBooks,testHud,"freeform/ff01.scn");
        std::vector<char> again;
        const bool readAgain=savedAgain && readSaveBytes(second,again,false);
        const auto third=hybrid+".third.zoo";
        const bool loadedAgain=readAgain && load(second,bootstrap,testBooks,testHud);
        const bool savedThird=loadedAgain && save(third,bootstrap,testBooks,testHud,"freeform/ff01.scn");
        std::vector<char> final;
        const bool stable=savedThird && readSaveBytes(third,final,false) && final.size()==again.size() &&
                          bootstrap.zooReader.originalBytes()==world.zooReader.originalBytes();
        check("repeated combined saves do not recursively grow ("+std::to_string(combined.size())+","+
              std::to_string(again.size())+","+std::to_string(final.size())+")",stable);
        std::error_code ignored;std::filesystem::remove(second,ignored);std::filesystem::remove(third,ignored);
      }
      {std::error_code ignored;std::filesystem::remove(hybrid,ignored);}

    }
    Research::get().branches() = previousResearch;
    if (goals) goals->awards = previousAwards;
    std::error_code ec; if (written) std::filesystem::remove(temp, ec);
  }
  ZooReader probe = z;
  AssetBuffer invalid;
  check("empty file is rejected", !probe.load(invalid));
  check("failed reload clears previous exact/game/entity state",
        !probe.isExact() && !probe.hasGame() && probe.getObjects().empty() && probe.getHabitats().empty());
  out += std::to_string(passed) + "/" + std::to_string(total) + " original-save checks passed\n";
  return out;
}

// The world as it's been made - the ground, paths, walkways, fences and
// exhibits, objects - written and read: a saved zoo has it, and so does a
// map design (saveMap: a freeform map made in the ZT1 Lab, over one of the
// game's maps)
void SaveGame::putWorld(void *out, World &world) {
  Out &o = *static_cast<Out *>(out);
  // The ground
  WorldMap &map = world.worldMap;
  o.raw(map.width);
  o.raw(map.height);
  for (int y = 0; y < map.height; y++)
    for (int x = 0; x < map.width; x++) {
      const MapTile &t = map.tiles[y][x];
      o.raw(t.height);
      o.raw(t.cornerHeight);
      o.raw(t.terrainType);
      o.raw(t.edgeBits);
      o.raw(t.substrate);
    }
  o.raw(static_cast<uint32_t>(map.pathTypes.size()));
  for (const std::string &s : map.pathTypes)
    o.str(s);
  o.raw(static_cast<uint32_t>(map.pathTile.size()));
  for (int16_t v : map.pathTile)
    o.raw(v);
  // Walkways
  o.raw(static_cast<uint32_t>(world.walkways.decks.size()));
  for (const auto &[pos, d] : world.walkways.decks) {
    o.raw(pos.first);
    o.raw(pos.second);
    o.str(d.type);
    o.raw(d.h);
  }
  // Fences, exhibits, filters
  Fences &f = world.fences;
  o.raw(static_cast<uint32_t>(f.edges.size()));
  for (const auto &[e, p] : f.edges) {
    putEdge(o, e);
    o.str(f.typeList[p.type].key);
    o.raw(p.gate);
    o.raw(p.facing);
    o.raw(p.ownerX);
    o.raw(p.ownerY);
    o.raw(p.order);
    o.raw(p.drag);
    o.raw(p.tank);
    o.raw(p.life);
    o.raw(p.tank2);
  }
  o.raw(static_cast<uint32_t>(f.exhibitList.size()));
  for (const Fences::Exhibit &x : f.exhibitList) {
    o.raw(x.id);
    o.str(x.name);
    o.raw(x.named);
    o.raw(x.pocket);
    o.raw(x.tank);
    o.raw(x.constructedDay);
    o.raw(x.constructedMonth);
    o.raw(x.constructedYear);
    o.raw(x.groundHeight);
    o.raw(x.floorHeight);
    o.raw(x.water);
    o.raw(x.wallSub);
    o.raw(x.salt);
    o.raw(x.filling);
    o.raw(x.purity);
    o.raw(x.purityClock);
    for (double v : {x.donationsNow, x.donationsLast, x.donationsTotal, x.upkeepNow, x.upkeepLast,
                     x.upkeepTotal, x.viewed, x.builtAt})
      o.raw(v);
    o.raw(static_cast<uint32_t>(x.tiles.size()));
    for (auto [tx, ty] : x.tiles) {
      o.raw(tx);
      o.raw(ty);
    }
    o.raw(static_cast<uint32_t>(x.before.size()));
    for (const auto &[at, g] : x.before) {
      o.raw(at.first);
      o.raw(at.second);
      o.raw(g.height);
      for (int c = 0; c < 4; c++)
        o.raw(g.corner[c]);
      o.raw(g.terrain);
    }
  }
  o.raw(f.made);
  o.raw(f.placed);
  o.raw(f.filtersMade);
  o.raw(static_cast<uint32_t>(f.filterList.size()));
  for (const Fences::Filter &x : f.filterList) {
    o.raw(x.x);
    o.raw(x.y);
    o.raw(x.tank);
    o.raw(x.health);
    o.raw(x.decayClock);
    o.raw(x.filterClock);
    o.raw(x.dx);
    o.raw(x.dy);
    o.str(x.name);
    o.raw(x.placedMonth);
    o.raw(x.upkeepLast);
    o.raw(x.upkeepCurrent);
    o.raw(x.upkeepTotal);
    o.str(x.file);
  }
  // Objects (the map's and those bought)
  PlacedObjects &po = world.placedObjects;
  o.raw(static_cast<uint32_t>(po.list.size()));
  for (const PlacedObjects::Object &x : po.list) {
    o.str(x.className);
    o.str(x.subClass);
    o.str(x.typeName);
    o.str(x.name);
    o.raw(x.x);
    o.raw(x.y);
    o.raw(x.z);
    o.raw(x.facing);
    o.raw(x.fence);
    o.raw(x.bought);
    o.raw(x.id);
    o.str(x.label);
    o.raw(x.price);
    o.raw(x.income);
    o.raw(x.upkeep);
    o.raw(x.openedMonth);
    o.raw(x.visitorsNow);
    o.raw(x.visitorsLast);
    o.raw(x.visitorsTotal);
    o.raw(static_cast<uint32_t>(x.colours.size()));
    for (int c : x.colours)
      o.raw(c);
    o.raw(x.fill);
    o.raw(x.nameId);
    o.raw(x.program);
    o.raw(static_cast<uint32_t>(x.sold.size()));
    for (const auto &[k, n] : x.sold) {
      o.str(k);
      o.raw(n);
    }
  }
  o.raw(po.nextId);
  o.raw(static_cast<uint32_t>(po.counts.size()));
  for (const auto &[k, n] : po.counts) {
    o.str(k);
    o.raw(n);
  }
}

bool SaveGame::getWorld(void *in, World &world, uint32_t version) {
  In &i = *static_cast<In *>(in);
  // The ground
  WorldMap &map = world.worldMap;
  int w = i.raw<int>(), h = i.raw<int>();
  if (i.bad || w < 1 || h < 1 || w > 512 || h > 512)
    return false;
  if (w != map.width || h != map.height)
    map.makeBlank(w, h, 0);
  map.minHeight = INT32_MAX;
  map.maxHeight = INT32_MIN;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      MapTile &t = map.tiles[y][x];
      i.get(t.height);
      i.get(t.cornerHeight);
      i.get(t.terrainType);
      i.get(t.edgeBits);
      i.get(t.substrate);
      for (int corner : t.cornerHeight) {
        map.minHeight = std::min(map.minHeight, corner);
        map.maxHeight = std::max(map.maxHeight, corner);
      }
    }
  map.pathTypes.resize(i.count(32768, 4));
  for (std::string &s : map.pathTypes) {
    s = i.str();
    // Early Lab/show fixtures used a concrete-path placeholder key.
    if(version<=12&&s=="road"&&!world.resourceManager->hasResource("paths/road.ai"))s="path";
  }
  const auto pathTiles = i.count(512 * 512, sizeof(int16_t));
  if (i.bad || pathTiles != uint32_t(w * h)) return false;
  map.pathTile.resize(pathTiles);
  std::set<int> usedPaths;
  for (int16_t &v : map.pathTile) {
    i.get(v);
    if (v < -1 || v >= int(map.pathTypes.size())) return false;
    if (v >= 0) usedPaths.insert(v);
  }
  for (int type : usedPaths)
    requireContent(i, world.resourceManager, "paths/" + map.pathTypes[type] + ".ai");
  map.touch();
  world.walkways.decks.clear();
  for (uint32_t n = i.count(); n > 0 && !i.bad; n--) {
    int x = i.raw<int>(), y = i.raw<int>();
    Walkways::Deck d;
    d.type = i.str();
    if(version<=12&&d.type=="road"&&!world.resourceManager->hasResource("paths/road.ai"))d.type="path";
    requireContent(i, world.resourceManager, "paths/" + d.type + ".ai");
    i.get(d.h);
    world.walkways.decks[{x, y}] = d;
  }
  // Fences
  Fences &f = world.fences;
  f.mapW = w;
  f.mapH = h;
  f.mapRef = &map;
  f.mapEdit = &map;
  f.edges.clear();
  for (uint32_t n = i.count(); n > 0 && !i.bad; n--) {
    Fences::Edge e = getEdge(i);
    Fences::Piece p;
    const auto fenceFile = i.str();
    p.type = f.typeIndex(fenceFile);
    if (p.type < 0) missingContent(i, fenceFile);
    i.get(p.gate);
    i.get(p.facing);
    i.get(p.ownerX);
    i.get(p.ownerY);
    i.get(p.order);
    i.get(p.drag);
    i.get(p.tank);
    i.get(p.life);
    if (version >= 4)
      i.get(p.tank2);
    if (p.type >= 0)
      f.edges[e] = p;
  }
  f.exhibitList.clear();
  for (uint32_t n = i.count(); n > 0 && !i.bad; n--) {
    Fences::Exhibit x;
    i.get(x.id);
    x.name = i.str();
    i.get(x.named);
    i.get(x.pocket);
    i.get(x.tank);
    i.get(x.constructedDay);
    i.get(x.constructedMonth);
    i.get(x.constructedYear);
    i.get(x.groundHeight);
    i.get(x.floorHeight);
    i.get(x.water);
    i.get(x.wallSub);
    if (version < 5)
      x.wallSub -= x.floorHeight; // (it was the walls' top in height units)
    if (version < 5)
      x.wallSub *= 2; // (to the half units of 5 and 6)
    if (version < 7 && x.tank)
      x.wallSub = std::max(1, (x.wallSub + 1) / 2); // (half units to the original's whole ones)
    i.get(x.salt);
    i.get(x.filling);
    i.get(x.purity);
    i.get(x.purityClock);
    if (version >= 2)
      for (double *v : {&x.donationsNow, &x.donationsLast, &x.donationsTotal, &x.upkeepNow, &x.upkeepLast,
                        &x.upkeepTotal, &x.viewed, &x.builtAt})
        i.get(*v);
    for (uint32_t t = i.count(); t > 0 && !i.bad; t--) {
      int tx = i.raw<int>(), ty = i.raw<int>();
      x.tiles.insert({tx, ty});
    }
    if (version >= 8)
      for (uint32_t t = i.count(); t > 0 && !i.bad; t--) {
        int tx = i.raw<int>(), ty = i.raw<int>();
        Fences::Exhibit::Ground &g = x.before[{tx, ty}];
        i.get(g.height);
        for (int c = 0; c < 4; c++)
          i.get(g.corner[c]);
        i.get(g.terrain);
      }
    f.exhibitList.push_back(x);
  }
  i.get(f.made);
  i.get(f.placed);
  i.get(f.filtersMade);
  f.filterList.clear();
  for (uint32_t n = i.count(); n > 0 && !i.bad; n--) {
    Fences::Filter x;
    i.get(x.x);
    i.get(x.y);
    i.get(x.tank);
    i.get(x.health);
    i.get(x.decayClock);
    i.get(x.filterClock);
    i.get(x.dx);
    i.get(x.dy);
    x.name = i.str();
    i.get(x.placedMonth);
    i.get(x.upkeepLast);
    i.get(x.upkeepCurrent);
    i.get(x.upkeepTotal);
    if (version >= 9) x.file = i.str();
    requireContent(i, world.resourceManager, x.file);
    f.filterList.push_back(x);
  }
  // Objects: the art found again for each
  PlacedObjects &po = world.placedObjects;
  std::vector<PlacedObjects::Object> old = po.list;
  po.list.clear();
  for (uint32_t n = i.count(); n > 0 && !i.bad; n--) {
    PlacedObjects::Object x;
    x.className = i.str();
    x.subClass = i.str();
    x.typeName = i.str();
    po.resolveExt(x);
    x.name = i.str();
    i.get(x.x);
    i.get(x.y);
    i.get(x.z);
    i.get(x.facing);
    i.get(x.fence);
    i.get(x.bought);
    i.get(x.id);
    x.label = i.str();
    i.get(x.price);
    i.get(x.income);
    i.get(x.upkeep);
    i.get(x.openedMonth);
    if (version >= 2) {
      i.get(x.visitorsNow);
      i.get(x.visitorsLast);
      i.get(x.visitorsTotal);
      for (uint32_t n = i.raw<uint32_t>(); n > 0 && n < 8 && !i.bad; n--)
        x.colours.push_back(i.raw<int>());
      i.get(x.fill);
      if (version >= 3) {
        i.get(x.nameId);
        i.get(x.program);
      }
    }
    for (uint32_t s = i.count(); s > 0 && !i.bad; s--) {
      std::string k = i.str();
      x.sold[k] = i.raw<int>();
    }
    // (its art: the map's own objects as loaded, bought ones by their file)
    for (const PlacedObjects::Object &m : old)
      if (m.className == x.className && m.subClass == x.subClass && m.typeName == x.typeName) {
        x.art = m.art;
        x.slopeUp = m.slopeUp;
        x.slopeDown = m.slopeDown;
        break;
      }
    if (!x.art || x.bought)
      x.art = po.artOfFile(PlacedObjects::fileOf(x));
    // (painted: its colours' art)
    if (x.art && !x.colours.empty()) {
      std::vector<int> picks = x.colours;
      for (size_t c = 0; c < picks.size(); c++)
        po.setColour(x, static_cast<int>(c), picks[c]);
    }
    if (x.art && x.bought)
      po.loadLayers(x);
    if (!x.art) missingContent(i, PlacedObjects::fileOf(x));
    if (x.art) po.list.push_back(x);
  }
  i.get(po.nextId);
  po.counts.clear();
  for (uint32_t n = i.count(); n > 0 && !i.bad; n--) {
    std::string k = i.str();
    po.counts[k] = i.raw<int>();
  }
  if (!i.bad) world.refindInside();
  return !i.bad;
}

bool SaveGame::save(const std::string &path, World &world, ZooSim &sim, UiGameScreen &hud,
                    const std::string &mapPath) {
  saveFailure.clear();
  Out o;
  o.raw(kMagic);
  o.raw(kVersion);
  // The world snapshot below contains all terrain, paths, boundaries and
  // placed objects. A converted original save must not depend on the user's
  // original .zoo still existing at the same absolute file path.
  o.str(isOriginal(mapPath) ? "freeform/ff01.scn" : mapPath);
  // The books
  o.raw(sim.money);
  o.raw(sim.zooRating);
  o.raw(sim.day);
  o.raw(sim.dayCount);
  o.raw(static_cast<uint32_t>(sim.months.size()));
  for (const ZooSim::Month &m : sim.months) {
    o.raw(m.month);
    o.raw(m.year);
    o.raw(m.lines);
    o.raw(m.rating);
  }
  o.raw(static_cast<uint32_t>(sim.importedAnnual.size()));
  for (const auto &row : sim.importedAnnual) {
    o.raw(static_cast<uint32_t>(row.size()));
    for (float value : row) o.raw(value);
  }
  o.raw(static_cast<uint32_t>(sim.importedTotals.size()));
  for (float value : sim.importedTotals) o.raw(value);
  // The zoo's name and admission
  const UiGameScreen::ZooInfo &info = hud.zooInfo();
  o.str(info.name);
  o.raw(info.admission);
  o.raw(hud.marketingLevel);
  // Research
  auto &branches = Research::get().branches();
  o.raw(static_cast<uint32_t>(branches.size()));
  for (const ResearchBranch &b : branches) {
    o.raw(b.fundingLevel);
    o.raw(b.currentCategory);
    o.raw(b.currentProgram);
    o.raw(static_cast<uint32_t>(b.categories.size()));
    for (size_t ci = 0; ci < b.categories.size(); ++ci) {
      const ResearchCategory &c = b.categories[ci];
      o.raw(ci >= b.enabled.size() || b.enabled[ci]);
      o.raw(static_cast<uint32_t>(c.programs.size()));
      for (const ResearchProgram &p : c.programs) {
        o.raw(p.progress);
        o.raw(p.done);
      }
    }
  }
  putWorld(&o, world);
  // Food, dung, litter
  o.raw(static_cast<uint32_t>(world.items.list.size()));
  for (const ZooItems::Item &x : world.items.list) {
    o.raw(x.id);
    o.raw(x.kind);
    o.raw(x.exhibit);
    o.raw(x.x);
    o.raw(x.y);
    o.raw(x.units);
    o.raw(x.full);
    o.str(x.food);
  }
  o.raw(world.items.nextId);
  // Animals
  Animals &an = world.animals;
  o.raw(static_cast<uint32_t>(an.list.size()));
  for (const Animals::Member &m : an.list) {
    o.str(an.typeList[m.type].file);
    o.raw(m.id);
    o.raw(m.female);
    o.raw(m.baby);
    o.str(m.name);
    o.raw(m.x);
    o.raw(m.y);
    o.raw(m.happiness);
    o.raw(m.hunger);
    o.raw(m.health);
    o.raw(m.energy);
    o.raw(m.dirty);
    o.raw(m.sick);
    o.raw(m.escaped);
    o.raw(m.boxed);
    o.raw(m.tranquilised);
    o.raw(m.lastAte);
    o.raw(m.lastSlept);
    o.raw(m.life);
    o.raw(m.ready);
    o.raw(m.grow);
    o.raw(m.fx); o.raw(m.fy); o.raw(m.egg); o.raw(m.hatchIn);
    o.raw(m.rampaging); o.raw(m.rampageTicks); o.raw(m.dying); o.raw(m.dieAfter); o.raw(m.sleeping);
    for (int value : {m.hungerCheck, m.healthCheck, m.energyCheck, m.otherCheck, m.captivityCheck,
                      m.habitatCheck, m.boredCheck, m.socialCheck, m.reproductionCheck,
                      m.preyCheck, m.huntCheck}) o.raw(value);
    o.raw(m.swimZ); o.raw(m.swimTo);
  }
  o.raw(an.nextId);
  o.raw(static_cast<uint32_t>(an.counts.size()));
  for (const auto &[k, n] : an.counts) {
    o.str(an.typeList[k].file);
    o.raw(n);
  }
  o.raw(an.now);
  // Staff
  Staff &st = world.staff;
  o.raw(static_cast<uint32_t>(st.list.size()));
  for (const Staff::Member &m : st.list) {
    o.str(st.typeList[m.type].file);
    o.raw(m.id);
    o.raw(m.female);
    o.raw(m.hair);
    o.raw(m.skin);
    o.str(m.name);
    // (one in a tank, or climbing in or out, is kept on the bank by its gate)
    const bool wet = m.tank >= 0 || m.water != 0 || m.climbing;
    o.raw(wet ? m.outX : m.x);
    o.raw(wet ? m.outY : m.y);
    o.raw(m.duties);
    o.raw(static_cast<uint32_t>(m.exhibits.size()));
    for (int e : m.exhibits)
      o.raw(e);
    o.raw(m.fx); o.raw(m.fy); o.raw(m.workCheck); o.raw(m.crowdClock);
  }
  o.raw(st.nextId);
  o.raw(static_cast<uint32_t>(st.counts.size()));
  for (const auto &[k, n] : st.counts) {
    o.str(st.typeList[k].file);
    o.raw(n);
  }
  // Guests
  Guests &gs = world.guests;
  o.raw(static_cast<uint32_t>(gs.list.size()));
  for (const Guests::Guest &g : gs.list) {
    o.raw(g.id);
    o.raw(g.type);
    o.raw(g.colours);
    o.raw(g.x);
    o.raw(g.y);
    o.raw(g.happiness);
    o.raw(g.hunger);
    o.raw(g.thirst);
    o.raw(g.bathroom);
    o.raw(g.tired);
    o.raw(g.favourite);
    o.raw(g.leaving);
    o.raw(g.trash);
    o.str(g.name);
    o.raw(g.arrived);
    o.raw(g.fx); o.raw(g.fy); o.raw(g.inventory); o.raw(g.sick); o.raw(g.sawEscape); o.raw(g.souvenir);
    for (int value : {g.hungerCheck, g.thirstCheck, g.bathroomCheck, g.leaveCheck, g.souvenirCheck,
                      g.energyCheck, g.trashCheck, g.viewCheck, g.chaseCheck}) o.raw(value);
    o.raw(static_cast<uint32_t>(g.visited.size()));
    for (int value : g.visited) o.raw(value);
    o.raw(static_cast<uint32_t>(g.thoughts.size()));
    for (const auto &thought : g.thoughts) o.str(thought);
    o.str(g.favourite>=0&&g.favourite<int(an.typeList.size())?an.typeList[g.favourite].file:std::string());
    o.raw(static_cast<uint32_t>(g.seen.size()));
    for(int exhibit:g.seen)o.raw(exhibit);
  }
  o.raw(gs.nextId);
  o.raw(gs.clock);
  o.raw(world.clock);
  // The view
  const Camera &cam = world.worldRenderer.getCamera();
  o.raw(cam.x);
  o.raw(cam.y);
  o.raw(cam.zoom);
  // The goals: each one's state, the awards received
  {
    std::vector<int> states;
    std::vector<int> awards;
    if (SaveGame::goals) {
      for (const Goals::Goal &g : SaveGame::goals->all())
        states.push_back(g.state);
      awards = SaveGame::goals->awards;
    }
    o.raw(static_cast<uint32_t>(states.size()));
    for (int st : states)
      o.raw(st);
    o.raw(static_cast<uint32_t>(awards.size()));
    for (int a : awards)
      o.raw(a);
  }
  // The way the view's turned (its camera above is for that turn)
  o.raw(world.worldRenderer.getRotation());
  o.str(world.shows.snapshot());
  const auto &original = world.zooReader.originalBytes();
  o.raw(static_cast<uint32_t>(original.size()));
  o.b.insert(o.b.end(), original.begin(), original.end());

  std::string extension=std::filesystem::path(path).extension().string();
  std::transform(extension.begin(),extension.end(),extension.begin(),::tolower);
  if(extension==".zoo") {
    std::vector<uint8_t> original;
    if(!encodeOriginal(world,sim,hud,original,saveFailure))return false;
    Out combined;
    combined.b.assign(original.begin(),original.end());
    const uint64_t offset=combined.b.size(),length=o.b.size();
    const uint32_t originalCrc=saveCrc(combined.b.data(),combined.b.size());
    const uint32_t nativeCrc=saveCrc(o.b.data(),o.b.size());
    combined.b.insert(combined.b.end(),o.b.begin(),o.b.end());
    combined.raw(kEngineFooter);combined.raw(uint32_t(1));combined.raw(offset);combined.raw(length);
    combined.raw(nativeCrc);combined.raw(originalCrc);
    if(!atomicSave(path,combined.b)){saveFailure="The combined zoo could not be written.";return false;}
    return true;
  }
  if(!atomicSave(path,o.b)){saveFailure="The zoo could not be written.";return false;}
  return true;
}

static bool readAll(const std::string &path, std::vector<char> &out) {
  return readSaveBytes(path,out);
}

std::string SaveGame::mapOf(const std::string &path) {
  std::vector<char> b;
  if (!readAll(path, b))
    return "";
  In i{b};
  if (i.raw<uint32_t>() != kMagic || i.raw<uint32_t>() > kVersion)
    return "";
  return i.str();
}

bool SaveGame::load(const std::string &path, World &world, ZooSim &sim, UiGameScreen &hud) {
  saveFailure.clear();
  std::vector<char> b;
  if (!readAll(path, b))
    return false;
  In i{b};
  if (i.raw<uint32_t>() != kMagic)
    return false;
  const uint32_t version = i.raw<uint32_t>();
  if (version < 1 || version > kVersion)
    return false;
  i.str(); // the map (already loaded)
  i.get(sim.money);
  i.get(sim.zooRating);
  i.get(sim.day);
  i.get(sim.dayCount);
  if (!std::isfinite(sim.money) || !std::isfinite(sim.zooRating) || !std::isfinite(sim.day)) return false;
  sim.months.resize(i.count(10000, 112));
  if (i.bad || sim.months.empty()) return false;
  for (ZooSim::Month &m : sim.months) {
    i.get(m.month);
    i.get(m.year);
    i.get(m.lines);
    i.get(m.rating);
    if (m.month < 0 || m.month > 11 || m.year < 1 || m.year > 9999 ||
        !std::isfinite(m.rating) || std::any_of(m.lines.begin(), m.lines.end(),
          [](double value) { return !std::isfinite(value); })) return false;
  }
  sim.importedAnnual.clear(); sim.importedTotals.clear();
  if (version >= 12) {
    const auto nr = i.raw<uint32_t>();
    if (nr > 1000) return false;
    sim.importedAnnual.resize(nr);
    for (auto &row : sim.importedAnnual) {
      const auto nc = i.raw<uint32_t>();
      if (nc > 1000) return false;
      row.resize(nc);
      for (auto &value : row) i.get(value);
    }
    const auto nt = i.raw<uint32_t>();
    if (nt > 1000) return false;
    sim.importedTotals.resize(nt);
    for (auto &value : sim.importedTotals) i.get(value);
  }
  sim.dirty = true;
  UiGameScreen::ZooInfo info = hud.zooInfo();
  info.name = i.str();
  i.get(info.admission);
  info.cash = static_cast<long>(sim.money);
  hud.setZooInfo(info);
  i.get(hud.marketingLevel);
  auto &branches = Research::get().branches();
  uint32_t nb = i.raw<uint32_t>();
  for (uint32_t k = 0; k < nb && !i.bad; k++) {
    ResearchBranch dummy;
    ResearchBranch &br = k < branches.size() ? branches[k] : dummy;
    i.get(br.fundingLevel);
    i.get(br.currentCategory);
    i.get(br.currentProgram);
    uint32_t nc = i.raw<uint32_t>();
    for (uint32_t c = 0; c < nc && !i.bad; c++) {
      const bool enabled = version >= 12 ? i.raw<bool>() : true;
      if (c < br.enabled.size()) br.enabled[c] = enabled;
      uint32_t np = i.raw<uint32_t>();
      for (uint32_t p = 0; p < np && !i.bad; p++) {
        float progress = version >= 3 ? i.raw<float>() : static_cast<float>(i.raw<int>());
        bool done = i.raw<bool>();
        if (c < br.categories.size() && p < br.categories[c].programs.size()) {
          br.categories[c].programs[p].progress = progress;
          br.categories[c].programs[p].done = done;
        }
      }
    }
  }
  if (!getWorld(&i, world, version))
    return false;
  Fences &f = world.fences;
  // Items
  world.items.list.clear();
  for (uint32_t n = i.count(); n > 0 && !i.bad; n--) {
    ZooItems::Item x;
    i.get(x.id);
    i.get(x.kind);
    i.get(x.exhibit);
    i.get(x.x);
    i.get(x.y);
    i.get(x.units);
    i.get(x.full);
    x.food = i.str();
    if(version<=12&&x.food=="grass"&&!world.resourceManager->hasResource(ZooItems::foodFile("grass")))x.food="graschow";
    // Older original imports wrote the registry class instead of the chow.
    if (x.kind == ZooItems::Kind::Food && !x.food.empty() && !(version<=12 && x.food=="food"))
      requireContent(i, world.resourceManager, ZooItems::foodFile(x.food));
    world.items.list.push_back(x);
  }
  i.get(world.items.nextId);
  // Animals
  Animals &an = world.animals;
  an.clear();
  for (uint32_t n = i.count(); n > 0 && !i.bad; n--) {
    Animals::Member m;
    const auto animalFile = i.str();
    m.type = an.typeOfFile(animalFile);
    if (m.type < 0) missingContent(i, animalFile);
    i.get(m.id);
    i.get(m.female);
    i.get(m.baby);
    m.name = i.str();
    i.get(m.x);
    i.get(m.y);
    i.get(m.happiness);
    i.get(m.hunger);
    i.get(m.health);
    i.get(m.energy);
    i.get(m.dirty);
    i.get(m.sick);
    i.get(m.escaped);
    i.get(m.boxed);
    if (version >= 11) i.get(m.tranquilised);
    i.get(m.lastAte);
    i.get(m.lastSlept);
    i.get(m.life);
    i.get(m.ready);
    i.get(m.grow);
    if (version >= 12) {
      i.get(m.fx); i.get(m.fy); i.get(m.egg); i.get(m.hatchIn);
      i.get(m.rampaging); i.get(m.rampageTicks); i.get(m.dying); i.get(m.dieAfter); i.get(m.sleeping);
      for (int *value : {&m.hungerCheck, &m.healthCheck, &m.energyCheck, &m.otherCheck, &m.captivityCheck,
                         &m.habitatCheck, &m.boredCheck, &m.socialCheck, &m.reproductionCheck,
                         &m.preyCheck, &m.huntCheck}) i.get(*value);
      i.get(m.swimZ); i.get(m.swimTo);
    }
    m.exhibit = f.exhibitAt(static_cast<int>(m.x), static_cast<int>(m.y));
    m.food = 100 - m.hunger;
    if (m.type >= 0) {
      if (m.egg) an.play(m, "egg_idle", Animals::Member::Mode::Loop, 3600.0f);
      else if (m.dying && m.dieAfter && an.set(m, "bdie")) an.pushSet(m, "bdie");
      an.list.push_back(m);
    }
  }
  i.get(an.nextId);
  for (uint32_t n = i.count(); n > 0 && !i.bad; n--) {
    int k = an.typeOfFile(i.str());
    int c = i.raw<int>();
    if (k >= 0)
      an.counts[k] = c;
  }
  i.get(an.now);
  an.markDirty();
  // Staff
  Staff &st = world.staff;
  st.clear();
  for (uint32_t n = i.count(); n > 0 && !i.bad; n--) {
    Staff::Member m;
    const auto staffFile = i.str();
    m.type = st.typeOfFile(staffFile);
    if (m.type < 0) missingContent(i, staffFile);
    i.get(m.id);
    i.get(m.female);
    i.get(m.hair);
    i.get(m.skin);
    m.name = i.str();
    i.get(m.x);
    i.get(m.y);
    i.get(m.duties);
    for (uint32_t e = i.raw<uint32_t>(); e > 0 && !i.bad; e--)
      m.exhibits.push_back(i.raw<int>());
    if (version >= 12) { i.get(m.fx); i.get(m.fy); i.get(m.workCheck); i.get(m.crowdClock); }
    if (m.type >= 0)
      st.list.push_back(m);
  }
  i.get(st.nextId);
  st.counts.clear();
  for (uint32_t n = i.count(); n > 0 && !i.bad; n--) {
    int k = st.typeOfFile(i.str());
    int c = i.raw<int>();
    if (k >= 0)
      st.counts[k] = c;
  }
  // Guests
  Guests &gs = world.guests;
  gs.list.clear();
  for (uint32_t n = i.count(); n > 0 && !i.bad; n--) {
    Guests::Guest g;
    i.get(g.id);
    i.get(g.type);
    i.get(g.colours);
    i.get(g.x);
    i.get(g.y);
    i.get(g.happiness);
    i.get(g.hunger);
    i.get(g.thirst);
    i.get(g.bathroom);
    i.get(g.tired);
    i.get(g.favourite);
    i.get(g.leaving);
    i.get(g.trash);
    g.name = i.str();
    i.get(g.arrived);
    if (version >= 12) {
      i.get(g.fx); i.get(g.fy); i.get(g.inventory); i.get(g.sick); i.get(g.sawEscape); i.get(g.souvenir);
      for (int *value : {&g.hungerCheck, &g.thirstCheck, &g.bathroomCheck, &g.leaveCheck, &g.souvenirCheck,
                         &g.energyCheck, &g.trashCheck, &g.viewCheck, &g.chaseCheck}) i.get(*value);
      uint32_t nv = i.raw<uint32_t>(); if (nv > 100000) return false;
      while (nv-- && !i.bad) g.visited.insert(i.raw<int>());
      uint32_t nt = i.raw<uint32_t>(); if (nt > 10) return false;
      while (nt-- && !i.bad) g.thoughts.push_back(i.str());
    }
    if(version>=14) {
      const auto favourite=i.str();
      g.favourite=favourite.empty()?-1:an.typeOfFile(favourite);
      for(uint32_t n=i.count(10000,4);n>0&&!i.bad;--n)g.seen.insert(i.raw<int>());
    }
    g.playFor = 0.5f;
    if (g.type >= 0 && g.type < static_cast<int>(gs.typeList.size()))
      gs.list.push_back(g);
  }
  i.get(gs.nextId);
  i.get(gs.clock);
  i.get(world.clock);
  Camera &cam = world.worldRenderer.getCamera();
  i.get(cam.x);
  i.get(cam.y);
  i.get(cam.zoom);
  if (version >= 2) {
    std::vector<int> states, awards;
    for (uint32_t n = i.raw<uint32_t>(); n > 0 && n < 10000 && !i.bad; n--)
      states.push_back(i.raw<int>());
    for (uint32_t n = i.raw<uint32_t>(); n > 0 && n < 1000 && !i.bad; n--)
      awards.push_back(i.raw<int>());
    if (SaveGame::goals && !i.bad) {
      // (what the goals did, done again quietly; the awards as they were)
      SaveGame::goals->awards.clear();
      SaveGame::goals->restore(states);
      SaveGame::goals->awards = awards;
      hud.setAwards(awards);
    }
  }
  if (version >= 6 && !i.bad) {
    int turn = i.raw<int>();
    float cx = cam.x, cy = cam.y, cz = cam.zoom;
    if (!i.bad && turn >= 0 && turn < 4) {
      world.rotateView((turn - world.worldRenderer.getRotation() + 4) % 4);
      cam.x = cx;
      cam.y = cy;
      cam.zoom = cz;
    }
  }
  if(version>=10 && !i.bad) {auto shows=i.str();if(!i.bad && !world.shows.restore(shows))i.bad=true;}
  else {world.shows.clear();world.shows.refresh();}
  if (version >= 13 && !i.bad) {
    const auto size = i.raw<uint32_t>();
    if (i.bad || size > 32 * 1024 * 1024 || size > i.b.size() - i.at) return false;
    if (size) {
      AssetBuffer original; original.data = const_cast<char *>(i.b.data() + i.at); original.size = size;
      ZooReader decoded;
      if (!decoded.load(original) || !decoded.hasGame()) return false;
      world.zooReader = std::move(decoded);
    } else world.zooReader = ZooReader();
    i.at += size;
  }
  if (!i.bad) {
    gs.entranceX = gs.entranceY = -1;
    world.findEntrance();
  }
  if(version<=12)for(auto &item:world.items.list)
    if(item.kind==ZooItems::Kind::Food&&item.food=="food") {
      bool carnivore=false;item.food=st.foodFor(item.exhibit,f,carnivore);
      requireContent(i,world.resourceManager,ZooItems::foodFile(item.food));
      SDL_Log("[Save] migrated legacy generic food to %s",item.food.c_str());
    }
  // Derived habitat counts must be valid immediately, including callers
  // that resume a staff task before the first World::update frame.
  for(auto &ex:f.exhibitList) {
    ex.animals=an.countIn(ex.id);ex.dinosaurs=an.dinosaursIn(ex.id);
  }
  world.reindex();
  return !i.bad;
}

bool SaveGame::encodeOriginal(World &world,ZooSim &sim,UiGameScreen &hud,
                              std::vector<uint8_t> &bytes,std::string &error) {
  bytes.clear();
  ZooReader z=world.zooReader;
  if(z.original.empty()) {
    int size=0;void *data=world.resourceManager->getFileContent("maps/sm_aqua.zoo",&size);
    if(!data || size<=0){std::free(data);error="The original Marine Mania map template is unavailable.";return false;}
    auto buffer=MemoryManager::get().wrap(data,size);
    if(!z.load(buffer) || !z.hasGame()){error="The original map template cannot be read completely.";return false;}
  }
  if(z.version<70){error="Export of original formats before version 70 is not supported yet.";return false;}
  const bool needsMarine=std::any_of(world.fences.exhibitList.begin(),world.fences.exhibitList.end(),
      [](const auto &ex){return ex.tank;});
  const bool hasShows=std::any_of(world.fences.exhibitList.begin(),world.fences.exhibitList.end(),[](const auto &ex){return ex.show;});
  if((needsMarine && z.version<100) || (hasShows && z.version<103)) {
    // The native section keeps the full older archive. For the compatible
    // stream, promote to Marine Mania's stock manager layout and build every
    // live record afresh in that version.
    int size=0;void *data=world.resourceManager->getFileContent("maps/sm_aqua.zoo",&size);
    if(!data || size<=0){std::free(data);error="The Marine Mania export template is unavailable.";return false;}
    auto buffer=MemoryManager::get().wrap(data,size);
    if(!z.load(buffer) || !z.hasGame() || z.version<100){error="The Marine Mania export template is invalid.";return false;}
  }
  // Raised decks are engine-only: the compatible stream keeps ground paths,
  // while the native section of a combined save retains every deck.
  z.resetOriginalEvents=true;
  std::map<std::string,bool> originalAssets;
  auto originalAsset=[&](const std::string &file) {
    auto found=originalAssets.find(file);if(found!=originalAssets.end())return found->second;
    const bool supported=world.resourceManager->availableInGame(file);
    originalAssets.emplace(file,supported);return supported;
  };
  std::map<std::string,int> stockSpecies;
  for(const auto &type:world.animals.typeList)
    stockSpecies[type.file]=originalSpeciesName(world.resourceManager,type);
  const auto old=z.objects;
  z.objects.clear();
  std::set<uint32_t> used;
  uint32_t next=std::max(uint32_t(1),z.nextUnitId);
  // Reserve original identities before allocating new records. Separate native
  // animal/staff/object counters can otherwise produce duplicate original IDs.
  for(const auto &o:old)if(o.id && o.id<UINT32_MAX)next=std::max(next,o.id+1);
  std::set<uint32_t> reserved;for(const auto &o:old)if(o.id)reserved.insert(o.id);
  auto identity=[&](uint32_t preferred,bool retained=false) {
    if(preferred && (retained || !reserved.count(preferred)) && used.insert(preferred).second)return preferred;
    while(!next || used.count(next))++next;
    used.insert(next);return next++;
  };
  auto source=[&](const std::string &cls,uint32_t id)->ZooReader::ZooObject {
    // Zero ids in older map designs aren't identities. Nor may a promoted
    // map reuse an unrelated template unit or its differently versioned body.
    if(id&&world.zooReader.version==z.version)
      for(const auto &o:world.zooReader.objects)
        if(o.decoded&&o.className==cls&&o.id==id)return o;
    ZooReader::ZooObject o;o.className=cls;o.decoded=true;return o;
  };
  auto direction=[](float x,float y) {
    return uint32_t((int(std::lround(std::atan2(x,-y)*4/3.14159265358979323846))+8)%8);
  };
  auto position=[&](ZooReader::ZooObject &o,float x,float y,float height) {
    o.x=int32_t(std::lround(x*64));o.y=int32_t(std::lround(y*64));o.z=int32_t(std::lround(height*16));
  };
  auto ground=[&](float x,float y) {return world.animals.groundAt(world.worldMap,x,y);};
  std::map<int,uint32_t> objectIds;
  for(const auto &object:world.placedObjects.list) {
    if(object.fence)continue;
    // Stock entrance pieces and map decorations can have art with no .ai.
    // A custom item only qualifies if the stock game owns its definition/art.
    if(!originalAsset(PlacedObjects::fileOf(object))&&
       !originalAsset("objects/"+object.typeName+"/idle/idle.ani"))continue;
    const bool transient=std::any_of(old.begin(),old.end(),[&](const auto &record) {
      return !record.decoded && record.payload.empty() && record.className==object.className &&
             record.subClass==object.subClass && record.typeName==object.typeName;
    });
    if(transient)continue; // ripples/waves have no persistent record in zoo.exe
    auto o=source(object.className,object.id);
    if(o.className.empty()){error="An object has no original entity class.";return false;}
    o.subClass=object.subClass;o.typeName=object.typeName;
    o.id=identity(object.id,!o.payload.empty());objectIds[object.id]=o.id;
    o.name=object.label.empty()?object.name:object.label;
    position(o,object.x,object.y,object.z);o.direction=object.facing;
    o.colours.clear();for(int n:object.colours)o.colours.push_back(uint8_t(n));
    if(o.className=="building") {
      auto &post=o.suffixOverride;
      if(o.commonEnd && o.commonEnd<=o.payload.size())post.assign(o.payload.begin()+o.commonEnd,o.payload.end());
      else post.resize(48+(z.version>=68?4:0)+(z.version>=74?4:0)+(z.version>=75?4:0),0);
      auto word=[&](size_t at){uint32_t n=0;if(at+4<=post.size())std::memcpy(&n,post.data()+at,4);return n;};
      const size_t users=word(16);
      if(post.size()<48 || users>(post.size()-20)/8){error="A building occupancy record is invalid.";return false;}
      // Saved original customers are no longer performing those original jobs.
      post.erase(post.begin()+20,post.begin()+20+users*8);
      auto put=[&](size_t at,const auto &value){if(at+sizeof value<=post.size())std::memcpy(post.data()+at,&value,sizeof value);};
      const uint32_t previousIncome=word(8);
      const auto income=uint32_t(std::max(0.0,object.income));
      put(0,uint32_t(0));put(4,uint32_t(object.visitorsTotal));put(8,income);
      if(object.price>=0)put(12,float(object.price));
      put(16,uint32_t(0));put(20,uint32_t(std::max(0,object.fill)));
      put(24,uint32_t(int64_t(word(24))+int64_t(income)-previousIncome));
      put(32,uint32_t(object.visitorsNow));put(36,uint32_t(object.visitorsLast));
      if(z.version>=68)put(48,uint32_t(std::max(0.0,object.upkeep)));
    }
    z.objects.push_back(std::move(o));
  }
  auto make=[&](const std::string &cls,const std::string &sub,const std::string &type,float x,float y,float h) {
    ZooReader::ZooObject o;o.className=cls;o.subClass=sub;o.typeName=type;
    o.id=identity(0);o.decoded=true;position(o,x,y,h);return o;
  };
  // Paths and boundary pieces are rebuilt from the live grids; deleted pieces
  // must not reappear from the source archive.
  const auto &map=world.worldMap;
  for(int y=0;y<map.height;++y)for(int x=0;x<map.width;++x) {
    int type=map.getPathType(x,y);if(type<0)continue;
    const auto &path=map.pathTypes[type];
    z.objects.push_back(make("paths","paths",originalAsset("paths/"+path+".ai")?path:"path",x+.5f,y+.5f,ground(x+.5f,y+.5f)));
  }
  for(const auto &[edge,p]:world.fences.edges) {
    if(p.type<0 || p.type>=int(world.fences.typeList.size()))continue;
    const FenceType *baseType=&world.fences.typeList[p.type];
    if(!originalAsset(baseType->file)||baseType->key=="tankwall") {
      const std::string preferred=baseType->tank?(baseType->showFence?"tankwal3":"tankwal2"):
                                  baseType->zooWall?"zoowall":"chainlnk";
      const FenceType *fallback=nullptr;
      for(const auto &candidate:world.fences.typeList)
        if(candidate.tank==baseType->tank&&candidate.zooWall==baseType->zooWall&&
           candidate.showFence==baseType->showFence&&candidate.key!="tankwall"&&originalAsset(candidate.file)) {
          if(!fallback||candidate.key==preferred)fallback=&candidate;
          if(candidate.key==preferred)break;
        }
      if(!fallback){error="No original fence can represent this boundary.";return false;}
      baseType=fallback;
    }
    const auto &base=*baseType;
    const int ax=edge.alongX?edge.x:edge.x-1,ay=edge.alongX?edge.y-1:edge.y;
    const int bx=edge.x,by=edge.y;
    const auto *a=world.fences.exhibit(world.fences.exhibitAt(ax,ay));
    const auto *b=world.fences.exhibit(world.fences.exhibitAt(bx,by));
    const bool aTank=a&&a->tank,bTank=b&&b->tank;
    const FenceType *wall=&base;
    // zoo.exe checks the entire boundary for show-fence types (0x44e1e8).
    // Our shared edge grid can retain an ordinary piece from the neighbour.
    if(base.tank && ((aTank&&a->show)||(bTank&&b->show)) && !base.showFence) {
      for(const auto &candidate:world.fences.typeList)if(candidate.tank&&candidate.showFence&&originalAsset(candidate.file)) {
        wall=&candidate;if(candidate.key=="tankwal3")break;
      }
      if(!wall->showFence){error="The show tank has no installed show-wall type.";return false;}
    }
    const auto &type=*wall;
    std::vector<std::pair<int,int>> owners;
    if(base.tank) {
      if(aTank)owners.push_back({ax,ay});
      if(bTank&&(!aTank||a->id!=b->id))owners.push_back({bx,by});
    }
    if(owners.empty())owners.push_back({p.ownerX,p.ownerY});
    // A shared wall has one record for each tank's interior owner tile.
    for(const auto &[ox,oy]:owners) {
      const int facing=base.tank&&(aTank||bTank)?
        (edge.alongX?(oy==ay?4:0):(ox==ax?2:6)):p.facing;
      const float x=edge.x+(edge.alongX?.5f:0),y=edge.y+(edge.alongX?0:.5f);
      auto o=make(type.tank?"tankwall":"fences",type.key,p.gate&&owners.size()==1?"g":"f",
                  x,y,ground(ox+.5f,oy+.5f));
      if(facing==2)--o.x;if(facing==4)--o.y;
      const float life=std::max(0.f,p.life<0?float(type.life):p.life);
      o.direction=facing;o.hasCondition=true;o.life=uint32_t(std::ceil(life));
      o.strength=life>0?type.strength:0;
      for(const auto &record:old)if(record.className==o.className&&record.subClass==o.subClass &&
          record.typeName==o.typeName && record.x==o.x && record.y==o.y && record.hasCondition) {
        if(o.life<=record.life && o.life>0)
          o.strength=uint32_t(std::max(1,int(record.strength)-int(record.life-o.life)*type.decayDelta));
        break;
      }
      int show=-1,home=-1;o.portal=world.fences.showPortal(edge,show,home);
      if(z.version>=55) {
        // The original's decay timer is in milliseconds (0x434b1d).
        const uint32_t period=uint32_t(std::max(1,type.decayDelta))*12000;
        const double fraction=life>0?double(life)-(std::ceil(life)-1):1;
        Out condition;condition.raw(o.strength);condition.raw(o.life);
        condition.raw(period);condition.raw(uint32_t(std::max(1.0,period*fraction)));
        if(type.tank&&z.version>=101){condition.raw(uint8_t(o.portal));condition.raw(uint8_t(0));}
        o.suffixOverride.assign(condition.b.begin(),condition.b.end());
      }
      z.objects.push_back(std::move(o));
    }
  }
  for(const auto &filter:world.fences.filterList) {
    if(!originalAsset(filter.file))continue;
    const auto &type=world.fences.filterType(filter.file);
    auto o=make("filter",std::filesystem::path(type.file).stem().string(),"f",
                filter.x+type.footprintX*.5f,filter.y+type.footprintY*.5f,ground(filter.x,filter.y));
    o.name=filter.name;o.direction=direction(filter.dx,filter.dy);
    o.filterHealth=int32_t(filter.health);
    auto cost=[](double value) { return uint32_t(std::clamp(value, 0.0, double(UINT32_MAX))); };
    o.filterCosts = {cost(filter.upkeepLast), cost(filter.upkeepCurrent), cost(filter.upkeepTotal)};
    o.filterPeriod = uint32_t(std::max(1, (sim.year()-1)*12 + sim.month()+1));
#ifdef _WIN32
    SYSTEMTIME placed{}; FILETIME built{};
    const int month = std::max(0, filter.placedMonth);
    placed.wYear = WORD(2001 + month/12); placed.wMonth = WORD(month%12+1); placed.wDay = 1;
    if (SystemTimeToFileTime(&placed, &built))
      o.filterBuilt = uint64_t(built.dwLowDateTime) | (uint64_t(built.dwHighDateTime)<<32);
#endif
    z.objects.push_back(std::move(o));
  }
  for(const auto &item:world.items.list) {
    std::string type=item.food;
    if(type.empty()) {
      switch(item.kind) {
        case ZooItems::Kind::Food:type="herbchow";break;
        case ZooItems::Kind::Dung:type="poo";break;
        case ZooItems::Kind::Litter:type="trash";break;
      }
    }
    if(item.kind==ZooItems::Kind::Food && !originalAsset(ZooItems::foodFile(type)))continue;
    const bool dung=item.kind==ZooItems::Kind::Dung;
    // The stock poo definition has no cUnderwater flag. A generic engine
    // pile on a submerged tank floor makes zoo.exe refuse to fill the tank.
    // Keep that pile in the native snapshot; dry banks still export normally.
    const auto *habitat=world.fences.exhibit(world.fences.exhibitAt(int(std::floor(item.x)),int(std::floor(item.y))));
    if(dung && habitat && habitat->tank && habitat->water>0 &&
       ground(item.x,item.y)<habitat->floorHeight+world.fences.waterSubtiles(*habitat)*0.5f)continue;
    const bool litter=item.kind==ZooItems::Kind::Litter;
    auto o=make(dung||litter?"objects":"food",dung||litter?"other":"food",
                dung?"poo":litter?"trash":type,item.x,item.y,ground(item.x,item.y));
    o.food=int32_t(item.units);z.objects.push_back(std::move(o));
  }
  z.mapWidth=map.width;z.mapHeight=map.height;z.tiles.clear();
  const auto &camera = world.worldRenderer.getCamera();
  const int turn = world.worldRenderer.getRotation() & 3;
  const float scale = camera.zoom < 0.75f ? 0.5f : camera.zoom > 1.5f ? 2.0f : 1.0f;
  const float originX[] = {float(map.width), float(map.height-map.width), -float(map.height), 0};
  const float originY[] = {-float(map.width), -float(map.width+map.height), -float(map.height), 0};
  z.view[0] = scale < 1 ? -2 : scale > 1 ? 2 : 0;
  z.view[1] = (4-turn)&3;
  z.view[2] = int32_t(std::lround((originX[turn]*32-camera.x)*scale-camera.screenCenterX));
  z.view[3] = int32_t(std::lround((originY[turn]*16-camera.y)*scale-camera.screenCenterY));
  z.startCameraX=std::clamp(world.guests.entranceX>=0?world.guests.entranceX:z.startCameraX,0,map.width-1);
  z.startCameraY=std::clamp(world.guests.entranceY>=0?world.guests.entranceY:z.startCameraY,0,map.height-1);
  for(int y=0;y<map.height;++y)for(int x=0;x<map.width;++x) {
    const auto &t=map.tiles[y][x];ZooReader::ZooTile tile{};
    const int lo=*std::min_element(t.cornerHeight,t.cornerHeight+4);
    tile.height=t.height;tile.terrain=t.terrainType<18?t.terrainType:0;tile.edgeBits=t.edgeBits;
    const int order[]={0,3,2,1};
    for(int k=0;k<4;++k) {
      int n=t.cornerHeight[order[k]]-lo;
      if(n<0 || n>3){error="An elevation cannot be represented by the original slope format.";return false;}
      tile.cornerBits|=uint8_t(n<<(2*k));
    }
    z.tiles.push_back(tile);
  }
  const int currentYear=sim.year()+2000,currentMonth=sim.month()+1;
  z.date[0]=uint16_t(currentYear);z.date[1]=uint16_t(currentMonth);
  z.date[2]=uint16_t(sim.weekday());z.date[3]=uint16_t(sim.dayOfMonth());
  const double seconds=(sim.day-std::floor(sim.day))*86400.0;
  z.date[4]=uint16_t(seconds/3600);z.date[5]=uint16_t(int(seconds/60)%60);z.date[6]=uint16_t(int(seconds)%60);z.date[7]=0;
  // Convert elapsed simulation seconds back to the original calendar's FILETIME.
  auto dateTime=[&](double ago) {
    auto leap=[](int y){return y/4-y/100+y/400;};
    int64_t days=int64_t(currentYear-1601)*365+leap(currentYear-1)-leap(1600);
    for(int m=1;m<currentMonth;++m)days+=ZooSim::daysInMonth(m-1,currentYear-2000);
    const double now=double(days+sim.dayOfMonth()-1)*86400+seconds;
    return uint64_t(std::max(0.0,now-std::max(0.0,ago)/ZooSim::kSecondsPerDay*86400)*10000000);
  };
  for(const auto &m:world.animals.list) {
    const auto &type=world.animals.typeList[m.type];
    if(!originalAsset(type.file))continue;
    auto o=source("animals",m.id);o.id=identity(m.id,!o.payload.empty());
    o.subClass=type.key;o.typeName=m.baby?"y":(m.female?"f":"m");o.name=m.name;
    position(o,m.x,m.y,ground(m.x,m.y)+m.swimZ);o.direction=direction(m.fx,m.fy);
    o.hasAnimal=true;o.movementMode=type.underwater?3:2;
    auto &a=o.animal;a.happiness=int(m.happiness);a.hunger=int(m.hunger);a.health=int(m.health);a.energy=int(m.energy);
    a.female=m.female;a.dung=int(m.dirty);a.sick=m.sick;a.sleeping=m.sleeping;
    a.life=m.life;a.grow=m.grow;a.ready=m.ready;a.egg=m.egg;a.hatchIn=m.hatchIn;
    a.dying=m.dying;a.crated=m.boxed;a.escaped=m.escaped;a.rampaging=m.rampaging;a.rampageTicks=m.rampageTicks;
    a.lastAte=dateTime(world.animals.now-m.lastAte);a.lastSlept=dateTime(world.animals.now-m.lastSlept);
    a.checks={m.hungerCheck,m.healthCheck,m.energyCheck,m.otherCheck,a.checks[4],m.captivityCheck,
              m.habitatCheck,m.boredCheck,m.socialCheck,m.reproductionCheck};a.chaseCheck=m.huntCheck;
    z.objects.push_back(std::move(o));
  }
  for(const auto &m:world.staff.list) {
    const auto &type=world.staff.typeList[m.type];
    if(!originalAsset(type.file))continue;
    std::string cls=type.kind==Staff::Kind::Maint?"maint":type.kind==Staff::Kind::Guide?"tour":
                    type.kind==Staff::Kind::Helicopter?"helicopter":"keeper";
    auto o=source(cls,m.id);o.id=identity(m.id,!o.payload.empty());o.subClass=type.key;o.typeName=m.female?"f":"m";o.name=m.name;
    const bool wet=m.tank>=0 || m.water!=0 || m.climbing;
    const float x=wet?m.outX:m.x,y=wet?m.outY:m.y;position(o,x,y,ground(x,y));o.direction=direction(m.fx,m.fy);
    o.colours={uint8_t(m.hair),uint8_t(m.skin)};o.hasStaff=true;o.movementMode=2;
    auto &s=o.staff;s.work=int(m.workCheck);s.crowd=int(m.crowdClock);
    std::copy(std::begin(m.duties),std::end(m.duties),s.duties.begin());s.exhibits.clear();
    for(int id:m.exhibits)if(auto *ex=world.fences.exhibit(id))if(!ex->tiles.empty())s.exhibits.push_back(*ex->tiles.begin());
    z.objects.push_back(std::move(o));
  }
  for(const auto &g:world.guests.list) {
    const auto &type=world.guests.typeList[g.type];
    auto o=source("guests",g.id);o.id=identity(g.id,!o.payload.empty());o.subClass=type.key;o.typeName="";o.name=g.name;
    position(o,g.x,g.y,ground(g.x,g.y));o.direction=direction(g.fx,g.fy);
    o.colours.clear();for(int n:g.colours)o.colours.push_back(uint8_t(n));
    o.hasGuest=true;o.movementMode=2;auto &a=o.guest;
    a.happiness=int(g.happiness);a.hunger=int(g.hunger);a.thirst=int(g.thirst);a.energy=int(g.tired);a.bathroom=int(g.bathroom);
    a.sick=g.sick;a.leaving=g.leaving;a.afraid=g.sawEscape;a.arrived=dateTime(world.guests.clock-g.arrived);
    a.favourite=g.favourite>=0&&g.favourite<int(world.animals.typeList.size())?
        stockSpecies[world.animals.typeList[g.favourite].file]:-1;
    if(a.favourite==0)a.favourite=-1;
    std::copy(std::begin(g.inventory),std::end(g.inventory),a.items.begin());
    // Retain recent original species ids, then add species in exhibits
    // viewed in this engine. Never write building ids into this list.
    a.seen.erase(std::remove_if(a.seen.begin(),a.seen.end(),[&](uint32_t species) {
      return std::none_of(world.animals.typeList.begin(),world.animals.typeList.end(),
        [&](const auto &type){return stockSpecies[type.file]>0&&uint32_t(stockSpecies[type.file])==species;});
    }),a.seen.end());
    a.visited.clear();
    for(int id:g.seen)if(const auto *ex=world.fences.exhibit(id);ex&&!ex->tiles.empty()) {
      a.visited.push_back(*ex->tiles.begin());
      for(const auto &animal:world.animals.list)if(animal.exhibit==id&&originalAsset(world.animals.typeList[animal.type].file)) {
        const uint32_t species=stockSpecies[world.animals.typeList[animal.type].file];
        if(std::find(a.seen.begin(),a.seen.end(),species)==a.seen.end())a.seen.push_back(species);
      }
    }
    for(int id:g.visited)if(objectIds.count(id))if(const auto *object=world.placedObjects.byId(id))
      a.visited.emplace_back(int(std::floor(object->x)),int(std::floor(object->y)));
    if(a.seen.size()>5)a.seen.erase(a.seen.begin(),a.seen.end()-5);
    if(a.visited.size()>10)a.visited.erase(a.visited.begin(),a.visited.end()-10);
    a.guide=0;a.chaseCheck=g.chaseCheck;
    a.checks={g.hungerCheck,g.thirstCheck,g.bathroomCheck,g.leaveCheck,g.souvenirCheck,g.energyCheck,
              g.trashCheck,a.checks[7],g.viewCheck,a.checks[9]};
    z.objects.push_back(std::move(o));
  }
  // Ambient scenery is not editable in the live engine yet; retain it exactly.
  for(const auto &o:old)if(o.className=="ambient"){auto copy=o;copy.id=identity(o.id,!o.payload.empty());z.objects.push_back(std::move(copy));}
  Out showDefinitions;
  std::vector<std::vector<uint8_t>> programs;
  // Every live habitat's schedule is rebuilt below. Carrying the old table
  // too accumulated an unused copy on each original-game exchange.
  uint32_t programId=2000,showId=1000;
  auto encodeTrick=[&](Out &out,const Shows::Trick &trick,const std::string &file,int species) {
    for(const auto &program:z.showPrograms)if(program.species==species)
      for(const auto &saved:program.tricks)if(saved.id==trick.id) {
        out.b.insert(out.b.end(),saved.raw.begin(),saved.raw.end());return;
      }
    std::string behavior=trick.behavior,pre=trick.pre,post=trick.post,icon=trick.icon,gray=trick.icon;
    int normalHelp=0,grayHelp=0;
    if(auto *ai=world.resourceManager->getIniReader(file)) {
      const std::string section="m/tricks/"+trick.key;
      behavior=ai->get(section,"name",behavior);pre=ai->get(section,"keeperpretrick",pre);
      post=ai->get(section,"keeperposttrick",post);icon=ai->get(section,"normalIcon",icon);
      gray=ai->get(section,"grayedIcon",gray);normalHelp=ai->getInt(section,"normalHelpID",0);
      grayHelp=ai->getInt(section,"grayedHelpID",0);delete ai;
    }
    // The second saved byte is visibility, not the native trick type.
    out.raw(uint8_t(trick.available));out.raw(uint8_t(trick.visible));out.raw(uint16_t(trick.id));
    out.raw(int32_t(species));out.raw(int32_t(trick.building));
    out.str(behavior);out.str("");out.str(pre);out.str(post);
    out.raw(uint32_t(0));out.raw(int32_t(trick.complexity));out.raw(uint8_t(0));
    out.raw(int32_t(trick.satisfaction));out.raw(int32_t(trick.delta));out.raw(int32_t(trick.depth));out.raw(uint32_t(0));
    if(z.version>=103){out.raw(int32_t(normalHelp));out.raw(int32_t(grayHelp));out.str(icon);out.str(gray);}
  };
  const auto originalHabitats=z.habitats;z.habitats.clear();
  for(const auto &ex:world.fences.exhibitList)if(!ex.pocket&&!ex.tiles.empty()) {
    ZooReader::Habitat h;
    for(const auto &oldHab:originalHabitats)if(ex.tiles.count({oldHab.x,oldHab.y})){h=oldHab;break;}
    auto [x,y]=*ex.tiles.begin();h.x=x;h.y=y;h.name=ex.name;h.tank=ex.tank;h.show=ex.show;
    h.gateX=h.gateY=h.gateSide=-1;
    for(const auto &[edge,p]:world.fences.edges)if(p.gate&&ex.tiles.count({p.ownerX,p.ownerY})) {
      h.gateX=p.ownerX;h.gateY=p.ownerY;h.gateSide=p.facing;break;
    }
    h.tankHeight=ex.wallSub;h.waterLevel=int(std::lround(ex.water*std::max(0,ex.wallSub-1)));
    h.salt=ex.salt;h.filling=ex.filling;h.purity=int(ex.purity);
    if(ex.show) {
      const auto *show=world.shows.show(ex.id);
      int species=0;std::string file;
      if(show)for(const auto &type:world.animals.typeList)if(type.file==show->species && originalAsset(type.file)){species=stockSpecies[type.file];file=type.file;break;}
      uint16_t script=0xffff;
      const bool stockProgram=show&&species&&originalShowScript(world.resourceManager,world.shows,*show);
      if(!stockProgram)species=0; // keep unsupported custom programs native-only
      if(stockProgram) {
        if(++programId>=0xffff){error="Too many original show programs.";return false;}
        script=uint16_t(programId);Out program;
        program.raw(script);program.raw(int32_t(-1));program.raw(int32_t(species));
        program.raw(uint32_t(show->script.size()));
        for(const auto &key:show->script) {
          const auto *trick=world.shows.find(file,key);
          if(!trick){error="A saved show trick has no installed definition.";return false;}
          encodeTrick(program,*trick,file,species);
        }
        programs.emplace_back(program.b.begin(),program.b.end());
      }
      if(++showId>=0xffff){error="Too many original shows.";return false;}
      const int interval=show&&show->frequency>=0?world.shows.settings.interval[std::clamp(show->frequency,0,2)]:0;
      Out record;record.raw(int32_t(interval));record.raw(uint32_t(0));
      record.raw(uint32_t(species?1:0));if(species)record.raw(int32_t(species));
      record.raw(uint32_t(species?1:0));
      if(species) {
        record.raw(int32_t(species));record.raw(script);record.raw(uint16_t(0xffff));
        record.raw(float(show?show->admission:world.shows.settings.admission));
        for(int k=0;k<6;++k)record.raw(uint32_t(0));record.raw(dateTime(0));
      }
      if(z.version>=97){record.raw(uint32_t(0));record.raw(uint32_t(interval*1000));record.raw(uint16_t(showId));}
      if(z.version>=105)for(int k=0;k<3;++k)record.raw(uint32_t(0));
      if(z.version>=106)for(int k=0;k<6;++k)record.raw(uint32_t(0));
      if(z.version>=97) {
        record.raw(uint16_t(0));record.raw(uint16_t(showId));
        for(int k=0;k<3;++k)record.raw(uint32_t(0));
        record.raw(uint16_t(0));for(int k=0;k<3;++k)record.raw(uint8_t(0));
        for(int k=0;k<5;++k)record.raw(uint32_t(0));
      }
      h.rawShow.assign(record.b.begin(),record.b.end());
    }
    z.habitats.push_back(std::move(h));
  }
  if(hasShows && z.version>=97) {
    showDefinitions.raw(uint32_t(programs.size()));
    for(const auto &program:programs)showDefinitions.b.insert(showDefinitions.b.end(),program.begin(),program.end());
    showDefinitions.raw(uint16_t(programId));showDefinitions.raw(uint16_t(showId));
    z.replacementShows.assign(showDefinitions.b.begin(),showDefinitions.b.end());
  }
  z.nextUnitId=next;z.cash=float(sim.money);z.rating=uint32_t(std::clamp(sim.zooRating,0.0,100.0));
  z.admission=hud.zooInfo().admission;z.marketing=hud.marketingLevel;
  auto researchValue=[&](int kind,int id,uint32_t value) {
    for(auto &record:z.research)if(record.kind==kind&&record.id==id){record.value=value;return;}
    z.research.push_back({kind,id,value});
  };
  for(const auto &branch:Research::get().branches()) {
    researchValue(0,branch.nameId,uint32_t(branch.fundingLevel));
    for(size_t c=0;c<branch.categories.size();++c) {
      const auto &category=branch.categories[c];
      researchValue(1,category.nameId,c>=branch.enabled.size()||branch.enabled[c]);
      for(const auto &program:category.programs) {
        float progress=program.done?float(program.cost):program.progress;
        uint32_t bits=0;std::memcpy(&bits,&progress,4);researchValue(2,program.nameId,bits);
      }
    }
  }
  if(goals)z.awards=goals->awards;
  // The original has rolling 12-month and 20-year tables plus lifetime
  // totals. Apply native changes against the imported monthly baseline,
  // preserving the original split rows and older annual history.
  const bool imported=world.zooReader.hasGame();
  if(imported) {
    const auto &source = world.zooReader.getBooks();
    // Promoting an old land zoo to Marine Mania adds finance rows.
    // Keep its existing history inside the new validated table dimensions.
    for (size_t row=0; row<z.books.monthly.size(); ++row) {
      z.books.monthly[row].fill(0);
      if (row<source.monthly.size()) z.books.monthly[row]=source.monthly[row];
    }
    for (size_t row=0; row<z.books.yearly.size(); ++row) {
      std::fill(z.books.yearly[row].begin(), z.books.yearly[row].end(), 0);
      if (row<source.yearly.size())
        std::copy_n(source.yearly[row].begin(), std::min(source.yearly[row].size(),z.books.yearly[row].size()),z.books.yearly[row].begin());
    }
    for (size_t row=0; row<z.books.total.size(); ++row)
      z.books.total[row]=row<source.total.size()?source.total[row]:0;
    z.books.month=source.month;z.books.year=source.year;
  } else {
    for(auto &row:z.books.monthly)row.fill(0);
    for(auto &row:z.books.yearly)std::fill(row.begin(),row.end(),0);
    std::fill(z.books.total.begin(),z.books.total.end(),0);
    z.books.month=1;z.books.year=0;
  }
  const size_t financeRows=z.books.monthly.size();
  if(financeRows<15 || z.books.yearly.size()!=financeRows || z.books.total.size()!=financeRows ||
     std::any_of(z.books.yearly.begin(),z.books.yearly.end(),[](const auto &row){return row.size()!=20;})) {
    error="The original finance layout cannot represent these books.";return false;
  }
  const auto baseline=z.books.monthly;
  const int referenceYear=imported?int(world.zooReader.getDate()[0])-2000:1;
  const int referenceMonth=imported?int(world.zooReader.getDate()[1])-1:0;
  const int referenceSerial=(referenceYear-1)*12+referenceMonth;
  const int latestSerial=(sim.year()-1)*12+sim.month();
  const int referenceSlot=int(z.books.year)%20;
  auto yearSlot=[&](int year){return ((referenceSlot+year-referenceYear)%20+20)%20;};
  if(sim.year()-referenceYear>=20)
    for(auto &row:z.books.yearly)std::fill(row.begin(),row.end(),0);
  else for(int year=referenceYear+1;year<=sim.year();++year)
    for(auto &row:z.books.yearly)row[yearSlot(year)]=0;
  for(int serial=referenceSerial+1;serial<=latestSerial && serial<=referenceSerial+12;++serial)
    for(auto &row:z.books.monthly)row[serial%12]=0;
  const std::vector<std::vector<int>> rows={{4},{5},{8,20},{6,7},{30},{9,10},{3},{2},{0,1,28},{11,12,13},{19},{29}};
  for(const auto &month:sim.months) {
    const int serial=(month.year-1)*12+month.month;
    for(size_t line=0;line<rows.size();++line) {
      const int first=rows[line][0];
      if(size_t(first)>=financeRows)continue; // later expansions added finance categories
      double before=0;
      if(imported && serial<=referenceSerial && serial>referenceSerial-12)
        for(int row:rows[line])if(size_t(row)<financeRows)before+=baseline[row][month.month];
      const double target=month.lines[line]*(line>=ZooSim::Construction?-1:1);
      const float delta=float(target-before);
      if(serial>latestSerial-12) {
        double current=0;for(int row:rows[line])if(size_t(row)<financeRows)current+=z.books.monthly[row][month.month];
        z.books.monthly[first][month.month]+=float(target-current);
        if(line>0)z.books.monthly[14][month.month]+=delta*(line>=ZooSim::Construction?-1:1);
      }
      if(month.year>sim.year()-20)z.books.yearly[first][yearSlot(month.year)]+=delta;
      z.books.total[first]+=delta;
      if(line>0) {
        const float net=delta*(line>=ZooSim::Construction?-1:1);
        if(month.year>sim.year()-20)z.books.yearly[14][yearSlot(month.year)]+=net;
        z.books.total[14]+=net;
      }
    }
  }
  z.books.month=uint32_t(sim.month()+1);z.books.year=uint32_t(yearSlot(sim.year()));
  // Original thought references must never point to deleted or omitted
  // engine-only actors. Surviving original records keep their native ids.
  std::set<uint32_t> thoughtUnits;for(const auto &o:z.objects)if(o.id)thoughtUnits.insert(o.id);
  z.thoughts.erase(std::remove_if(z.thoughts.begin(),z.thoughts.end(),[&](const auto &thought) {
    const bool habitatMissing=thought.habitatX>=0&&thought.habitatY>=0&&
      std::none_of(z.habitats.begin(),z.habitats.end(),[&](const auto &habitat) {
        return habitat.x==thought.habitatX&&habitat.y==thought.habitatY;
      });
    return (thought.owner&&!thoughtUnits.count(thought.owner)) ||
           (thought.subject&&!thoughtUnits.count(thought.subject)) || habitatMissing;
  }),z.thoughts.end());
  return z.writeOriginal(bytes,error);
}

bool SaveGame::saveOriginal(const std::string &path,World &world,ZooSim &sim,
                            UiGameScreen &hud,std::string &error) {
  std::vector<uint8_t> bytes;
  if(!encodeOriginal(world,sim,hud,bytes,error))return false;
  const std::filesystem::path target(path),temporary=target.string()+".writing";
  FILE *file=std::fopen(temporary.string().c_str(),"wb");
  if(!file){error="The original-format output cannot be opened.";return false;}
  const bool written=std::fwrite(bytes.data(),1,bytes.size(),file)==bytes.size();
  const bool closed=std::fclose(file)==0;
  if(!written||!closed){std::filesystem::remove(temporary);error="The original-format write failed.";return false;}
#ifdef _WIN32
  if(!MoveFileExA(temporary.string().c_str(),target.string().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(temporary);error="The original-format output could not replace the destination.";return false;
  }
#else
  std::error_code ec;std::filesystem::rename(temporary,target,ec);
  if(ec){std::filesystem::remove(temporary);error=ec.message();return false;}
#endif
  error.clear();return true;
}

std::string SaveGame::checkOriginalWriter(World &world, const std::string &path, bool current) {
  if(path.rfind("wetdungcheck:",0)==0) {
    World probe(world.resourceManager);probe.loadBlank(32,32);
    ZooSim books;books.start(1000000,50);UiGameScreen hud(world.resourceManager,"","");
    if(!load(path.substr(13),probe,books,hud))return "Wet dung fixture load failed.";
    probe.items.list.clear();
    int tank=-1;std::pair<int,int> wet{},dry{};bool dryFound=false;
    for(const auto &ex:probe.fences.exhibitList)if(ex.tank&&ex.water>0&&!ex.tiles.empty()) {tank=ex.id;wet=*ex.tiles.begin();break;}
    for(int y=1;y<probe.worldMap.height&&!dryFound;++y)for(int x=1;x<probe.worldMap.width;++x)
      if(probe.fences.exhibitAt(x,y)<0) {dry={x,y};dryFound=true;break;}
    if(tank<0||!dryFound)return "Wet dung fixture needs a filled tank and dry ground.";
    const auto *habitat=probe.fences.exhibit(tank);const auto bank=*habitat->tiles.rbegin();
    auto *bankTile=probe.worldMap.getTileMutable(bank.first,bank.second);
    bankTile->height=habitat->floorHeight+habitat->wallSub+1;
    std::fill(std::begin(bankTile->cornerHeight),std::end(bankTile->cornerHeight),bankTile->height);
    probe.items.add(ZooItems::Kind::Dung,tank,wet.first+.5f,wet.second+.5f);
    probe.items.add(ZooItems::Kind::Dung,-1,dry.first+.5f,dry.second+.5f);
    probe.items.add(ZooItems::Kind::Dung,tank,bank.first+.5f,bank.second+.5f);
    std::vector<uint8_t> bytes;std::string error;ZooReader decoded;
    bool projected=encodeOriginal(probe,books,hud,bytes,error);
    if(projected) {AssetBuffer input;input.data=bytes.data();input.size=bytes.size();projected=decoded.load(input)&&decoded.hasGame();}
    int wetCount=0,dryCount=0,bankCount=0;
    for(const auto &object:decoded.getObjects())if(object.className=="objects"&&object.typeName=="poo") {
      if(object.x/64==wet.first&&object.y/64==wet.second)++wetCount;
      if(object.x/64==dry.first&&object.y/64==dry.second)++dryCount;
      if(object.x/64==bank.first&&object.y/64==bank.second)++bankCount;
    }
    std::string report=(projected&&wetCount==0&&dryCount==1&&bankCount==1?"PASS ":"FAIL ")+std::string("stock projection omits submerged dung and retains dry ground and tank-bank dung\n");
    for(const char *extension:{".zt1save",".zoo"}) {
      const auto file=std::filesystem::temp_directory_path()/("zt1-wet-dung-"+std::to_string(SDL_GetTicks64())+extension);
      const bool saved=save(file.string(),probe,books,hud,"freeform/ff01.scn");
      World restored(world.resourceManager);restored.loadBlank(32,32);
      const bool loaded=saved&&load(file.string(),restored,books,hud);
      bool wetKept=false,dryKept=false,bankKept=false;
      for(const auto &item:restored.items.list)if(item.kind==ZooItems::Kind::Dung) {
        wetKept=wetKept||(item.exhibit==tank&&int(item.x)==wet.first&&int(item.y)==wet.second);
        dryKept=dryKept||(item.exhibit==-1&&int(item.x)==dry.first&&int(item.y)==dry.second);
        bankKept=bankKept||(item.exhibit==tank&&int(item.x)==bank.first&&int(item.y)==bank.second);
      }
      report+=(loaded&&wetKept&&dryKept&&bankKept?"PASS ":"FAIL ")+std::string(extension)+" retains native submerged, dry and bank dung\n";
      std::error_code ec;std::filesystem::remove(file,ec);
    }
    return report;
  }
  if(path.rfind("groundcheck:",0)==0) {
    World probe(world.resourceManager);probe.loadBlank(32,32);
    ZooSim books;books.start(1000000,50);UiGameScreen hud(world.resourceManager,"","");
    if(!load(path.substr(12),probe,books,hud))return "Ground fixture load failed.";
    int tank=-1;std::pair<int,int> tileAt;
    for(auto &ex:probe.fences.exhibitList)if(ex.tank&&!ex.tiles.empty()) {
      tank=ex.id;tileAt=*ex.tiles.begin();
      Fences::Exhibit::Ground prior;prior.height=5;prior.terrain=4;
      prior.corner[0]=5;prior.corner[1]=6;prior.corner[2]=7;prior.corner[3]=6;
      ex.before[tileAt]=prior;break;
    }
    if(tank<0)return "Ground fixture needs a tank.";
    const auto before=probe.fences.exhibit(tank)->before;
    std::string report;
    const bool setting=Features::tankGroundBack;Features::tankGroundBack=true;
    for(const char *extension:{".zt1save",".zoo"}) {
      const auto file=std::filesystem::temp_directory_path()/("zt1-ground-"+std::to_string(SDL_GetTicks64())+extension);
      const bool saved=save(file.string(),probe,books,hud,"freeform/ff01.scn");
      World restored(world.resourceManager);restored.loadBlank(32,32);
      const bool loaded=saved&&load(file.string(),restored,books,hud);
      const auto *ex=restored.fences.exhibit(tank);
      bool equal=loaded&&ex&&ex->before.size()==before.size();
      if(ex)for(const auto &[at,g]:before) {
        const auto copy=ex->before.find(at);
        equal=equal&&copy!=ex->before.end()&&copy->second.height==g.height&&copy->second.terrain==g.terrain&&
          std::equal(std::begin(g.corner),std::end(g.corner),copy->second.corner);
      }
      Fences::Edge edge{};bool found=false;
      for(const auto &[at,p]:restored.fences.edges)if(p.tank==tank||p.tank2==tank){edge=at;found=true;break;}
      if(found)restored.fences.remove(edge,restored.worldMap);
      const auto *tile=restored.worldMap.getTile(tileAt.first,tileAt.second);
      const auto &expected=before.at(tileAt);
      const bool demolished=found&&tile&&tile->height==expected.height&&tile->terrainType==expected.terrain&&
        std::equal(std::begin(expected.corner),std::end(expected.corner),tile->cornerHeight);
      report+=(equal&&demolished?"PASS ":"FAIL ")+std::string(extension)+" exact native pre-tank slope and terrain through full save and demolition\n";
      std::error_code ec;std::filesystem::remove(file,ec);
    }
    Features::tankGroundBack=setting;return report;
  }
  if(path.rfind("subset:",0)==0) {
    const auto split=path.find(':',7);
    if(split==std::string::npos)return "Use subset:class,class:new-output.zoo";
    const std::string classes=","+path.substr(7,split-7)+",",output=path.substr(split+1);
    if(std::filesystem::exists(output))return "Choose a new diagnostic output.";
    ZooSim books;books.start(1000000,50);UiGameScreen hud(world.resourceManager,"","");
    restoreOriginalBooks(world,books);restoreOriginalManagers(world,hud);
    std::vector<uint8_t> bytes;std::string error;
    if(!encodeOriginal(world,books,hud,bytes,error))return error;
    AssetBuffer input;input.data=bytes.data();input.size=bytes.size();ZooReader reduced;
    if(!reduced.load(input)||!reduced.hasGame())return "Cannot parse current projection.";
    reduced.objects.erase(std::remove_if(reduced.objects.begin(),reduced.objects.end(),[&](const auto &o){
      return classes.find(","+o.className+",")==std::string::npos;
    }),reduced.objects.end());
    reduced.thoughts.clear();
    if(classes==",none,")reduced.habitats.clear();
    if(!reduced.writeOriginal(bytes,error))return error;
    const std::vector<char> data(bytes.begin(),bytes.end());
    return atomicSave(output,data)?"Diagnostic subset exported: "+output:"Cannot save diagnostic subset.";
  }
  if(path.rfind("verify:",0)==0) {
    World loaded(world.resourceManager);if(!loaded.loadBlank(32,32))return "Cannot create diagnostic world.";
    ZooSim books;books.start(0,0);UiGameScreen hud(world.resourceManager,"","");
    if(!load(path.substr(7),loaded,books,hud))return "Native diagnostic load failed: " + lastError();
    std::string report="Native "+std::to_string(loaded.worldMap.width)+"x"+std::to_string(loaded.worldMap.height)+
      " animals="+std::to_string(loaded.animals.list.size())+" staff="+std::to_string(loaded.staff.list.size())+
      " objects="+std::to_string(loaded.placedObjects.list.size())+" decks="+std::to_string(loaded.walkways.decks.size())+"\n";
    for(const auto &animal:loaded.animals.list)
      report+=std::to_string(animal.id)+" "+loaded.animals.typeList[animal.type].file+" "+animal.name+"\n";
    for(const auto &worker:loaded.staff.list)
      report+="staff "+std::to_string(worker.id)+" "+loaded.staff.typeList[worker.type].file+" "+worker.name+"\n";
    for(const auto &object:loaded.placedObjects.list)if(object.bought)
      report+="object "+std::to_string(object.id)+" "+PlacedObjects::fileOf(object)+"\n";
    for(const auto &filter:loaded.fences.filterList)
      report+="filter "+filter.file+" at "+std::to_string(filter.x)+","+std::to_string(filter.y)+"\n";
    for(const auto &item:loaded.items.list)if(item.kind==ZooItems::Kind::Food)
      report+="food "+item.food+" units="+std::to_string(item.units)+"\n";
    std::map<std::string,int> boundaries,paths;
    for(const auto &[edge,piece]:loaded.fences.edges)++boundaries[loaded.fences.typeList[piece.type].key];
    for(int type:loaded.worldMap.pathTile)if(type>=0&&type<int(loaded.worldMap.pathTypes.size()))
      ++paths[loaded.worldMap.pathTypes[type]];
    for(const auto &[key,count]:boundaries)report+="fence "+key+" count="+std::to_string(count)+"\n";
    for(const auto &[key,count]:paths)report+="path "+key+" count="+std::to_string(count)+"\n";
    report+=loaded.shows.command({"shows"});
    return report;
  }
  if(current) {
    if(std::filesystem::exists(path))return "Choose a new diagnostic output.";
    ZooSim books;books.start(world.zooReader.hasGame()?world.zooReader.getCash():1000000,
                             world.zooReader.hasGame()?world.zooReader.getRating():50);restoreOriginalBooks(world,books);
    UiGameScreen hud(world.resourceManager,"","");restoreOriginalManagers(world,hud);
    auto info=hud.zooInfo();if(world.zooReader.hasGame())info.admission=world.zooReader.getAdmission();hud.setZooInfo(info);
    std::string error;
    if(!saveOriginal(path,world,books,hud,error))return error;
    return "Current zoo exported: "+path;
  }
  if(path=="-" || path.rfind("read:",0)==0) {
    ZooReader inspected;std::vector<char> data;
    if(path!="-") {
      if(!readSaveBytes(path.substr(5),data,false))return "Cannot read diagnostic input.";
      AssetBuffer bytes;bytes.data=data.data();bytes.size=data.size();
      if(!inspected.load(bytes)||!inspected.hasGame())return "Cannot parse diagnostic input.";
    }
    const auto &reader=path=="-"?world.zooReader:inspected;
    const auto *view = reader.getView();
    std::string report = "view zoom=" + std::to_string(view[0]) + " rotation=" + std::to_string(view[1]) +
      " scroll=" + std::to_string(view[2]) + "," + std::to_string(view[3]) + "\n";
    report+="thoughts="+std::to_string(reader.getThoughts().size())+"\n";
    for(size_t n=0;n<std::min(size_t(8),reader.getThoughts().size());++n) {
      const auto &t=reader.getThoughts()[n];report+="thought text="+std::to_string(t.text)+" owner="+std::to_string(t.owner)+
        " subject="+std::to_string(t.subject)+" habitat="+std::to_string(t.habitatX)+","+std::to_string(t.habitatY)+
        " rendered="+originalThoughtText(reader,t,world.resourceManager)+"\n";
    }
    std::set<std::string> seen;
    std::map<std::string,int> counts;
    for(const auto &o:reader.getObjects()) {
      ++counts[o.className];
      auto key=o.className+"/"+o.subClass+"/"+o.typeName;
      if(seen.insert(key).second)report+=key+" side="+std::to_string(o.direction)+" portal="+std::to_string(o.portal)+" life="+std::to_string(o.life)+" strength="+std::to_string(o.strength)+" id="+std::to_string(o.id)+" xyz="+std::to_string(o.x)+","+std::to_string(o.y)+","+std::to_string(o.z)+" bytes="+std::to_string(o.payload.size())+"\n";
    }
    for(const auto &h:reader.getHabitats()) {
      report+="habitat "+h.name+" height="+std::to_string(h.tankHeight)+" water="+std::to_string(h.waterLevel)+" salt="+std::to_string(h.salt)+" show="+std::to_string(h.show)+" raw=";
      for(size_t i=0;i<std::min(size_t(160),h.rawShow.size());++i){char b[4];std::snprintf(b,sizeof b,"%02x",h.rawShow[i]);report+=b;}
      report+="\n";
    }
    if(reader.getVersion()>=97 && reader.archive.showsEnd>reader.archive.showsBegin) {
      const std::vector<char> raw(reader.original.begin(),reader.original.end());In in{raw};in.at=reader.archive.showsBegin;
      uint32_t scripts=in.raw<uint32_t>();
      report+="scripts="+std::to_string(scripts)+"\n";
      for(uint32_t n=0;n<scripts && n<1000 && !in.bad;++n) {
        const auto id=in.raw<uint16_t>();const auto nameId=in.raw<int32_t>(),species=in.raw<int32_t>();const auto tricks=in.raw<uint32_t>();
        report+="script "+std::to_string(id)+" nameId="+std::to_string(nameId)+" species="+std::to_string(species)+" n="+std::to_string(tricks)+"\n";
        for(uint32_t k=0;k<tricks && k<1000 && !in.bad;++k) {
          auto a=in.raw<uint32_t>(),b=in.raw<uint32_t>(),c=in.raw<uint32_t>();
          const auto s0=in.str(),s1=in.str(),s2=in.str(),s3=in.str();
          auto d=in.raw<uint32_t>(),e=in.raw<uint32_t>();auto flag=in.raw<uint8_t>();
          for(int j=0;j<4;++j)in.raw<uint32_t>();
          if(reader.getVersion()>=103){in.raw<uint64_t>();in.str();in.str();}
          if(k<4)report+="  "+std::to_string(a)+","+std::to_string(b)+","+std::to_string(c)+","+std::to_string(d)+","+
                             std::to_string(e)+","+std::to_string(flag)+" ["+s0+","+s1+","+s2+","+s3+"]\n";
        }
      }
    }
    for(const auto &[cls,n]:counts)report+=cls+"="+std::to_string(n)+" ";
    report+="cash="+std::to_string(reader.getCash())+" date="+std::to_string(reader.getDate()[0])+"-"+
            std::to_string(reader.getDate()[1])+"-"+std::to_string(reader.getDate()[3])+"\n";
    return report;
  }
  if (std::filesystem::exists(path)) return "Choose a new diagnostic output.";
  std::vector<uint8_t> bytes; std::string error;
  if (!world.zooReader.writeOriginal(bytes,error)) return error;
  FILE *f=std::fopen(path.c_str(),"wb");if(!f)return "Cannot open diagnostic output.";
  const bool ok=std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size();
  const bool closed=std::fclose(f)==0;
  return ok&&closed ? "Original-format writer validated: "+path : "Cannot write diagnostic output.";
}

// A map design: a freeform map made in the ZT1 Lab (Test Zoo > Save as a
// map), over one of the game's maps - that map's .zoo, then the world as it
// was made (putWorld) and the view it starts on. A mod's .scn names it in
// [start] design=, and World::loadFreeform puts it over the map (the
// "custommaps" switch, Features.hpp)
bool SaveGame::saveMap(const std::string &path, World &world, const std::string &over) {
  Out o;
  o.raw(kMapMagic);
  o.raw(kVersion);
  o.str(over);
  putWorld(&o, world);
  const Camera &cam = world.worldRenderer.getCamera();
  o.raw(cam.x);
  o.raw(cam.y);
  o.raw(cam.zoom);
  o.raw(world.worldRenderer.getRotation());
  o.str(world.shows.snapshot(false));
  FILE *fp = std::fopen(path.c_str(), "wb");
  if (!fp)
    return false;
  bool ok = std::fwrite(o.b.data(), 1, o.b.size(), fp) == o.b.size();
  std::fclose(fp);
  return ok;
}

bool SaveGame::applyMap(const std::vector<char> &b, World &world) {
  In i{b};
  if (i.raw<uint32_t>() != kMapMagic)
    return false;
  const uint32_t version = i.raw<uint32_t>();
  if (version < 8 || version > kVersion)
    return false;
  i.str(); // the map it's over (already loaded)
  if (!getWorld(&i, world, version))
    return false;
  Camera &cam = world.worldRenderer.getCamera();
  int cx = 0, cy = 0;
  float cz = 1;
  i.get(cx);
  i.get(cy);
  i.get(cz);
  int turn = i.raw<int>();
  if (!i.bad && turn >= 0 && turn < 4) {
    world.rotateView((turn - world.worldRenderer.getRotation() + 4) % 4);
    cam.x = cx;
    cam.y = cy;
    cam.zoom = cz;
  }
  if(version>=10 && !i.bad) {auto shows=i.str();if(!i.bad && !world.shows.restore(shows))i.bad=true;}
  else {world.shows.clear();world.shows.refresh();}
  world.reindex();
  return !i.bad;
}

Goals *SaveGame::goals = nullptr;
