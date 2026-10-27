const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const os = require('os');
const path = require('path');
const profiles = require('../mod-profiles');

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'zt1-mod-profiles-test-'));
  const cfg = path.join(root, 'mods.cfg');
  const modRoot = path.join(root, 'downloaded');
  const stateDir = path.join(root, 'sessions');
  fs.writeFileSync(cfg, 'folder=' + modRoot + '\nenabled=My Choice\n');
  t.after(() => {
    // The verified mkdtemp result is the only recursive cleanup target.
    assert.equal(path.dirname(root), path.resolve(os.tmpdir()));
    assert.ok(path.basename(root).startsWith('zt1-mod-profiles-test-'));
    fs.rmSync(root, { recursive: true, force: true });
  });
  return { root, cfg, modRoot, stateDir,
    create: folder => profiles.createTestProfile({ configFile: cfg, modRoot, stateDir, mod: { folder } }) };
}

test('testing adds one mod without changing saved switches', t => {
  const f = fixture(t), original = fs.readFileSync(f.cfg);
  const session = f.create('Test Animal');
  assert.deepEqual(profiles.readConfig(session.file).enabled, ['Test Animal']);
  assert.equal(profiles.readConfig(session.file).loadMods, true);
  assert.deepEqual(fs.readFileSync(f.cfg), original);
  session.dispose();
  assert.deepEqual(fs.readFileSync(f.cfg), original);
});

test('in-game switches and folder changes in a test stay private', t => {
  const f = fixture(t), session = f.create('Test Animal');
  fs.writeFileSync(session.file, 'folder=elsewhere\nenabled=Another Mod\n');
  assert.deepEqual(profiles.readConfig(f.cfg), { folder: f.modRoot, enabled: ['My Choice'] });
  session.dispose();
});

test('simultaneous Zoo and Play tests do not share their switch files', t => {
  const f = fixture(t), zoo = f.create('Zoo Animal'), play = f.create('Marine Animal');
  assert.notEqual(zoo.file, play.file);
  assert.deepEqual(profiles.readConfig(play.file).enabled, ['Marine Animal']);
  zoo.dispose();
  assert.ok(fs.existsSync(play.file));
  play.dispose();
  play.dispose();
});

test('a normal launch cannot inherit a temporary profile', () => {
  assert.deepEqual(profiles.engineEnvironment(null, { ZT1_MODS_CONFIG: 'old-test.cfg', ZT1_CONFIG_PATH: 'old-zoo.ini', OTHER: 'keep' }), { OTHER: 'keep' });
  assert.deepEqual(profiles.engineEnvironment({ file: 'new-test.cfg' }, { zt1_mods_config: 'old-test.cfg' }), { ZT1_MODS_CONFIG: 'new-test.cfg' });
});

test('a temporary master switch and game settings cannot alter normal preferences', t => {
  const f = fixture(t), normalIni = path.join(f.root, 'zoo.ini');
  fs.writeFileSync(normalIni, '[Extras]\nmods=0\nmodmenu=0\n');
  profiles.writeConfig(f.cfg, { folder: f.modRoot, loadMods: false, enabled: ['Old Selection'] });
  const before = fs.readFileSync(f.cfg);
  const session = profiles.createTestProfile({ configFile: f.cfg, modRoot: f.modRoot, stateDir: f.stateDir, mod: { folder: 'Only This Test' }, gameSettingsFile: normalIni });
  assert.deepEqual(profiles.readConfig(session.file).enabled, ['Only This Test']);
  assert.equal(profiles.readConfig(session.file).loadMods, true);
  fs.writeFileSync(session.settingsFile, '[Extras]\nmodmenu=1\n');
  profiles.writeConfig(session.file, { folder: f.modRoot, loadMods: false, enabled: [] });
  assert.equal(fs.readFileSync(normalIni, 'utf8'), '[Extras]\nmods=0\nmodmenu=0\n');
  assert.deepEqual(fs.readFileSync(f.cfg), before);
  assert.equal(profiles.engineEnvironment(session, {}).ZT1_CONFIG_PATH, session.settingsFile);
  const privateIni = session.settingsFile;
  session.dispose();
  assert.equal(fs.existsSync(privateIni), false);
});

test('missing config means every downloaded mod starts off', t => {
  const f = fixture(t);
  fs.unlinkSync(f.cfg);
  assert.deepEqual(profiles.readConfig(f.cfg), { folder: '', enabled: [] });
  const session = f.create('Test Only');
  assert.deepEqual(profiles.readConfig(session.file).enabled, ['Test Only']);
  assert.equal(fs.existsSync(f.cfg), false);
  session.dispose();
});

