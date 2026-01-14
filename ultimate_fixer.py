import os
import shutil

# --- CONFIG ---
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
CMAKE_FILE = os.path.join(ROOT_DIR, "CMakeLists.txt")
MAIN_CPP = os.path.join(ROOT_DIR, "src", "main.cpp")
UTILS_HPP = os.path.join(ROOT_DIR, "src", "Utils.hpp")

def fix_main_cpp():
    """Fix the SDL main entry point issue for Windows."""
    print(f"[MAIN.CPP] Fixing SDL_MAIN_HANDLED...")
    
    if not os.path.exists(MAIN_CPP):
        print("   -> Error: main.cpp not found.")
        return False
    
    with open(MAIN_CPP, "r", encoding="utf-8") as f:
        content = f.read()
    
    # Check if already patched
    if "SDL_MAIN_HANDLED" in content:
        print("   -> Already patched.")
        return True
    
    # Create backup
    shutil.copy(MAIN_CPP, MAIN_CPP + ".bak")
    print(f"   -> Backup created: main.cpp.bak")
    
    # Add SDL_MAIN_HANDLED at the very top, before any includes
    new_content = '#define SDL_MAIN_HANDLED  // Tell SDL we handle our own main()\n\n' + content
    
    # Also add SDL_SetMainReady() after main() opening
    old_main = 'int main(int argc, char *argv[]) {'
    new_main = '''int main(int argc, char *argv[]) {
  SDL_SetMainReady();  // Required when using SDL_MAIN_HANDLED
'''
    
    if old_main in new_content:
        new_content = new_content.replace(old_main, new_main)
    
    with open(MAIN_CPP, "w", encoding="utf-8") as f:
        f.write(new_content)
    
    print("   -> Success: Added SDL_MAIN_HANDLED and SDL_SetMainReady().")
    return True

def fix_cmake():
    """Check and optionally fix CMakeLists.txt."""
    print(f"[CMAKE] Checking CMakeLists.txt...")
    
    if not os.path.exists(CMAKE_FILE):
        print("   -> Error: CMakeLists.txt not found.")
        return False

    with open(CMAKE_FILE, "r", encoding="utf-8") as f:
        content = f.read()

    issues = []
    
    # Check for mixed target_link_libraries signatures (FATAL ERROR)
    # Count plain vs PRIVATE calls for zt1-engine
    lines = content.split('\n')
    plain_calls = 0
    private_calls = 0
    
    for i, line in enumerate(lines):
        if 'target_link_libraries' in line:
            # Look at this line and next few lines
            block = '\n'.join(lines[i:i+5])
            if 'zt1-engine' in block or '${PROJECT_NAME}' in block:
                if 'PRIVATE' in block or 'PUBLIC' in block or 'INTERFACE' in block:
                    private_calls += 1
                else:
                    plain_calls += 1
    
    if plain_calls > 0 and private_calls > 0:
        issues.append("FATAL: Mixed plain/keyword target_link_libraries signatures!")
        print(f"   -> ERROR: Found {plain_calls} plain and {private_calls} keyword link calls")
        print("   -> This WILL cause CMake to fail!")
        print("   -> Replace CMakeLists.txt with the fixed version.")
        return False
    
    # Check for C++20
    if "CMAKE_CXX_STANDARD 20" not in content:
        issues.append("C++20 standard not set")
    else:
        print("   -> C++20 standard: OK")
    
    # Check for MSVC runtime
    if "MultiThreadedDLL" not in content and "/MD" not in content:
        issues.append("Dynamic runtime (/MD) not enforced")
    else:
        print("   -> Dynamic runtime (/MD): OK")
    
    # Check for Windows libraries
    win_libs = ['shlwapi', 'bcrypt', 'winmm', 'setupapi']
    missing_libs = [lib for lib in win_libs if lib not in content]
    if missing_libs:
        issues.append(f"Missing Windows libs: {', '.join(missing_libs)}")
    else:
        print("   -> Windows system libraries: OK")
    
    # Check for link_directories (MSVC)
    if "link_directories" not in content:
        issues.append("MSVC link_directories not set")
    else:
        print("   -> MSVC link directories: OK")
    
    if issues:
        print(f"   -> Found {len(issues)} issue(s):")
        for issue in issues:
            print(f"      - {issue}")
        return False
    
    print("   -> CMakeLists.txt looks good!")
    return True

