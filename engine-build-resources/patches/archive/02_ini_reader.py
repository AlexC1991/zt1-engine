import os

def apply(src_dir, root_dir):
    filename = "IniReader.cpp"
    filepath = os.path.join(src_dir, filename)
    
    new_code = """#include "IniReader.hpp"
#include <sstream>
#include <SDL2/SDL.h>
#include "Utils.hpp"
#include <algorithm>

void IniReader::printContent() {} 

IniReader::IniReader(const std::string &filename) {
  FILE * fd = fopen(filename.c_str(), "r");
  if (fd == NULL) return;
  fseek(fd, 0L, SEEK_END);
  size_t size = ftell(fd) + 1;
  fseek(fd, 0, SEEK_SET);
  void * buffer = malloc(size);
  if (!buffer) { fclose(fd); return; }
  fread(buffer, sizeof(char), size, fd);
  fclose(fd);
  load(std::string((char *) buffer, size));
  free(buffer);
}

IniReader::IniReader(void *buffer, size_t size) { load(std::string((char *) buffer, size)); }
IniReader::~IniReader() {}

std::string IniReader::get(const std::string &section, const std::string &key, const std::string &default_value) {
  std::string s_lower = Utils::string_to_lower(section);
  std::string k_lower = Utils::string_to_lower(key);
  if (content.count(s_lower) && content[s_lower].count(k_lower)) return content[s_lower][k_lower];
  return default_value;
}

std::vector<std::string> IniReader::getList(const std::string &section, const std::string &key, const std::vector<std::string> &default_value) {
  std::string val = get(section, key, "");
  if (val.empty()) return {};
  std::vector<std::string> list;
  std::stringstream stream(val);
  std::string entry;
  while(std::getline(stream, entry, ';')) list.push_back(entry);
  return list;
}

int IniReader::getInt(const std::string &section, const std::string &key, const int &default_value) {
  std::string val = get(section, key);
  return val.empty() ? default_value : std::stoi(val);
}

uint32_t IniReader::getUnsignedInt(const std::string &section, const std::string &key, const uint32_t default_value) {
  std::string val = get(section, key);
  return val.empty() ? default_value : std::stoul(val);
}

std::vector<int> IniReader::getIntList(const std::string &key, const std::string &value, const std::vector<int> &default_value) {
  std::vector<int> list;
  for(std::string s : getList(key, value)) if(!s.empty()) list.push_back(std::stoi(s));
  return list;
}

std::map<std::string, std::string> IniReader::getSection(const std::string &section) {
  if (!this->content.contains(section)) return {};
  return this->content[section];
}

std::vector<std::string> IniReader::getSections() {
  std::vector<std::string> sections;
  for (auto entry: this->content) sections.push_back(entry.first);
  return sections;
}

bool IniReader::isList(const std::string &section, const std::string &key) {
  return !get(section, key).empty() && get(section, key).find(";") != std::string::npos;
}

void IniReader::load(std::string file_content) {
  if (file_content.empty()) return;
  std::string current_section = "";
  std::stringstream ss(file_content);
  std::string line;
  
  while (std::getline(ss, line)) {
    line.erase(std::remove(line.begin(), line.end(), '\\r'), line.end());
    size_t first = line.find_first_not_of(" \\t");
    if (first == std::string::npos) continue;
    line = line.substr(first);
    if (line[0] == ';' || line[0] == '#') continue;
    
    if (line[0] == '[') {
      size_t end = line.find(']');
      if (end != std::string::npos) current_section = Utils::string_to_lower(line.substr(1, end - 1));
      continue;
    }
    
    size_t eq = line.find('=');
    if (eq != std::string::npos) {
      std::string key = line.substr(0, eq);
      std::string val = line.substr(eq + 1);
      
      size_t k_end = key.find_last_not_of(" \\t");
      if (k_end != std::string::npos) key = key.substr(0, k_end + 1);
      
      size_t v_start = val.find_first_not_of(" \\t");
      if (v_start != std::string::npos) val = val.substr(v_start);
      size_t v_end = val.find_last_not_of(" \\t");
      if (v_end != std::string::npos) val = val.substr(0, v_end + 1);
      
      if (!current_section.empty() && !key.empty()) {
        key = Utils::string_to_lower(key);
        if (!content[current_section].count(key)) content[current_section][key] = val;
        else content[current_section][key] += ";" + val;
      }
    }
  }
}
"""
    if os.path.exists(filepath):
        with open(filepath, "r", encoding="utf-8") as f:
            if f.read().replace('\r\n', '\n').strip() == new_code.replace('\r\n', '\n').strip(): return False

    with open(filepath, "w", encoding="utf-8") as f: f.write(new_code)
    print("    -> Updated IniReader.cpp")
    return True