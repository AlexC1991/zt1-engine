# Game menu pause checks - 27 October 2026

Opening Game Options now pauses the entire simulation. Both the window and
the original sidebar panel count as game modals, alongside questions and
naming dialogs. Dismissing another popup cannot resume through an open
Game Options window. Closing the menu resumes only a zoo that the modal
paused; a zoo the player had already paused stays paused.

The change is in `src/main.cpp`: `gameModalOpen()`, `resumeAfterPopup()` and
the main game-loop pause gate. It was built using the Lab's Write Code
workspace, with no build errors. All gameplay interaction used the Lab MCP.

## Native Play checks

- Saved the current session to a separate backup before restarting Play.
- Loaded that backup in the updated engine.
- Opened Options from a running zoo: the pause indicator appeared.
- Compared captures taken more than five seconds apart: the map rectangle
  `(50,60)-(1850,270)` and cash/date rectangle `(200,862)-(1010,896)` had no
  changed pixels.
- Closed Options: the zoo resumed.
- Manually paused, opened Options, then clicked Done: the zoo stayed paused.
- Checked the sidebar menu route as well.
- Reran the Lab Test Zoo `activitycheck`: **51/51 passed**.

Screenshots and the session backup are retained in
`E:/zt1-shots/menu-and-roadmap-2026-10-27/`. The original player save was
not overwritten. Play is left paused. This fix is in the development build;
no release was created.
