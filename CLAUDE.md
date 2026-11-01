@AGENTS.md

## Driving the ZT1 Lab (zt1-lab MCP)

- One Lab runs on this PC and every session shares it: a build, or a Test Zoo
  load from another session, resets yours. Finish your Lab captures before
  starting work that also needs the Lab.
- Edit engine code in the Lab with `lab_edit_engine_file` (its parameter is
  `file`, not `path`) and build with `lab_build_engine`, or build `build-x64`
  directly; either way, restart the engine in the Lab to use the new build.
- After changing `lab-app/main.js` or `preload.js`, use `lab_restart`; for
  renderer-only changes, `lab_reload`. Run `node --check <file>` first.
- `lab_action <name> <arg>` runs any Lab button by name; an unknown name lists
  them all. Play the original with `playOriginal <file.zoo>`, send clicks with
  `playSteps "click x y; wait ms"` (and `volume 0` to mute it), read it with
  `origLive`, `memPeek`, `memEach`.
