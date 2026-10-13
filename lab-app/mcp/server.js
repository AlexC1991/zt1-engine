#!/usr/bin/env node
// ZT1 Lab's MCP server: an AI agent (any MCP client) drives the running
// Lab app through it, on stdio. Each tool is a call to the Lab's control
// port (127.0.0.1:18492), which does what the Lab's own UI does - so it's
// all seen happening in the window, and no mouse or keyboard is taken.
// Start the Lab first (lab-app/ZT1 Lab.cmd).
const http = require('http');
const readline = require('readline');

const PORT = 18492;

function call(method, params, timeout = 180000) {
  return new Promise((resolve) => {
    const body = JSON.stringify({ method, params: params || {} });
    const req = http.request(
      { host: '127.0.0.1', port: PORT, path: '/call', method: 'POST', headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) }, timeout },
      (res) => {
        let out = '';
        res.setEncoding('utf8');
        res.on('data', (c) => (out += c));
        res.on('end', () => {
          try {
            resolve(JSON.parse(out));
          } catch {
            resolve({ error: 'bad answer from the Lab' });
          }
        });
      },
    );
    req.on('timeout', () => req.destroy());
    req.on('error', () => resolve({ error: 'The ZT1 Lab isn’t running (start lab-app/ZT1 Lab.cmd), or its control port is closed.' }));
    req.end(body);
  });
}

const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const text = (o) => ({ content: [{ type: 'text', text: typeof o === 'string' ? o : JSON.stringify(o, null, 1) }] });

