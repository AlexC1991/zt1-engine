#!/usr/bin/env python3
"""
╔══════════════════════════════════════════════════════════════════╗
║              ZOO TYCOON 1 ENGINE - FULL BUILD                    ║
║                   Runs all 5 build steps                         ║
╚══════════════════════════════════════════════════════════════════╝
"""

import os
import sys
import subprocess
import time
import traceback
import io
from contextlib import redirect_stdout, redirect_stderr

# Navigate to project root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)

# Colors
class C:
    RESET = '\033[0m'
    BOLD = '\033[1m'
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    CYAN = '\033[96m'
    DIM = '\033[2m'
    MAGENTA = '\033[95m'

LOGO = f"""
{C.CYAN}{C.BOLD}
    ███████╗ ██████╗  ██████╗    ████████╗██╗   ██╗ ██████╗ ██████╗  ██████╗ ███╗   ██╗
    ╚══███╔╝██╔═══██╗██╔═══██╗   ╚══██╔══╝╚██╗ ██╔╝██╔════╝██╔═══██╗██╔═══██╗████╗  ██║
      ███╔╝ ██║   ██║██║   ██║      ██║    ╚████╔╝ ██║     ██║   ██║██║   ██║██╔██╗ ██║
     ███╔╝  ██║   ██║██║   ██║      ██║     ╚██╔╝  ██║     ██║   ██║██║   ██║██║╚██╗██║
    ███████╗╚██████╔╝╚██████╔╝      ██║      ██║   ╚██████╗╚██████╔╝╚██████╔╝██║ ╚████║
    ╚══════╝ ╚═════╝  ╚═════╝       ╚═╝      ╚═╝    ╚═════╝ ╚═════╝ ╚═════╝ ╚═╝  ╚═══╝
{C.RESET}
{C.YELLOW}                          🦁 ENGINE BUILDER v2.0 🐘{C.RESET}
{C.DIM}                    Open Source Zoo Tycoon 1 Reimplementation{C.RESET}
"""

SUCCESS_BANNER = f"""
{C.GREEN}{C.BOLD}
    ╔═══════════════════════════════════════════════════════════════╗
    ║                                                               ║
    ║   ███████╗██╗   ██╗ ██████╗ ██████╗███████╗███████╗███████╗   ║
    ║   ██╔════╝██║   ██║██╔════╝██╔════╝██╔════╝██╔════╝██╔════╝   ║
    ║   ███████╗██║   ██║██║     ██║     █████╗  ███████╗███████╗   ║
    ║   ╚════██║██║   ██║██║     ██║     ██╔══╝  ╚════██║╚════██║   ║
    ║   ███████║╚██████╔╝╚██████╗╚██████╗███████╗███████║███████║   ║
    ║   ╚══════╝ ╚═════╝  ╚═════╝ ╚═════╝╚══════╝╚══════╝╚══════╝   ║
    ║                                                               ║
    ║              🎉 BUILD COMPLETED SUCCESSFULLY! 🎉              ║
    ╚═══════════════════════════════════════════════════════════════╝
{C.RESET}"""

FAIL_BANNER = f"""
{C.RED}{C.BOLD}
    ╔═══════════════════════════════════════════════════════════════╗
    ║                                                               ║
    ║        ███████╗ █████╗ ██╗██╗     ███████╗██████╗             ║
    ║        ██╔════╝██╔══██╗██║██║     ██╔════╝██╔══██╗            ║
    ║        █████╗  ███████║██║██║     █████╗  ██║  ██║            ║
    ║        ██╔══╝  ██╔══██║██║██║     ██╔══╝  ██║  ██║            ║
    ║        ██║     ██║  ██║██║███████╗███████╗██████╔╝            ║
    ║        ╚═╝     ╚═╝  ╚═╝╚═╝╚══════╝╚══════╝╚═════╝             ║
    ║                                                               ║
    ║                 ❌ BUILD FAILED - SEE ERRORS ABOVE            ║
    ╚═══════════════════════════════════════════════════════════════╝
{C.RESET}"""

def enable_ansi():
    if os.name == 'nt':
        os.system('')

class BuildError:
    """Stores error information from a failed step"""
    def __init__(self, step_num, step_name, error_type, message, traceback_str=None):
        self.step_num = step_num
        self.step_name = step_name
        self.error_type = error_type
        self.message = message
        self.traceback_str = traceback_str
    
    def display(self):
        print(f"\n{C.RED}{'═' * 65}{C.RESET}")
        print(f"{C.RED}{C.BOLD}  ERROR IN STEP {self.step_num}: {self.step_name}{C.RESET}")
        print(f"{C.RED}{'═' * 65}{C.RESET}")
        print(f"\n  {C.YELLOW}Error Type:{C.RESET} {self.error_type}")
        print(f"\n  {C.YELLOW}Message:{C.RESET}")
        for line in str(self.message).split('\n')[:30]:  # Limit to 30 lines
            print(f"    {line}")
        if self.traceback_str:
            print(f"\n  {C.YELLOW}Traceback:{C.RESET}")
            for line in self.traceback_str.split('\n')[-15:]:  # Last 15 lines
                print(f"    {C.DIM}{line}{C.RESET}")

