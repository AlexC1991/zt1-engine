#!/usr/bin/env python3
"""
╔══════════════════════════════════════════════════════════════════╗
║            ZOO TYCOON 1 ENGINE - STEP 3: BUILD ENGINE            ║
╚══════════════════════════════════════════════════════════════════╝
"""
import os
import sys
import subprocess
import time
import re

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)
BUILD_DIR = os.path.join(ROOT_DIR, "build")
REL_DIR = os.path.join(BUILD_DIR, "Release")

class C:
    RESET = '\033[0m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    CYAN = '\033[96m'
    RED = '\033[91m'
    BOLD = '\033[1m'

def enable_ansi():
    if os.name == 'nt': os.system('')

def run_smart_build(command, cwd, desc):
    print(f"  {C.CYAN}{desc}...{C.RESET}\n")
    
    # Force unbuffered output so we see it live
    env = os.environ.copy()
    env["PYTHONUNBUFFERED"] = "1"
    
    process = subprocess.Popen(
        command, 
        cwd=cwd, 
        stdout=subprocess.PIPE, 
        stderr=subprocess.STDOUT, 
        text=True, 
        errors='replace',
        bufsize=1, 
        env=env
    )
    
    prog_regex = re.compile(r"\[\s*(\d+)%\]")
    full_log = []
    
    while True:
        line = process.stdout.readline()
        if not line and process.poll() is not None: break
        
        if line:
            full_log.append(line)
            clean = line.strip()
            
            # Update Progress Bar
            match = prog_regex.search(clean)
            if match:
                fname = os.path.basename(clean.split(' ')[-1])
                if fname.endswith('.obj'): fname = fname[:-4]
                msg = f"  {C.GREEN}[{match.group(1)}%]{C.RESET} Compiling: {C.YELLOW}{fname}{C.RESET}"
                sys.stdout.write('\r' + ' '*70 + '\r' + msg)
                sys.stdout.flush()
            
            # Show Configuration Steps
            elif "Configuring" in desc and ("Check" in clean or "Detect" in clean):
                 sys.stdout.write('\r' + ' '*70 + '\r' + f"  {C.CYAN}• {clean[:60]}...{C.RESET}")
                 sys.stdout.flush()

    print() # New line after done
    return process.poll(), full_log

def print_error_box(log_lines):
    print(f"\n{C.RED}{C.BOLD}╔══════════════════════════════════════════════════════════════════╗{C.RESET}")
    print(f"{C.RED}{C.BOLD}║                       BUILD ERRORS DETECTED                      ║{C.RESET}")
    print(f"{C.RED}{C.BOLD}╚══════════════════════════════════════════════════════════════════╝{C.RESET}")
    
    # Filter for lines containing "error" or "fatal"
    error_lines = []
    for i, line in enumerate(log_lines):
        if "error" in line.lower() or "fatal" in line.lower():
            # Add the line before it (often has context) and the error line
            if i > 0: error_lines.append(log_lines[i-1].strip())
            error_lines.append(line.strip())
    
    # If no specific "error" keyword found, just show the last 20 lines
    if not error_lines:
        error_lines = [l.strip() for l in log_lines[-20:]]
    
    # Print the first 20 relevant lines
    for line in error_lines[:20]:
        print(f"  {C.YELLOW}{line}{C.RESET}")
        
    if len(error_lines) > 20:
        print(f"  {C.DIM}... (and {len(error_lines)-20} more lines) ...{C.RESET}")

def main():
    enable_ansi()
    print(f"\n{C.CYAN}=== STEP 3: BUILD ENGINE ==={C.RESET}\n")
    os.makedirs(BUILD_DIR, exist_ok=True)
    
    # 1. Configure
    code, log = run_smart_build(["cmake", "-A", "Win32", ".."], BUILD_DIR, "Configuring")
    if code != 0:
        print_error_box(log)
        return 1
        
    # 2. Build
    start = time.time()
    code, log = run_smart_build(["cmake", "--build", ".", "--config", "Release"], BUILD_DIR, "Building")
    
    if code != 0:
        print_error_box(log)
        return 1
        
    print(f"\n{C.GREEN}✓ Build Successful! ({time.time()-start:.1f}s){C.RESET}")
    print(f"  Exe: {os.path.join(REL_DIR, 'zt1-engine.exe')}")
    print()
    return 0

if __name__ == "__main__":
    sys.exit(main())