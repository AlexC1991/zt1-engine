import os
import shutil

src_dir = r"C:\Users\batty\.gemini\antigravity\brain\ae1d15e6-e134-47f9-8f76-bc3a646d5664"
dst_dir = r"C:\Users\batty\OneDrive\Desktop\Lua\zt1-engine\build\Release"

# Final Blank Asset
src_name = "uploaded_image_1768702624360.png"
src_path = os.path.join(src_dir, src_name)

# EXACT NAMES found in mapselec_lyt.txt
dst_names = [
    "Back to main menu_N.png", 
    "back to main menu_N.png",
    "Back_N.png", 
    "back_N.png",
    "Play Map_N.png",
    "PlayMap_N.png", # Fallback
    "Play_N.png",
    "play_N.png"
]

if os.path.exists(src_path):
    for dst_name in dst_names:
        dst_path = os.path.join(dst_dir, dst_name)
        try:
            shutil.copy2(src_path, dst_path)
            print(f"DEPLOYED: {src_name} -> {dst_name}")
        except Exception as e:
            print(f"ERROR: {e}")
else:
    print(f"MISSING: {src_path}")
