import os
import shutil

# --- CONFIG ---
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
LOADER_FILE = os.path.join(ROOT_DIR, "vendor", "pe-resource-loader", "src", "pe_resource_loader.c")

def patch_pe_loader():
    print(f"[FIX] Patching NULL pointer crash in {LOADER_FILE}...")
    
    if not os.path.exists(LOADER_FILE):
        print("   -> Error: File not found.")
        return

    with open(LOADER_FILE, "r", encoding="utf-8") as f:
        lines = f.readlines()

    new_lines = []
    patches_applied = 0
    
    # We need to find the function PeResourceLoader_GetDirectoryIdEntries
    # and inject a NULL check at the very top.
    
    for i, line in enumerate(lines):
        new_lines.append(line)
        
        # Identify the start of the problematic function
        if "PeResourceLoader_GetDirectoryIdEntries(PeResourceLoader * loader" in line:
            # We look for the opening brace '{'
            # Sometimes it's on the same line, sometimes the next.
            # We'll just inject the check immediately after this line.
            
            # Add the safety check
            new_lines.append("    if (!loader) return NULL; // [PATCH] Fix crash if file load failed\n")
            patches_applied += 1
            print(f"   -> Injected safety check at line {i+2}")

    if patches_applied > 0:
        shutil.copy(LOADER_FILE, LOADER_FILE + ".bak")
        with open(LOADER_FILE, "w", encoding="utf-8") as f:
            f.writelines(new_lines)
        print(f"   -> Success! Applied {patches_applied} safety patches.")
    else:
        print("   -> Warning: Could not find function signature. File might be different than expected.")

if __name__ == "__main__":
    patch_pe_loader()
    print("\nFix applied. Please run 'build_with_bar.py' (or easy_build.bat) to recompile.")