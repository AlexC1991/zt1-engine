// A Lab test gets its own switches. Ordinary Play uses the player's file.
const fs = require('fs');
const path = require('path');
const gameFolder = dir => ['animals.ztd', 'ui.ztd', 'scenery.ztd'].filter(name => fs.existsSync(path.join(dir, name))).length >= 2;

function readConfig(file) {
  const cfg = { folder: '', enabled: [] };
  if (!fs.existsSync(file)) return cfg;
  for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    if (line.slice(0, eq) === 'folder') cfg.folder = line.slice(eq + 1);
    else if (line.slice(0, eq) === 'enabled' && line.slice(eq + 1)) cfg.enabled.push(line.slice(eq + 1));
    else if (line.slice(0, eq) === 'loadMods') cfg.loadMods = line.slice(eq + 1) !== '0';
  }
  return cfg;
}

function gameConfigPath(exe, cwd, localData) {
  // Same portable/installed distinction as Paths::setup in the engine.
  const app = path.dirname(exe);
  return path.join(gameFolder(cwd) || gameFolder(app) ? app : localData, 'mods.cfg');
}

function gameSettingsPath(exe, cwd, localData) {
  const app = path.dirname(exe);
  return path.join(gameFolder(cwd) ? cwd : gameFolder(app) ? app : localData, 'zoo.ini');
}

function installedMods(modRoot) {
  const available = new Map();
  if (fs.existsSync(modRoot)) for (const entry of fs.readdirSync(modRoot, { withFileTypes: true })) {
    if (!entry.isDirectory() || entry.name.startsWith('.')) continue;
    let info = {};
    const meta = path.join(modRoot, entry.name, 'mod.json');
    try { if (fs.existsSync(meta)) info = JSON.parse(fs.readFileSync(meta, 'utf8')); } catch {}
    const found = { ...info, folder: entry.name };
    available.set(entry.name, found);
    if (info.id) available.set(info.id, found);
  }
  return available;
}

function dependencyReason(configFile, modRoot, folder) {
  const available = installedMods(modRoot);
  const target = available.get(folder);
  for (const name of readConfig(configFile).enabled) {
    if (name === folder) continue;
    const mod = available.get(name);
    if (!mod) continue;
    for (const dep of [...(mod.dependencies || []), ...(mod.requires || [])]) {
      const key = typeof dep === 'string' ? dep : dep.id;
      if (key === folder || (target && key === target.id)) return `Required by ${mod.name || mod.folder}. Switch that mod off first.`;
    }
  }
  return '';
}

function createTestProfile({ configFile, modRoot, mod, stateDir, gameSettingsFile }) {
  const enabled = new Set();
  const available = installedMods(modRoot);
  available.set(mod.folder, available.get(mod.folder) || mod);
  const visited = new Set();
  function add(key) {
    const found = available.get(key);
    if (!found) throw new Error('Missing required mod: ' + key);
    if (visited.has(found.folder)) return;
    visited.add(found.folder);
    enabled.add(found.folder);
    for (const dep of [...(found.dependencies || []), ...(found.requires || [])]) add(typeof dep === 'string' ? dep : dep.id);
  }
  add(mod.folder);
  if (gameSettingsFile && !fs.existsSync(gameSettingsFile)) throw new Error('Start the normal Test Zoo once before testing mods, so its game settings are ready.');
  fs.mkdirSync(stateDir, { recursive: true });
  const dir = fs.mkdtempSync(path.join(stateDir, 'mod-test-'));
  const file = path.join(dir, 'mods.cfg');
  const settingsFile = gameSettingsFile ? path.join(dir, 'zoo.ini') : null;
  try {
    fs.writeFileSync(file, ['folder=' + modRoot, 'loadMods=1', ...[...enabled].map(folder => 'enabled=' + folder)].join('\n') + '\n');
    if (settingsFile) fs.copyFileSync(gameSettingsFile, settingsFile);
  } catch (error) {
    fs.rmSync(file, { force: true });
    if (settingsFile) fs.rmSync(settingsFile, { force: true });
    try { fs.rmdirSync(dir); } catch {}
    throw error;
  }
  return {
    file,
    settingsFile,
    dispose() {
      // Only the file and directory allocated above: never a mods directory.
      fs.rmSync(file, { force: true });
      if (settingsFile) fs.rmSync(settingsFile, { force: true });
      try { fs.rmdirSync(dir); } catch {}
    },
  };
}

function engineEnvironment(profile, inherited = process.env) {
  const env = { ...inherited };
  for (const key of Object.keys(env)) if (['ZT1_MODS_CONFIG', 'ZT1_CONFIG_PATH'].includes(key.toUpperCase())) delete env[key];
  if (profile) env.ZT1_MODS_CONFIG = profile.file;
  if (profile && profile.settingsFile) env.ZT1_CONFIG_PATH = profile.settingsFile;
  return env;
}

function writeConfig(file, cfg) {
  const temporary = file + '.tmp-' + process.pid + '-' + require('crypto').randomUUID();
  const text = [...(cfg.folder ? ['folder=' + cfg.folder] : []), ...(cfg.loadMods === undefined ? [] : ['loadMods=' + (cfg.loadMods ? '1' : '0')]), ...cfg.enabled.map(folder => 'enabled=' + folder)].join('\n') + '\n';
  try { fs.writeFileSync(temporary, text); fs.renameSync(temporary, file); }
  finally { fs.rmSync(temporary, { force: true }); }
}

module.exports = { readConfig, gameConfigPath, gameSettingsPath, createTestProfile, engineEnvironment, dependencyReason, writeConfig };
