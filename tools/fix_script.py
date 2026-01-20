import os
import re

def lock_precision_camera():
    # 1. Path Setup
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir = os.path.dirname(script_dir) if os.path.basename(script_dir).lower() == "tools" else script_dir
    world_path = os.path.join(root_dir, "src", "World.cpp")

    if not os.path.exists(world_path):
        print("[ERROR] World.cpp not found.")
        return

    with open(world_path, 'r') as f:
        content = f.read()

    # 2. Precision Clamping Logic based on User Dial-In Data
    # This logic dynamically selects the limits based on map size detected by ZooReader.
    final_logic = r"""    if (mapW > 0 && mapH > 0) {
        // [UX] Locked Precision Camera Limits
        int maxX = 0, minX = 0, maxY = 0, minY = 0;

        if (mapW <= 75) {
            // Small Map (75x75)
            maxX = 3160; minX = 2485; maxY = 515; minY = 2140;
        } else if (mapW <= 125) {
            // Medium Map (125x125)
            maxX = 4545; minX = 4230; maxY = 465; minY = 3600;
        } else {
            // Large Map (150x150+)
            maxX = 5415; minX = 5085; maxY = 510; minY = 4400;
        }

        if (this->camX > maxX)  this->camX = maxX;
        if (this->camX < -minX) this->camX = -minX;
        if (this->camY > maxY)  this->camY = maxY;
        if (this->camY < -minY) this->camY = -minY;
    }"""

    # 3. Find and replace the entire previous clamping/debug block
    # We target the 'if (mapW > 0 && mapH > 0)' block specifically
    pattern = r"if \(mapW > 0 && mapH > 0\) \{.*?static int debug_tick = 0;.*?if \(debug_tick\+\+ % 60 == 0\) \{.*?\}.*?\}"
    
    if re.search(pattern, content, re.DOTALL):
        new_content = re.sub(pattern, final_logic, content, flags=re.DOTALL)
        print("[SUCCESS] Found Debug block. Replacing with Locked Precision Limits.")
    else:
        # Fallback to general block replacement if debug logger was already removed
        pattern_fallback = r"if \(mapW > 0 && mapH > 0\) \{.*?this->camX =.*?;.*?this->camX =.*?;.*?this->camY =.*?;.*?this->camY =.*?;.*?\}"
        if re.search(pattern_fallback, content, re.DOTALL):
            new_content = re.sub(pattern_fallback, final_logic, content, flags=re.DOTALL)
            print("[SUCCESS] Found Standard block. Replacing with Locked Precision Limits.")
        else:
            print("[ERROR] Could not find any existing camera logic block to replace.")
            return

    with open(world_path, 'w') as f:
        f.write(new_content)
    
    print("[SUCCESS] Camera coordinates are now locked in for all map sizes.")

if __name__ == "__main__":
    lock_precision_camera()