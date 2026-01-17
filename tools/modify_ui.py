
import os

filepath = "build/Release/ui/startup.lyt"

if not os.path.exists(filepath):
    print(f"Error: {filepath} not found.")
    exit(1)

with open(filepath, 'r') as f:
    lines = f.readlines()

new_lines = []
in_target_section = False
target_sections = [
    "[PlayScenario]", "[PlayFreeform]", "[LoadGame]", 
    "[ContinueGame]", "[ZooItems]", "[Credits]", "[Exit]"
]

for line in lines:
    stripped = line.strip()
    if stripped.startswith("[") and stripped.endswith("]"):
        if stripped in target_sections:
            in_target_section = True
        else:
            in_target_section = False
    
    if in_target_section and "x = center" in line:
        # Move to x=180 (Left alignment adjustment)
        new_lines.append(line.replace("x = center", "x = 180"))
    elif in_target_section and "x=center" in line:
        new_lines.append(line.replace("x=center", "x=180"))
    else:
        new_lines.append(line)

with open(filepath, 'w') as f:
    f.writelines(new_lines)

print("Modified startup.lyt successfully.")
