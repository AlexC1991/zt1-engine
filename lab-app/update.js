'use strict';

const https = require('https');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const MANIFEST_URL = 'https://github.com/AlexC1991/zt1-engine/releases/latest/download/ZT1-Lab-update.json';
const PUBLIC_KEY = `-----BEGIN PUBLIC KEY-----\nMCowBQYDK2VwAyEABGQRg9tkIg53GGoBPFrdqdi1DV5/6sAa3Np2bYdA+hE=\n-----END PUBLIC KEY-----\n`;
const APP_ID = 'zt1-lab';

function compareVersions(a, b) {
  const aa = a.split('.').map(Number), bb = b.split('.').map(Number);
  for (let i = 0; i < 3; i++) if (aa[i] !== bb[i]) return aa[i] - bb[i];
  return 0;
}

function request(url, onResponse, redirects = 0) {
  return new Promise((resolve, reject) => {
    if (redirects > 5) return reject(new Error('Too many redirects'));
    const req = https.get(url, {headers:{'User-Agent':'ZT1-Lab-Updater/1'}}, (res) => {
      if ([301,302,303,307,308].includes(res.statusCode) && res.headers.location) {
        res.resume();
        return resolve(request(new URL(res.headers.location, url).toString(), onResponse, redirects + 1));
      }
      if (res.statusCode !== 200) {
        res.resume();
        return reject(new Error(`Update server returned HTTP ${res.statusCode}`));
      }
      Promise.resolve(onResponse(res)).then(resolve, reject);
    });
    req.setTimeout(30000, () => req.destroy(new Error('Update request timed out')));
    req.on('error', reject);
  });
}

function readManifest() {
  return request(MANIFEST_URL, (res) => new Promise((resolve, reject) => {
    const chunks = []; let size = 0;
    res.on('data', (b) => { size += b.length; if (size > 64 * 1024) res.destroy(new Error('Update manifest is too large')); else chunks.push(b); });
    res.on('end', () => { try { resolve(JSON.parse(Buffer.concat(chunks).toString('utf8'))); } catch { reject(new Error('Update manifest is invalid JSON')); } });
    res.on('error', reject);
  }));
}

function validate(m) {
  if (!m || m.app !== APP_ID || !/^\d+\.\d+\.\d+$/.test(m.version) ||
      m.file !== `ZT1-Lab-Setup-${m.version}.exe` || !Number.isSafeInteger(m.size) || m.size < 1024 * 1024 || m.size > 2 * 1024 * 1024 * 1024 ||
      !/^[a-f0-9]{128}$/.test(m.sha512) || typeof m.signature !== 'string') throw new Error('Update manifest fields are invalid');
  const message = [m.app, m.version, m.file, String(m.size), m.sha512].join('\n');
  let sig;
  try { sig = Buffer.from(m.signature, 'base64'); } catch { throw new Error('Update signature is malformed'); }
  if (sig.length !== 64 || !crypto.verify(null, Buffer.from(message), PUBLIC_KEY, sig)) throw new Error('Update signature is not trusted');
  return m;
}

function download(m, destination) {
  const url = `https://github.com/AlexC1991/zt1-engine/releases/latest/download/${encodeURIComponent(m.file)}`;
  return request(url, (res) => new Promise((resolve, reject) => {
    const hash = crypto.createHash('sha512'); let bytes = 0;
    const out = fs.createWriteStream(destination, {flags:'wx'});
    const fail = (err) => { out.destroy(); fs.rm(destination, {force:true}, () => reject(err)); };
    res.on('data', (chunk) => { bytes += chunk.length; if (bytes > m.size) return fail(new Error('Downloaded installer is larger than its signed size')); hash.update(chunk); });
    res.on('error', fail); out.on('error', fail);
    out.on('finish', () => {
      if (bytes !== m.size || hash.digest('hex') !== m.sha512) return fail(new Error('Downloaded installer did not match its signed size and SHA-512'));
      resolve();
    });
    res.pipe(out);
  }));
}

module.exports = { compareVersions, readManifest, validate, download };
