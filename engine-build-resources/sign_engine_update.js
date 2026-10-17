'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const PUBLIC_KEY_HEX = '09bdfbf256b798cd631fa24074f8e0d4f5514c68cebca1c352e29af5b9d6d46fb837e9e3f3112b998bf6cdbcfebf7e0297c8a7abf6ed7e74c6b42d243363e353';

const [version, installerPath, feedPath] = process.argv.slice(2);
if (!/^\d+\.\d+\.\d+$/.test(version || '')) throw new Error('Engine version must be three numeric components');
const installer = path.resolve(installerPath || '');
const feed = path.resolve(feedPath || '');
const filename = path.basename(installer);
if (filename !== `ZT1-Engine-Setup-${version}.exe` || !fs.statSync(installer).isFile())
  throw new Error('Installer name/version mismatch or installer missing');
if (!fs.statSync(feed).isFile()) throw new Error('latest.yml is missing');

let privatePem = process.env.ZT1_ENGINE_UPDATE_KEY?.replace(/\\n/g, '\n');
if (!privatePem) privatePem = fs.readFileSync(path.join(process.env.APPDATA, 'zt1-engine-release', 'update-key.pem'), 'utf8');
const publicJwk = crypto.createPublicKey(privatePem).export({format:'jwk'});
const actualPublicKeyHex = Buffer.concat([
  Buffer.from(publicJwk.x, 'base64url'), Buffer.from(publicJwk.y, 'base64url')
]).toString('hex');
if (publicJwk.crv !== 'P-256' || actualPublicKeyHex !== PUBLIC_KEY_HEX)
  throw new Error('The engine update private key does not match the public key in src/Updater.cpp');

const data = fs.readFileSync(installer);
const size = String(data.length);
const sha512 = crypto.createHash('sha512').update(data).digest('base64');
const fields = ['zt1-engine', version, filename, size, sha512];
const signature = crypto.sign('sha256', Buffer.from(fields.join('\n')), {
  key: privatePem,
  dsaEncoding: 'ieee-p1363'
}).toString('base64');

let yml = fs.readFileSync(feed, 'utf8').replace(/\r\n/g, '\n');
yml = yml.replace(/^zt1(?:App|Size|Signature):.*\n/gm, '').replace(/\n*$/, '\n');
yml += `zt1App: 'zt1-engine'\nzt1Size: '${size}'\nzt1Signature: '${signature}'\n`;
fs.writeFileSync(feed, yml, 'utf8');
console.log(`[OK] Signed ${filename}: ${size} bytes, SHA-512 ${sha512}`);