def run_step(step_num, name, script_name, errors_list):
    """Run a build step and return success status."""
    print(f"\n{C.MAGENTA}{'═' * 65}{C.RESET}")
    print(f"  {C.BOLD}STEP {step_num}/5: {name}{C.RESET}")
    print(f"{C.MAGENTA}{'═' * 65}{C.RESET}\n")
    
    script_path = os.path.join(SCRIPT_DIR, script_name)
    
    # Check if script exists
    if not os.path.exists(script_path):
        error = BuildError(
            step_num, name, 
            "FileNotFoundError",
            f"Script not found: {script_path}"
        )
        errors_list.append(error)
        print(f"  {C.RED}✗ Script not found: {script_name}{C.RESET}")
        return False
    
    # Import and run the step's main function
    import importlib.util
    spec = importlib.util.spec_from_file_location(f"step{step_num}", script_path)
    module = importlib.util.module_from_spec(spec)
    
    try:
        spec.loader.exec_module(module)
        result = module.main()
        
        if result != 0:
            # Step returned non-zero, check for build_error.log
            log_file = os.path.join(ROOT_DIR, "build_error.log")
            error_msg = f"Step returned exit code {result}"
            
            if os.path.exists(log_file):
                with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
                    log_content = f.read()
                # Extract last 50 lines
                error_msg = '\n'.join(log_content.split('\n')[-50:])
            
            error = BuildError(step_num, name, "StepFailed", error_msg)
            errors_list.append(error)
            return False
        
        return True
        
    except Exception as e:
        tb = traceback.format_exc()
        error = BuildError(
            step_num, name,
            type(e).__name__,
            str(e),
            tb
        )
        errors_list.append(error)
        print(f"  {C.RED}✗ Exception: {e}{C.RESET}")
        return False

def main():
    enable_ansi()
    os.system('cls' if os.name == 'nt' else 'clear')
    print(LOGO)
    
    start_time = time.time()
    errors_list = []
    
    steps = [
        (1, "CLEAN WORKSPACE", "step1_clean.py"),
        (2, "PATCH SOURCE CODE", "step2_patch.py"),
        (3, "BUILD ENGINE", "step3_build.py"),
        (4, "IMPORT ASSETS", "step4_import.py"),
        (5, "SETUP RUNTIME", "step5_setup.py"),
    ]
    
    completed_steps = []
    failed_step = None
    
    for step_num, name, script in steps:
        success = run_step(step_num, name, script, errors_list)
        
        if success:
            completed_steps.append((step_num, name))
        else:
            failed_step = (step_num, name)
            # Step 3 (build) is critical - stop if it fails
            if step_num == 3:
                break
            # Other steps can have warnings but continue
            # Actually, let's stop on any failure for now
            break
    
    elapsed = time.time() - start_time
    
    # Display results
    if not errors_list:
        print(SUCCESS_BANNER)
        print(f"  {C.DIM}Total time: {elapsed:.1f} seconds{C.RESET}")
        
        # Summary of completed steps
        print(f"\n  {C.GREEN}Completed steps:{C.RESET}")
        for num, name in completed_steps:
            print(f"    {C.GREEN}✓{C.RESET} Step {num}: {name}")
        
        return 0
    else:
        # Display all errors
        for error in errors_list:
            error.display()
        
        print(FAIL_BANNER)
        print(f"  {C.DIM}Total time: {elapsed:.1f} seconds{C.RESET}")
        
        # Summary
        print(f"\n  {C.GREEN}Completed steps:{C.RESET}")
        for num, name in completed_steps:
            print(f"    {C.GREEN}✓{C.RESET} Step {num}: {name}")
        
        if failed_step:
            print(f"\n  {C.RED}Failed at:{C.RESET}")
            print(f"    {C.RED}✗{C.RESET} Step {failed_step[0]}: {failed_step[1]}")
        
        # Save error report
        report_file = os.path.join(ROOT_DIR, "build_report.txt")
        with open(report_file, "w", encoding="utf-8") as f:
            f.write("ZOO TYCOON ENGINE BUILD REPORT\n")
            f.write(f"{'=' * 50}\n")
            f.write(f"Time: {elapsed:.1f} seconds\n\n")
            
            f.write("COMPLETED STEPS:\n")
            for num, name in completed_steps:
                f.write(f"  ✓ Step {num}: {name}\n")
            
            if failed_step:
                f.write(f"\nFAILED AT:\n")
                f.write(f"  ✗ Step {failed_step[0]}: {failed_step[1]}\n")
            
            f.write(f"\nERRORS:\n")
            for error in errors_list:
                f.write(f"\n{'─' * 50}\n")
                f.write(f"Step {error.step_num}: {error.step_name}\n")
                f.write(f"Type: {error.error_type}\n")
                f.write(f"Message:\n{error.message}\n")
                if error.traceback_str:
                    f.write(f"Traceback:\n{error.traceback_str}\n")
        
        print(f"\n  {C.YELLOW}Full report saved to: {report_file}{C.RESET}")
        
        return 1

if __name__ == "__main__":
    try:
        code = main()
        print()
        input(f"  {C.CYAN}Press Enter to exit...{C.RESET}")
        sys.exit(code)
    except KeyboardInterrupt:
        print(f"\n  {C.YELLOW}Build cancelled{C.RESET}")
        sys.exit(1)
    except Exception as e:
        print(f"\n{C.RED}{'═' * 65}{C.RESET}")
        print(f"{C.RED}UNEXPECTED ERROR IN BUILD SYSTEM{C.RESET}")
        print(f"{C.RED}{'═' * 65}{C.RESET}")
        print(f"\n  {C.RED}{type(e).__name__}: {e}{C.RESET}")
        print(f"\n  {C.DIM}{traceback.format_exc()}{C.RESET}")
        input(f"\n  {C.CYAN}Press Enter to exit...{C.RESET}")
        sys.exit(1)