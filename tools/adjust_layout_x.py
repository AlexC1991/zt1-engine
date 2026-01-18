import os
import sys
import re

# Resolve path relative to this script
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
LAYOUT_FILE = os.path.join(PROJECT_ROOT, "build", "Release", "ui", "mapselec.lyt")

# Definitions of groups
GROUPS = {
    "1": {
        "name": "Starting Cash Label ('Starting Cash')",
        "elements": {
            "Starting Cash Label": ["x"]
        }
    },
    "2": {
        "name": "Starting Cash Value + Spinners ($100,000)",
        "elements": {
            "Starting Cash": ["x"],
            "up_Spinner": ["x"],
            "Down_Spinner": ["x"]
        }
    },
    "3": {
        "name": "All Starting Cash Elements (Label, Value, Spinners)",
        "elements": {
            "Starting Cash Label": ["x"],
            "Starting Cash": ["x"],
            "up_Spinner": ["x"],
            "Down_Spinner": ["x"]
        }
    },
    "4": {
        "name": "Difficulty Controls (Label + Text)",
        "elements": {
            "Difficulty Label": ["x"],
            "Difficulty Text": ["x"]
        }
    }
}

def load_file(filepath):
    with open(filepath, 'r') as f:
        return f.readlines()

def save_file(filepath, lines):
    with open(filepath, 'w') as f:
        f.writelines(lines)
    print(f"Saved changes to {filepath}")

def apply_offset(lines, offset, target_elements):
    new_lines = []
    current_section = None
    modified_count = 0
    
    for line in lines:
        stripped = line.strip()
        
        # Detect section
        section_match = re.match(r'^\[(.*)\]$', stripped)
        if section_match:
            current_section = section_match.group(1)
            new_lines.append(line)
            continue
            
        # Modify properties if in target section and elements
        if current_section in target_elements:
            key_match = re.match(r'^(x)\s*=\s*(-?\d+|center)(.*)', stripped, re.IGNORECASE)
            if key_match:
                key = key_match.group(1).lower()
                val_str = key_match.group(2)
                comment = key_match.group(3)
                
                if key in target_elements[current_section]:
                    try:
                        val = int(val_str)
                        new_val = val + offset
                        new_line = f"{key}={new_val}{comment}\n"
                        new_lines.append(new_line)
                        print(f"  {current_section}.{key}: {val} -> {new_val}")
                        modified_count += 1
                        continue
                    except ValueError:
                        pass
                        
        new_lines.append(line)
        
    return new_lines, modified_count

def main():
    if not os.path.exists(LAYOUT_FILE):
        print(f"Error: Could not find {LAYOUT_FILE}")
        return

    print(f"Target File: {LAYOUT_FILE}")
    print("-" * 50)
    
    while True:
        print("\nWhat do you want to move?")
        for key, group in GROUPS.items():
            print(f"  {key}. {group['name']}")
        print("  0. Exit")
        
        choice = input("\nSelect (0-4): ").strip()
        
        if choice == '0':
            print("Exiting.")
            break
            
        if choice not in GROUPS:
            print("Invalid selection.")
            continue
            
        try:
            print(f"\nMoving: {GROUPS[choice]['name']}")
            print("Enter X offset (negative = LEFT, positive = RIGHT).")
            offset_input = input("Offset: ").strip()
            
            if not offset_input: continue
            offset = int(offset_input)
            
            if offset == 0:
                print("Zero offset. Skipping.")
                continue
                
            lines = load_file(LAYOUT_FILE)
            new_lines, count = apply_offset(lines, offset, GROUPS[choice]['elements'])
            
            if count > 0:
                save_file(LAYOUT_FILE, new_lines)
                print(f"Successfully shifted {count} elements.")
                print("Restart game to see changes.")
            else:
                print("No elements modified.")
                
        except ValueError:
            print("Invalid input. Please enter an integer.")
        except KeyboardInterrupt:
            print("\nExiting.")
            break

if __name__ == "__main__":
    main()