// Each tool: description, input schema, and what it does
const TOOLS = {
  lab_reload: {
    description: 'Reload the Lab’s page (after changing the Lab app’s own files). Unsaved editor changes are lost.',
    schema: {},
    run: () => call('reload'),
  },
  lab_restart: {
    description: 'Restart the whole Lab app (after changing lab-app/main.js or preload.js), and wait until it’s back. Unsaved editor changes are lost.',
    schema: {},
    async run() {
      const r = await call('restart');
      if (r.error) return text(r);
      await wait(3000);
      for (let i = 0; i < 60; i++) {
        const p = await call('ping', {}, 2000);
        if (p.result) {
          await wait(2500); // the page and its engine connecting
          return text('The Lab is back.');
        }
        await wait(1000);
      }
      return text('The Lab hasn’t come back after a minute.');
    },
  },
  lab_state: {
    description: 'What the ZT1 Lab is showing and doing: the view, the selected function, the lab zoo (mod, step, checks, watched values, animals), the engine code editor (open files, unsaved, cursor, code intelligence, last build and its problems), Play.',
    schema: {},
    run: () => call('state'),
  },
  lab_screenshot: {
    description: 'A picture of the ZT1 Lab window as it is now (it needn’t be in front). Note: in Play, the game is its own window and isn’t in the picture.',
    schema: { width: { type: 'number', description: 'Scale to this width (default 1600)' } },
    async run(a) {
      const r = await call('screenshot', a);
      if (r.error) return text(r);
      return { content: [{ type: 'image', data: r.result.png, mimeType: 'image/png' }] };
    },
  },
  lab_tab: {
    description: 'Switch the Lab to a tab, by id or name: code (Original Game), engine (Write Code), scene (Test Zoo), play (Play Game), mods (Experiments); files opens Write Code on Game files.',
    schema: { tab: { type: 'string' } },
    required: ['tab'],
    run: (a) => call('tab', a),
  },
  lab_click: {
    description: 'Click something on screen in the Lab: by a CSS selector, or by its text (a button, tab, list item...).',
    schema: { selector: { type: 'string' }, text: { type: 'string' } },
    run: (a) => call('click', a),
  },
  lab_type: {
    description: 'Type text into a box in the Lab (CSS selector), as if typed.',
    schema: { selector: { type: 'string' }, text: { type: 'string' } },
    required: ['selector', 'text'],
    run: (a) => call('type', a),
  },
  lab_read: {
    description: 'The visible text of the current view (or of a CSS selector): to read results off the screen.',
    schema: { selector: { type: 'string' } },
    run: (a) => call('read', a),
  },
  lab_action: {
    description: 'Run one of the Lab’s button actions by name (as data-act in its UI), with its argument.',
    schema: { name: { type: 'string' }, arg: { type: 'string' } },
    required: ['name'],
    run: (a) => call('action', a),
  },
  lab_search_functions: {
    description: 'Functions view: search zoo.exe’s functions by name/address/note, and the game’s text (strings) with the functions that use each; field:0x3d6 lists every instruction in zoo.exe touching that offset (read or write, by function) - who uses a field.',
    schema: { query: { type: 'string' } },
    required: ['query'],
    run: (a) => call('searchFunctions', a),
  },
  lab_open_function: {
    description: 'Code view: open a zoo.exe function by address: its decompiled C (from Ghidra), callers, callees, and the engine code implementing it; where Ghidra made no function, raw: the instructions there from where that code starts.',
    schema: { address: { type: 'string', description: '0x4378df' }, find: { type: 'string', description: 'typed into the Code panel Find (first match lit, in view); returns only the C lines holding it, numbered, instead of all the C' } },
    required: ['address'],
    run: (a) => call('openFunction', a),
  },
  lab_where_used: {
    description: 'Where an address in zoo.exe (a global, a function) is used: Ghidra’s references, shown in the Lab.',
    schema: { address: { type: 'string' } },
    required: ['address'],
    run: (a) => call('whereUsed', a),
  },
  lab_name_function: {
    description: 'Functions view: give a zoo.exe function a name (and a note, and a class), saved to the knowledge base and named in Ghidra, under the Lab’s user.',
    schema: { address: { type: 'string' }, name: { type: 'string' }, note: { type: 'string' }, cls: { type: 'string' } },
    required: ['address', 'name'],
    run: (a) => call('nameFunction', a),
  },
  lab_game_files: {
    description: 'Game files view: list the game’s files (as the engine reads them) whose path starts with prefix; or q: the search box’s words (type:ai, in:dd, area:animals, has:text - files with a line holding the text, spaces and case not counting, e.g. has:cpreattack=1; each file then comes with its matching lines).',
    schema: { prefix: { type: 'string' }, q: { type: 'string' }, limit: { type: 'number', description: 'how many names to return (default 40; count is always the total)' } },
    run: (a) => call('gameFiles', a),
  },
  lab_game_file: {
    description: 'Game files view: open one of the game’s files (a layout, a config) and return its text.',
    schema: { name: { type: 'string' } },
    required: ['name'],
    run: (a) => call('gameFile', a),
  },
  lab_search_engine: {
    description: 'Engine code view: search all of our engine’s source for text.',
    schema: { query: { type: 'string' } },
    required: ['query'],
    run: (a) => call('searchEngine', a),
  },
  lab_open_engine_file: {
    description: 'Engine code view: open one of our engine’s files (src/...) at a line, in the Lab’s editor; returns the lines around it, numbered.',
    schema: { file: { type: 'string' }, line: { type: 'number' } },
    required: ['file'],
    run: (a) => call('openEngineFile', a),
  },
  lab_read_engine_file: {
    description: 'Lines of an open engine file (numbered).',
    schema: { file: { type: 'string' }, from: { type: 'number' }, to: { type: 'number' } },
    run: (a) => call('readEngineFile', a),
  },
  lab_edit_engine_file: {
    description: 'Edit an engine file in the Lab’s editor, as if typed (undoable, seen on screen): replace find with replace (the first, or all), or insert text before a line. Not saved until lab_save_engine (or a build).',
    schema: {
      file: { type: 'string' },
      find: { type: 'string' },
      replace: { type: 'string' },
      all: { type: 'boolean' },
      insertAtLine: { type: 'number' },
      text: { type: 'string' },
    },
    run: (a) => call('editEngineFile', a),
  },
  lab_save_engine: {
    description: 'Save every changed engine file open in the Lab.',
    schema: {},
    run: () => call('saveEngine'),
  },
  lab_problems: {
    description: 'The errors and warnings in an engine file: from clangd as you type, and from the last build.',
    schema: { file: { type: 'string' } },
    run: (a) => call('problems', a),
  },
  lab_species_check: {
    description: 'Scene › Animal › Check: a species in bulk (its file stem, e.g. "orca"; "*marine" every tank animal; "*" all) - art missing or not loading, Marine Mania layers (under/shadow) missing or with other frame counts, behaviour-set steps the engine skips, moves to places it does not know, animations/sets named but not there. brief: true for counts only.',
    schema: { species: { type: 'string' }, brief: { type: 'boolean' } },
    run: (a) => call('speciesCheck', a),
  },
  lab_sync_status: {
    description: 'Engine code › Sync: the branch, how far behind/ahead of main, what changed, conflicts, the open pull request, who you are on GitHub (owner/helper/contributor) and CodeRook. fetch: true asks GitHub first.',
    schema: { fetch: { type: 'boolean' } },
    run: (a) => call('syncStatus', a),
  },
  lab_sync: {
    description: 'Engine code › Sync, one step, as its button does: pull (main merged in; conflicts listed), resolve (file, how: ours|theirs|done), abort, commit (message), push (this branch to GitHub, or the fork without push rights), propose (title, body: a pull request into main on AlexC1991/zt1-engine), mirror (owner: main to CodeRook), branch (name). Only when the user asked for it.',
    schema: { step: { type: 'string' }, message: { type: 'string' }, title: { type: 'string' }, body: { type: 'string' }, file: { type: 'string' }, how: { type: 'string' }, name: { type: 'string' } },
    required: ['step'],
    run: (a) => call('syncStep', a),
  },
  lab_make_release: {
    description: 'Engine code › Make release, and wait for it: the engine built x64 Release in build-x64/, then dist/zt1-engine-<date>-win64.zip + .sha256 (as the Alpha was made), without the original game’s files. Nothing is uploaded. Returns the zip, its sha256, what was left out, or the error.',
    schema: {},
    async run() {
      const r = await call('makeRelease');
      if (r.error) return text(r);
      for (let i = 0; i < 1800; i++) {
        await wait(1000);
        const s = await call('state');
        const b = s.result && s.result.engineCode.build;
        if (b && b.state !== 'building') return text({ state: b.state, release: b.release, log: (b.log || []).slice(-25) });
      }
      return text('Still making it after 30 minutes.');
    },
  },
  lab_build_engine: {
    description: 'Press Build engine (saves first) and wait for it: ok or the problems, each with its file and line. On success the lab zoo restarts on the new engine.',
    schema: {},
    async run() {
      const r = await call('buildEngine');
      if (r.error) return text(r);
      for (let i = 0; i < 600; i++) {
        await wait(1000);
        const s = await call('state');
        const b = s.result && s.result.engineCode.build;
        if (b && b.state !== 'building') return text(b);
      }
      return text('Still building after 10 minutes.');
    },
  },
  lab_run_mod: {
    description: 'Build and run a mod (re/lab/scripts/<name>.cpp) in the lab zoo; see lab_state for its steps, checks and watched values.',
    schema: { name: { type: 'string' } },
    required: ['name'],
    run: (a) => call('runMod', a),
  },
  // ---- Scene: the lab zoo as an editor
  lab_scene: {
    description:
      'Scene view: the map loaded, the clock and speed, the tool, the tile under the cursor, what’s selected (with all its live values), the counts and a summary of everything in the zoo.',
    schema: {},
    run: () => call('scene'),
  },
  lab_scene_maps: {
    description: 'Scene view: the freeform maps that can be loaded (stem, name, size).',
    schema: {},
    run: () => call('sceneMaps'),
  },
  lab_scene_load: {
    description: 'Scene view: load a map (its stem, from lab_scene_maps) into the lab zoo; flat: true makes the whole zoo plain grass with nothing in it.',
    schema: { map: { type: 'string' }, flat: { type: 'boolean' } },
    required: ['map'],
    run: (a) => call('sceneLoad', a),
  },
  lab_scene_outline: {
    description: 'Scene view: everything in the zoo: exhibits, animals, staff, buildings (with ids, for lab_scene_select); guests and scenery counted (full: listed with their ids).',
    schema: { full: { type: 'boolean', description: 'list every guest and every scenery id (else counted)' } },
    run: (a) => call('sceneOutline', a),
  },
  lab_scene_items: {
    description:
      'Scene view: what can be put down, by palette tab (animals, shelters, toys, buildings, scenery, foliage, rocks, fences, paths, staff, terrain), optionally searched. Each item’s "item" is what lab_scene_place takes.',
    schema: { category: { type: 'string' }, query: { type: 'string' } },
    run: (a) => call('sceneItems', a),
  },
  lab_scene_place: {
    description:
      'Scene view: use the game’s own tool at a map tile x, y, as a mouse would: an item from lab_scene_items (an animal, object, fence, path, staff, terrain "paint:<type>" or "height:<mode>"), or tool "bulldoze" / "gate" / "select". toX, toY drag to another tile (fences and paths are laid along the drag; fences go from grid point to grid point, so x, y is a tile’s corner; a closed fence makes an exhibit). Returns the tool, what’s selected, the counts and what the engine said.',
    schema: {
      item: { type: 'string' },
      tool: { type: 'string' },
      x: { type: 'number' },
      y: { type: 'number' },
      toX: { type: 'number' },
      toY: { type: 'number' },
      female: { type: 'boolean' },
      facing: { type: 'number', description: '0 SE, 1 SW, 2 NW, 3 NE (objects)' },
      size: { type: 'number', description: 'terrain brush, 1-5' },
      lineMode: { type: 'string', description: 'fences and paths: Bend, Bend (other way), Straight or Rectangle (a whole exhibit in one drag)' },
    },
    run: (a) => call('scenePlace', a),
  },
  lab_scene_select: {
    description: 'Scene view: select something to inspect: by kind (animal, guest, staff, object, exhibit) and id, or what’s at a tile x, y (as a click), or kind "tile" with x, y.',
    schema: { kind: { type: 'string' }, id: { type: 'number' }, x: { type: 'number' }, y: { type: 'number' }, full: { type: 'boolean', description: 'with its animations and behaviour sets listed (else just how many)' } },
    run: (a) => call('sceneSelect', a),
  },
  lab_scene_set: {
    description: 'Scene view: change a value of what’s selected (animal: happiness, hunger, health, energy, dirty, food, pending; guest: happiness, hunger, thirst, bathroom, tired; exhibit: name; object: facing 0-7).',
    schema: { field: { type: 'string' }, value: {} },
    required: ['field', 'value'],
    run: (a) => call('sceneSet', a),
  },
  lab_scene_remove: {
    description: 'Scene view: sell the selected animal, fire the selected staff member, or remove the selected object.',
    schema: {},
    run: () => call('sceneRemove'),
  },
  lab_scene_time: {
    description: 'Scene view: run or pause the zoo (play), its speed (0.25-16), or advance it so many seconds of game time.',
    schema: { play: { type: 'boolean' }, speed: { type: 'number' }, advance: { type: 'number' } },
    run: (a) => call('sceneTime', a),
  },
  lab_engine_suggest: {
    description:
      'Engine code: type text into a file at a line (col: column, default the line’s end) as at the keyboard, with the suggestions open - returns the Lab’s own suggestions there: inside ZT_STEP("…") / ZT_FN("…") zoo.exe’s functions by name, summary or address; after +0x the mapped fields; inside num("…") / set(m, "…") the game’s .ai keys and behaviour sets; 0x… elsewhere functions by address. (Unsaved: undo with lab_action engUndo or don’t build.)',
    schema: { file: { type: 'string' }, line: { type: 'number' }, col: { type: 'number' }, text: { type: 'string' } },
    required: ['file', 'line'],
    run: (a) => call('engineSuggest', a),
  },
  lab_scene_watch: {
    description:
      'Scene: the Watch panel - follow an animal (id, on: true; on: false lets it go), clear: true empties the log. Returns the log: a row each time a followed animal starts doing something else or goes a tile, in game time (works while paused and skipping time with lab_scene_time advance). last: how many rows (80).',
    schema: { id: { type: 'number' }, on: { type: 'boolean' }, clear: { type: 'boolean' }, last: { type: 'number' } },
    run: (a) => call('sceneWatch', a),
  },
  lab_engine_calls: {
    description: 'Engine code: who calls the function at file:line (on its name or anywhere inside it), by clangd, shown in the Calls tab; dir "out" for what it calls; depth opens callers of callers (1-4).',
    schema: { file: { type: 'string' }, line: { type: 'number' }, dir: { type: 'string' }, depth: { type: 'number' } },
    required: ['file', 'line'],
    run: (a) => call('engineCalls', a),
  },
  lab_scene_view: {
    description: 'Scene view: an overlay on or off (grid, exhibits, routes, paths, footprints), the view mode (scene, heightmap, animal), rotate: true, zoom: 1 / -1; or the art viewer: species (and anim: one of its animations by name; layers: true draws shadow + art, under + art and the animation together; frame: hold every layer on that frame, -1 plays; step: 1 / -1 moves the held frame).',
    schema: { overlay: { type: 'string' }, on: { type: 'boolean' }, mode: { type: 'string' }, rotate: { type: 'boolean' }, zoom: { type: 'number' }, species: { type: 'string' }, anim: { type: 'string' }, layers: { type: 'boolean' }, frame: { type: 'number' }, step: { type: 'number' } },
    run: (a) => call('sceneView', a),
  },
  lab_scene_route: {
    description:
      'Scene: can it get there? A route search with the Route tool, by a walker kind’s own rules: as "guest" or "staff" from tile x, y to toX, toY, or as the selected "animal" from where it stands to x, y. Returns found, why it failed, the tiles looked at (of its limit), tiles it couldn’t go onto, steps refused (fences, cliffs) and the time; the search is drawn in the view. The selected walker’s own last search is in lab_scene (sel.route).',
    schema: { as: { type: 'string' }, x: { type: 'number' }, y: { type: 'number' }, toX: { type: 'number' }, toY: { type: 'number' } },
    required: ['x', 'y'],
    run: (a) => call('sceneRoute', a),
  },
  lab_scene_act: {
    description:
      'Scene: an action on what’s selected, as the inspector’s buttons. Animal: anim (arg: an animation), behave (arg: a behaviour set), hungry, tired, sick, heal, happy, unhappy, dung, dart, crate. Guest: anim, hungry, thirst, bathroom, tired, leave. Staff: anim. Exhibit: food, nofood, dung (arg: how many), wear, break, dirty (arg: purity). The selected one’s anims and sets are in lab_scene (sel).',
    schema: { action: { type: 'string' }, arg: { type: 'string' } },
    required: ['action'],
    run: (a) => call('sceneAct', a),
  },
  lab_kb_cloud: {
    description:
      'The shared knowledge base: what every install of the Lab has worked out about the original zoo.exe (re/cloud). Reports where it is pointed, or with what: "pull" brings what others found into re/knowledge.json, "contribute" sends what this checkout has worked out (provisional until reviewed), "pending" lists what is waiting to be reviewed. dry: true says what would happen and changes nothing. Every fact is about one build of the game (2002 Complete Collection, SHA-256 checked), so a contribution from another build is refused.',
    schema: {
      what: { type: 'string', description: 'pull, contribute or pending (omit to just report where it is pointed)' },
      dry: { type: 'boolean', description: 'say what would happen, change nothing' },
    },
    run: (a) => call('kbCloud', a),
  },
  lab_kb_cloud_status: {
    description: 'The shared knowledge base: where this Lab is pointed (re/private/cloud.json or ZT1_KB_API), or that it has none set up yet. See re/cloud/README.md.',
    schema: {},
    run: () => call('kbCloud', {}),
  },
  lab_scene_pin: {
    description: 'Scene: pin a value of the selected animal or guest (held at value, or its current one, every update), or on: false to let it go.',
    schema: { field: { type: 'string' }, value: { type: 'number' }, on: { type: 'boolean' } },
    required: ['field'],
    run: (a) => call('scenePin', a),
  },
  lab_scene_trace: {
    description:
      'Scene stepping: what the engine reports as its code runs (mode: off, selected = what’s picked and the tools used, all), stop at every step (stepEach), or a breakpoint at "src/File.cpp:line" (on: false removes it).',
    schema: { mode: { type: 'string' }, stepEach: { type: 'boolean' }, breakpoint: { type: 'string' }, on: { type: 'boolean' } },
    run: (a) => call('sceneTrace', a),
  },
  lab_scene_step: {
    description:
      'Scene stepping: go on from where the engine is stopped (how: step, over, out, continue), or stop at the next step traced (step while running). Returns where it stopped: who, what, file:line, the zoo.exe function, its values (before -> after) and the stack.',
    schema: { how: { type: 'string' } },
    run: (a) => call('sceneStep', a),
  },
  lab_scene_steps: {
    description: 'Scene stepping: the latest steps the engine reported (last: how many, who: a name to filter by), and where it’s stopped if it is.',
    schema: { last: { type: 'number' }, who: { type: 'string' }, q: { type: 'string', description: 'only steps mentioning this (also typed into the Steps filter)' } },
    run: (a) => call('sceneSteps', a),
  },
  lab_try_function: {
    description:
      'Try zoo.exe: run one of the original game’s functions (address) on what’s picked in the Scene, laid out as its struct (struct: ZTAnimal, ZTHabitat...; worked out if left out) with live values. Returns what it used (known fields, before -> after), what it wanted and nobody’s mapped (missing: struct+offset), the text it used, the calls it made (calls: skip or run) and what it returned. fill gives a missing offset a value ({struct, offset, value}; value null removes it).',
    schema: {
      address: { type: 'string' },
      struct: { type: 'string' },
      calls: { type: 'string' },
      fill: { type: 'object', properties: { struct: { type: 'string' }, offset: { type: 'number' }, value: {} } },
    },
    run: (a) => call('tryFunction', a),
  },
  lab_scene_console: {
    description: 'Scene view: run a command in the game’s dev console (help lists them: exhibits, dung, food, litter, wear, money, raise/lower, layroad, ...) and return its answer.',
    schema: { command: { type: 'string' } },
    required: ['command'],
    run: (a) => call('sceneConsole', a),
  },
  lab_engine_log: {
    description: 'The last lines of the Lab engine’s log (builds, mods’ logs).',
    schema: { lines: { type: 'number' } },
    run: (a) => call('engineLog', a),
  },
  lab_play: {
    description: 'Play view: start (start: true) or stop the whole game inside the Lab - our engine, or game: "original" for the real zoo.exe (windowed by our hook, src/original/). shot: a .png path - a picture of the original as it draws (it isn’t in lab_screenshot).',
    schema: { start: { type: 'boolean' }, game: { type: 'string', enum: ['engine', 'original'] }, shot: { type: 'string' } },
    run: (a) => call('play', a),
  },
};

