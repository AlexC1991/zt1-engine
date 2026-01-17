
import os
import sys
import re

# --- CONFIGURATION ---
# Set X position individually for each button.
# Center of screen is approx 400.
# Left-align typical value: 100-180.
# --- CONFIGURATION ---
# Set X position individually for each button.
# "x" is where the TEXT STARTS (Left Alignment).
# Screen width is 800.
# Try 280-300 for a centered-looking column.
BUTTON_CONFIG = {
    "[PlayScenario]": 260,
    "[PlayFreeform]": 300,
    "[LoadGame]":     315,
    "[ContinueGame]": 295,
    "[ZooItems]":     295,
    "[Credits]":      320,
    "[Exit]":         325,
}
# ---------------------

# Robustly find path whether run from root or tools/
import os
script_dir = os.path.dirname(os.path.abspath(__file__))
FILELINE_PATH = os.path.join(script_dir, "../build/Release/ui/startup.lyt")

def update_alignment():
    if not os.path.exists(FILELINE_PATH):
        print(f"Error: {FILELINE_PATH} not found.")
        return

    print("Updating Main Menu button positions individually...")
    
    with open(FILELINE_PATH, 'r') as f:
        lines = f.readlines()

    new_lines = []
    current_section = None
    count_x = 0
    count_j = 0

    for line in lines:
        stripped = line.strip()
        
        # Detect section start
        if stripped.startswith("[") and stripped.endswith("]"):
            current_section = stripped
        
        # Check if we are in a target section
        if current_section in BUTTON_CONFIG:
            target_x = BUTTON_CONFIG[current_section]
            
            # Update X
            if re.match(r'^\s*x\s*=', line):
                 indent = line[:line.find('x')]
                 new_lines.append(f"{indent}x = {target_x}\n")
                 count_x += 1
                 continue
            
            # Update Justify (Force Left)
            if re.match(r'^\s*justify\s*=', line):
                 indent = line[:line.find('j')]
                 new_lines.append(f"{indent}justify = left\n")
                 count_j += 1
                 continue

        new_lines.append(line)

    with open(FILELINE_PATH, 'w') as f:
        f.writelines(new_lines)

    print(f"Success! Updated {count_x} X-positions and {count_j} Justify settings.")

if __name__ == "__main__":
    update_alignment()
