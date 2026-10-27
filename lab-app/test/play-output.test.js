const test = require('node:test');
const assert = require('node:assert/strict');
const { PassThrough } = require('node:stream');
const { watchEngineOutput } = require('../play-output');

function fixture(t) {
  const proc = { stdout: new PassThrough(), stderr: new PassThrough() };
  const state = { current: true, ready: 0, switches: [] };
  const close = watchEngineOutput(proc, {
    isCurrent: () => state.current,
    onReady: () => state.ready++,
    onSwitch: result => state.switches.push(result),
  });
  t.after(() => { close(); proc.stdout.destroy(); proc.stderr.destroy(); });
  return { proc, state };
}

test('readiness survives a split at every byte boundary', t => {
  const line = Buffer.from('[Embed] ready\r\n');
  for (let split = 1; split < line.length; split++) {
    const { proc, state } = fixture(t);
    proc.stdout.write(line.subarray(0, split));
    proc.stdout.write(line.subarray(split));
    assert.equal(state.ready, 1, `split ${split}`);
  }
});

test('readiness is delivered once and only from the current stdout', t => {
  const { proc, state } = fixture(t);
  proc.stderr.write('[Embed] ready\n');
  assert.equal(state.ready, 0);
  proc.stdout.write('other log\n[Embed] ready\n[Embed] ready\n');
  assert.equal(state.ready, 1);
});

test('a replaced process cannot move the new game or settle its switch', t => {
  const { proc, state } = fixture(t);
  state.current = false;
  proc.stdout.write('[Embed] ready\n[Mods] live switch applied; ok\n');
  assert.equal(state.ready, 0);
  assert.deepEqual(state.switches, []);
});

test('split replies preserve UTF-8 error text and all reply types', t => {
  const { proc, state } = fixture(t);
  const reply = Buffer.from('[Mods] switch blocked: Café habitat\n');
  for (const byte of reply) proc.stderr.write(Buffer.from([byte]));
  proc.stdout.write('[Mods] live switch applied; ok\n[Mods] live switch rolled back\n');
  assert.deepEqual(state.switches, [
    { error: 'Café habitat' }, { ok: true },
    { error: 'The game kept the previous switches to preserve this zoo.' },
  ]);
});
