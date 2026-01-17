import os
import json

def setup_fresh_save():
    save_folder = "Saved Game"
    save_file = os.path.join(save_folder, "user.json")

    # 1. Ensure folder exists
    if not os.path.exists(save_folder):
        os.makedirs(save_folder)
        print(f"[SETUP] Created directory: {save_folder}")

    # 2. Define the known scenarios (Based on your logs)
    # This list represents what the engine loads from scenario.cfg
    all_scenarios = [
        "TUTORIAL 1 - Game Controls",
        "TUTORIAL 2 - Basic Gameplay",
        "TUTORIAL 3 - Making Animals Happy",
        "Small Zoo (Beginner)",
        "Seasideville Zoo (Beginner)",
        "Forest Zoo (Beginner)",
        "Revitalize Burkitsville Zoo (Intermediate)",
        "Inner City Zoo (Intermediate)",
        "Saving the Great Cats (Intermediate)",
        "Endangered Species Zoo (Intermediate)",
        "Island Zoo (Advanced)",
        "African Savannah Zoo (Advanced)",
        "Mountain Zoo (Advanced)",
        "Tropical Rainforest Zoo (Advanced)",
        "Paradise Island (Very Advanced)",
        "Breeding Giant Pandas (Very Advanced)"
    ]

    # 3. Apply Logic: 
    # - Tutorials = Unlocked
    # - Beginner = Unlocked
    # - Others = Locked
    save_data = {"scenarios": {}}
    
    print("\n--- APPLYING LOCK LOGIC ---")
    for name in all_scenarios:
        status = 0 # Default to Locked (0)
        
        # Logic Rules
        lower_name = name.lower()
        if "tutorial" in lower_name:
            status = 1 # Unlocked
        elif "(beginner)" in lower_name:
            status = 1 # Unlocked
        
        # Store in dictionary
        save_data["scenarios"][name] = status
        
        # Visual Log
        status_str = "UNLOCKED" if status == 1 else "LOCKED  "
        print(f"[{status_str}] {name}")

    # 4. Write JSON
    try:
        with open(save_file, "w") as f:
            json.dump(save_data, f, indent=4)
        print(f"\n[SUCCESS] Generated fresh save file at: {save_file}")
        print("You can now verify the file contents.")
    except Exception as e:
        print(f"[ERROR] Could not write file: {e}")

if __name__ == "__main__":
    setup_fresh_save()