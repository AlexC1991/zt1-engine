import subprocess
import sys
import os
import shutil

# --- CONFIG ---
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(ROOT_DIR, "build")
REL_DIR = os.path.join(BUILD_DIR, "Release")
SRC_DIR = os.path.join(ROOT_DIR, "src")

# ANSI Colors
class Colors:
    RESET = '\033[0m'
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m' 
    CYAN = '\033[96m'
    WHITE = '\033[97m'
    BOLD = '\033[1m'

def enable_windows_ansi():
    if os.name == 'nt':
        os.system('')

def print_header():
    enable_windows_ansi()
    os.system('cls' if os.name == 'nt' else 'clear')
    print(f"{Colors.CYAN}{Colors.BOLD}")
    print("╔══════════════════════════════════════════════════════════╗")
    print("║           OPEN ZOO TYCOON - MASTER BUILDER               ║")
    print("║                    (Full Fix Edition)                    ║")
    print("╚══════════════════════════════════════════════════════════╝")
    print(f"{Colors.RESET}")

def print_step(step, total, msg):
    print(f"\n{Colors.BLUE}{Colors.BOLD}[STEP {step}/{total}] {msg}{Colors.RESET}")
    print(f"    {'─' * 50}")

# --- STAGE 1: CLEANUP ---
def clean_build_folder():
    print_step(1, 5, "Cleaning Workspace")
    if os.path.exists(BUILD_DIR):
        print(f"  {Colors.YELLOW}⚠ Deleting old build folder...{Colors.RESET}")
        try:
            shutil.rmtree(BUILD_DIR)
            print(f"  {Colors.GREEN}✓ Workspace cleaned.{Colors.RESET}")
        except Exception as e:
            print(f"  {Colors.RED}✗ Could not delete build folder. Is the game open?{Colors.RESET}")
            print(f"    Error: {e}")
            sys.exit(1)
    else:
        print(f"  {Colors.GREEN}✓ Workspace is already clean.{Colors.RESET}")

