import os

def apply(src_dir, root_dir):
    """Add new UiAction values for list selection changes"""
    
    filepath = os.path.join(src_dir, "ui", "UiAction.hpp")
    
    if not os.path.exists(filepath):
        print("    ! UiAction.hpp not found")
        return False
    
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()
    
    # Check if already updated
    if "SCENARIO_LIST_SELECTION" in content:
        return False
    
    # Add new actions before the closing brace
    new_content = '''#ifndef UI_ACTION_HPP
#define UI_ACTION_HPP
enum class UiAction {
    NONE=0,
    STARTUP_EXIT,
    CREDITS_EXIT=2,
    STARTUP_PLAY_SCENARIO=32,
    STARTUP_ZOO_ITEMS=35,
    STARTUP_PLAY_FREEFORM=39,
    STARTUP_CREDITS=40,
    SCENARIO_BACK_TO_MAIN_MENU,
    
    // [PATCH] List selection actions
    SCENARIO_LIST_SELECTION=100,    // Scenario was selected in list
    FREEFORM_LIST_SELECTION=101,    // Freeform map was selected in list
};
#endif // UI_ACTION_HPP
'''
    
    with open(filepath, "w", encoding="utf-8") as f:
        f.write(new_content)
    
    print("    -> Added list selection actions to UiAction.hpp")
    return True
