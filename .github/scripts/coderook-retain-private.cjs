// A GitHub checkout does not hold CodeRook's owner-only tooling. Keep those
// paths out of this machine's deletion baseline while retaining the complete
// remote manifest. cbx --sync can still remove public files deleted in Git.
const path = require('node:path');

function privateMatcher(pattern) {
  const folder = pattern.endsWith('/');
  const body = pattern.replace(/^\//, '').replace(/\/$/, '');
  const anchored = pattern.startsWith('/') || body.includes('/');
  let regex = '';
  for (let i = 0; i < body.length; i++) {
    if (body[i] === '*' && body[i + 1] === '*') {
      regex += body[i + 2] === '/' ? '(?:.*/)?' : '.*';
      i += body[i + 2] === '/' ? 2 : 1;
    } else if (body[i] === '*') regex += '[^/]*';
    else if (body[i] === '?') regex += '[^/]';
    else regex += body[i].replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  }
  return new RegExp((anchored ? '^' : '(?:^|/)') + regex + (folder ? '(?:/|$)' : '$'));
}

function retainPrivate(link, patterns) {
  if (!link || !link.repositoryId || !link.manifest) throw Error('Run cbx link before retaining private paths.');
  const matchers = patterns.map(privateMatcher);
  const local = link.local || link.manifest;
  const retained = Object.keys(local).filter((p) => matchers.some((r) => r.test(p)));
  return {
    link: { ...link, local: Object.fromEntries(Object.entries(local).filter(([p]) => !retained.includes(p))) },
    count: retained.length,
  };
}

async function main() {
  const cli = process.env.CBX_CLI_ROOT || path.dirname(require.resolve('@coderook/cli/package.json'));
  const identify = require(path.join(cli, 'dist/cbx/src/core/identify.js'));
  identify.declareClient('cli', require(path.join(cli, 'package.json')).version);
  const config = require(path.join(cli, 'dist/cli/src/config.js'));
  const api = require(path.join(cli, 'dist/cli/src/api.js'));
  const folder = process.cwd();
  const current = await config.readLink(folder);
  if (!current) throw Error('This checkout is not linked to CodeRook.');
  const patterns = await api.getPrivatePaths(current.repositoryId);
  const retained = retainPrivate(current, patterns);
  await config.writeLink(folder, retained.link);
  console.log(`Retained ${retained.count} owner-only paths outside the GitHub mirror's deletion baseline.`);
}

module.exports = { privateMatcher, retainPrivate };
if (require.main === module) main().catch((e) => { console.error(e.message); process.exitCode = 1; });
