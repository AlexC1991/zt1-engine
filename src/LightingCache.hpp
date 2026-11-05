#pragma once
#include "LightingAssets.hpp"
#include <cstdint>
#include <vector>
namespace LightingAssets {
// addition: generated geometry belongs to the player's local cache, never a
// shipped asset. Keys include the reconstructed silhouettes and algorithm version.
struct CacheStats {uint64_t hits=0,misses=0,writes=0,rejected=0,lastKey=0;};
CacheStats modelCacheStats();
bool loadModelCache(uint64_t key,Model &);
void saveModelCache(uint64_t key,const Model &);
std::vector<unsigned char> encodeModelCache(uint64_t key,const Model &);
bool decodeModelCache(uint64_t key,const std::vector<unsigned char> &,Model &);
}
