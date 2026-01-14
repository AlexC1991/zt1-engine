import os
import shutil
import subprocess
import sys

# --- CONFIG ---
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(ROOT_DIR, "build")
REL_DIR = os.path.join(BUILD_DIR, "Release")

def nuke_build():
    print("[1/4] Cleaning old 64-bit build artifacts...")
    if os.path.exists(BUILD_DIR):
        try:
            shutil.rmtree(BUILD_DIR)
            print("   -> Build folder deleted.")
        except Exception as e:
            print(f"   -> Warning: Could not delete build folder: {e}")
            print("   -> Please manually delete the 'build' folder and run this again.")
            sys.exit(1)
    else:
        print("   -> Clean start.")

def configure_x86():
    print("\n[2/4] Configuring for 32-bit (x86)...")
    
    if not os.path.exists(BUILD_DIR):
        os.makedirs(BUILD_DIR)
    
    # The magic flag is -A Win32. This forces VS to use the 32-bit compiler.
    cmd = ["cmake", "-A", "Win32", ".."]
    
    result = subprocess.run(cmd, cwd=BUILD_DIR)
    if result.returncode != 0:
        print("\n[ERROR] CMake Configuration failed!")
        sys.exit(1)

def build_engine():
    print("\n[3/4] Compiling 32-bit Engine...")
    print("      (This will take a few minutes - rebuilding libraries)")
    
    cmd = ["cmake", "--build", ".", "--config", "Release"]
    
    # We pipe output to show progress
    process = subprocess.Popen(cmd, cwd=BUILD_DIR)
    process.wait()
    
    if process.returncode != 0:
        print("\n[ERROR] Build Failed.")
        sys.exit(1)

def transfer_assets():
    print("\n[4/4] Restoring Game Assets...")
    # We need to create the Release folder if cmake didn't make it yet
    if not os.path.exists(REL_DIR):
        os.makedirs(REL_DIR)
        
    print("   -> Please run 'import_assets.py' manually after this to copy your game files back.")
    print("   -> (The previous assets were deleted with the build folder)")

if __name__ == "__main__":
    print("==========================================")
    print("      ZOO TYCOON 32-BIT CONVERTER")
    print("==========================================")
    print("This will align the engine memory with the original 2001 files.")
    
    nuke_build()
    configure_x86()
    build_engine()
    transfer_assets()
    
    print("\n------------------------------------------")
    print("[SUCCESS] 32-bit Build Complete!")
    print("------------------------------------------")
    print("1. Run 'python import_assets.py' to restore your game files.")
    print("2. Run 'python fetch_dlls.py' to grab the new 32-bit DLLs.")
    print("3. Launch 'build/Release/zt1-engine.exe'")
    input("\nPress Enter to exit...")