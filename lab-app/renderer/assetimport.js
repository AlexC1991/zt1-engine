// External art enters through the editor, and Save uses its normal compiler.
// OBJ files are poses, not rigs: multiple selected files become a pose sequence.
const AssetImport = (() => {
  const directions = ['N', 'NE', 'E', 'SE', 'S'];
  function integer(v, min, max, label) {
    const n = Number(v);
    if (!Number.isInteger(n) || n < min || n > max) throw Error(`${label}: choose ${min} to ${max}.`);
    return n;
  }
  function parseObj(text) {
    const vertices = [], uv = [], faces = [];
    let material = '';
    const index = (s, a) => {
      const n = Number(s);
      if (!Number.isInteger(n) || n === 0) throw Error('An OBJ face has an invalid index.');
      const i = n < 0 ? a.length + n : n - 1;
      if (i < 0 || i >= a.length) throw Error('An OBJ face points outside its vertices or texture coordinates.');
      return i;
    };
    for (const raw of text.split(/\r?\n/)) {
      const p = raw.split('#')[0].trim().split(/\s+/), kind = p.shift();
      if (kind === 'v') {
        const v = p.slice(0, 3).map(Number);
        if (v.length !== 3 || !v.every(Number.isFinite)) throw Error('An OBJ vertex is invalid.');
        vertices.push(v);
      } else if (kind === 'vt') {
        const t = p.slice(0, 2).map(Number);
        if (t.length !== 2 || !t.every(Number.isFinite)) throw Error('An OBJ texture coordinate is invalid.');
        uv.push(t);
      } else if (kind === 'usemtl') material = p.join(' ');
      else if (kind === 'f') {
        if (p.length < 3) throw Error('An OBJ face needs at least three corners.');
        const corners = p.map(s => { const a = s.split('/'); return { v: index(a[0], vertices), t: a[1] ? index(a[1], uv) : -1 }; });
        for (let i = 1; i + 1 < corners.length; i++) faces.push({ c: [corners[0], corners[i], corners[i + 1]], material });
      }
      if (vertices.length > 100000 || faces.length > 100000) throw Error('Use an OBJ with at most 100,000 vertices and triangles.');
    }
    if (!faces.length) throw Error('This OBJ has no polygon faces. Export a mesh with faces first.');
    return { vertices, uv, faces };
  }
  function parseMtl(text) {
    const materials = {}; let current;
    for (const raw of text.split(/\r?\n/)) {
      const p = raw.split('#')[0].trim().split(/\s+/), kind = p.shift();
      if (kind === 'newmtl') current = materials[p.join(' ')] = { color: [160, 180, 130] };
      else if (current && kind === 'Kd') {
        const rgb = p.slice(0, 3).map(Number);
        if (rgb.length === 3 && rgb.every(Number.isFinite)) current.color = rgb.map(n => Math.round(Math.max(0, Math.min(1, n)) * 255));
      } else if (current && kind === 'map_Kd') current.texture = p.join(' ').replace(/\\/g, '/').split('/').pop();
    }
    return materials;
  }
  function canvas(w, h) { const c = document.createElement('canvas'); c.width = w; c.height = h; return c; }
  async function picture(file) {
    const url = URL.createObjectURL(file), img = new Image();
    try { img.src = url; await img.decode(); } finally { URL.revokeObjectURL(url); }
    if (img.width * img.height > 32000000) throw Error('Use an image under 32 million pixels.');
    const c = canvas(img.width, img.height); c.getContext('2d').drawImage(img, 0, 0); return c;
  }
  function bounds(c) {
    const a = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
    let x0 = c.width, y0 = c.height, x1 = -1, y1 = -1;
    for (let y = 0; y < c.height; y++) for (let x = 0; x < c.width; x++) if (a[(y * c.width + x) * 4 + 3] >= 128) {
      x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y);
    }
    return x1 < 0 ? null : { x: x0, y: y0, w: x1 - x0 + 1, h: y1 - y0 + 1 };
  }
  function fit(c, box, ed, scale) {
    const out = canvas(ed.w, ed.h), g = out.getContext('2d'); g.imageSmoothingEnabled = false;
    if (box) {
      const w = Math.max(1, Math.round(box.w * scale)), h = Math.max(1, Math.round(box.h * scale));
      g.drawImage(c, box.x, box.y, box.w, box.h, Math.round(ed.ax - w / 2), ed.ay - h, w, h);
    }
    return g.getImageData(0, 0, ed.w, ed.h);
  }
  function renderObj(model, yaw, materials, textures, size = 160) {
    const a = yaw * Math.PI / 180, e = Math.PI / 6;
    const projected = model.vertices.map(([x, y, z]) => {
      const X = x * Math.cos(a) + z * Math.sin(a), Z = z * Math.cos(a) - x * Math.sin(a);
      return [X, y * Math.cos(e) - Z * Math.sin(e), Z * Math.cos(e) + y * Math.sin(e)];
    });
    const xs = projected.map(p => p[0]), ys = projected.map(p => p[1]);
    const minX = Math.min(...xs), maxX = Math.max(...xs), minY = Math.min(...ys), maxY = Math.max(...ys);
    const scale = (size - 8) / Math.max(maxX - minX, maxY - minY, 0.0001);
    const pts = projected.map(p => [(p[0] - (minX + maxX) / 2) * scale + size / 2, size - 4 - (p[1] - minY) * scale, p[2]]);
    const c = canvas(size, size), g = c.getContext('2d'), im = g.createImageData(size, size), zbuf = new Float64Array(size * size).fill(-Infinity);
    const edge = (a, b, x, y) => (x - a[0]) * (b[1] - a[1]) - (y - a[1]) * (b[0] - a[0]);
    for (const face of model.faces) {
      const p = face.c.map(q => pts[q.v]), area = edge(p[0], p[1], p[2][0], p[2][1]); if (Math.abs(area) < 0.000001) continue;
      const mat = materials[face.material] || { color: [153, 175, 128] }, tex = textures[mat.texture];
      const v = face.c.map(q => model.vertices[q.v]), u = v[1].map((n, i) => n - v[0][i]), w = v[2].map((n, i) => n - v[0][i]);
      const normal = [u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]];
      const len = Math.hypot(...normal) || 1, light = 0.48 + 0.52 * Math.abs((normal[0] * -0.4 + normal[1] * 0.8 + normal[2] * 0.45) / len);
      const x0 = Math.max(0, Math.floor(Math.min(...p.map(q => q[0])))), x1 = Math.min(size - 1, Math.ceil(Math.max(...p.map(q => q[0]))));
      const y0 = Math.max(0, Math.floor(Math.min(...p.map(q => q[1])))), y1 = Math.min(size - 1, Math.ceil(Math.max(...p.map(q => q[1]))));
      for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) {
        const b0 = edge(p[1], p[2], x + 0.5, y + 0.5) / area, b1 = edge(p[2], p[0], x + 0.5, y + 0.5) / area, b2 = 1 - b0 - b1;
        if (Math.min(b0, b1, b2) < -0.000001) continue;
        const z = b0 * p[0][2] + b1 * p[1][2] + b2 * p[2][2], pixel = y * size + x; if (z <= zbuf[pixel]) continue;
        let rgb = mat.color;
        if (tex && face.c.every(q => q.t >= 0)) {
          const t = face.c.map(q => model.uv[q.t]), U = b0 * t[0][0] + b1 * t[1][0] + b2 * t[2][0], V = b0 * t[0][1] + b1 * t[1][1] + b2 * t[2][1];
          const tx = Math.min(tex.width - 1, Math.floor(((U % 1 + 1) % 1) * tex.width)), ty = Math.min(tex.height - 1, Math.floor((1 - ((V % 1 + 1) % 1)) * tex.height));
          const offset = (ty * tex.width + tx) * 4; if (tex.data[offset + 3] < 128) continue; rgb = tex.data.slice(offset, offset + 3);
        }
        zbuf[pixel] = z; const offset = pixel * 4;
        for (let k = 0; k < 3; k++) im.data[offset + k] = Math.min(255, Math.round(rgb[k] * light)); im.data[offset + 3] = 255;
      }
    }
    g.putImageData(im, 0, 0); return c;
  }
  async function importFiles(files) {
    const directions = AN.item ? [...AN_DIRS] : ['N', 'NE', 'E', 'SE', 'S'];
    const ed = AN.ed; if (!ed) return;
    if (!files.length) return;
    if (files.reduce((sum, f) => sum + f.size, 0) > 100 * 1024 * 1024) throw Error('Choose source files totalling under 100 MB.');
    const objects = files.filter(f => /\.obj$/i.test(f.name)).sort((a, b) => a.name.localeCompare(b.name, undefined, { numeric: true }));
    let result = {}, height, ms = ed.ms;
    if (objects.length) {
      if (ed.pic) throw Error('Import a PNG for this picture; import OBJ models into an animal animation.');
      if (objects.length > 64) throw Error('Select at most 64 OBJ poses.');
      const a = await ask('Convert OBJ to game frames', [
        { key: 'height', label: 'Height in game pixels', value: '48' },
        { key: 'front', label: 'Model front (Y is up)', value: '0', options: [{ value: '0', label: '+Z' }, { value: '90', label: '+X' }, { value: '180', label: '-Z' }, { value: '270', label: '-X' }] },
        { key: 'ms', label: 'Milliseconds per pose (multiple OBJ files, sorted by name; a single mesh stays still)', value: String(ed.ms) },
      ], 'Convert and preview');
      if (!a) return; height = integer(a.height, 4, Math.min(ed.h, ed.ay), 'Height'); ms = integer(a.ms, 20, 2000, 'Frame time');
      const materials = {}, textures = {};
      for (const f of files.filter(f => /\.mtl$/i.test(f.name))) Object.assign(materials, parseMtl(await f.text()));
      for (const f of files.filter(f => /\.(png|jpg|jpeg|webp|bmp)$/i.test(f.name))) {
        const c = await picture(f); textures[f.name] = c.getContext('2d').getImageData(0, 0, c.width, c.height);
      }
      const missing = Object.values(materials).filter(m => m.texture && !textures[m.texture]);
      if (missing.length) throw Error('Select the texture images named in the MTL along with the OBJ and MTL files.');
      for (const f of objects) {
        const text = await f.text(), model = parseObj(text);
        if (/^\s*mtllib\s/m.test(text) && !files.some(q => /\.mtl$/i.test(q.name))) throw Error('Select the model’s MTL file along with the OBJ to preserve its colours.');
        for (let i = 0; i < directions.length; i++) (result[directions[i]] ||= []).push(renderObj(model, 180 - i * 45 - Number(a.front), materials, textures));
      }
    } else {
      const images = files.filter(f => /\.(png|gif|webp|bmp|jpg|jpeg)$/i.test(f.name));
      if (images.length !== 1) throw Error('Select one picture or sprite sheet, or select OBJ pose files with their MTL and textures.');
      const img = await picture(images[0]);
      const a = await ask('Convert image to game frames', [
        { key: 'layout', label: 'Source layout', value: 'single', options: [{ value: 'single', label: 'One picture' }, { value: 'strip', label: 'Horizontal frame strip' }, ...(!ed.pic ? [{ value: 'grid', label: 'Grid: frames across, directions down' }] : [])] },
        { key: 'columns', label: 'Columns (strip or grid)', value: '4' },
        { key: 'rows', label: 'Rows (grid)', value: String(directions.length) },
        { key: 'directions', label: 'Grid rows, in order', value: directions.join(',') },
        { key: 'repeat', label: 'For one picture or strip', value: 'selected', options: [{ value: 'selected', label: 'Only this direction' }, ...(!ed.pic ? [{ value: 'all', label: 'Repeat for every direction (same view; no new angles)' }] : [])] },
        { key: 'height', label: `Fit visible art to height (pixels; canvas ${ed.w} × ${ed.h})`, value: String(ed.pic ? Math.max(4, ed.h - 4) : 48) },
        { key: 'ms', label: 'Milliseconds per frame', value: String(ed.ms) },
        { key: 'align', label: 'Placement', value: ['ground', 'path'].includes(AN.item?.kind) ? 'canvas' : 'feet', options: [{value:'feet',label:'Fit art with its feet on the ground point'}, {value:'canvas',label:'Keep the whole cell on the canvas (tiles and textures)'}] },
      ], 'Convert and preview');
      if (!a) return;
      height = a.align === 'canvas' ? ed.h : integer(a.height, 4, ed.pic ? ed.h : Math.min(ed.h, ed.ay), 'Height'); ms = integer(a.ms, 20, 2000, 'Frame time');
      result.canvasAligned = a.align === 'canvas';
      const cols = a.layout === 'single' ? 1 : integer(a.columns, 1, 64, 'Columns'), rows = a.layout === 'grid' ? integer(a.rows, 1, directions.length, 'Rows') : 1;
      if (cols * rows > 160) throw Error('Use at most 160 cells per sheet.');
      let dirs = a.layout === 'grid' ? a.directions.split(',').map(s => s.trim().toUpperCase()) : [ed.dir];
      if (dirs.length !== rows || dirs.some(d => !directions.includes(d)) || new Set(dirs).size !== dirs.length) throw Error('Give one unique direction per grid row, using ' + directions.join(', ') + '.');
      for (let y = 0; y < rows; y++) for (let x = 0; x < cols; x++) {
        const left = Math.round(x * img.width / cols), top = Math.round(y * img.height / rows), right = Math.round((x + 1) * img.width / cols), bottom = Math.round((y + 1) * img.height / rows);
        if (right <= left || bottom <= top) throw Error('The sheet has more cells than pixels.');
        const c = canvas(right - left, bottom - top); c.getContext('2d').drawImage(img, left, top, c.width, c.height, 0, 0, c.width, c.height);
        (result[dirs[y]] ||= []).push(c);
      }
      if (a.repeat === 'all' && a.layout !== 'grid' && !ed.pic) result = { ...Object.fromEntries(directions.map(d => [d, result[ed.dir]])), canvasAligned: a.align === 'canvas' };
    }
    // A shared scale keeps the pose sequence and directional views consistent.
    const canvasAligned = result.canvasAligned; delete result.canvasAligned;
    const boxes = new Map(); let tallest = 0, widest = 0;
    for (const frames of Object.values(result)) for (const c of frames) {
      const b = bounds(c); boxes.set(c, b); if (b) { tallest = Math.max(tallest, b.h); widest = Math.max(widest, b.w); }
    }
    if (!tallest) throw Error('The imported art is empty or fully transparent.');
    const scale = Math.min(height / tallest, Math.max(1, 2 * Math.min(ed.ax, ed.w - ed.ax) - 2) / widest);
    edPushAll();
    for (const [d, frames] of Object.entries(result)) ed.dirs[d] = frames.map((c, fi) => ({ cells: ed.layers.map((_, i) => {
      if (i !== ed.li) return ed.dirs[d][fi]?.cells[i] || null;
      if (!canvasAligned) return fit(c, boxes.get(c), { ...ed, ay: ed.pic ? Math.round((ed.h + tallest * scale) / 2) : ed.ay }, scale);
      const out = canvas(ed.w, ed.h), g = out.getContext('2d'); g.imageSmoothingEnabled = false; g.drawImage(c, 0, 0, ed.w, ed.h);
      return g.getImageData(0, 0, ed.w, ed.h);
    }) }));
    ed.ms = ms; ed.fi = 0; ed.dirty = true; ed.showRef = false;
    anRender(); toast(`Converted ${Object.values(result).reduce((n, a) => n + a.length, 0)} frames in ${Object.keys(result).length} directions. Preview them, then Save.`, 6500);
  }
  function choose() {
    const inp = document.createElement('input'); inp.type = 'file'; inp.multiple = true; inp.hidden = true;
    inp.accept = '.png,.gif,.webp,.bmp,.jpg,.jpeg,.obj,.mtl';
    inp.onchange = async () => { try { await importFiles([...inp.files]); } catch (e) { toast(esc(e.message || String(e)), 8000); } finally { inp.remove(); } };
    inp.oncancel = () => inp.remove();
    document.body.appendChild(inp);
    inp.click();
  }
  async function pictureProject(png, w, h) {
    const img = new Image(); img.src = 'data:image/png;base64,' + png; await img.decode();
    const source = canvas(img.width, img.height); source.getContext('2d').drawImage(img, 0, 0);
    const b = bounds(source); if (!b) throw Error('This animation frame is empty.');
    const scale = Math.min((w - 4) / b.w, (h - 4) / b.h), target = canvas(w, h), g = target.getContext('2d');
    g.imageSmoothingEnabled = false;
    const W = Math.max(1, Math.round(b.w * scale)), H = Math.max(1, Math.round(b.h * scale));
    g.drawImage(source, b.x, b.y, b.w, b.h, Math.round((w - W) / 2), Math.round((h - H) / 2), W, H);
    return { version: 1, w, h, ax: w >> 1, ay: h >> 1, ms: 100, layers: [{ name: 'Picture', visible: true }], dirs: { N: [{ cells: [target.toDataURL('image/png').split(',')[1]] }] } };
  }
  return { choose, parseObj, parseMtl, integer, pictureProject };
})();
