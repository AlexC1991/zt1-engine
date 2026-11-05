const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const vm = require('node:vm');
const { Readable } = require('node:stream');
const updates = require('../update');
const view = require('../renderer/update-view');

function directory(t) {
  const base = process.env.ZT1_TEST_OUTPUT || (process.platform === 'win32' ? 'E:/zt1-shots/updater-checks' : os.tmpdir());
  fs.mkdirSync(base, {recursive:true});
  const root = fs.mkdtempSync(path.join(base, 'lab-update-'));
  t.after(() => fs.rmSync(root, {recursive:true,force:true}));
  return root;
}

function manifest(data) {
  return {app:'zt1-lab',version:'2026.11.3',file:'ZT1-Lab-Setup-2026.11.3.exe',size:data.length,
    sha512:crypto.createHash('sha512').update(data).digest('hex'),signature:Buffer.alloc(64).toString('base64'),base:'https://github.com/AlexC1991/zt1-engine/releases/latest/download/'};
}

test('unsigned or wrongly signed release manifests remain rejected', () => {
  const m = manifest(Buffer.alloc(1024*1024));
  assert.throws(() => updates.validate(m), /not trusted/);
  assert.throws(() => updates.validate({...m,file:'other.exe'}), /fields are invalid/);
  assert.throws(() => updates.validate({...m,size:Infinity}), /fields are invalid/);
});

test('download emits measured progress and verifies its exact size and digest without a network', async t => {
  const root = directory(t), data = Buffer.alloc(1024*1024, 7), m = manifest(data), progress = [];
  let verified = false;
  const transport = async (_url, read) => read(Readable.from([data.subarray(0,500000),data.subarray(500000)]));
  await updates.download(m,path.join(root,'installer.exe'),p => progress.push(p),() => { verified = true; },transport);
  assert.equal(verified,true);
  assert.equal(progress.at(-1).bytes,data.length);
  assert.equal(progress.at(-1).total,data.length);
  assert(progress.at(-1).bytesPerSecond > 0);
  assert.equal(progress.at(-1).etaSeconds,0);
  assert.deepEqual(fs.readFileSync(path.join(root,'installer.exe')),data);
});

test('truncated and mismatching downloads are removed and never become verified installers', async t => {
  const root = directory(t), data = Buffer.alloc(1024*1024, 3), m = manifest(data);
  for (const [name,body] of [['short',data.subarray(0,100)],['changed',Buffer.alloc(data.length,4)],['oversize',Buffer.alloc(data.length+1)]]) {
    const file=path.join(root,name+'.exe');
    await assert.rejects(updates.download(m,file,()=>{},()=>{},async (_url,read) => read(Readable.from([body]))),/signed size|larger than/);
    assert.equal(fs.existsSync(file),false);
  }
});

test('exclusive download failure does not remove an installer owned by another process', async t => {
  const root=directory(t),data=Buffer.alloc(1024*1024,9),m=manifest(data),file=path.join(root,'exists.exe');
  fs.writeFileSync(file,'keep this file');
  await assert.rejects(updates.download(m,file,()=>{},()=>{},async (_url,read)=>read(Readable.from([data]))),/EEXIST/);
  assert.equal(fs.readFileSync(file,'utf8'),'keep this file');
});

// Only this isolated controller fixture substitutes signature verification;
// exported production validate() above uses the real pinned public key.
function controllerFixture(t, overrides = {}) {
  const root = directory(t), source = fs.readFileSync(path.join(__dirname,'../update.js'),'utf8');
  const module = {exports:{}};
  const sandbox = {module,exports:module.exports,Buffer,URL,process:{...process,env:{...process.env,LOCALAPPDATA:root,ZT1_LAB_UPDATE_TEST:''}},setTimeout,clearTimeout,
    require:name => name==='crypto'?{...crypto,verify:()=>true}:require(name)};
  vm.runInNewContext(source,sandbox,{filename:'update.js'});
  const data=Buffer.alloc(1024*1024),m=manifest(data),states=[];
  let downloads=0, launches=0;
  const {launchHelper = async request=>{launches++;assert.equal(request.app,'lab');assert.equal(request.sha512,m.sha512);return {ok:true};}, ...serviceOverrides} = overrides;
  const ctl=module.exports.controller({app:{isPackaged:true,getVersion:()=> '2026.11.1',getPath:()=>root},send:s=>states.push(s),
    launchHelper,
    services:{readManifest:async()=>m,matches:()=>downloads>0,download:async (_m,file,progress,verify)=>{downloads++;progress({bytes:m.size,total:m.size,bytesPerSecond:200000,etaSeconds:0});verify();fs.writeFileSync(file,data);},...serviceOverrides}});
  t.after(()=>ctl.dispose());
  return {ctl,states,get downloads(){return downloads;},get launches(){return launches;}};
}

test('controller exposes verification/progress and cannot install just because the Lab closes', async t => {
  const f=controllerFixture(t);
  await f.ctl.check(true);
  assert.equal(f.ctl.state().state,'ready');
  assert(f.states.some(s=>s.state==='verifying'));
  assert.equal(f.ctl.state().bytes,1024*1024);
  assert.equal((await f.ctl.install(false)).ok,false);
  assert.equal(f.launches,0);
  assert.equal((await f.ctl.install(true)).ok,true);
  assert.equal(f.launches,1);
  assert.equal(f.ctl.state().state,'installing');
});

test('ready rechecks keep the valid installer instead of redownloading it', async t => {
  const f=controllerFixture(t);
  await f.ctl.check(true);await f.ctl.check(true);
  assert.equal(f.downloads,1);
  assert.equal(f.ctl.state().state,'ready');
});

test('helper launch failure leaves the Lab open with the ready download', async t => {
  const f=controllerFixture(t,{launchHelper:async()=>{throw new Error('Helper readiness timed out');}});
  await f.ctl.check(true);
  assert.equal((await f.ctl.install(true)).ok,false);
  assert.equal(f.ctl.state().state,'ready');
  assert.match(f.ctl.state().message,/Helper readiness timed out/);
});

test('the mini menu shows one download label and keeps metadata in Details', () => {
  const html=view.menu({state:'downloading',bytes:64000000,total:128000000,bytesPerSecond:4000000,etaSeconds:16,current:'2026.11.1',version:'2026.11.3',source:'https://example.test/releases/'});
  assert.equal((html.match(/Downloading/g)||[]).length,1);
  assert(html.includes('50%'));
  assert(html.indexOf('2026.11.3')>html.indexOf('<details'));
  assert(html.includes('~16 sec left'));
  assert(!html.includes('style="width:NaN'));
  assert(view.menu({state:'error',message:'<script>'}).includes('&lt;script&gt;'));
});

test('paused HD supervision still blocks a queued restart until its worker has finished', () => {
  assert.match(view.busyReason({engine:{state:{live:true,auto:false,script:'hd_art_batch'}}}),/HD generation/);
  assert.equal(view.busyReason({engine:{state:{live:false,script:'hd_art_batch'}}}),'');
  assert.match(view.busyReason({play:{running:true}}),/Play/);
  assert.match(view.busyReason({engine:{state:{build:'building'}}}),/build/);
  assert.match(view.busyReason({editor:{stem:'test_script',text:'edited',saved:'original'}}),/script changes/);
  assert.equal(view.busyReason({editor:{stem:'test_script',text:'saved',saved:'saved'}}),'');
});
