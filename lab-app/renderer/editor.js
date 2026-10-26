// The Lab's code editor: Monaco (VS Code's editor) with clangd (the C++
// language server) behind it: highlighting, find and replace, go to line,
// multi-cursor, folding, and on the engine's own code autocomplete, hover,
// go to definition, references and errors as you type. Build errors show as
// squiggles too.
const CodeEditor = (() => {
  let monacoReady = null;
  let editor = null;
  let host = null; // (the editor's element, moved into whichever view shows it)
  let root = ''; // the repo, absolute
  const files = new Map(); // rel -> { model, saved (version id), crlf, readOnly }
  const hooks = { open: null, save: null, change: null, calls: null, menu: null, zoo: null };

  // ------------------------------------------------------------ paths
  const toAbs = (rel) => (root + '/' + rel).replace(/\\/g, '/');
  const uriOf = (rel) => 'file:///' + encodeURI(toAbs(rel)).replace(/#/g, '%23');
  const norm = (u) => decodeURIComponent(String(u)).replace(/^file:\/\/\//i, '').replace(/\\/g, '/').toLowerCase();
  // a clangd location's file: the repo's (rel), or another (abs, read-only)
  function relOf(uri) {
    const n = norm(uri);
    const r = root.replace(/\\/g, '/').toLowerCase() + '/';
    if (n.startsWith(r)) return decodeURIComponent(String(uri)).replace(/^file:\/\/\//i, '').replace(/\\/g, '/').slice(r.length);
    return null;
  }

  // ------------------------------------------------------------ Monaco
  function loadMonaco() {
    if (monacoReady) return monacoReady;
    monacoReady = new Promise((resolve) => {
      window.MonacoEnvironment = {
        // (its helper worker; if it can't start, Monaco runs it in the page)
        getWorkerUrl: () =>
          URL.createObjectURL(
            new Blob(
              [`self.MonacoEnvironment={baseUrl:'${new URL('../node_modules/monaco-editor/min/', location.href).href}'};importScripts('${new URL('../node_modules/monaco-editor/min/vs/base/worker/workerMain.js', location.href).href}');`],
              { type: 'text/javascript' },
            ),
          ),
      };
      window.require.config({ paths: { vs: '../node_modules/monaco-editor/min/vs' } });
      window.require(['vs/editor/editor.main'], () => {
        const m = window.monaco;
        m.editor.defineTheme('zt1lab', {
          base: 'vs-dark',
          inherit: true,
          rules: [
            { token: 'keyword', foreground: 'c39bf0' },
            { token: 'type', foreground: 'c39bf0' },
            { token: 'number', foreground: 'f2a36b' },
            { token: 'string', foreground: '9ad49c' },
            { token: 'comment', foreground: '6c737b', fontStyle: 'italic' },
            { token: 'delimiter', foreground: 'b5bcc3' },
            { token: 'identifier', foreground: 'e4e7ea' },
            { token: 'keyword.directive', foreground: 'c39bf0' },
          ],
          colors: {
            'editor.background': '#101214',
            'editor.foreground': '#e4e7ea',
            'editorLineNumber.foreground': '#454b52',
            'editorLineNumber.activeForeground': '#9cc4ef',
            'editor.lineHighlightBackground': '#161a1e',
            'editor.selectionBackground': '#1d2b38',
            'editorCursor.foreground': '#ffffff',
            'editorWidget.background': '#1e2227',
            'editorWidget.border': '#2f353c',
            'editorSuggestWidget.background': '#1e2227',
            'editorSuggestWidget.selectedBackground': '#1d2b38',
            'editorHoverWidget.background': '#1e2227',
            'minimap.background': '#101214',
            'scrollbarSlider.background': '#2a2f3588',
          },
        });
        registerProviders(m);
        resolve(m);
      });
    });
    return monacoReady;
  }

  async function mount(container) {
    const m = await loadMonaco();
    if (!host) {
      host = document.createElement('div');
      host.style.cssText = 'position:absolute;inset:0';
      editor = m.editor.create(host, {
        theme: 'zt1lab',
        language: 'cpp',
        fontFamily: "'IBM Plex Mono', Consolas, monospace",
        fontSize: 13,
        lineHeight: 21,
        automaticLayout: true,
        minimap: { enabled: true, renderCharacters: false },
        smoothScrolling: true,
        cursorBlinking: 'smooth',
        renderWhitespace: 'selection',
        bracketPairColorization: { enabled: true },
        guides: { bracketPairs: true, indentation: true },
        tabSize: 2,
        insertSpaces: true,
        scrollBeyondLastLine: false,
        fixedOverflowWidgets: true,
        'semanticHighlighting.enabled': true,
      });
      editor.addCommand(m.KeyMod.CtrlCmd | m.KeyCode.KeyS, () => hooks.save && hooks.save());
      // The Lab's own items in the right-click menu (beside Monaco's: go to
      // definition, references, peek...): each hands the file and the place
      // clicked to the Lab (hooks.menu)
      const item = (id, label, order, keys, group = '0_lab') =>
        editor.addAction({
          id: 'lab.' + id,
          label,
          contextMenuGroupId: group,
          contextMenuOrder: order,
          keybindings: keys,
          run: (ed) => {
            const p = ed.getPosition();
            const sel = ed.getModel().getValueInRange(ed.getSelection());
            const word = sel || (ed.getModel().getWordAtPosition(p) || {}).word || '';
            if (hooks.menu) hooks.menu(id, { rel: current(), line: p.lineNumber, col: p.column, word, text: ed.getModel().getLineContent(p.lineNumber) });
          },
        });
      item('callers', 'Who calls this', 1, [m.KeyMod.Shift | m.KeyMod.Alt | m.KeyCode.KeyH]);
      item('callees', 'What this calls', 2);
      item('search', 'Search the engine’s code for this', 3);
      item('breakpoint', 'Scene: stop here (breakpoint)', 1, [m.KeyCode.F9], '1_lab');
      item('zoo', 'Open its zoo.exe function', 2, [], '1_lab');
      item('copy', 'Copy file:line', 3, [], '1_lab');
      item('build', 'Build engine', 4, [], '1_lab');
      editor.onDidChangeCursorPosition((e) => hooks.cursor && hooks.cursor(e.position.lineNumber, e.position.column));
      // (go to definition in another file: opened in the Lab)
      if (m.editor.registerEditorOpener)
        m.editor.registerEditorOpener({
          openCodeEditor(_src, resource, sel) {
            const rel = relOf(resource.toString());
            const line = sel ? sel.startLineNumber || sel.lineNumber || 1 : 1;
            if (hooks.open) hooks.open(rel || decodeURIComponent(resource.toString()), line);
            return true;
          },
        });
      startLsp();
    }
    if (host.parentElement !== container) container.appendChild(host);
    editor.layout();
    return editor;
  }

  // ------------------------------------------------------------ files
  async function ensureRoot() {
    if (!root) root = (await window.lab.lspStart()).root || '';
  }

  async function open(rel, text, opts = {}) {
    const m = await loadMonaco();
    await ensureRoot();
    let f = files.get(rel);
    if (!f) {
      // (a game file: read-only, as config text, and nothing for clangd; a
      // mod's data file - opts.data - the same, but to change)
      const data = !!opts.game || !!opts.data;
      const uri = m.Uri.parse(
        opts.game ? 'game:///' + encodeURI(rel.replace(/^game:/, '')) : opts.data ? 'mod:///' + encodeURI(rel.replace(/^mod:/, '')) : opts.abs ? 'file:///' + encodeURI(rel.replace(/\\/g, '/')) : uriOf(rel),
      );
      const model = m.editor.getModel(uri) || m.editor.createModel(text, data ? 'ini' : 'cpp', uri);
      f = { model, saved: model.getAlternativeVersionId(), crlf: !!opts.crlf, readOnly: !!opts.readOnly || !!opts.game, game: data, version: 1, disk: text };
      files.set(rel, f);
      model.onDidChangeContent(() => {
        f.version++;
        if (hooks.change) hooks.change(rel);
        clearTimeout(f.t);
        if (!f.game) f.t = setTimeout(() => lspNotify('textDocument/didChange', { textDocument: { uri: lspUri(model), version: f.version }, contentChanges: [{ text: model.getValue() }] }), 250);
      });
      lspOpen(model, f);
    }
    return f;
  }

  async function show(rel, line, col) {
    const f = files.get(rel);
    if (!f || !editor) return;
    const fresh = editor.getModel() !== f.model;
    if (fresh) editor.setModel(f.model);
    editor.updateOptions({ readOnly: f.readOnly });
    if (line) {
      f.want = line;
      editor.revealLineInCenter(line);
      editor.setPosition({ lineNumber: line, column: col || 1 });
      // (a file just put in the editor isn't laid out yet: shown at its top
      // unless the line's brought into view again once it is - and once more
      // later, as the editor itself, just made, takes longer. The latest line
      // asked for: a second show soon after the first wins)
      if (fresh)
        for (const ms of [60, 300])
          setTimeout(() => {
            if (editor.getModel() === f.model) editor.revealLineInCenter(f.want);
          }, ms);
    }
    editor.focus();
  }

  const isOpen = (rel) => files.has(rel);
  const isDirty = (rel) => {
    const f = files.get(rel);
    return !!f && f.model.getAlternativeVersionId() !== f.saved;
  };
  const textOf = (rel) => (files.has(rel) ? files.get(rel).model.getValue() : null);
  // The file as it was on disk when opened or last saved (to tell when
  // something else has changed it since)
  const diskOf = (rel) => (files.has(rel) ? files.get(rel).disk : null);
  // Changed on disk and not edited here: the editor takes the new text
  function reload(rel, text) {
    const f = files.get(rel);
    if (!f || isDirty(rel)) return false;
    f.model.setValue(text);
    f.saved = f.model.getAlternativeVersionId();
    f.disk = text;
    return true;
  }
  function markSaved(rel, text) {
    const f = files.get(rel);
    if (!f) return;
    if (text !== undefined) f.disk = text;
    f.saved = f.model.getAlternativeVersionId();
    lspNotify('textDocument/didSave', { textDocument: { uri: lspUri(f.model) } });
  }
  const current = () => {
    if (!editor || !editor.getModel()) return null;
    for (const [rel, f] of files) if (f.model === editor.getModel()) return rel;
    return null;
  };

  // build errors, as squiggles in each file
  function setBuildProblems(problems) {
    if (!window.monaco) return;
    const m = window.monaco;
    const by = new Map();
    for (const p of problems) {
      if (!by.has(p.file)) by.set(p.file, []);
      by.get(p.file).push(p);
    }
    for (const [rel, f] of files)
      m.editor.setModelMarkers(
        f.model,
        'build',
        (by.get(rel) || []).map((p) => ({
          severity: m.MarkerSeverity.Error,
          message: `${p.message} (${p.code}, build)`,
          startLineNumber: p.line,
          startColumn: p.col || 1,
          endLineNumber: p.line,
          endColumn: f.model.getLineMaxColumn(Math.min(p.line, f.model.getLineCount())),
        })),
      );
  }

  // ------------------------------------------------------------ clangd
  const lsp = { started: false, ready: null, id: 0, pending: new Map(), status: 'off', onStatus: null };
  const lspUri = (model) => model.uri.toString(true);

  function setStatus(s) {
    lsp.status = s;
    if (lsp.onStatus) lsp.onStatus(s);
  }

  function lspRequest(method, params) {
    return lsp.ready.then(
      () =>
        new Promise((resolve) => {
          const id = ++lsp.id;
          lsp.pending.set(id, resolve);
          window.lab.lspSend({ jsonrpc: '2.0', id, method, params });
          setTimeout(() => lsp.pending.has(id) && (lsp.pending.delete(id), resolve(null)), 15000);
        }),
    );
  }
  function lspNotify(method, params) {
    if (!lsp.started) return;
    lsp.ready.then(() => window.lab.lspSend({ jsonrpc: '2.0', method, params }));
  }
  function lspOpen(model, f) {
    if (f.lspOpened || f.game) return;
    if (lsp.started) f.lspOpened = true;
    lspNotify('textDocument/didOpen', { textDocument: { uri: lspUri(model), languageId: 'cpp', version: f.version, text: model.getValue() } });
  }

  function startLsp() {
    if (lsp.started) return;
    lsp.started = true;
    setStatus('starting');
    let readyResolve;
    lsp.ready = new Promise((r) => (readyResolve = r));
    window.lab.onLsp((msg) => {
      if (msg.id !== undefined && (msg.result !== undefined || msg.error !== undefined) && lsp.pending.has(msg.id)) {
        const r = lsp.pending.get(msg.id);
        lsp.pending.delete(msg.id);
        r(msg.error ? null : msg.result);
        return;
      }
      if (msg.method && msg.id !== undefined) {
        // (a request from clangd: answered, plainly)
        window.lab.lspSend({ jsonrpc: '2.0', id: msg.id, result: msg.method === 'workspace/configuration' ? [] : null });
        return;
      }
      if (msg.method === 'textDocument/publishDiagnostics') diagnostics(msg.params);
      else if (msg.method === '$/progress' && msg.params && msg.params.value) {
        const v = msg.params.value;
        if (v.kind === 'end') setStatus('ready');
        else if (v.title || v.message) setStatus(`indexing ${v.percentage != null ? v.percentage + '%' : ''}`.trim());
      }
    });
    window.lab.onLspExit(() => setStatus('stopped'));
    window.lab.lspStart().then((r) => {
      if (r.error) {
        setStatus('missing');
        return;
      }
      root = r.root;
      const id = ++lsp.id;
      lsp.pending.set(id, () => {
        window.lab.lspSend({ jsonrpc: '2.0', method: 'initialized', params: {} });
        setStatus('ready');
        readyResolve();
        // (files opened before clangd was up: told of now)
        for (const f of files.values()) lspOpen(f.model, f);
      });
      window.lab.lspSend({
        jsonrpc: '2.0',
        id,
        method: 'initialize',
        params: {
          processId: null,
          rootUri: 'file:///' + encodeURI(root.replace(/\\/g, '/')),
          capabilities: {
            textDocument: {
              synchronization: { didSave: true },
              completion: { completionItem: { snippetSupport: false, documentationFormat: ['markdown', 'plaintext'] } },
              hover: { contentFormat: ['markdown', 'plaintext'] },
              signatureHelp: { signatureInformation: { documentationFormat: ['markdown', 'plaintext'] } },
              definition: {},
              references: {},
              callHierarchy: {},
              documentSymbol: { hierarchicalDocumentSymbolSupport: true },
              publishDiagnostics: {},
            },
            window: { workDoneProgress: true },
          },
        },
      });
    });
  }

  function modelFor(uri) {
    const n = norm(uri);
    for (const f of files.values()) if (norm(f.model.uri.toString()) === n) return f.model;
    return null;
  }

  function diagnostics(p) {
    const model = modelFor(p.uri);
    if (!model || !window.monaco) return;
    const m = window.monaco;
    const sev = { 1: m.MarkerSeverity.Error, 2: m.MarkerSeverity.Warning, 3: m.MarkerSeverity.Info, 4: m.MarkerSeverity.Hint };
    m.editor.setModelMarkers(
      model,
      'clangd',
      p.diagnostics.map((d) => ({
        severity: sev[d.severity] || m.MarkerSeverity.Info,
        message: d.message,
        source: d.source || 'clangd',
        startLineNumber: d.range.start.line + 1,
        startColumn: d.range.start.character + 1,
        endLineNumber: d.range.end.line + 1,
        endColumn: d.range.end.character + 1,
      })),
    );
  }

  const pos = (p) => ({ line: p.lineNumber - 1, character: p.column - 1 });
  const range = (m, r) => new m.Range(r.start.line + 1, r.start.character + 1, r.end.line + 1, r.end.character + 1);

  // A file a definition is in, made a model so Monaco can go there
  async function ensureModel(uri) {
    if (modelFor(uri)) return;
    const rel = relOf(uri);
    if (rel) {
      const r = await window.lab.srcReadAll(rel);
      if (r) await open(rel, r.text, { crlf: r.crlf, readOnly: !/^(src|vendor)\//.test(rel) });
    } else {
      const abs = decodeURIComponent(String(uri)).replace(/^file:\/\/\//i, '');
      const r = await window.lab.readAny(abs);
      if (r) await open(abs, r.text, { abs: true, readOnly: true });
    }
  }

  function registerProviders(m) {
    const K = m.languages.CompletionItemKind;
    const kinds = [K.Text, K.Text, K.Method, K.Function, K.Constructor, K.Field, K.Variable, K.Class, K.Interface, K.Module, K.Property, K.Unit, K.Value, K.Enum, K.Keyword, K.Snippet, K.Color, K.File, K.Reference, K.Folder, K.EnumMember, K.Constant, K.Struct, K.Event, K.Operator, K.TypeParameter];
    m.languages.registerCompletionItemProvider('cpp', {
      triggerCharacters: ['.', '>', ':', '"', '<', '/'],
      async provideCompletionItems(model, position) {
        const r = await lspRequest('textDocument/completion', { textDocument: { uri: lspUri(model) }, position: pos(position) });
        if (!r) return { suggestions: [] };
        const items = Array.isArray(r) ? r : r.items || [];
        const word = model.getWordUntilPosition(position);
        const fallback = new m.Range(position.lineNumber, word.startColumn, position.lineNumber, word.endColumn);
        return {
          incomplete: !!r.isIncomplete,
          suggestions: items.map((it) => ({
            label: it.label.trim(),
            kind: kinds[it.kind] || K.Text,
            detail: it.detail,
            documentation: it.documentation ? { value: it.documentation.value || String(it.documentation) } : undefined,
            insertText: it.textEdit ? it.textEdit.newText : it.insertText || it.label.trim(),
            range: it.textEdit && it.textEdit.range ? range(m, it.textEdit.range) : fallback,
            filterText: it.filterText,
            sortText: it.sortText,
          })),
        };
      },
    });
    m.languages.registerHoverProvider('cpp', {
      async provideHover(model, position) {
        const r = await lspRequest('textDocument/hover', { textDocument: { uri: lspUri(model) }, position: pos(position) });
        if (!r || !r.contents) return null;
        const c = r.contents;
        const value = typeof c === 'string' ? c : c.value || (Array.isArray(c) ? c.map((x) => x.value || x).join('\n\n') : '');
        return { contents: [{ value }], range: r.range ? range(m, r.range) : undefined };
      },
    });
    // The Lab's own suggestions beside clangd's (app.js fills hooks.zoo): the
    // binary's mapping - zoo.exe's functions inside ZT_STEP("...") and
    // ZT_FN("..."), mapped fields after +0x - and the game's own names inside
    // num("...") / set(m, "...") - its .ai keys and behaviour sets - so none of
    // them has to be remembered
    m.languages.registerCompletionItemProvider('cpp', {
      triggerCharacters: ['"', 'x'],
      async provideCompletionItems(model, position) {
        if (!hooks.zoo) return { suggestions: [] };
        const before = model.getLineContent(position.lineNumber).slice(0, position.column - 1);
        const got = await hooks.zoo.complete(before);
        if (!got || !got.items.length) return { suggestions: [] };
        const r = new m.Range(position.lineNumber, position.column - got.typed.length, position.lineNumber, position.column);
        return {
          incomplete: true,
          suggestions: got.items.map((it, i) => ({
            label: { label: it.label, description: it.desc || '' },
            kind: K[it.kind] || K.Value,
            detail: it.detail,
            documentation: it.doc ? { value: it.doc } : undefined,
            insertText: it.insert,
            range: r,
            filterText: it.filter || it.label,
            sortText: String(i).padStart(4, '0'),
          })),
        };
      },
    });
    // (an address or a game name under the mouse: what the Lab knows of it)
    m.languages.registerHoverProvider('cpp', {
      provideHover(model, position) {
        if (!hooks.zoo) return null;
        const w = model.getWordAtPosition(position);
        if (!w) return null;
        const md = hooks.zoo.hover(w.word, model.getLineContent(position.lineNumber));
        return md ? { contents: [{ value: md }], range: new m.Range(position.lineNumber, w.startColumn, position.lineNumber, w.endColumn) } : null;
      },
    });
    const locations = async (method, model, position, extra) => {
      const r = await lspRequest(method, { textDocument: { uri: lspUri(model) }, position: pos(position), ...(extra || {}) });
      if (!r) return [];
      const list = (Array.isArray(r) ? r : [r]).map((l) => ({ uri: l.targetUri || l.uri, range: l.targetSelectionRange || l.range }));
      for (const l of list) await ensureModel(l.uri);
      return list.map((l) => ({ uri: (modelFor(l.uri) || { uri: m.Uri.parse(l.uri) }).uri, range: range(m, l.range) }));
    };
    m.languages.registerDefinitionProvider('cpp', { provideDefinition: (model, p) => locations('textDocument/definition', model, p) });
    m.languages.registerDeclarationProvider('cpp', { provideDeclaration: (model, p) => locations('textDocument/declaration', model, p) });
    m.languages.registerReferenceProvider('cpp', {
      provideReferences: (model, p) => locations('textDocument/references', model, p, { context: { includeDeclaration: true } }),
    });
    m.languages.registerSignatureHelpProvider('cpp', {
      signatureHelpTriggerCharacters: ['(', ','],
      async provideSignatureHelp(model, position) {
        const r = await lspRequest('textDocument/signatureHelp', { textDocument: { uri: lspUri(model) }, position: pos(position) });
        if (!r || !r.signatures || !r.signatures.length) return null;
        return {
          value: {
            signatures: r.signatures.map((s) => ({
              label: s.label,
              documentation: s.documentation ? { value: s.documentation.value || String(s.documentation) } : undefined,
              parameters: (s.parameters || []).map((p) => ({ label: p.label })),
            })),
            activeSignature: r.activeSignature || 0,
            activeParameter: r.activeParameter || 0,
          },
          dispose() {},
        };
      },
    });
  }

  // ------------------------------------------------------------ calls
  // Who calls a function (incoming) or what it calls (outgoing), by clangd's
  // call hierarchy. At a place in a file: the function named there, else the
  // one the place is inside. Items keep clangd's own item, to go a level on
  const SYM_FN = new Set([6, 9, 12]); // method, constructor, function
  async function enclosing(model, line) {
    const r = await lspRequest('textDocument/documentSymbol', { textDocument: { uri: lspUri(model) } });
    let best = null;
    const walk = (list) => {
      for (const d of list || []) {
        const rg = d.range || (d.location && d.location.range);
        if (!rg || line - 1 < rg.start.line || line - 1 > rg.end.line) continue;
        if (SYM_FN.has(d.kind) && d.selectionRange) best = d;
        walk(d.children);
      }
    };
    walk(r);
    return best;
  }
  async function callsAt(rel, line, col) {
    const f = files.get(rel);
    if (!f) return { error: 'not open' };
    await lsp.ready;
    const td = { uri: lspUri(f.model) };
    let items = await lspRequest('textDocument/prepareCallHierarchy', { textDocument: td, position: { line: line - 1, character: Math.max(0, (col || 1) - 1) } });
    let inside = false;
    if (!items || !items.length) {
      // (not on a function's name: the function it's in)
      const fn = await enclosing(f.model, line);
      if (!fn) return { error: 'not in a function' };
      items = await lspRequest('textDocument/prepareCallHierarchy', { textDocument: td, position: fn.selectionRange.start });
      inside = true;
    }
    if (!items || !items.length) return { error: 'clangd has nothing there' };
    return { item: items[0], inside };
  }
  const placeOf = (it, r) => ({ rel: relOf(it.uri), abs: relOf(it.uri) ? null : norm(it.uri), line: (r || it.selectionRange).start.line + 1 });
  async function callsOf(item, dir) {
    const r = await lspRequest(dir === 'out' ? 'callHierarchy/outgoingCalls' : 'callHierarchy/incomingCalls', { item });
    return (r || []).map((c) => {
      const it = dir === 'out' ? c.to : c.from;
      // (incoming: where in the caller it calls; outgoing: where it's defined)
      const at = dir === 'out' ? placeOf(it) : placeOf(it, c.fromRanges && c.fromRanges[0]);
      return { name: it.name, detail: it.detail || '', item: it, ...at, times: (c.fromRanges || []).length };
    });
  }

  // Edits made by code (the MCP), as if typed: undoable, seen in the editor.
  // replace: every (or the first) exact match of find; insert: text before
  // a line; lines: the text of lines from..to
  function replace(rel, find, withText, all) {
    const f = files.get(rel);
    if (!f || f.readOnly) return { error: 'not open, or read only' };
    const matches = f.model.findMatches(find, false, false, true, null, false);
    if (!matches.length) return { replaced: 0 };
    const use = all ? matches : [matches[0]];
    f.model.pushEditOperations([], use.map((mm) => ({ range: mm.range, text: withText })), () => null);
    if (editor && editor.getModel() === f.model) editor.revealLineInCenter(use[0].range.startLineNumber);
    return { replaced: use.length, line: use[0].range.startLineNumber };
  }
  function insert(rel, line, text) {
    const f = files.get(rel);
    if (!f || f.readOnly) return { error: 'not open, or read only' };
    const m = window.monaco;
    const n = Math.max(1, Math.min(line, f.model.getLineCount() + 1));
    const at = n > f.model.getLineCount() ? new m.Range(n - 1, f.model.getLineMaxColumn(n - 1), n - 1, f.model.getLineMaxColumn(n - 1)) : new m.Range(n, 1, n, 1);
    const t = n > f.model.getLineCount() ? '\n' + text : text.endsWith('\n') ? text : text + '\n';
    f.model.pushEditOperations([], [{ range: at, text: t }], () => null);
    if (editor && editor.getModel() === f.model) editor.revealLineInCenter(n);
    return { ok: true };
  }
  // Typed at a place, as if at the keyboard (the editor showing that file):
  // what's typed opens suggestions as typing would; then the list is asked for
  function typeAt(rel, line, col, text) {
    const f = files.get(rel);
    if (!f || f.readOnly || !editor || editor.getModel() !== f.model) return { error: 'not the file shown, or read only' };
    const ln = Math.max(1, Math.min(line, f.model.getLineCount()));
    const c = col ? Math.min(col, f.model.getLineMaxColumn(ln)) : f.model.getLineMaxColumn(ln);
    editor.setPosition({ lineNumber: ln, column: c });
    editor.revealLineInCenter(ln);
    editor.focus();
    if (text) editor.trigger('keyboard', 'type', { text });
    editor.trigger('lab', 'editor.action.triggerSuggest', {});
    const p = editor.getPosition();
    return { ok: true, before: f.model.getLineContent(p.lineNumber).slice(0, p.column - 1) };
  }
  function lines(rel, from, to) {
    const f = files.get(rel);
    if (!f) return null;
    const n = f.model.getLineCount();
    from = Math.max(1, from || 1);
    to = Math.min(n, to || n);
    const out = [];
    for (let i = from; i <= to; i++) out.push(`${i}: ${f.model.getLineContent(i)}`);
    return { lines: out.join('\n'), total: n };
  }
  function markers(rel) {
    const f = files.get(rel);
    if (!f || !window.monaco) return [];
    return window.monaco.editor.getModelMarkers({ resource: f.model.uri }).map((x) => ({
      line: x.startLineNumber,
      col: x.startColumn,
      severity: x.severity >= 8 ? 'error' : x.severity >= 4 ? 'warning' : 'info',
      message: x.message,
      from: x.owner,
    }));
  }

  return {
    callsAt,
    callsOf,
    placeOf,
    replace,
    insert,
    typeAt,
    lines,
    markers,
    mount,
    open,
    show,
    isOpen,
    isDirty,
    textOf,
    markSaved,
    diskOf,
    reload,
    current,
    setBuildProblems,
    hooks,
    lsp,
    crlfOf: (rel) => (files.get(rel) || {}).crlf,
    readOnlyOf: (rel) => (files.get(rel) || {}).readOnly,
    focus: () => editor && editor.focus(),
    action: (id) => editor && editor.getAction(id) && editor.getAction(id).run(),
  };
})();
