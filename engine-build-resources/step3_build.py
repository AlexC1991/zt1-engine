#!/usr/bin/env python3
"""
+==================================================================+
|            ZOO TYCOON 1 ENGINE - STEP 3: BUILD ENGINE            |
+==================================================================+
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
    DIM = '\033[2m'

def enable_ansi():
    if os.name == 'nt': os.system('')


# MSBuild decides what to recompile from timestamps only. Files restored from
# a zip (the first-run fix) keep their ORIGINAL, older timestamp, so a build
# folder that already has objects from before the zip keeps linking the old
# code - that is how a clone's exe kept hanging at "Loading strings from
# lang.dll" with the fixed pe_resource_loader.c on disk. So compare contents:
# any source whose hash changed since the last build gets its timestamp
# bumped, and MSBuild recompiles it.
SOURCE_DIRS = ["src", os.path.join("vendor", "pe-resource-loader", "src"),
               os.path.join("vendor", "pe-resource-loader", "include")]
SOURCE_EXTS = (".c", ".cpp", ".h", ".hpp")
HASH_FILE = os.path.join(BUILD_DIR, ".source_hashes.json")


def _source_hashes():
    import hashlib
    out = {}
    for d in SOURCE_DIRS:
        top = os.path.join(ROOT_DIR, d)
        for root, _, files in os.walk(top):
            for f in files:
                if f.lower().endswith(SOURCE_EXTS):
                    p = os.path.join(root, f)
                    with open(p, "rb") as fh:
                        out[os.path.relpath(p, ROOT_DIR)] = hashlib.sha1(fh.read()).hexdigest()
    return out


def refresh_changed_sources():
    """Touch sources whose content changed since the last build."""
    import json
    now = _source_hashes()
    old = {}
    if os.path.exists(HASH_FILE):
        try:
            with open(HASH_FILE, encoding="utf-8") as fh:
                old = json.load(fh)
        except Exception:
            old = {}
    # No record yet but objects exist: we cannot tell what they were built
    # from, so treat everything as changed (one full recompile).
    have_objects = os.path.isdir(os.path.join(BUILD_DIR, "zt1-engine.dir"))
    changed = [p for p, h in now.items() if old.get(p) != h] if (old or have_objects) else []
    t = time.time()
    for p in changed:
        os.utime(os.path.join(ROOT_DIR, p), (t, t))
    if changed:
        print(f"  {C.CYAN}{len(changed)} source file(s) changed since the last build - recompiling them{C.RESET}")
    return now


def save_source_hashes(hashes):
    import json
    with open(HASH_FILE, "w", encoding="utf-8") as fh:
        json.dump(hashes, fh, indent=0, sort_keys=True)

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
                 sys.stdout.write('\r' + ' '*70 + '\r' + f"  {C.CYAN}* {clean[:60]}...{C.RESET}")
                 sys.stdout.flush()

    print() # New line after done
    return process.poll(), full_log

def print_error_box(log_lines):
    print(f"\n{C.RED}{C.BOLD}+==================================================================+{C.RESET}")
    print(f"{C.RED}{C.BOLD}|                       BUILD ERRORS DETECTED                      |{C.RESET}")
    print(f"{C.RED}{C.BOLD}+==================================================================+{C.RESET}")
    
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
    # x64, like the project's CI and the README's manual build. Win32 also
    # "works" but is not what the shipped/verified binaries are built as.
    arch = os.environ.get("ZT1_ARCH", "x64").strip() or "x64"
    code, log = run_smart_build(["cmake", "-A", arch, ".."], BUILD_DIR, "Configuring")
    if code != 0:
        print_error_box(log)
        return 1
        
    # 2. Build
    hashes = refresh_changed_sources()
    start = time.time()
    code, log = run_smart_build(["cmake", "--build", ".", "--config", "Release"], BUILD_DIR, "Building")
    
    if code != 0:
        print_error_box(log)
        return 1
    save_source_hashes(hashes)
        
    print(f"\n{C.GREEN}[OK]{C.RESET} Build Successful! ({time.time()-start:.1f}s){C.RESET}")
    print(f"  Exe: {os.path.join(REL_DIR, 'zt1-engine.exe')}")
    print()
    return 0

if __name__ == "__main__":
    sys.exit(main())