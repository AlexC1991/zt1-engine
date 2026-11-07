"""Resumable local HD-art job, launched and supervised by a Lab script.

Colour frames and versioned emission layers remain separate. Nothing is
published or enabled. A completed chunk is verified before its scratch is freed.
"""
from pathlib import Path
import argparse,ctypes,hashlib,json,os,shutil,struct,subprocess,sys,time


def atomic_json(path,value):
    temp=path.with_suffix(path.suffix+'.tmp')
    temp.write_text(json.dumps(value,indent=2),encoding='utf-8')
    # Windows readers can briefly hold the destination without delete sharing.
    # Keep the previous checkpoint intact and retry the atomic replacement.
    for attempt in range(60):
        try:
            temp.replace(path)
            return
        except PermissionError:
            if attempt == 59:
                raise
            time.sleep(.05)


def announce(root,message):
    # The Lab may close or detach its console while the owned batch is still
    # running. Keep progress on disk; a broken output pipe must not fail work.
    with (root/'worker.log').open('a',encoding='utf-8') as log:
        log.write(message+'\n')
    try:
        print(message,flush=True)
    except OSError:
        pass


def dimensions(path):
    with path.open('rb') as f:
        header=f.read(24)
    if header[:8]!=b'\x89PNG\r\n\x1a\n' or len(header)!=24:
        raise ValueError(f'Invalid PNG: {path}')
    return struct.unpack('>II',header[16:24])


def keep_children_with_job():
    if os.name!='nt': return None
    from ctypes import wintypes as w
    class BASIC(ctypes.Structure):
        _fields_=[('process_time',ctypes.c_longlong),('job_time',ctypes.c_longlong),('flags',w.DWORD),('minimum',ctypes.c_size_t),('maximum',ctypes.c_size_t),('active',w.DWORD),('affinity',ctypes.c_size_t),('priority',w.DWORD),('scheduling',w.DWORD)]
    class IO(ctypes.Structure):
        _fields_=[(n,ctypes.c_ulonglong) for n in ['read_ops','write_ops','other_ops','read_bytes','write_bytes','other_bytes']]
    class EXTENDED(ctypes.Structure):
        _fields_=[('basic',BASIC),('io',IO),('process_limit',ctypes.c_size_t),('job_limit',ctypes.c_size_t),('peak_process',ctypes.c_size_t),('peak_job',ctypes.c_size_t)]
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    k.CreateJobObjectW.argtypes=[ctypes.c_void_p,w.LPCWSTR];k.CreateJobObjectW.restype=w.HANDLE
    k.SetInformationJobObject.argtypes=[w.HANDLE,ctypes.c_int,ctypes.c_void_p,w.DWORD]
    k.AssignProcessToJobObject.argtypes=[w.HANDLE,w.HANDLE];k.GetCurrentProcess.restype=w.HANDLE
    job=k.CreateJobObjectW(None,None);limits=EXTENDED();limits.basic.flags=0x2000
    if not job or not k.SetInformationJobObject(job,9,ctypes.byref(limits),ctypes.sizeof(limits)) or not k.AssignProcessToJobObject(job,k.GetCurrentProcess()):
        raise ctypes.WinError(ctypes.get_last_error())
    return job


