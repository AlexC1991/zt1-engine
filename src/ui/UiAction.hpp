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
    
    // [PATCH] Starting cash spinner buttons (using button IDs)
    CASH_SPINNER_UP=11511,          // Increase starting cash
    CASH_SPINNER_DOWN=11512,        // Decrease starting cash

    // [PATCH] Play Buttons (mapped to Layout IDs)
    PLAY_SCENARIO_START = 50008,    // "Play" on Scenario screen
    PLAY_FREEFORM_START = 11513,    // "Play" on Freeform screen

    // In-game HUD buttons (ui/main.lyt ids)
    HUD_ZOOM_IN = 1007,
    HUD_ROTATE_CCW = 1008,
    HUD_ROTATE_CW = 1009,
    HUD_ZOOM_OUT = 1023,
    HUD_PAUSE = 1071,
    HUD_PLAY = 1072,

    // The game menu's File Options page (ui/gameopt1.lyt button ids)
    GAME_MAIN_MENU = 1503,
    GAME_EXIT = 1504,
};

// Panels: the in-game screen's sub-layouts (ui/gamescrn.lyt), by layout id.
// A button with action=3 target=<id> toggles a panel; action=2 target=<id>
// closes it.
constexpr int kPanelToggleAction = 1000000;
constexpr int kPanelCloseAction = 2000000;
inline bool isPanelToggle(UiAction a) {
  int v = static_cast<int>(a);
  return v > kPanelToggleAction && v < kPanelCloseAction;
}
inline bool isPanelClose(UiAction a) {
  int v = static_cast<int>(a);
  return v > kPanelCloseAction && v < kPanelCloseAction + kPanelToggleAction;
}
inline int panelOf(UiAction a) {
  return static_cast<int>(a) % kPanelToggleAction;
}
// action=2 target=-1: close the panel the button is in
constexpr int kPanelSelf = 999999;
#endif // UI_ACTION_HPP
