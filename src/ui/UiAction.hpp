#ifndef UI_ACTION_HPP
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