# --- STAGE 2: CODE PATCHING ---
def apply_patches():
    print_step(2, 5, "Applying Source Code Patches")
    patches_applied = 0

    # =========================================================================
    # PATCH 1: ResourceManager.cpp - Path normalization + Animation crash fix
    # =========================================================================
    res_file = os.path.join(SRC_DIR, "ResourceManager.cpp")
    if os.path.exists(res_file):
        with open(res_file, "r", encoding="utf-8") as f:
            content = f.read()
        
        modified = False
        
        # Fix 1a: getResourceLocation - normalize paths
        old_getres = 'std::string ResourceManager::getResourceLocation(const std::string &resource_name) {'
        new_getres = '''std::string ResourceManager::getResourceLocation(const std::string &resource_name_raw) {
  // [PATCH] Normalize: lowercase + forward slashes
  std::string resource_name = Utils::string_to_lower(resource_name_raw);
  std::replace(resource_name.begin(), resource_name.end(), '\\\\', '/');'''
        
        if old_getres in content and "resource_name_raw" not in content:
            content = content.replace(old_getres, new_getres)
            modified = True
        
        # Fix 1b: load_resource_map - normalize stored keys
        old_loop = 'for (std::string file : ZtdFile::getFileList(current_archive)) {'
        new_loop = '''for (std::string file_raw : ZtdFile::getFileList(current_archive)) {
        // [PATCH] Normalize storage keys
        std::string file = Utils::string_to_lower(file_raw);
        std::replace(file.begin(), file.end(), '\\\\', '/');'''
        
        if old_loop in content and "file_raw" not in content:
            content = content.replace(old_loop, new_loop)
            modified = True
        
        # Fix 1c: getAnimation - prevent crash on missing animations
        old_getanim = '''Animation *ResourceManager::getAnimation(const std::string &file_name) {
  std::string resource_location = getResourceLocation(file_name);
  if (!resource_location.empty()) {
    return AniFile::getAnimation(&this->pallet_manager, resource_location, file_name);
  } else {
    std::string full_file_name = file_name + ".ani";
    resource_location = getResourceLocation(full_file_name);
    return AniFile::getAnimation(&this->pallet_manager, resource_location, full_file_name);
  }
}'''
        
        new_getanim = '''// [PATCH] Animation loading crash fix
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
        
        if old_getanim in content:
            content = content.replace(old_getanim, new_getanim)
            modified = True
        elif "// [PATCH] Animation loading crash fix" not in content:
            # Try alternative - just check if the function exists and isn't patched
            if "Animation *ResourceManager::getAnimation" in content and "Warning: Could not load animation" not in content:
                print(f"  {Colors.YELLOW}⚠ ResourceManager::getAnimation needs manual patching{Colors.RESET}")
        
        if modified:
            with open(res_file, "w", encoding="utf-8") as f:
                f.write(content)
            print(f"  {Colors.GREEN}✓ Patched: ResourceManager.cpp (path normalization + animation fix){Colors.RESET}")
            patches_applied += 1
        else:
            print(f"  {Colors.GREEN}✓ ResourceManager.cpp already patched or different version{Colors.RESET}")

    # =========================================================================
    # PATCH 2: AniFile.cpp - NULL safety checks
    # =========================================================================
    ani_file = os.path.join(SRC_DIR, "AniFile.cpp")
    if os.path.exists(ani_file):
        with open(ani_file, "r", encoding="utf-8") as f:
            content = f.read()
        
        modified = False
        
        # Add NULL check at start of getAnimation
        old_ani_start = '''Animation * AniFile::getAnimation(PalletManager * pallet_manager, const std::string &ztd_file, const std::string &file_name) {
  IniReader * ini_reader = ZtdFile::getIniReader(ztd_file, file_name);'''
        
        new_ani_start = '''// [PATCH] NULL safety checks
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
        
        if old_ani_start in content:
            content = content.replace(old_ani_start, new_ani_start)
            modified = True
        
        # Fix animation loading loop to handle NULL
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
            modified = True
        
        if modified:
            with open(ani_file, "w", encoding="utf-8") as f:
                f.write(content)
            print(f"  {Colors.GREEN}✓ Patched: AniFile.cpp (NULL safety checks){Colors.RESET}")
            patches_applied += 1
        else:
            print(f"  {Colors.GREEN}✓ AniFile.cpp already patched or different version{Colors.RESET}")

    # =========================================================================
    # PATCH 3: pe_resource_loader.c - NULL pointer crash fix
    # =========================================================================
    loader_file = os.path.join(ROOT_DIR, "vendor", "pe-resource-loader", "src", "pe_resource_loader.c")
    if os.path.exists(loader_file):
        with open(loader_file, "r", encoding="utf-8") as f:
            content = f.read()
        
        sig = "PeResourceLoader_GetDirectoryIdEntries(PeResourceLoader * loader"
        if sig in content and "if (!loader) return NULL; // [PATCH]" not in content:
            content = content.replace(sig, sig + "\n    if (!loader) return NULL; // [PATCH] Fix crash")
            with open(loader_file, "w", encoding="utf-8") as f:
                f.write(content)
            print(f"  {Colors.GREEN}✓ Patched: pe_resource_loader.c (NULL check){Colors.RESET}")
            patches_applied += 1
        else:
            print(f"  {Colors.GREEN}✓ pe_resource_loader.c already patched{Colors.RESET}")

    # =========================================================================
    # PATCH 4: IniReader.cpp - Missing return statement
    # =========================================================================
    ini_file = os.path.join(SRC_DIR, "IniReader.cpp")
    if os.path.exists(ini_file):
        with open(ini_file, "r", encoding="utf-8") as f:
            lines = f.readlines()
        
        patched = False
        new_lines = []
        for i, line in enumerate(lines):
            new_lines.append(line)
            if 'SDL_Log("Could not open ini file %s"' in line:
                if i + 1 < len(lines) and "return" not in lines[i + 1]:
                    new_lines.append("        return; // [PATCH] Prevent crash on missing ini\n")
                    patched = True
        
        if patched:
            with open(ini_file, "w", encoding="utf-8") as f:
                f.writelines(new_lines)
            print(f"  {Colors.GREEN}✓ Patched: IniReader.cpp (missing return){Colors.RESET}")
            patches_applied += 1
        else:
            print(f"  {Colors.GREEN}✓ IniReader.cpp already patched{Colors.RESET}")

    print(f"\n  {Colors.CYAN}Total patches applied: {patches_applied}{Colors.RESET}")
    return patches_applied

# --- STAGE 3: COMPILE ---
def run_build():
    print_step(3, 5, "Building Engine (32-bit)")
    if not os.path.exists(BUILD_DIR):
        os.makedirs(BUILD_DIR)
    
    print("  • Configuring CMake for x86...")
    result = subprocess.run(["cmake", "-A", "Win32", ".."], cwd=BUILD_DIR, 
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0:
        print(f"  {Colors.RED}✗ CMake configuration failed{Colors.RESET}")
        print(result.stdout)
        return False
    print(f"  {Colors.GREEN}✓ Configuration complete{Colors.RESET}")
    
    print("  • Compiling... (this may take a few minutes)")
    process = subprocess.Popen(
        ["cmake", "--build", ".", "--config", "Release"],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        cwd=BUILD_DIR, text=True
    )
    
    errors = []
    while True:
        line = process.stdout.readline()
        if not line and process.poll() is not None:
            break
        if line:
            line = line.strip()
            if "%]" in line:
                sys.stdout.write(f"\r    {line[:65].ljust(65)}")
                sys.stdout.flush()
            if "error" in line.lower() and "0 error" not in line.lower():
                errors.append(line)
    
    print()  # New line after progress
    
    if process.returncode == 0:
        print(f"  {Colors.GREEN}✓ Build successful!{Colors.RESET}")
        return True
    else:
        print(f"  {Colors.RED}✗ Build failed with {len(errors)} error(s){Colors.RESET}")
        for err in errors[:5]:
            print(f"    {Colors.RED}{err}{Colors.RESET}")
        return False

# --- STAGE 4: SETUP RUNTIME ---
def setup_runtime():
    print_step(4, 5, "Setting Up Runtime Environment")
    
    if not os.path.exists(REL_DIR):
        os.makedirs(REL_DIR)
    
    # Copy fonts folder
    fonts_src = os.path.join(ROOT_DIR, "fonts")
    fonts_dst = os.path.join(REL_DIR, "font")  # Note: engine expects "font" not "fonts"
    if os.path.exists(fonts_src) and not os.path.exists(fonts_dst):
        shutil.copytree(fonts_src, fonts_dst)
        print(f"  {Colors.GREEN}✓ Copied fonts folder{Colors.RESET}")
    elif os.path.exists(fonts_dst):
        print(f"  {Colors.GREEN}✓ Fonts folder already exists{Colors.RESET}")
    else:
        print(f"  {Colors.YELLOW}⚠ Fonts folder not found at {fonts_src}{Colors.RESET}")
    
    # Check for game assets
    required_ztd = ["animals.ztd", "ui.ztd", "sounds.ztd"]
    missing_ztd = [f for f in required_ztd if not os.path.exists(os.path.join(REL_DIR, f))]
    
    if missing_ztd:
        print(f"  {Colors.YELLOW}⚠ Missing ZTD files: {', '.join(missing_ztd)}{Colors.RESET}")
        import_script = os.path.join(ROOT_DIR, "import_assets.py")
        if os.path.exists(import_script):
            print(f"  {Colors.CYAN}ℹ Running import_assets.py...{Colors.RESET}")
            subprocess.run([sys.executable, import_script])
        else:
            print(f"  {Colors.YELLOW}⚠ Please copy ZTD files from your Zoo Tycoon installation{Colors.RESET}")
    else:
        print(f"  {Colors.GREEN}✓ Game assets found{Colors.RESET}")
    
    # Create zoo.ini with correct settings
    ini_path = os.path.join(REL_DIR, "zoo.ini")
    zoo_ini = """[resource]
path=.

[ui]
noMenuMusic=0
menuMusic=sounds/mainmenu.wav
progressRed=255
progressGreen=200
progressBlue=0
progressLeft=229
progressTop=417
progressRight=583
progressBottom=440

[Paths]
path=.
ai=ai
anim=anim
maps=maps
scenarios=scenarios
saves=saves
zoo=zoo

[lib]
res=res0.dll
lang=lang0.dll

[user]
fullscreen=0
screenwidth=1280
screenheight=720
colourdepth=32
msaa=0
"""
    with open(ini_path, "w") as f:
        f.write(zoo_ini)
    print(f"  {Colors.GREEN}✓ Created zoo.ini with correct settings{Colors.RESET}")
    
    # Create required folders
    folders = ["dlupdate", "updates", "xpack1", "xpack2", "dupdate", "zupdate", "zupdate1", "loc"]
    for folder in folders:
        folder_path = os.path.join(REL_DIR, folder)
        if not os.path.exists(folder_path):
            os.makedirs(folder_path)
    print(f"  {Colors.GREEN}✓ Created game folders{Colors.RESET}")
    
    # Copy DLLs from build subfolders
    dll_count = 0
    for root, _, files in os.walk(BUILD_DIR):
        for file in files:
            if file.endswith(".dll") and "Release" not in root:
                src = os.path.join(root, file)
                dst = os.path.join(REL_DIR, file)
                if not os.path.exists(dst):
                    try:
                        shutil.copy2(src, dst)
                        dll_count += 1
                    except:
                        pass
    if dll_count > 0:
        print(f"  {Colors.GREEN}✓ Copied {dll_count} DLL files{Colors.RESET}")
    
    # Create fallback DLL copies
    if os.path.exists(os.path.join(REL_DIR, "res0.dll")):
        shutil.copy2(os.path.join(REL_DIR, "res0.dll"), os.path.join(REL_DIR, "res.dll"))
    if os.path.exists(os.path.join(REL_DIR, "lang0.dll")):
        shutil.copy2(os.path.join(REL_DIR, "lang0.dll"), os.path.join(REL_DIR, "lang.dll"))

# --- STAGE 5: LAUNCH ---
def launch_game():
    print_step(5, 5, "Ready to Launch")
    
    exe_path = os.path.join(REL_DIR, "zt1-engine.exe")
    if not os.path.exists(exe_path):
        print(f"  {Colors.RED}✗ Executable not found: {exe_path}{Colors.RESET}")
        return
    
    print(f"  {Colors.GREEN}✓ Executable found: zt1-engine.exe{Colors.RESET}")
    print()
    
    choice = input(f"  {Colors.CYAN}Launch game now? (Y/N): {Colors.RESET}").strip().upper()
    if choice == "Y":
        print(f"  {Colors.GREEN}Launching...{Colors.RESET}")
        subprocess.Popen([exe_path], cwd=REL_DIR)

# --- MAIN ---
if __name__ == "__main__":
    print_header()
    
    clean_build_folder()
    apply_patches()
    
    if run_build():
        setup_runtime()
        
        print(f"\n{Colors.GREEN}{Colors.BOLD}{'═' * 60}{Colors.RESET}")
        print(f"  {Colors.GREEN}{Colors.BOLD}BUILD COMPLETE!{Colors.RESET}")
        print(f"  {Colors.WHITE}Location: {REL_DIR}{Colors.RESET}")
        print(f"{Colors.GREEN}{Colors.BOLD}{'═' * 60}{Colors.RESET}")
        
        launch_game()
    else:
        print(f"\n{Colors.RED}{Colors.BOLD}{'═' * 60}{Colors.RESET}")
        print(f"  {Colors.RED}{Colors.BOLD}BUILD FAILED{Colors.RESET}")
        print(f"  {Colors.WHITE}Check the errors above and try again{Colors.RESET}")
        print(f"{Colors.RED}{Colors.BOLD}{'═' * 60}{Colors.RESET}")
    
    print()
    input("Press Enter to exit...")