// ---- MCP over stdio (JSON-RPC, a message a line)
const out = (m) => process.stdout.write(JSON.stringify(m) + '\n');
const rl = readline.createInterface({ input: process.stdin });
rl.on('line', async (line) => {
  let msg;
  try {
    msg = JSON.parse(line);
  } catch {
    return;
  }
  const { id, method, params } = msg;
  if (method === 'initialize') {
    out({
      jsonrpc: '2.0',
      id,
      result: {
        protocolVersion: (params && params.protocolVersion) || '2025-06-18',
        capabilities: { tools: {} },
        serverInfo: { name: 'zt1-lab', version: '0.1.0' },
        instructions: 'Drives the running ZT1 Lab app (start lab-app/ZT1 Lab.cmd first). Every tool acts through the Lab’s own UI, visibly. Use lab_screenshot to see it.',
      },
    });
  } else if (method === 'tools/list') {
    out({
      jsonrpc: '2.0',
      id,
      result: {
        tools: Object.entries(TOOLS).map(([name, t]) => ({
          name,
          description: t.description,
          inputSchema: { type: 'object', properties: t.schema, ...(t.required ? { required: t.required } : {}) },
        })),
      },
    });
  } else if (method === 'tools/call') {
    const t = TOOLS[params.name];
    if (!t) return out({ jsonrpc: '2.0', id, error: { code: -32602, message: 'no tool ' + params.name } });
    try {
      const r = await t.run(params.arguments || {});
      const res = r && r.content ? r : text(r && r.error ? { error: r.error } : r && 'result' in r ? r.result : r);
      if (r && r.error) res.isError = true;
      out({ jsonrpc: '2.0', id, result: res });
    } catch (e) {
      out({ jsonrpc: '2.0', id, result: { ...text(String(e)), isError: true } });
    }
  } else if (method === 'ping') {
    out({ jsonrpc: '2.0', id, result: {} });
  } else if (id !== undefined) {
    out({ jsonrpc: '2.0', id, error: { code: -32601, message: 'no method ' + method } });
  }
});
