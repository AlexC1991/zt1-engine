// What the page may ask of the main process (nothing else of Node)
const { contextBridge, ipcRenderer } = require('electron');

// Messages for the page wait till all its scripts are in: one arriving
// between two of them reached a handler whose helpers weren't loaded yet
// (an engine message after a build, before stepview.js)
let ready = document.readyState !== 'loading';
const held = [];
if (!ready)
  window.addEventListener('DOMContentLoaded', () => {
    ready = true;
    held.splice(0).forEach((f) => f());
  });
const on = (channel, fn) =>
  ipcRenderer.on(channel, (e, ...a) => (ready ? fn(e, ...a) : held.push(() => fn(e, ...a))));

contextBridge.exposeInMainWorld('lab', {
  helpOpenExternal: (url) => ipcRenderer.invoke('help:openExternal', url),
  checkLabUpdate: () => ipcRenderer.invoke('lab:update'),
  labUpdateState: () => ipcRenderer.invoke('lab:updateState'),
  labUpdateInstall: () => ipcRenderer.invoke('lab:updateInstall'),
  onLabUpdate: (fn) => on('lab:updateState', (_e, st) => fn(st)),
  // the Lab engine
  engineSend: (cmd) => ipcRenderer.send('engine:send', cmd),
  engineStart: () => ipcRenderer.invoke('engine:start'),
  engineRestart: () => ipcRenderer.invoke('engine:restart'),
  onEngine: (fn) => on('engine:msg', (_e, m) => fn(m)),
  onEngineStatus: (fn) => on('engine:status', (_e, m) => fn(m)),
  onFrame: (fn) => on('engine:frame', (_e, f) => fn(f)),
  // Experiments > HD art: the AI-upscaled map art mod
  // the game, in the Play view
  playStart: (testFolder) => ipcRenderer.invoke('play:start', testFolder),
  playStop: () => ipcRenderer.invoke('play:stop'),
  playCmd: (line) => ipcRenderer.send('play:cmd', line),
  playArea: (area) => ipcRenderer.send('play:area', area),
  playShot: (file) => ipcRenderer.invoke('play:shot', file),
  playState: () => ipcRenderer.invoke('play:state'),
  playWindowCheck: () => ipcRenderer.invoke('play:windowCheck'),
  recStatus: () => ipcRenderer.invoke('orig:recStatus'),
  learnIdle: (seconds) => ipcRenderer.invoke('orig:learnIdle', seconds),
  recStart: (label) => ipcRenderer.invoke('orig:recStart', label),
  recStop: () => ipcRenderer.invoke('orig:recStop'),
  recList: () => ipcRenderer.invoke('orig:recList'),
  recLoad: (file) => ipcRenderer.invoke('orig:recLoad', file),
  recSave: (file, names, by) => ipcRenderer.invoke('orig:recSave', file, names, by),
  recDiscard: (file) => ipcRenderer.invoke('orig:recDiscard', file),
  origSteps: (from) => ipcRenderer.invoke('orig:steps', from),
  origWatch: (on) => ipcRenderer.invoke('orig:watch', on),
  origPauseGame: (pause) => ipcRenderer.invoke('orig:pauseGame', pause),
  origNameFn: (address, name, summary, confidence, cls, by) => ipcRenderer.invoke('orig:nameFn', address, name, summary, confidence, cls, by),
  origStepsOf: (file) => ipcRenderer.invoke('orig:stepsOf', file),
  acceptGuess: (address, name, summary, by) => ipcRenderer.invoke('orig:acceptGuess', address, name, summary, by),
  origAsk: (line, prefix) => ipcRenderer.invoke('orig:ask', line, prefix),
  origFind: (line) => ipcRenderer.invoke('orig:find', line),
  origWrites: (clear) => ipcRenderer.invoke('orig:writes', clear),
  onPlayExit: (fn) => on('play:exit', (_e, m) => fn(m)),
  playOriginal: () => ipcRenderer.invoke('play:original'),
  // Ghidra
  ghidra: (p) => ipcRenderer.invoke('ghidra:get', p),
  ghidraStatus: () => ipcRenderer.invoke('ghidra:status'),
  // the knowledge base
  kbLoad: () => ipcRenderer.invoke('kb:load'),
  kbRun: (args) => ipcRenderer.invoke('kb:run', args),
  zooRun: (req) => ipcRenderer.invoke('zoo:run', req),
  zooBin: (req) => ipcRenderer.invoke('zoo:bin', req),
  kbLoaders: () => ipcRenderer.invoke('kb:loaders'),
  kbLoaderSave: (loader, struct, by, confidence) => ipcRenderer.invoke('kb:loaderSave', loader, struct, by, confidence),
  kbClasses: () => ipcRenderer.invoke('kb:classes'),
  kbClass: (addr) => ipcRenderer.invoke('kb:class', addr),
  kbClassSave: (addr, name, by) => ipcRenderer.invoke('kb:classSave', addr, name, by),
  kbText: () => ipcRenderer.invoke('kb:text'),
  kbTextSave: (by) => ipcRenderer.invoke('kb:textSave', by),
  kbOrphans: () => ipcRenderer.invoke('kb:orphans'),
  ghidraRefresh: () => ipcRenderer.invoke('ghidra:refresh'),
  atlasBuild: () => ipcRenderer.invoke('atlas:build'),
  atlasLoad: () => ipcRenderer.invoke('atlas:load'),
  fidelityLoad: () => ipcRenderer.invoke('fidelity:load'),
  formulasLoad: () => ipcRenderer.invoke('formulas:load'),
  formulasSave: (r) => ipcRenderer.invoke('formulas:save', r),
  uiIds: (nums) => ipcRenderer.invoke('ui:ids', nums),
  atlasVerdict: (label, status, at, note, by) => ipcRenderer.invoke('atlas:verdict', label, status, at, note, by),
  setMe: (name) => ipcRenderer.invoke('me:set', name),
  // the shared knowledge base: what every install has found (re/cloud)
  kbCloud: () => ipcRenderer.invoke('kb:cloud'),
  kbCloudSync: (what, dry) => ipcRenderer.invoke('kb:cloudSync', what, !!dry),
  // sharing it in the background (the header's Sharing control)
  shareGet: () => ipcRenderer.invoke('share:get'),
  shareSet: (on) => ipcRenderer.invoke('share:set', !!on),
  shareNow: () => ipcRenderer.invoke('share:now'),
  onShare: (fn) => on('share:state', (_e, s) => fn(s)),
  // mods, the engine's source
  modsList: () => ipcRenderer.invoke('mods:list'),
  modsRead: (name) => ipcRenderer.invoke('mods:read', name),
  modsWrite: (name, text) => ipcRenderer.invoke('mods:write', name, text),
  modsExists: (name) => ipcRenderer.invoke('mods:exists', name),
  srcRead: (rel, line, before, after) => ipcRenderer.invoke('src:read', rel, line, before, after),
  openFile: (rel, line) => ipcRenderer.invoke('open:file', rel, line),
  // the engine's own source: read, searched, changed, built
  srcList: (which) => ipcRenderer.invoke('src:list', which),
  readAny: (abs) => ipcRenderer.invoke('file:readAny', abs),
  srcReadAll: (rel) => ipcRenderer.invoke('src:readAll', rel),
  srcCreate: (rel, text) => ipcRenderer.invoke('src:create', rel, text),
  srcWrite: (rel, text, crlf) => ipcRenderer.invoke('src:write', rel, text, crlf),
  srcGrep: (q) => ipcRenderer.invoke('src:grep', q),
  srcKeys: (keys) => ipcRenderer.invoke('src:keys', keys),
  engineBuild: () => ipcRenderer.invoke('engine:build'),
  onBuildLine: (fn) => on('build:line', (_e, l) => fn(l)),
  onBuildDone: (fn) => on('build:done', (_e, r) => fn(r)),
  reveal: (rel) => ipcRenderer.invoke('open:reveal', rel),
  openUrl: (url) => ipcRenderer.invoke('open:url', url),
  sync: (what, args) => ipcRenderer.invoke('sync:' + what, args || {}),
  onSyncLine: (fn) => on('sync:line', (_e, l) => fn(l)),
  engineRelease: (opts) => ipcRenderer.invoke('engine:release', opts || {}),
  animTool: (req) => ipcRenderer.invoke('anim:tool', req),
  gameModsList: () => ipcRenderer.invoke('gamemods:list'),
  gameModsEnable: (folder, on) => ipcRenderer.invoke('gamemods:enable', folder, on),
  gameModsTestZoo: (folder) => ipcRenderer.invoke('gamemods:testZoo', folder),
  gameModsTestState: () => ipcRenderer.invoke('gamemods:testState'),
  onModTest: (fn) => on('gamemods:test', (_e, mod) => fn(mod)),
  // Experiments > Your mods (renderer/modkit.js)
  modkitNew: (req) => ipcRenderer.invoke('modkit:new', req),
  modkitImport: (file) => ipcRenderer.invoke('modkit:import', file),
  modkitCheck: (folder) => ipcRenderer.invoke('modkit:check', folder),
  modkitContents: (folder) => ipcRenderer.invoke('modkit:contents', folder),
  modkitSpecies: (req) => ipcRenderer.invoke('modkit:species', req),
  modkitEntry: (folder, ztd, entry) => ipcRenderer.invoke('modkit:entry', folder, ztd, entry),
  studioTree: (folder) => ipcRenderer.invoke('studio:tree', folder),
  modkitEntrySave: (folder, ztd, entry, text, crlf) => ipcRenderer.invoke('modkit:entrySave', folder, ztd, entry, text, crlf),
  modkitCopyIn: (folder, file) => ipcRenderer.invoke('modkit:copyIn', folder, file),
  artDo: (folder, req) => ipcRenderer.invoke('art:do', folder, req),
  animalDo: (folder, req) => ipcRenderer.invoke('animal:do', folder, req),
  mapTarget: (req) => ipcRenderer.invoke('map:target', req),
  mapPack: (folder, req) => ipcRenderer.invoke('map:pack', folder, req),
  modkitSave: (folder, fields) => ipcRenderer.invoke('modkit:save', folder, fields),
  modkitPicture: (folder, req) => ipcRenderer.invoke('modkit:picture', folder, req),
  modkitOpen: (folder) => ipcRenderer.invoke('modkit:open', folder),
  modkitToken: (token) => ipcRenderer.invoke('modkit:token', token),
  modkitMine: () => ipcRenderer.invoke('modkit:mine'),
  modkitTakedown: (id) => ipcRenderer.invoke('modkit:takedown', id),
  modkitUpload: (folder) => ipcRenderer.invoke('modkit:upload', folder),
  onModkit: (fn) => on('modkit:progress', (_e, s) => fn(s)),
  // (a dropped file's path: Electron's webUtils, as File.path is gone)
  pathForFile: (f) => require('electron').webUtils.getPathForFile(f),
  onReleaseDone: (fn) => on('release:done', (_e, r) => fn(r)),
  // clangd, the C++ language server
  lspStart: () => ipcRenderer.invoke('lsp:start'),
  lspSend: (msg) => ipcRenderer.send('lsp:send', msg),
  onLsp: (fn) => on('lsp:msg', (_e, m) => fn(m)),
  onLspExit: (fn) => on('lsp:exit', () => fn()),
  // (the control port: a call from the MCP, and its answer)
  onControl: (fn) => on('ctl:call', (_e, c) => fn(c)),
  controlResult: (r) => ipcRenderer.send('ctl:result', r),
  // (the screenshot test: go to a view, maybe do something there)
  onTest: (fn) => on('test:do', (_e, m) => fn(m)),
});
