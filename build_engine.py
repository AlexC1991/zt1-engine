import subprocess
import sys
import os
import time
import re
from datetime import datetime

# --- CONFIG ---
BUILD_DIR = "build"
LOG_FILE = "build_log.txt"
ERROR_FILE = "build_errors.txt"

# ANSI Colors (Windows 10+ supports these)
class Colors:
    RESET = '\033[0m'
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    MAGENTA = '\033[95m'
    CYAN = '\033[96m'
    WHITE = '\033[97m'
    BOLD = '\033[1m'

def enable_windows_ansi():
    """Enable ANSI escape codes on Windows."""
    if os.name == 'nt':
        os.system('')  # This enables ANSI on Windows 10+

def print_header():
    """Print a fancy header."""
    enable_windows_ansi()
    os.system('cls' if os.name == 'nt' else 'clear')
    print(f"{Colors.CYAN}{Colors.BOLD}")
    print("╔══════════════════════════════════════════════════════════╗")
    print("║           OPEN ZOO TYCOON - ENGINE BUILDER               ║")
    print("║                    Version 2.0                           ║")
    print("╚══════════════════════════════════════════════════════════╝")
    print(f"{Colors.RESET}")
    print(f"  Started: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print()

def print_progress_bar(iteration, total, prefix='', suffix='', length=50):
    """Print a colorful progress bar."""
    percent = min(100, max(0, 100 * (iteration / float(max(total, 1)))))
    filled_length = int(length * iteration // max(total, 1))
    
    # Color gradient based on progress
    if percent < 33:
        color = Colors.RED
    elif percent < 66:
        color = Colors.YELLOW
    else:
        color = Colors.GREEN
    
    bar = '█' * filled_length + '░' * (length - filled_length)
    sys.stdout.write(f'\r  {color}{prefix} [{bar}] {percent:5.1f}% {suffix}{Colors.RESET}')
    sys.stdout.flush()

def print_stage(stage_num, total_stages, description):
    """Print a stage header."""
    print(f"\n{Colors.BLUE}{Colors.BOLD}[{stage_num}/{total_stages}] {description}{Colors.RESET}")
    print(f"    {'─' * 50}")

def print_success(message):
    print(f"  {Colors.GREEN}✓ {message}{Colors.RESET}")

def print_error(message):
    print(f"  {Colors.RED}✗ {message}{Colors.RESET}")

def print_warning(message):
    print(f"  {Colors.YELLOW}⚠ {message}{Colors.RESET}")

def print_info(message):
    print(f"  {Colors.CYAN}ℹ {message}{Colors.RESET}")

def run_cmake_configure():
    """Run CMake configuration with detailed output."""
    print_stage(1, 3, "Configuring Project (CMake)")
    
    if not os.path.exists(BUILD_DIR):
        os.makedirs(BUILD_DIR)
        print_info(f"Created build directory: {BUILD_DIR}")
    
    process = subprocess.Popen(
        ["cmake", ".."],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd=BUILD_DIR,
        text=True,
        encoding='utf-8',
        errors='replace'
    )
    
    config_lines = []
    found_items = {
        'compiler': None,
        'platform': None,
        'sdl': False,
        'sdl_image': False,
        'sdl_mixer': False,
        'sdl_ttf': False,
        'libzip': False,
        'zlib': False
    }
    
    spinner = ['⠋', '⠙', '⠹', '⠸', '⠼', '⠴', '⠦', '⠧', '⠇', '⠏']
    spin_idx = 0
    
    with open(LOG_FILE, "w", encoding='utf-8') as log:
        log.write(f"=== ZT1-Engine Build Log ===\n")
        log.write(f"Started: {datetime.now()}\n\n")
        log.write("=== CMAKE CONFIGURE ===\n")
        
        while True:
            line = process.stdout.readline()
            if not line and process.poll() is not None:
                break
            
            if line:
                log.write(line)
                config_lines.append(line)
                
                # Parse interesting info
                if "C compiler identification" in line:
                    found_items['compiler'] = line.split("is")[-1].strip()
                if "Platform:" in line:
                    found_items['platform'] = line.split(":")[-1].strip()
                if "Configuring SDL2_image" in line:
                    found_items['sdl_image'] = True
                if "Configuring SDL2_mixer" in line:
                    found_items['sdl_mixer'] = True
                if "Configuring SDL2_ttf" in line:
                    found_items['sdl_ttf'] = True
                if "Found ZLIB" in line:
                    found_items['zlib'] = True
                if "SDL2 was configured" in line:
                    found_items['sdl'] = True
                
                # Show spinner
                sys.stdout.write(f'\r  {Colors.CYAN}{spinner[spin_idx]} Configuring...{Colors.RESET}  ')
                sys.stdout.flush()
                spin_idx = (spin_idx + 1) % len(spinner)
    
    ret = process.poll()
    
    # Clear spinner line
    sys.stdout.write('\r' + ' ' * 40 + '\r')
    
    if ret == 0:
        print_success("Configuration complete!")
        
        # Show detected components
        if found_items['compiler']:
            print_info(f"Compiler: {found_items['compiler']}")
        if found_items['platform']:
            print_info(f"Platform: {found_items['platform']}")
        
        print()
        print(f"    {Colors.WHITE}Libraries detected:{Colors.RESET}")
        libs = [
            ('SDL2', found_items['sdl']),
            ('SDL2_image', found_items['sdl_image']),
            ('SDL2_mixer', found_items['sdl_mixer']),
            ('SDL2_ttf', found_items['sdl_ttf']),
            ('zlib', found_items['zlib'])
        ]
        for name, found in libs:
            status = f"{Colors.GREEN}✓{Colors.RESET}" if found else f"{Colors.RED}✗{Colors.RESET}"
            print(f"      {status} {name}")
    else:
        print_error("Configuration failed!")
        
    return ret

def run_build():
    """Run the build with detailed progress tracking."""
    print_stage(2, 3, "Compiling Engine")
    
    process = subprocess.Popen(
        ["cmake", "--build", ".", "--config", "Release"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd=BUILD_DIR,
        text=True,
        encoding='utf-8',
        errors='replace'
    )
    
    errors = []
    warnings = []
    current_file = ""
    files_compiled = 0
    last_percent = 0
    
    with open(LOG_FILE, "a", encoding='utf-8') as log:
        log.write("\n=== BUILD ===\n")
        
        while True:
            line = process.stdout.readline()
            if not line and process.poll() is not None:
                break
            
            if line:
                log.write(line)
                line_stripped = line.strip()
                
                # Track errors
                if ": error " in line.lower() or ": fatal error" in line.lower():
                    errors.append(line_stripped)
                
                # Track warnings
                if ": warning " in line.lower():
                    warnings.append(line_stripped)
                
                # Track what file is being compiled
                if line_stripped.endswith('.c') or line_stripped.endswith('.cpp'):
                    current_file = os.path.basename(line_stripped)
                    files_compiled += 1
                
                # Extract progress percentage
                if "[" in line and "%]" in line:
                    try:
                        match = re.search(r'\[\s*(\d+)%\]', line)
                        if match:
                            percent = int(match.group(1))
                            last_percent = percent
                            
                            # Get the target being built
                            suffix = current_file if current_file else "Building..."
                            print_progress_bar(percent, 100, prefix='Progress:', suffix=suffix[:30].ljust(30), length=40)
                    except:
                        pass
                
                # Show library completions
                if ".vcxproj ->" in line or ".lib" in line.lower():
                    if "SDL2-static.lib" in line:
                        pass  # Don't spam, progress bar shows it
    
    ret = process.poll()
    
    # Final progress
    print_progress_bar(100 if ret == 0 else last_percent, 100, prefix='Progress:', suffix='Complete!'.ljust(30), length=40)
    print()  # New line after progress bar
    
    # Summary
    print()
    print(f"    {Colors.WHITE}Build Summary:{Colors.RESET}")
    print_info(f"Files compiled: ~{files_compiled}")
    
    if warnings:
        print_warning(f"Warnings: {len(warnings)}")
    
    if errors:
        print_error(f"Errors: {len(errors)}")
        
        # Write errors to separate file
        with open(ERROR_FILE, "w", encoding='utf-8') as ef:
            ef.write(f"=== Build Errors - {datetime.now()} ===\n\n")
            ef.write(f"Total errors: {len(errors)}\n")
            ef.write(f"Total warnings: {len(warnings)}\n\n")
            
            ef.write("=" * 60 + "\n")
            ef.write("ERRORS:\n")
            ef.write("=" * 60 + "\n\n")
            for err in errors:
                ef.write(f"{err}\n\n")
            
            ef.write("\n" + "=" * 60 + "\n")
            ef.write("WARNINGS:\n")
            ef.write("=" * 60 + "\n\n")
            for warn in warnings[:50]:  # Limit warnings
                ef.write(f"{warn}\n")
            if len(warnings) > 50:
                ef.write(f"\n... and {len(warnings) - 50} more warnings\n")
        
        print_info(f"Error details saved to: {ERROR_FILE}")
    else:
        print_success("No errors!")
    
    return ret, errors, warnings

def verify_output():
    """Verify the build output exists."""
    print_stage(3, 3, "Verifying Output")
    
    exe_path = os.path.join(BUILD_DIR, "Release", "zt1-engine.exe")
    
    if os.path.exists(exe_path):
        size_mb = os.path.getsize(exe_path) / (1024 * 1024)
        print_success(f"Executable created: {exe_path}")
        print_info(f"Size: {size_mb:.2f} MB")
        return True
    else:
        # Try Debug folder
        exe_path_debug = os.path.join(BUILD_DIR, "Debug", "zt1-engine.exe")
        if os.path.exists(exe_path_debug):
            print_success(f"Executable created (Debug): {exe_path_debug}")
            return True
        
        print_error("Executable not found!")
        return False

def print_final_summary(success, errors, warnings, elapsed):
    """Print the final build summary."""
    print()
    print(f"{Colors.BOLD}{'═' * 60}{Colors.RESET}")
    
    if success:
        print(f"""
{Colors.GREEN}{Colors.BOLD}
   ██████╗ ██╗   ██╗██╗██╗     ██████╗ 
   ██╔══██╗██║   ██║██║██║     ██╔══██╗
   ██████╔╝██║   ██║██║██║     ██║  ██║
   ██╔══██╗██║   ██║██║██║     ██║  ██║
   ██████╔╝╚██████╔╝██║███████╗██████╔╝
   ╚═════╝  ╚═════╝ ╚═╝╚══════╝╚═════╝ 
                                        
   ███████╗██╗   ██╗ ██████╗ ██████╗███████╗███████╗███████╗
   ██╔════╝██║   ██║██╔════╝██╔════╝██╔════╝██╔════╝██╔════╝
   ███████╗██║   ██║██║     ██║     █████╗  ███████╗███████╗
   ╚════██║██║   ██║██║     ██║     ██╔══╝  ╚════██║╚════██║
   ███████║╚██████╔╝╚██████╗╚██████╗███████╗███████║███████║
   ╚══════╝ ╚═════╝  ╚═════╝ ╚═════╝╚══════╝╚══════╝╚══════╝
{Colors.RESET}""")
        
        print(f"  {Colors.WHITE}Build completed successfully!{Colors.RESET}")
        print(f"  Time elapsed: {elapsed:.1f} seconds")
        print()
        print(f"  {Colors.CYAN}Next steps:{Colors.RESET}")
        print(f"    1. Run {Colors.YELLOW}import_assets.py{Colors.RESET} to copy game files")
        print(f"    2. Launch {Colors.YELLOW}build/Release/zt1-engine.exe{Colors.RESET}")
        print()
        
        # Offer to open folder
        exe_dir = os.path.join(os.getcwd(), BUILD_DIR, "Release")
        print(f"  Executable location:")
        print(f"    {Colors.GREEN}{exe_dir}{Colors.RESET}")
        
    else:
        print(f"""
{Colors.RED}{Colors.BOLD}
   ██████╗ ██╗   ██╗██╗██╗     ██████╗ 
   ██╔══██╗██║   ██║██║██║     ██╔══██╗
   ██████╔╝██║   ██║██║██║     ██║  ██║
   ██╔══██╗██║   ██║██║██║     ██║  ██║
   ██████╔╝╚██████╔╝██║███████╗██████╔╝
   ╚═════╝  ╚═════╝ ╚═╝╚══════╝╚═════╝ 
                                        
   ███████╗ █████╗ ██╗██╗     ███████╗██████╗ 
   ██╔════╝██╔══██╗██║██║     ██╔════╝██╔══██╗
   █████╗  ███████║██║██║     █████╗  ██║  ██║
   ██╔══╝  ██╔══██║██║██║     ██╔══╝  ██║  ██║
   ██║     ██║  ██║██║███████╗███████╗██████╔╝
   ╚═╝     ╚═╝  ╚═╝╚═╝╚══════╝╚══════╝╚═════╝ 
{Colors.RESET}""")
        
        print(f"  {Colors.WHITE}Build failed with {len(errors)} error(s){Colors.RESET}")
        print(f"  Time elapsed: {elapsed:.1f} seconds")
        print()
        
        if errors:
            print(f"  {Colors.RED}First few errors:{Colors.RESET}")
            for err in errors[:5]:
                # Shorten the error for display
                short_err = err
                if len(err) > 80:
                    short_err = err[:77] + "..."
                print(f"    • {short_err}")
            
            if len(errors) > 5:
                print(f"    ... and {len(errors) - 5} more")
        
        print()
        print(f"  {Colors.CYAN}Check these files for details:{Colors.RESET}")
        print(f"    • {Colors.YELLOW}{ERROR_FILE}{Colors.RESET} - Errors and warnings only")
        print(f"    • {Colors.YELLOW}{LOG_FILE}{Colors.RESET} - Full build output")
    
    print()
    print(f"{Colors.BOLD}{'═' * 60}{Colors.RESET}")


if __name__ == "__main__":
    start_time = time.time()
    
    print_header()
    
    # Clear old logs
    for f in [LOG_FILE, ERROR_FILE]:
        if os.path.exists(f):
            os.remove(f)
    
    # Step 1: Configure
    ret = run_cmake_configure()
    if ret != 0:
        elapsed = time.time() - start_time
        print_final_summary(False, ["CMake configuration failed"], [], elapsed)
        print()
        print(f"  Opening {LOG_FILE}...")
        os.system(f'start "" "{LOG_FILE}"')
        input("\nPress Enter to exit...")
        sys.exit(1)
    
    # Step 2: Build
    ret, errors, warnings = run_build()
    
    # Step 3: Verify
    if ret == 0:
        success = verify_output()
    else:
        success = False
    
    elapsed = time.time() - start_time
    
    # Final summary
    print_final_summary(success, errors, warnings, elapsed)
    
    # Open error file if failed
    if not success and os.path.exists(ERROR_FILE):
        print(f"\n  Opening {ERROR_FILE}...")
        os.system(f'start "" "{ERROR_FILE}"')
    
    input("\nPress Enter to exit...")