const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function harness(onTick) {
  const sent = [], cleared = [];
  let ticks = 0;
  const context = {
    S: { engine: { ups: 1, log: [] }, rebuildTimer: 73 }, SC: {}, ACTIONS: {},
    lab: { onControl() {}, engineSend(command) { sent.push(command); } },
    setTimeout(callback) {
      onTick(context, sent, ++ticks);
      Promise.resolve().then(callback);
      return ticks;
    },
    clearTimeout(id) { cleared.push(id); },
  };
  vm.createContext(context);
  const source = fs.readFileSync(path.join(__dirname, '../renderer/control.js'), 'utf8');
  vm.runInContext(source + '\nshowScene = async () => true; engineUp = async () => true; globalThis.control = CONTROL;', context);
  return { context, sent, cleared };
}

test('map load ignores stale state and returns counts from its matching reply', async () => {
  const h = harness((c, sent, tick) => {
    const command = sent.at(-1);
    c.SC.mapLoaded = tick < 3
      ? { request: 'old-replayed-load', ok: true, scene: { map: 'old' } }
      : { request: command.request, ok: true, scene: { map: 'ff01', mapName: 'Grass', flat: true, counts: { guests: 5 } } };
  });
  const result = await h.context.control.sceneLoad({ map: 'ff01', flat: true });
  assert.equal(result.counts.guests, 5);
  assert.equal(result.map, 'Grass');
  assert.deepEqual(h.cleared, [73]);
});

test('map load resends to a restarted engine without accepting the old engine reply', async () => {
  const h = harness((c, sent, tick) => {
    if (tick === 1) c.S.engine.ups++;
    if (tick === 2) c.SC.mapLoaded = { request: sent.at(-1).request, ok: true,
      scene: { map: 'save:fixture', mapName: 'Fixture', flat: false, counts: { animals: 2 } } };
  });
  const result = await h.context.control.sceneLoad({ map: 'save:fixture' });
  assert.equal(h.sent.length, 2);
  assert.equal(h.sent[0].request, h.sent[1].request);
  assert.equal(result.counts.animals, 2);
});

test('unknown-map errors require an explicit matching load reply', async () => {
  const h = harness((c, sent) => {
    c.SC.mapLoaded = { request: sent.at(-1).request, ok: true,
      scene: { map: 'ff01', mapName: 'Grass', counts: {} } };
  });
  const result = await h.context.control.sceneLoad({ map: 'missing' });
  assert.match(result.error, /no map called missing/);
});

test('long console checks wait for their own complete reply despite noisy logs', async () => {
  const h = harness((c, sent, tick) => {
    c.S.engine.log = Array(600).fill('unrelated frame log');
    c.SC.consoleReply = tick < 40 ? { request: 'old', text: 'wrong result' }
      : { request: sent.at(-1).request, text: '51/51 checks passed\n' };
  });
  const result = await h.context.control.sceneConsole({ command: 'activitycheck' });
  assert.equal(result.text, '51/51 checks passed\n');
});