test('Lab and engine agree on portable versus installed settings', t => {
  const f = fixture(t), cwd = path.join(f.root, 'game'), app = path.join(f.root, 'engine');
  fs.mkdirSync(cwd); fs.mkdirSync(app);
  const exe = path.join(app, 'zt1-engine.exe'), local = path.join(f.root, 'local');
  assert.equal(profiles.gameConfigPath(exe, cwd, local), path.join(local, 'mods.cfg'));
  fs.writeFileSync(path.join(cwd, 'animals.ztd'), '');
  fs.writeFileSync(path.join(cwd, 'ui.ztd'), '');
  assert.equal(profiles.gameConfigPath(exe, cwd, local), path.join(app, 'mods.cfg'));
});

test('temporary tests include installed dependencies without changing normal switches', t => {
  const f = fixture(t);
  for (const [folder, info] of [['Theme', { dependencies: ['base-id'] }], ['Base', { id: 'base-id' }]]) {
    fs.mkdirSync(path.join(f.modRoot, folder), { recursive: true });
    fs.writeFileSync(path.join(f.modRoot, folder, 'mod.json'), JSON.stringify(info));
  }
  const session = f.create('Theme');
  assert.deepEqual(profiles.readConfig(session.file).enabled, ['Theme', 'Base']);
  assert.deepEqual(profiles.readConfig(f.cfg).enabled, ['My Choice']);
  session.dispose();
});

test('a missing dependency cancels the test before creating or changing switch files', t => {
  const f = fixture(t);
  fs.mkdirSync(path.join(f.modRoot, 'Theme'), { recursive: true });
  fs.writeFileSync(path.join(f.modRoot, 'Theme', 'mod.json'), JSON.stringify({ requires: ['Missing Pack'] }));
  assert.throws(() => f.create('Theme'), /Missing required mod: Missing Pack/);
  assert.equal(fs.existsSync(f.stateDir), false);
  assert.deepEqual(profiles.readConfig(f.cfg).enabled, ['My Choice']);
});

test('a required dependency stays on until its dependent mod is switched off', t => {
  const f = fixture(t);
  for (const [folder, info] of [['Theme', { name: 'My Theme', dependencies: ['base-id'] }], ['Base', { id: 'base-id' }]]) {
    fs.mkdirSync(path.join(f.modRoot, folder), { recursive: true });
    fs.writeFileSync(path.join(f.modRoot, folder, 'mod.json'), JSON.stringify(info));
  }
  fs.appendFileSync(f.cfg, 'enabled=Theme\nenabled=Base\n');
  assert.match(profiles.dependencyReason(f.cfg, f.modRoot, 'Base'), /Required by My Theme/);
  assert.equal(profiles.dependencyReason(f.cfg, f.modRoot, 'Theme'), '');
  fs.writeFileSync(f.cfg, 'enabled=Base\n');
  assert.equal(profiles.dependencyReason(f.cfg, f.modRoot, 'Base'), '');
});

test('an unfinished unrelated download cannot prevent another mod being tested', t => {
  const f = fixture(t);
  fs.mkdirSync(path.join(f.modRoot, 'Unfinished'), { recursive: true });
  fs.writeFileSync(path.join(f.modRoot, 'Unfinished', 'mod.json'), '{');
  const session = f.create('Valid Test');
  assert.ok(profiles.readConfig(session.file).enabled.includes('Valid Test'));
  session.dispose();
});

test('saving switches replaces the complete config and leaves no temporary files', t => {
  const f = fixture(t);
  profiles.writeConfig(f.cfg, { folder: f.modRoot, enabled: ['One', 'Two'] });
  assert.deepEqual(profiles.readConfig(f.cfg), { folder: f.modRoot, enabled: ['One', 'Two'] });
  profiles.writeConfig(f.cfg, { folder: f.modRoot, enabled: [] });
  assert.deepEqual(profiles.readConfig(f.cfg).enabled, []);
  assert.deepEqual(fs.readdirSync(f.root).filter(name => name.includes('.tmp-')), []);
});

test('a failed switch write preserves the previous config and cleans its temporary file', t => {
  const f = fixture(t), directory = path.join(f.root, 'not-a-file');
  fs.mkdirSync(directory);
  assert.throws(() => profiles.writeConfig(directory, { folder: f.modRoot, enabled: ['One'] }));
  assert.ok(fs.statSync(directory).isDirectory());
  assert.deepEqual(profiles.readConfig(f.cfg).enabled, ['My Choice']);
  assert.deepEqual(fs.readdirSync(f.root).filter(name => name.includes('.tmp-')), []);
});
