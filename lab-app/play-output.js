// The engine's stdout is a byte stream: readiness and switch replies can
// arrive across several chunks, or share one chunk with other log lines.
const readline = require('node:readline');

function watchEngineOutput(proc, { isCurrent, onReady, onSwitch }) {
  let ready = false;
  const readers = [proc.stdout, proc.stderr].map(stream => {
    const reader = readline.createInterface({ input: stream, crlfDelay: Infinity });
    reader.on('line', line => {
      if (!isCurrent()) return;
      if (!ready && stream === proc.stdout && line.includes('[Embed] ready')) {
        ready = true;
        onReady();
      }
      const blocked = '[Mods] switch blocked:';
      if (line.includes(blocked)) onSwitch({ error: line.split(blocked)[1].trim() });
      else if (line.includes('[Mods] live switch applied;')) onSwitch({ ok: true });
      else if (line.includes('[Mods] live switch rolled back'))
        onSwitch({ error: 'The game kept the previous switches to preserve this zoo.' });
    });
    return reader;
  });
  return () => readers.forEach(reader => reader.close());
}

module.exports = { watchEngineOutput };
