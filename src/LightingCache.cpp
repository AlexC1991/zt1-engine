#include "LightingCache.hpp"
#include "Paths.hpp"
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <type_traits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace LightingAssets {
namespace {
constexpr uint32_t format=1,maxPayload=16*1024*1024;
constexpr uint64_t maxCacheBytes=256ull*1024*1024;
CacheStats counts;
struct Header {uint64_t magic,key;uint32_t version,rawBytes,packedBytes,checksum;};
constexpr uint64_t magic=0x314548434143544cull;
static_assert(sizeof(Header)==32 && sizeof(Point)==12 && sizeof(Face)==72);
struct Shape {Point minimum,step;int32_t nx,ny,nz,occupied;uint32_t cells,faces;};
static_assert(sizeof(Shape)==48);
bool finite(Point p){return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::abs(p.x)<1000 && std::abs(p.y)<1000 && std::abs(p.z)<1000;}
bool valid(const Model &m) {
 if(m.nx<1 || m.nx>64 || m.ny<1 || m.ny>64 || m.nz<1 || m.nz>96)return false;
 if(!finite(m.minimum) || !finite(m.step) || m.step.x<=0 || m.step.y<=0 || m.step.z<=0)return false;
 if(m.solid.size()!=size_t(m.nx)*m.ny*m.nz || m.faces.size()>100000 || m.occupied<0 || size_t(m.occupied)>m.solid.size())return false;
 for(const auto &f:m.faces){if(!finite(f.normal) || !finite(f.outward))return false;for(auto p:f.p)if(!finite(p))return false;}
 return true;
}
std::filesystem::path directory() {
 if(const char *setting=std::getenv("ZT_LIGHTING_CACHE_DIR"))return *setting?std::filesystem::path(setting):std::filesystem::path{};
 return std::filesystem::path(Paths::dataDir())/"cache"/"lighting-v1";
}
std::filesystem::path file(uint64_t key) {
 const auto dir=directory();if(dir.empty())return {};
 char name[32];std::snprintf(name,sizeof name,"%016llx.ltc",static_cast<unsigned long long>(key));return dir/name;
}
// addition: bound only this cache's own files. Other files and source assets
// are never removed. A failed prune/write simply falls back to RAM geometry.
void prune(const std::filesystem::path &dir) {
 using namespace std::filesystem;std::error_code ec;
 struct Entry {path name;file_time_type time;uint64_t bytes;};std::vector<Entry> entries;uint64_t total=0;
 for(directory_iterator it(dir,ec),end;!ec && it!=end;it.increment(ec)) {
  const auto &e=*it;if(!e.is_regular_file(ec) || e.path().extension()!=".ltc")continue;
  const auto stem=e.path().stem().string();if(stem.size()!=16 || !std::all_of(stem.begin(),stem.end(),[](unsigned char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}))continue;
  const auto bytes=e.file_size(ec);const auto time=e.last_write_time(ec);if(ec)break;
  entries.push_back({e.path(),time,bytes});total+=bytes;
 }
 if(ec || (total<=maxCacheBytes && entries.size()<=2048))return;
 std::sort(entries.begin(),entries.end(),[](const auto &a,const auto &b){return a.time<b.time;});
 size_t left=entries.size();for(const auto &e:entries){if(total<=maxCacheBytes && left<=2048)break;if(remove(e.name,ec)){total-=e.bytes;--left;}ec.clear();}
}
}
CacheStats modelCacheStats(){return counts;}
// addition: local compressed geometry with bounded allocation, checksum and
// structural validation. A malformed cache is a miss, not a gameplay error.
std::vector<unsigned char> encodeModelCache(uint64_t key,const Model &m) {
 if(!valid(m))return {};
 Shape shape{m.minimum,m.step,m.nx,m.ny,m.nz,m.occupied,uint32_t(m.solid.size()),uint32_t(m.faces.size())};
 const size_t length=sizeof shape+m.solid.size()+m.faces.size()*sizeof(Face);if(length>maxPayload)return {};
 std::vector<unsigned char> raw(length);std::memcpy(raw.data(),&shape,sizeof shape);
 std::memcpy(raw.data()+sizeof shape,m.solid.data(),m.solid.size());
 std::memcpy(raw.data()+sizeof shape+m.solid.size(),m.faces.data(),m.faces.size()*sizeof(Face));
 uLongf packed=compressBound(uLong(raw.size()));std::vector<unsigned char> data(sizeof(Header)+packed);
 if(compress2(data.data()+sizeof(Header),&packed,raw.data(),uLong(raw.size()),Z_BEST_SPEED)!=Z_OK)return {};
 Header header{magic,key,format,uint32_t(raw.size()),uint32_t(packed),uint32_t(crc32(0,raw.data(),uInt(raw.size())))};
 std::memcpy(data.data(),&header,sizeof header);data.resize(sizeof header+packed);return data;
}
bool decodeModelCache(uint64_t key,const std::vector<unsigned char> &data,Model &out) {
 if(data.size()<sizeof(Header) || data.size()>maxPayload+sizeof(Header))return false;
 Header header{};std::memcpy(&header,data.data(),sizeof header);
 if(header.magic!=magic || header.key!=key || header.version!=format || header.rawBytes<sizeof(Shape) || header.rawBytes>maxPayload || header.packedBytes!=data.size()-sizeof header)return false;
 std::vector<unsigned char> raw(header.rawBytes);uLongf size=raw.size();
 if(uncompress(raw.data(),&size,data.data()+sizeof header,header.packedBytes)!=Z_OK || size!=raw.size() || uint32_t(crc32(0,raw.data(),uInt(raw.size())))!=header.checksum)return false;
 Shape shape{};std::memcpy(&shape,raw.data(),sizeof shape);
 if(shape.nx<1 || shape.nx>64 || shape.ny<1 || shape.ny>64 || shape.nz<1 || shape.nz>96 || shape.faces>100000 || shape.cells!=uint32_t(shape.nx)*shape.ny*shape.nz)return false;
 if(sizeof shape+uint64_t(shape.cells)+uint64_t(shape.faces)*sizeof(Face)!=raw.size())return false;
 Model m;m.minimum=shape.minimum;m.step=shape.step;m.nx=shape.nx;m.ny=shape.ny;m.nz=shape.nz;m.occupied=shape.occupied;
 m.solid.assign(raw.begin()+sizeof shape,raw.begin()+sizeof shape+shape.cells);m.faces.resize(shape.faces);
 std::memcpy(m.faces.data(),raw.data()+sizeof shape+shape.cells,shape.faces*sizeof(Face));if(!valid(m))return false;
 out=std::move(m);return true;
}
bool loadModelCache(uint64_t key,Model &out) {
 counts.lastKey=key;const auto path=file(key);std::error_code ec;
 if(path.empty() || !std::filesystem::is_regular_file(path,ec)){++counts.misses;return false;}
 const auto length=std::filesystem::file_size(path,ec);if(ec || length>maxPayload+sizeof(Header)){++counts.rejected;++counts.misses;return false;}
 std::vector<unsigned char> bytes(size_t(length),0);std::ifstream in(path,std::ios::binary);
 if(!in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())) || !decodeModelCache(key,bytes,out)){++counts.rejected;++counts.misses;return false;}
 std::filesystem::last_write_time(path,std::filesystem::file_time_type::clock::now(),ec);
 ++counts.hits;return true;
}
void saveModelCache(uint64_t key,const Model &model) {
 const auto path=file(key);if(path.empty())return;auto bytes=encodeModelCache(key,model);if(bytes.empty())return;
 std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);if(ec)return;
 // Timestamped temporary files keep interrupted writes out of the live cache.
 auto temp=path;temp+="."+std::to_string(SDL_GetPerformanceCounter())+".partial";
 {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()))){out.close();std::filesystem::remove(temp,ec);return;}}
#ifdef _WIN32
 // Atomic replacement repairs stale/corrupt entries without a delete window.
 if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING)){std::filesystem::remove(temp,ec);return;}
#else
 std::filesystem::rename(temp,path,ec);if(ec){std::filesystem::remove(temp,ec);return;}
#endif
 ++counts.writes;if(counts.writes==1 || counts.writes%32==0)prune(path.parent_path());
}
}
