#!/usr/bin/env python3
"""
Zoo Tycoon 1 Engine - Save File Generator
Generates a fresh save file with default scenario lock states.
Uses the same format as ScenarioDatabase for compatibility.
"""

import os
import json

# Standardized save location (project root, git-tracked)
# The game reads from this location relative to executable: ../../saves/
SAVE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "saves")
SAVE_FILE = "user_scenarios.json"

# Scenario definitions matching ScenarioDatabase.cpp
# Format: (id, name, default_state)
# States: "locked", "unlocked", "completed"
SCENARIOS = [
    # === BASE GAME - TUTORIALS (All unlocked) ===
    ("s001", "Tutorial: Zoo Tycoon Game Controls", "unlocked"),
    ("s002", "Tutorial: Basic Gameplay", "unlocked"),
    ("s003", "Tutorial: Making Animals Happy", "unlocked"),
    ("s004", "Tutorial: Building Exhibits", "unlocked"),
    ("s005", "Tutorial: Creating Animal Shows", "unlocked"),
    ("s006", "Tutorial: Combined Exhibits", "unlocked"),
    
    # === BASE GAME - BEGINNER (All unlocked) ===
    ("s010", "Small Zoo (Beginner)", "unlocked"),
    ("s011", "Forest Zoo (Beginner)", "unlocked"),
    ("s012", "Tropical Rainforest (Beginner)", "unlocked"),
    ("s013", "African Savannah (Beginner)", "unlocked"),
    
    # === BASE GAME - INTERMEDIATE (All locked) ===
    ("s020", "Revitalize Burkitsville Zoo (Intermediate)", "locked"),
    ("s021", "Inner City Zoo (Intermediate)", "locked"),
    ("s022", "Saving the Cats (Intermediate)", "locked"),
    ("s023", "Endangered Species (Intermediate)", "locked"),
    
    # === BASE GAME - ADVANCED (All locked) ===
    ("s030", "Island Zoo (Advanced)", "locked"),
    ("s031", "African Savannah Zoo (Advanced)", "locked"),
    ("s032", "Endangered Species (Advanced)", "locked"),
    ("s033", "Tropical Rainforest Zoo (Advanced)", "locked"),
    
    # === BASE GAME - VERY ADVANCED (All locked) ===
    ("s040", "Paradise Island (Very Advanced)", "locked"),
    ("s041", "Giant Pandas (Very Advanced)", "locked"),
    
    # === DINOSAUR DIGS - TUTORIALS ===
    ("dd_tut", "Tutorial: Dinosaur Digs", "unlocked"),
    
    # === DINOSAUR DIGS - BEGINNER ===
    ("dd_begin1", "The Dinosaur Zoo (Beginner)", "unlocked"),
    
    # === DINOSAUR DIGS - INTERMEDIATE ===
    ("dd_inter1", "Valley of the Dinosaurs (Intermediate)", "locked"),
    ("dd_inter2", "Jurassic Zoo (Intermediate)", "locked"),
    ("dd_inter3", "Ice Age Animal Zoo (Intermediate)", "locked"),
    
    # === DINOSAUR DIGS - ADVANCED ===
    ("dd_adv1", "Dinosaur Island Research Lab (Advanced)", "locked"),
    
    # === DINOSAUR DIGS - VERY ADVANCED ===
    ("dd_vadv1", "Return to Dinosaur Island (Very Advanced)", "locked"),
    ("dd_vadv2", "Breeding the T. Rex (Very Advanced)", "locked"),
    
    # === MARINE MANIA - BEGINNER ===
    ("mm_begin1", "Shark World (Beginner)", "unlocked"),
    ("mm_begin2", "Seasideville Dolphin Park (Beginner)", "unlocked"),
    ("mm_begin3", "Mountain Zoo (Beginner)", "unlocked"),
    
    # === MARINE MANIA - INTERMEDIATE ===
    ("mm_inter1", "Oceans of the World (Intermediate)", "locked"),
    ("mm_inter2", "Save the Marine Animals (Intermediate)", "locked"),
    ("mm_inter3", "Free Admission (Intermediate)", "locked"),
    ("mm_inter4", "Aquatic Show Park (Intermediate)", "locked"),
    
    # === MARINE MANIA - ADVANCED ===
    ("mm_adv1", "Marine Conservation (Advanced)", "locked"),
    ("mm_adv2", "Save the Zoo (Advanced)", "locked"),
    
    # === MARINE MANIA - VERY ADVANCED ===
    ("mm_super", "Super Zoo (Very Advanced)", "unlocked"),  # Special: Always unlocked
    ("mm_ultimate", "Ultimate Zoo (Very Advanced)", "locked"),
    ("mm_giant", "Giant Marine Park (Very Advanced)", "locked"),
]

def generate_save():
    """Generate a fresh save file with default lock states."""
    
    # Ensure save directory exists
    if not os.path.exists(SAVE_DIR):
        os.makedirs(SAVE_DIR)
        print(f"[SETUP] Created directory: {SAVE_DIR}")
    
    # Build save data
    save_data = {"scenarios": {}}
    
    print("\n=== SCENARIO LOCK STATES ===")
    print("-" * 50)
    
    for scenario_id, name, state in SCENARIOS:
        save_data["scenarios"][scenario_id] = state
        
        # Visual indicator (ASCII for Windows compatibility)
        if state == "unlocked":
            icon = "[O]"
        elif state == "completed":
            icon = "[X]"
        else:
            icon = "[L]"
        
        print(f"{icon} [{state.upper():9}] {name}")
    
    # Write JSON file
    save_path = os.path.join(SAVE_DIR, SAVE_FILE)
    try:
        with open(save_path, "w") as f:
            json.dump(save_data, f, indent=2)
        
        print("-" * 50)
        print(f"\n[SUCCESS] Generated save file: {save_path}")
        print(f"[INFO] Total scenarios: {len(SCENARIOS)}")
        
        unlocked = sum(1 for _, _, s in SCENARIOS if s == "unlocked")
        locked = sum(1 for _, _, s in SCENARIOS if s == "locked")
        print(f"[INFO] Unlocked: {unlocked}, Locked: {locked}")
        
    except Exception as e:
        print(f"[ERROR] Failed to write save file: {e}")
        return False
    
    return True

def main():
    print("=" * 50)
    print("  Zoo Tycoon 1 Engine - Save File Generator")
    print("=" * 50)
    
    generate_save()

if __name__ == "__main__":
    main()