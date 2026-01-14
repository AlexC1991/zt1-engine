import os
import shutil
import subprocess
import sys

# --- CONFIG ---
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
LOADER_FILE = os.path.join(ROOT_DIR, "vendor", "pe-resource-loader", "src", "pe_resource_loader.c")
BUILD_DIR = os.path.join(ROOT_DIR, "build")
REL_DIR = os.path.join(BUILD_DIR, "Release")

def step1_patch_code():
    print("\n[1/3] Applying Crash Fix to pe_resource_loader.c...")
    
    if not os.path.exists(LOADER_FILE):
        print("   -> [ERROR] File not found!")
        return

    with open(LOADER_FILE, "r", encoding="utf-8") as f:
        lines = f.readlines()

    new_lines = []
    patched = False
    
    for line in lines:
        new_lines.append(line)
        if "PeResourceLoader_GetDirectoryIdEntries(PeResourceLoader * loader" in line:
            # Check if we already patched it to avoid duplicates
            if "if (!loader)" not in "".join(lines):
                new_lines.append("    if (!loader) return NULL; // [PATCH] Safety check\n")
                patched = True

    if patched:
        shutil.copy(LOADER_FILE, LOADER_FILE + ".bak")
        with open(LOADER_FILE, "w", encoding="utf-8") as f:
            f.writelines(new_lines)
        print("   -> Success: Patch applied.")
    else:
        print("   -> Code is already patched.")

def step2_nuke_build():
    print("\n[2/3] Clearing old 64-bit build...")
    if os.path.exists(BUILD_DIR):
        try:
            shutil.rmtree(BUILD_DIR)
            print("   -> Build folder deleted.")
        except:
            print("   -> Could not delete folder (might be open). Proceeding anyway...")

def step3_build_x86():
    print("\n[3/3] Building Engine in 32-bit Mode (x86)...")
    print("      (This aligns the engine with the original 2001 game files)")
    
    if not os.path.exists(BUILD_DIR):
        os.makedirs(BUILD_DIR)
    
    # 1. Configure for Win32 (32-bit)
    cmd_config = ["cmake", "-A", "Win32", ".."]
    subprocess.run(cmd_config, cwd=BUILD_DIR)
    
    # 2. Build
    cmd_build = ["cmake", "--build", ".", "--config", "Release"]
    subprocess.run(cmd_build, cwd=BUILD_DIR)

if __name__ == "__main__":
    print("=== ZOO TYCOON DESKTOP FIXER ===")
    step1_patch_code()
    step2_nuke_build()
    step3_build_x86()
    
    print("\n-------------------------------------------------------")
    print("[ACTION REQUIRED] Files were wiped during rebuild.")
    print("1. Run 'python import_assets.py' to restore game files.")
    print("2. Run 'python fetch_dlls.py' to get the new DLLs.")
    print("3. Launch the game.")
    print("-------------------------------------------------------")
    input("Press Enter to exit...")