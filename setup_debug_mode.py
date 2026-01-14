import os
import shutil
import subprocess
import sys

# --- CONFIG ---
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(ROOT_DIR, "build")
REL_DIR = os.path.join(BUILD_DIR, "Release")
DBG_DIR = os.path.join(BUILD_DIR, "Debug")

def build_debug():
    print("==========================================")
    print("      SETTING UP DEBUG ENVIRONMENT        ")
    print("==========================================\n")
    
    print("[1/2] Compiling Engine (Debug Mode)...")
    print("      (This might take a few minutes, please wait)\n")
    
    # Run CMake build with --config Debug
    cmd = ["cmake", "--build", ".", "--config", "Debug"]
    result = subprocess.run(cmd, cwd=BUILD_DIR)
    
    if result.returncode != 0:
        print("\n[ERROR] Debug build failed!")
        sys.exit(1)
    else:
        print("\n   -> Success: Debug executable created.")

def transfer_assets():
    print("\n[2/2] Cloning Assets from Release to Debug...")
    
    if not os.path.exists(REL_DIR):
        print("[ERROR] Release folder not found. Did you run the normal build first?")
        return

    if not os.path.exists(DBG_DIR):
        os.makedirs(DBG_DIR)
    
    count = 0
    # Copy files
    for item in os.listdir(REL_DIR):
        s = os.path.join(REL_DIR, item)
        d = os.path.join(DBG_DIR, item)
        
        # We want to copy .ztd, .dll, .ini, and folders
        # We do NOT want to overwrite the zt1-engine.exe we just built
        if "zt1-engine" in item.lower():
            continue
            
        if os.path.isfile(s):
            shutil.copy2(s, d)
            count += 1
        elif os.path.isdir(s):
            if os.path.exists(d):
                shutil.rmtree(d) # Clean replace for folders
            shutil.copytree(s, d)
            count += 1
            
    print(f"   -> Copied {count} assets to the Debug folder.")

if __name__ == "__main__":
    build_debug()
    transfer_assets()
    print("\n------------------------------------------------")
    print("[READY] Debug Environment is Ready!")
    print("------------------------------------------------")
    print("1. Open Visual Studio.")
    print("2. In the top toolbar, change 'Release' to 'Debug'.")
    print("3. Click the Green Play Button.")
    print("   -> Now when it crashes, it will show you the exact Code Line.")
    input("\nPress Enter to exit...")