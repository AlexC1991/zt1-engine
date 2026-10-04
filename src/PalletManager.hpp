#ifndef PALLET_MANAGER_HPP
#define PALLET_MANAGER_HPP

#include <unordered_map>
#include <string>
#include <atomic>

#include "Pallet.hpp"


class PalletManager {
public:
  PalletManager();
  ~PalletManager();

  Pallet * getPallet(char * file_name_c);
  Pallet * getPallet(std::string &file_name);
  // A recoloured copy of a palette under its own name (staff: their hair
  // and skin swapped in)
  void addPallet(const std::string &name, const Pallet &pallet);
  // While loading art on this thread, frames naming 'from' use 'to'
  static void setOverride(const std::string &from, const std::string &to);
  static void clearOverrides();

  void addPalletFileToMap(const std::string &pallet_file, std::string ztd_file);
  void loadPalletMap(std::atomic<float> * progress, float progress_goal);

private:
  std::unordered_map<std::string, std::string> pallet_files_map;
  std::unordered_map<std::string, Pallet> pallet_map;

  void loadPallet(const std::string &file_name);

  bool loaded = false;
};

#endif // PALLET_MANAGER_HPP