def bake_layers(frames,up,hd):
    manifest=frames/'material-seeds.tsv'
    if not manifest.exists():return 0
    # Pillow is already provided with the owner's local image tooling.
    sys.path.insert(0,'E:/zt1-shots/python-libs')
    from PIL import Image,ImageChops
    count=0
    for line in dict.fromkeys(manifest.read_text().splitlines()):
        seed,relative,flat=line.split('\t')
        with Image.open(frames/'material-seeds'/seed) as im:mask=im.convert('RGBA')
        with Image.open(up/flat) as im:colour=im.convert('RGBA')
        with Image.open(frames/flat) as im:original=im.convert('RGB')
        big=mask.resize(colour.size,Image.Resampling.NEAREST)
        # Keep the reviewed material footprint and tint; borrow only fine HD
        # texture differences. Sunshine and neighbouring light are never baked.
        detail=ImageChops.subtract(colour.convert('L'),original.resize(colour.size,Image.Resampling.BILINEAR).convert('L'),offset=128)
        rgb=ImageChops.add(big.convert('RGB'),detail.convert('RGB'),offset=-128)
        alpha=ImageChops.darker(big.getchannel('A'),colour.getchannel('A'))
        rgb.putalpha(alpha)
        dest=hd/relative;dest.parent.mkdir(parents=True,exist_ok=True)
        temp=dest.with_suffix('.tmp');rgb.save(temp,format='PNG');temp.replace(dest);count+=1
    return count


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--engine',required=True);ap.add_argument('--cwd',required=True);ap.add_argument('--upscaler',required=True);ap.add_argument('--work',required=True);ap.add_argument('--output',required=True);ap.add_argument('--prefix',action='append');ap.add_argument('--limit',type=int,default=2048);ap.add_argument('--max-chunks',type=int,default=0);ap.add_argument('--materials-only',action='store_true')
    args=ap.parse_args();job=keep_children_with_job()
    root=Path(args.work).resolve();root.mkdir(parents=True,exist_ok=True)
    output=Path(args.output).resolve();hd=output/'hd';hd.mkdir(parents=True,exist_ok=True)
    lock=root/'running.lock'
    try:lock_handle=lock.open('x')
    except FileExistsError:raise RuntimeError(f'Batch lock exists: {lock}. Check its PID before removing a stale lock.')
    lock_handle.write(str(os.getpid()));lock_handle.flush()
    state_file=root/'status.json';state=json.loads(state_file.read_text()) if state_file.exists() else {'done_prefixes':[],'frames_done':0,'layers_done':0,'chunks_done':0}
    state.pop('error',None);state.pop('reason',None)
    state.update(pid=os.getpid(),started=time.time(),engine_sha256=hashlib.sha256(Path(args.engine).read_bytes()).hexdigest(),output=str(output),state='starting')
    env=dict(os.environ,SDL_AUDIODRIVER='dummy',ZT_HD_EXPORT_LIMIT=str(args.limit),ZT_HD_WORKERS='4',ZT1_MODS_CONFIG=str(root/'mods.cfg'),ZT1_CONFIG_PATH=str(root/'zoo.ini'))
    (root/'mods.cfg').write_text('folder='+str(root/'empty-mods')+'\nloadMods=0\n')
    if not (root/'zoo.ini').exists():
        (root/'zoo.ini').write_text('[resource]\npath=./dlupdate;./updates;./xpack2/loc;./xpack2;./dupdate;./zupdate1;./xpack1/loc;./xpack1;./zupdate;./loc;.\n[ui]\nnoMenuMusic=1\n[user]\nfullscreen=0\nscreenwidth=800\nscreenheight=600\n[lib]\nres=res0.dll\nlang=lang0.dll\n')
    (root/'progress').mkdir(exist_ok=True)
    def status(**values):state.update(values);atomic_json(state_file,state)
    def run(command,label):
        status(stage=label)
        with (root/(label+'.log')).open('ab') as log:
            child=subprocess.Popen(command,cwd=args.cwd,env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=log,creationflags=0x08000000 if os.name=='nt' else 0)
            last=0
            try:
                while child.poll() is None:
                    if time.monotonic()-last>10:
                        current=len(list((root/'chunk'/'up').glob('*.png'))) if label=='upscale' else 0
                        status(child_pid=child.pid,chunk_upscaled=current,heartbeat=time.time());last=time.monotonic()
                    time.sleep(1)
                if child.returncode:raise RuntimeError(f'{label} exited {child.returncode}; see {root/(label+".log")}')
            finally:
                if child.poll() is None:child.terminate();child.wait()
    try:
        index=root/'animations.txt'
        if not index.exists():run([args.engine,'--hd-index',str(index)],'inventory')
        indexed_resources=index.read_text().splitlines()
        if not args.prefix and not all(any(k.startswith(prefix) for k in indexed_resources) for prefix in ['animals/','objects/','staff/','guests/','fences/']):raise RuntimeError('Native inventory is incomplete; check zoo.ini resource paths.')
        if args.prefix:
            prefixes=args.prefix
            indexed=index.read_text().splitlines()
            for prefix in prefixes:
                if not any(k.startswith(prefix) for k in indexed):raise ValueError(f'No animation resource matches {prefix}')
        else:prefixes=sorted({'/'.join(k.split('/')[:2])+'/' for k in index.read_text().splitlines() if '/' in k})
        # Put lighting fixtures first so the owner can review the paired maps early.
        priority=['objects/streetlt/','objects/lightho/','objects/restrant/','objects/carousal/','objects/helibase/']
        prefixes=sorted(prefixes,key=lambda p:(priority.index(p) if p in priority else len(priority),p))
        status(prefixes_total=len(prefixes))
        meta={'id':'zt1-hd-lighting-preview','name':'ZT1 HD Art and Lighting Preview','author':'AlexC1991','version':'0.1.0','about':'Local 4x map-art preview with separate, versioned emission maps. Generation is in progress. Not enabled automatically.','replaces':[]}
        if not (output/'mod.json').exists():atomic_json(output/'mod.json',meta)
        atomic_json(output/'lighting.json',{'format':1,'scale':4,'maps':'hd/_lighting/<profile fingerprint>/<profile>/<view>/<native animation>/<direction>_<frame>.png','colour':'hd/<native animation>/<direction>_<frame>.png','sunlight':'runtime','cast_shadows':'runtime native geometry','fluorescence':'runtime excitation gate','status':'building'})
        for prefix in prefixes:
            if prefix in state['done_prefixes']:continue
            prefix_id=hashlib.sha256(prefix.encode()).hexdigest()[:16];skip=root/('done-'+prefix_id+'.txt');env['ZT_HD_SKIP_LIST']=str(skip)
            while True:
                if (root/'STOP').exists():status(state='paused',reason='STOP file');announce(root,'Paused by STOP file.');return
                if shutil.disk_usage(root).free<12*1024**3 or shutil.disk_usage(Path.home()).free<4*1024**3:raise RuntimeError('Paused: less than 12 GiB scratch or 4 GiB system disk free.')
                chunk=root/'chunk';frames=chunk/'frames';up=chunk/'up'
                frames.mkdir(parents=True,exist_ok=True);up.mkdir(exist_ok=True)
                status(state='running',prefix=prefix)
                run([args.engine,'--hd-export',str(frames),prefix],'export')
                originals=sorted(frames.glob('*.png'))
                if not originals:
                    state['done_prefixes'].append(prefix);status();break
                announce(root,f'HD: {prefix}: {len(originals)} frames; {state["frames_done"]} completed, {len(state["done_prefixes"])}/{len(prefixes)} groups.')
                if not args.materials_only:
                    executable=Path(args.upscaler)
                    model_input=chunk/'model-input';model_input.mkdir(exist_ok=True)
                    for f in originals:
                        linked=model_input/f.name
                        if not linked.exists():os.link(f,linked)
                    run([str(executable),'-i',str(model_input),'-o',str(up),'-n','realesrgan-x4plus','-s','4','-f','png','-m',str(executable.parent/'models'),'-j','1:2:2'],'upscale')
                    for f in originals:
                        w,h=dimensions(f)
                        if dimensions(up/f.name)!=(w*4,h*4):raise RuntimeError(f'Wrong upscale dimensions: {f.name}')
                    run([args.engine,'--hd-finish',str(frames),str(up),str(hd)],'finish')
                for f in originals:
                    destination=hd/f.name.replace('~','/');w,h=dimensions(f)
                    if dimensions(destination)!=(w*4,h*4):raise RuntimeError(f'Incomplete finalized frame: {destination}')
                # Bake against finalized alpha without sending masks through the model.
                for f in originals:shutil.copy2(hd/f.name.replace('~','/'),up/f.name)
                maps=bake_layers(frames,up,hd)
                with skip.open('a') as f:f.writelines(p.name+'\n' for p in originals)
                state['frames_done']+=len(originals);state['layers_done']+=maps;state['chunks_done']+=1
                (root/'progress'/f'{state["chunks_done"]:07d}.done').write_text(prefix)
                status()
                if chunk.resolve().parent!=root:raise RuntimeError('Scratch path is outside this batch.')
                shutil.rmtree(chunk)
                announce(root,f'HD verified: {state["frames_done"]} colour frames, {state["layers_done"]} material maps, {state["chunks_done"]} batches.')
                if args.max_chunks and state['chunks_done']>=args.max_chunks:status(state='sample-complete');return
        status(state='complete',finished=time.time())
        # (a mod.json already written up for players is left as it is)
        current=json.loads((output/'mod.json').read_text()) if (output/'mod.json').exists() else {}
        if str(current.get('about','')).startswith('Local 4x'):meta['about']='Local 4x map-art preview with separate emission maps. Review in the Lab before publishing.';atomic_json(output/'mod.json',meta)
        lighting=json.loads((output/'lighting.json').read_text());lighting['status']='complete';atomic_json(output/'lighting.json',lighting)
        announce(root,'HD generation complete. The mod remains off until selected.')
    except Exception as error:
        status(state='failed',error=str(error));raise
    finally:lock_handle.close();lock.unlink(missing_ok=True)


if __name__=='__main__':main()
