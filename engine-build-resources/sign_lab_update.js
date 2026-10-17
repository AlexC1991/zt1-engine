'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const [version, installerPath, outputPath] = process.argv.slice(2);
if (!/^\d+\.\d+\.\d+$/.test(version || '')) throw new Error('Lab version must be three numeric components');
const installer = path.resolve(installerPath || '');
const filename = path.basename(installer);
if (filename !== `ZT1-Lab-Setup-${version}.exe` || !fs.statSync(installer).isFile()) throw new Error('Installer name/version mismatch or installer missing');
let privatePem = process.env.ZT1_LAB_UPDATE_KEY?.replace(/\\n/g, '\n');
if (!privatePem) privatePem = fs.readFileSync(path.join(process.env.APPDATA, 'zt1-lab-release', 'update-key.pem'), 'utf8');
const hash = crypto.createHash('sha512');
(async () => {
  for await (const chunk of fs.createReadStream(installer)) hash.update(chunk);
  const size = fs.statSync(installer).size;
  const fields = ['zt1-lab', version, filename, String(size), hash.digest('hex')];
  const signature = crypto.sign(null, Buffer.from(fields.join('\n')), privatePem).toString('base64');
  const manifest = {app:fields[0], version, file:filename, size, sha512:fields[4], signature};
  fs.writeFileSync(outputPath, JSON.stringify(manifest, null, 2) + '\n');
  console.log(`[OK] Signed ${filename}: ${size} bytes, SHA-512 ${fields[4]}`);
})().catch((err) => { console.error(err.message); process.exitCode = 1; });
