const test = require('node:test');
const assert = require('node:assert/strict');
const {EventEmitter} = require('node:events');
const path = require('node:path');
const helper = require('../update-helper-launch');

function setup() {
  const root = path.resolve('E:/zt1-shots/updater-checks/pending');
  const request = {app:'lab',parent:process.pid,pendingDir:root,installer:path.join(root,'setup.exe'),
    launch:path.resolve('E:/zt1-shots/updater-checks/ZT1 Lab.exe'),size:1048576,sha512:'a'.repeat(128),version:'2099.1.1'};
  const files = new Map();
  const calls = [];
  const child = new EventEmitter(); child.kill = () => calls.push('kill'); child.unref = () => calls.push('unref');
  const io = {existsSync:()=>true,mkdirSync(){},copyFileSync(...a){calls.push(a);},
    readFileSync:p=>{if(!files.has(p))throw Error('missing');return files.get(p);},rmSync:p=>files.delete(p),
    realpathSync:p=>path.resolve(p),writeFileSync:(p,s)=>files.set(p,s),renameSync:(a,b)=>{files.set(b,files.get(a));files.delete(a);}};
  const callbacks = {};
  const timers = {setInterval(fn){callbacks.poll=fn;return 1;},clearInterval(){},setTimeout(fn){callbacks.timeout=fn;return 2;},clearTimeout(){}};
  const spawn = (exe,args,opts) => { calls.push({exe,args,opts});return child; };
  return {root,request,files,calls,child,io,callbacks,timers,spawn};
}

test('Lab quits only after helper readiness, with direct arguments and no shell', async () => {
  const x=setup(), promise=helper.launch(x.request,{candidates:[path.join(x.root,'source.exe')],spawn:x.spawn,io:x.io,timers:x.timers});
  const call=x.calls.find(c=>c.args);
  assert.equal(call.opts.windowsHide,true);assert.equal(call.opts.shell,undefined);
  assert.equal(call.args[call.args.indexOf('--installer')+1],x.request.installer);
  const ready=call.args[call.args.indexOf('--ready-file')+1];
  x.files.set(ready,'ready');x.callbacks.poll();
  assert.deepEqual(await promise,{ok:true});assert(x.calls.includes('unref'));assert(!x.calls.includes('kill'));
});
test('failed or timed-out helpers are stopped so an ordinary later close cannot install', async () => {
  for(const failure of ['timeout','exit','error']){
    const x=setup(), promise=helper.launch(x.request,{candidates:['helper.exe'],spawn:x.spawn,io:x.io,timers:x.timers});
    if(failure==='timeout')x.callbacks.timeout();
    else if(failure==='exit')x.child.emit('exit',1);
    else x.child.emit('error',new Error('blocked'));
    assert.equal((await promise).ok,false);assert(x.calls.includes('kill'));
  }
});
test('invalid handoff parameters never spawn', async () => {
  const x=setup();
  const result=await helper.launch({...x.request,sha512:'wrong'},{candidates:['helper.exe'],spawn:x.spawn,io:x.io,timers:x.timers});
  assert.equal(result.ok,false);assert.equal(x.calls.length,0);
});
test('readiness acknowledgement is confined to the nonce-named file in pending', () => {
  const x=setup(),token='b'.repeat(32),file=path.join(x.root,`launch-${token}.ack`);
  assert(helper.acknowledge(['--update-ack',file,'--update-token',token],'2099.1.1',x.root,x.io));
  assert.equal(x.files.get(file),token+'\n2099.1.1\n');
  assert.equal(helper.acknowledge(['--update-ack',path.join(x.root,'other.ack'),'--update-token',token],'2099.1.1',x.root,x.io),false);
  assert.equal(helper.acknowledge(['--update-ack',path.join(x.root,'..',`launch-${token}.ack`),'--update-token',token],'2099.1.1',x.root,x.io),false);
});