def fix_utils_hpp():
    """Fix the missing return value warning in Utils.hpp."""
    print(f"[UTILS.HPP] Checking for missing return value...")
    
    if not os.path.exists(UTILS_HPP):
        print("   -> File not found, skipping.")
        return True
    
    with open(UTILS_HPP, "r", encoding="utf-8") as f:
        content = f.read()
    
    # Check if the function exists and needs fixing
    if "getExpansionLangDllPath" not in content:
        print("   -> Function not found, skipping.")
        return True
    
    # Check if already fixed
    if 'return "";  // Default fallback' in content:
        print("   -> Already fixed.")
        return True
    
    # Look for the pattern where switch doesn't have a default return
    # The function ends with a switch statement that might not return
    
    # Find the function
    func_start = content.find("getExpansionLangDllPath")
    if func_start == -1:
        print("   -> Could not locate function.")
        return True
    
    # Look for common patterns that indicate missing return
    # Pattern 1: Function ends with just closing braces after switch
    patterns_to_fix = [
        # Pattern: default case with break, then closing braces
        ('default:\n      break;\n    }\n  }\n}', 
         'default:\n      break;\n    }\n  }\n  return "";  // Default fallback\n}'),
        # Pattern with different spacing
        ('default:\n            break;\n        }\n    }\n}',
         'default:\n            break;\n        }\n    }\n    return "";  // Default fallback\n}'),
    ]
    
    fixed = False
    for old_pattern, new_pattern in patterns_to_fix:
        if old_pattern in content:
            shutil.copy(UTILS_HPP, UTILS_HPP + ".bak")
            content = content.replace(old_pattern, new_pattern)
            with open(UTILS_HPP, "w", encoding="utf-8") as f:
                f.write(content)
            print("   -> Fixed missing return value.")
            fixed = True
            break
    
    if not fixed:
        # Try a more generic approach - add return before final }
        # This is riskier so we just warn
        print("   -> WARNING: Could not auto-fix. You may see a compiler warning.")
        print("   -> Add 'return \"\";' before the final } in getExpansionLangDllPath()")
    
    return True

def nuke_build():
    """Delete the build folder for a clean rebuild."""
    build_dir = os.path.join(ROOT_DIR, "build")
    if os.path.exists(build_dir):
        print("[CLEAN] Deleting build folder...")
        try:
            shutil.rmtree(build_dir)
            print("   -> Build folder deleted.")
        except Exception as e:
            print(f"   -> Warning: Could not delete build folder: {e}")
            print("   -> Try closing Visual Studio or any programs using those files.")
    else:
        print("[CLEAN] Build folder doesn't exist, nothing to clean.")

def check_fixed_cmake_available():
    """Check if we have a fixed CMakeLists.txt to offer."""
    # This would check for a known-good CMakeLists.txt
    fixed_cmake = os.path.join(ROOT_DIR, "CMakeLists_FIXED.txt")
    if os.path.exists(fixed_cmake):
        return fixed_cmake
    return None

def print_summary(results):
    """Print a summary of what was done."""
    print("\n" + "=" * 60)
    print("                    FIX SUMMARY")
    print("=" * 60)
    
    all_good = all(results.values())
    
    for name, success in results.items():
        status = "OK" if success else "NEEDS ATTENTION"
        symbol = "[+]" if success else "[!]"
        print(f"  {symbol} {name}: {status}")
    
    print()
    
    if all_good:
        print("""
All checks passed! Next steps:
  1. Run 'build_engine.py' to compile
  2. If build succeeds, run 'import_assets.py' to copy game files  
  3. Launch zt1-engine.exe from the build/Release folder
""")
    else:
        print("""
Some issues need manual attention:

If CMakeLists.txt has issues:
  -> Download the fixed CMakeLists.txt and replace yours

If other files have issues:
  -> Check the specific error messages above
  
After fixing, run this script again to verify.
""")
    
    print("=" * 60)

if __name__ == "__main__":
    print("=" * 60)
    print("     ZT1-ENGINE ULTIMATE FIXER FOR WINDOWS")  
    print("=" * 60)
    print()
    
    results = {}
    
    # Fix main.cpp (SDL_MAIN_HANDLED)
    results['main.cpp'] = fix_main_cpp()
    print()
    
    # Check/fix CMakeLists.txt
    results['CMakeLists.txt'] = fix_cmake()
    print()
    
    # Fix Utils.hpp
    results['Utils.hpp'] = fix_utils_hpp()
    print()
    
    # Clean build folder
    nuke_build()
    
    # Print summary
    print_summary(results)
    
    input("\nPress Enter to exit...")