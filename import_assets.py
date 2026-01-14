import os
import shutil
import tkinter as tk
from tkinter import filedialog, messagebox

def find_build_folder():
    """Tries to auto-detect where the engine executable lives."""
    possible_paths = [
        os.path.join("build", "Release"), # Visual Studio / CMake default
        os.path.join("build", "Debug"),
        "build"                           # Makefile / Linux default
    ]
    
    for p in possible_paths:
        if os.path.exists(p) and os.path.exists(os.path.join(p, "zt1-engine.exe")):
            return os.path.abspath(p)
    
    # If we can't find the exe, just return the 'build' folder if it exists
    if os.path.exists("build"):
        return os.path.abspath("build")
        
    return None

def import_game_files():
    # 1. Setup GUI
    root = tk.Tk()
    root.withdraw() # Hide the main window

    # 2. Find Destination (Engine Build Folder)
    dest_dir = find_build_folder()
    
    if not dest_dir:
        messagebox.showinfo("Target Missing", "Could not auto-detect the 'build' folder.\nPlease select your 'zt1-engine/build' folder manually.")
        dest_dir = filedialog.askdirectory(title="Select your zt1-engine BUILD folder")
        if not dest_dir: return

    print(f"Target Directory: {dest_dir}")

    # 3. Ask for Source (Original Zoo Tycoon Install)
    messagebox.showinfo("Select Game", "Please select your original 'Zoo Tycoon' installation folder.\n(Where animals.ztd and zoo.exe are located)")
    source_dir = filedialog.askdirectory(title="Select Original Zoo Tycoon Folder")
    
    if not source_dir:
        print("No source selected. Cancelled.")
        return

    # 4. Define what to copy
    extensions = ['.ztd', '.dll', '.ini']
    files_copied = 0
    
    print(f"\nScanning {source_dir}...")

    # 5. Copy Files
    for filename in os.listdir(source_dir):
        lower_name = filename.lower()
        
        # Check if it matches our extensions
        if any(lower_name.endswith(ext) for ext in extensions):
            src_path = os.path.join(source_dir, filename)
            dst_path = os.path.join(dest_dir, filename)
            
            try:
                if os.path.isfile(src_path):
                    print(f"Copying: {filename}...")
                    shutil.copy2(src_path, dst_path)
                    files_copied += 1
            except Exception as e:
                print(f"Failed to copy {filename}: {e}")

    # 6. Success Message
    if files_copied > 0:
        msg = f"Success! Copied {files_copied} files to:\n{dest_dir}\n\nYou can now play the game!"
        print("\n" + msg)
        messagebox.showinfo("Import Complete", msg)
        
        # Open the folder for the user
        os.startfile(dest_dir)
    else:
        messagebox.showwarning("No Files Found", "No .ztd, .dll, or .ini files were found in that folder.\nAre you sure you selected the right directory?")

if __name__ == "__main__":
    import_game_files()