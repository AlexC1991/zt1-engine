import os
import shutil

# --- CONFIG ---
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(ROOT_DIR, "build")
RELEASE_DIR = os.path.join(BUILD_DIR, "Release")

def find_and_copy_file(filename):
    print(f"Searching for {filename}...")
    found = False
    
    for root, dirs, files in os.walk(BUILD_DIR):
        if filename in files:
            source_path = os.path.join(root, filename)
            dest_path = os.path.join(RELEASE_DIR, filename)
            
            try:
                shutil.copy2(source_path, dest_path)
                print(f"   [SUCCESS] Found and copied: {filename}")
                found = True
                # We stop after finding the first one to avoid overwrites
                return True
            except Exception as e:
                print(f"   [ERROR] Found but failed to copy: {e}")
    
    if not found:
        print(f"   [FAIL] Could not find {filename} anywhere in the build folder.")
    return False

if __name__ == "__main__":
    print("=== DLL HUNTER 3000 ===")
    
    if not os.path.exists(RELEASE_DIR):
        print(f"Error: Release folder not found at {RELEASE_DIR}")
    else:
        # We specifically need z.dll. 
        # We also check for others just in case they were built dynamically.
        targets = ["z.dll", "zip.dll", "SDL2.dll", "SDL2_image.dll", "SDL2_mixer.dll", "SDL2_ttf.dll"]
        
        count = 0
        for target in targets:
            if find_and_copy_file(target):
                count += 1
        
        print("-" * 30)
        print(f"Fixed {count} missing files.")
        
    input("\nPress Enter to exit...")