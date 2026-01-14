"""
ZT1-Engine Patch Script
Patches ResourceManager.cpp and AniFile.cpp to fix animation loading crashes
"""

import os
import shutil

# Path to the source directory
SRC_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src")

def backup_file(filepath):
    """Create a backup of the file if one doesn't exist"""
    backup_path = filepath + ".backup"
    if not os.path.exists(backup_path):
        shutil.copy2(filepath, backup_path)
        print(f"  Created backup: {backup_path}")
    return backup_path

def patch_resource_manager():
    """Patch ResourceManager.cpp to handle missing animations gracefully"""
    filepath = os.path.join(SRC_DIR, "ResourceManager.cpp")
    
    if not os.path.exists(filepath):
        print(f"  ERROR: {filepath} not found!")
        return False
    
    backup_file(filepath)
    
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Check if already patched
    if "// PATCHED: Animation loading crash fix" in content:
        print("  Already patched!")
        return True
    
    # Old function to find and replace
    old_function = '''Animation *ResourceManager::getAnimation(const std::string &file_name) {
  std::string resource_location = getResourceLocation(file_name);
  if (!resource_location.empty()) {
    return AniFile::getAnimation(&this->pallet_manager, resource_location, file_name);
  } else {
    std::string full_file_name = file_name + ".ani";
    resource_location = getResourceLocation(full_file_name);
    return AniFile::getAnimation(&this->pallet_manager, resource_location, full_file_name);
  }
}'''

    # New patched function
    new_function = '''// PATCHED: Animation loading crash fix
Animation *ResourceManager::getAnimation(const std::string &file_name) {
  // First try the exact path
  std::string resource_location = getResourceLocation(file_name);
  if (!resource_location.empty()) {
    Animation* anim = AniFile::getAnimation(&this->pallet_manager, resource_location, file_name);
    if (anim != nullptr) {
      return anim;
    }
  }
  
  // Try with .ani extension
  std::string full_file_name = file_name + ".ani";
  resource_location = getResourceLocation(full_file_name);
  if (!resource_location.empty()) {
    Animation* anim = AniFile::getAnimation(&this->pallet_manager, resource_location, full_file_name);
    if (anim != nullptr) {
      return anim;
    }
  }
  
  // Animation not found - return nullptr instead of crashing
  SDL_Log("Warning: Could not load animation: %s", file_name.c_str());
  return nullptr;
}'''

    if old_function in content:
        content = content.replace(old_function, new_function)
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print("  Patched successfully!")
        return True
    else:
        print("  WARNING: Could not find exact function to patch.")
        print("  Attempting alternative patch method...")
        
        # Try to find the function with different whitespace
        import re
        pattern = r'Animation \*ResourceManager::getAnimation\(const std::string &file_name\) \{[^}]+\{[^}]+\}[^}]+\}'
        
        if re.search(pattern, content):
            content = re.sub(pattern, new_function, content)
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(content)
            print("  Patched successfully (alternative method)!")
            return True
        else:
            print("  ERROR: Could not patch ResourceManager.cpp")
            print("  Please apply the patch manually.")
            return False

def patch_anifile():
    """Patch AniFile.cpp to handle NULL cases"""
    filepath = os.path.join(SRC_DIR, "AniFile.cpp")
    
    if not os.path.exists(filepath):
        print(f"  ERROR: {filepath} not found!")
        return False
    
    backup_file(filepath)
    
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Check if already patched
    if "// PATCHED: NULL safety checks" in content:
        print("  Already patched!")
        return True
    
    # Old function start
    old_start = '''Animation * AniFile::getAnimation(PalletManager * pallet_manager, const std::string &ztd_file, const std::string &file_name) {
  IniReader * ini_reader = ZtdFile::getIniReader(ztd_file, file_name);'''

    # New function start with safety checks
    new_start = '''// PATCHED: NULL safety checks
Animation * AniFile::getAnimation(PalletManager * pallet_manager, const std::string &ztd_file, const std::string &file_name) {
  // Safety check - if ztd_file is empty, we can't load anything
  if (ztd_file.empty()) {
    SDL_Log("Warning: Empty ZTD file path for animation: %s", file_name.c_str());
    return nullptr;
  }

  IniReader * ini_reader = ZtdFile::getIniReader(ztd_file, file_name);
  if (ini_reader == nullptr) {
    SDL_Log("Warning: Could not read ini for animation: %s", file_name.c_str());
    return nullptr;
  }'''

    if old_start in content:
        content = content.replace(old_start, new_start)
    else:
        print("  WARNING: Could not find exact function start to patch.")
        print("  Trying alternative approach...")
        
        # Try finding just the function signature
        old_sig = "Animation * AniFile::getAnimation(PalletManager * pallet_manager, const std::string &ztd_file, const std::string &file_name) {"
        if old_sig in content:
            # Insert safety checks after the opening brace
            insert_code = '''
  // PATCHED: NULL safety checks
  // Safety check - if ztd_file is empty, we can't load anything
  if (ztd_file.empty()) {
    SDL_Log("Warning: Empty ZTD file path for animation: %s", file_name.c_str());
    return nullptr;
  }
'''
            content = content.replace(old_sig, old_sig + insert_code)
        else:
            print("  ERROR: Could not patch AniFile.cpp")
            return False
    
    # Also patch the animation loading loop to handle NULL
    old_loop = '''  for (std::string direction : ini_reader->getList("animation", "animation")) {
    (*animations)[direction] = AniFile::loadAnimationData(pallet_manager, ztd_file, directory + "/" + direction);
    (*animations)[direction]->width = width;
    (*animations)[direction]->height = height;
  }

  return new Animation(animations);
}'''

    new_loop = '''  bool has_valid_animation = false;
  for (std::string direction : ini_reader->getList("animation", "animation")) {
    AnimationData* anim_data = AniFile::loadAnimationData(pallet_manager, ztd_file, directory + "/" + direction);
    if (anim_data != nullptr) {
      (*animations)[direction] = anim_data;
      (*animations)[direction]->width = width;
      (*animations)[direction]->height = height;
      has_valid_animation = true;
    } else {
      SDL_Log("Warning: Could not load animation direction %s from %s", direction.c_str(), directory.c_str());
    }
  }

  // If no animations loaded successfully, clean up and return nullptr
  if (!has_valid_animation) {
    delete animations;
    return nullptr;
  }

  return new Animation(animations);
}'''

    if old_loop in content:
        content = content.replace(old_loop, new_loop)
    else:
        print("  WARNING: Could not patch animation loop (may already be different)")
    
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(content)
    
    print("  Patched successfully!")
    return True

def main():
    print("=" * 60)
    print("  ZT1-Engine Animation Crash Fix Patcher")
    print("=" * 60)
    print()
    
    if not os.path.exists(SRC_DIR):
        print(f"ERROR: Source directory not found: {SRC_DIR}")
        print("Make sure this script is in the zt1-engine root folder.")
        input("\nPress Enter to exit...")
        return
    
    print("[1/2] Patching ResourceManager.cpp...")
    rm_success = patch_resource_manager()
    
    print()
    print("[2/2] Patching AniFile.cpp...")
    ani_success = patch_anifile()
    
    print()
    print("=" * 60)
    if rm_success and ani_success:
        print("  All patches applied successfully!")
        print()
        print("  Now rebuild the engine:")
        print("    cd build")
        print("    cmake --build . --config Release")
    else:
        print("  Some patches failed. Please check the errors above.")
    print("=" * 60)
    
    input("\nPress Enter to exit...")

if __name__ == "__main__":
    main()