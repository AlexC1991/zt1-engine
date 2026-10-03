#!/usr/bin/env python3
"""
Zoo Tycoon 1 File Access Monitor
================================
Uses Windows API to monitor which files zoo.exe reads.
This reveals the exact order and paths of sprite loading.

Requires: pip install watchdog psutil

Alternative: Use Process Monitor (procmon.exe) from Sysinternals for more detail.
"""
import sys
import os
import time
import threading
from pathlib import Path
from collections import defaultdict
from datetime import datetime

try:
    import psutil
except ImportError:
    print("ERROR: pip install psutil")
    sys.exit(1)

# Try to use watchdog for filesystem monitoring
try:
    from watchdog.observers import Observer
    from watchdog.events import FileSystemEventHandler
    HAS_WATCHDOG = True
except ImportError:
    HAS_WATCHDOG = False
    print("WARNING: watchdog not installed (pip install watchdog)")
    print("Using basic file monitoring instead.")


class ZT1FileHandler(FileSystemEventHandler):
    """Handler for file system events"""

    def __init__(self, zt_path):
        self.zt_path = Path(zt_path)
        self.file_access_log = []
        self.terrain_files = []
        self.lock = threading.Lock()

    def on_any_event(self, event):
        if event.is_directory:
            return

        path = Path(event.src_path)
        event_type = event.event_type

        # Filter for interesting files
        suffix = path.suffix.lower()
        if suffix in ['.ztd', '.ani', '.pal', '.tga', '.wav', '.zoo', '.scn']:
            with self.lock:
                entry = {
                    'time': datetime.now().strftime('%H:%M:%S.%f')[:-3],
                    'type': event_type,
                    'path': str(path),
                    'name': path.name
                }
                self.file_access_log.append(entry)

                # Track terrain specifically
                if 'terrain' in str(path).lower() or path.name.startswith('ic'):
                    self.terrain_files.append(entry)

                print(f"[{entry['time']}] {event_type:10s} {path.name}")


def monitor_with_watchdog(zt_path):
    """Monitor file access using watchdog library"""
    print(f"Monitoring: {zt_path}")
    print("=" * 70)
    print("Load a map in Zoo Tycoon to see file access...")
    print("Press Ctrl+C to stop and see summary.")
    print("=" * 70)
    print()

    handler = ZT1FileHandler(zt_path)
    observer = Observer()
    observer.schedule(handler, str(zt_path), recursive=True)
    observer.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        observer.stop()

    observer.join()

    # Print summary
    print("\n" + "=" * 70)
    print("FILE ACCESS SUMMARY")
    print("=" * 70)

    print(f"\nTotal files accessed: {len(handler.file_access_log)}")

    # Group by extension
    by_ext = defaultdict(list)
    for entry in handler.file_access_log:
        ext = Path(entry['path']).suffix.lower()
        by_ext[ext].append(entry)

    print("\nBy extension:")
    for ext, files in sorted(by_ext.items()):
        print(f"  {ext}: {len(files)} files")

    # Show terrain files
    if handler.terrain_files:
        print(f"\nTerrain-related files ({len(handler.terrain_files)}):")
        for entry in handler.terrain_files[:30]:
            print(f"  [{entry['time']}] {entry['name']}")

    return handler.file_access_log


def monitor_open_handles(pid):
    """Monitor open file handles for a process"""
    print(f"Monitoring file handles for PID {pid}")
    print("=" * 70)
    print()

    seen_files = set()

    try:
        proc = psutil.Process(pid)
        print(f"Process: {proc.name()}")
        print()
    except psutil.NoSuchProcess:
        print("Process not found!")
        return

    print("Currently open files:")
    try:
        for f in proc.open_files():
            path = f.path
            if path not in seen_files:
                seen_files.add(path)
                # Filter for game files
                if any(x in path.lower() for x in ['zoo', 'terrain', '.ztd', '.ani', '.pal']):
                    print(f"  {path}")
    except psutil.AccessDenied:
        print("  Access denied - try running as Administrator")

    print(f"\nMonitoring for new file opens... (Ctrl+C to stop)")
    print()

    try:
        while True:
            try:
                for f in proc.open_files():
                    path = f.path
                    if path not in seen_files:
                        seen_files.add(path)
                        timestamp = datetime.now().strftime('%H:%M:%S')
                        print(f"[{timestamp}] OPEN: {path}")
            except (psutil.AccessDenied, psutil.NoSuchProcess):
                pass
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass

    print("\n" + "=" * 70)
    print(f"Total unique files seen: {len(seen_files)}")

    # Filter terrain
    terrain_files = [f for f in seen_files if 'terrain' in f.lower()]
    if terrain_files:
        print(f"\nTerrain files ({len(terrain_files)}):")
        for f in sorted(terrain_files):
            print(f"  {f}")


def find_zoo_process():
    """Find zoo.exe"""
    for proc in psutil.process_iter(['pid', 'name', 'exe']):
        try:
            if proc.info['name'].lower() == 'zoo.exe':
                return proc.info['pid'], proc.info.get('exe', '')
        except:
            continue
    return None, None


def main():
    print("=" * 70)
    print("ZOO TYCOON 1 FILE ACCESS MONITOR")
    print("=" * 70)
    print()

    # Default ZT path
    zt_path = r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon"

    if not os.path.exists(zt_path):
        print(f"Default path not found: {zt_path}")
        zt_path = input("Enter Zoo Tycoon install path: ").strip()
        if not os.path.exists(zt_path):
            print("Path not found!")
            sys.exit(1)

    # Check if game is running
    pid, exe_path = find_zoo_process()

    print("Options:")
    print("  1. Monitor directory for file access (requires watchdog)")
    print("  2. Monitor process open file handles")
    print("  3. Both (recommended)")
    print()

    choice = input("Select [1/2/3]: ").strip()

    if choice == '1':
        if HAS_WATCHDOG:
            monitor_with_watchdog(zt_path)
        else:
            print("watchdog not installed!")

    elif choice == '2':
        if pid:
            monitor_open_handles(pid)
        else:
            print("zoo.exe not running! Start the game first.")

    elif choice == '3':
        if not pid:
            print("zoo.exe not running! Start the game first.")
            print("Will monitor directory only...")
            if HAS_WATCHDOG:
                monitor_with_watchdog(zt_path)
            return

        # Start directory monitor in thread
        if HAS_WATCHDOG:
            handler = ZT1FileHandler(zt_path)
            observer = Observer()
            observer.schedule(handler, str(zt_path), recursive=True)
            observer.start()

        # Monitor handles in main thread
        monitor_open_handles(pid)

        if HAS_WATCHDOG:
            observer.stop()
            observer.join()

    else:
        print("Invalid choice")


if __name__ == '__main__':
    main()
