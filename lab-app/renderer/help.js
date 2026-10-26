// Offline Help: bundled articles, full-text search and local tutorial media.
const HELP = {
  doc: store.get('helpDoc', 'start'),
  query: store.get('helpQuery', ''),
  group: store.get('helpGroup', ''),
  history: [], cursor: -1, positions: new Map(),
};
const HELP_GROUPS = ['Start here', 'Making mods', 'Recorded tutorials', 'Reference'];
const helpDoc = (id) => LAB_HELP_LIBRARY.find((d) => d.id === id);

function helpPauseMedia() {
  for (const video of $('v-help').querySelectorAll('video')) video.pause();
}

function helpRememberPosition() {
  const reader = $('help-reader');
  if (reader) HELP.positions.set(HELP.doc, reader.scrollTop);
}

function helpOpen(id, section = '', history = true) {
  if (!helpDoc(id)) return;
  helpRememberPosition();
  helpPauseMedia();
  HELP.doc = id;
  store.set('helpDoc', id);
  if (history && HELP.history[HELP.cursor] !== id) {
    HELP.history.splice(HELP.cursor + 1);
    HELP.history.push(id);
    HELP.cursor = HELP.history.length - 1;
  }
  if (S.tab !== 'help') setTab('help');
  renderHelpArticle();
  renderHelpList();
  if (section) helpSection(section);
}

function helpSection(id) {
  const heading = document.getElementById('help-section-' + id);
  if (heading) heading.scrollIntoView({ block: 'start' });
}

function helpResults() {
  const terms = HELP.query.toLocaleLowerCase().trim().split(/\s+/).filter(Boolean);
  return LAB_HELP_LIBRARY.filter((d) => !HELP.group || d.group === HELP.group)
    .map((d) => {
      const title = (d.title + ' ' + (d.keywords || '')).toLocaleLowerCase();
      const text = (title + ' ' + d.summary + ' ' + d.text).toLocaleLowerCase();
      return { doc: d, score: terms.reduce((n, t) => n + (title.includes(t) ? 5 : 1), 0), matches: terms.every((t) => text.includes(t)) };
    }).filter((r) => r.matches).sort((a, b) => b.score - a.score);
}

function renderHelpList() {
  const list = $('help-list');
  if (!list) return;
  const results = helpResults();
  $('help-count').textContent = results.length + (results.length === 1 ? ' guide' : ' guides');
  for (const b of $('help-groups').querySelectorAll('button')) {
    const selected = b.dataset.group === HELP.group;
    b.classList.toggle('on', selected);
    b.setAttribute('aria-pressed', String(selected));
  }
  list.innerHTML = results.length ? results.map(({ doc: d }) => `<button class="help-result${HELP.doc === d.id ? ' on' : ''}" data-help-open="${esc(d.id)}"${HELP.doc === d.id ? ' aria-current="page"' : ''}><span class="help-result-group">${esc(d.group)}</span><strong>${esc(d.title)}</strong><span>${esc(d.summary)}</span>${d.legacy ? '<small>Earlier interface</small>' : ''}</button>`).join('') : '<div class="help-empty"><strong>No matching guides</strong><p>Try fewer words, another category, or clear the search.</p><button class="btn small" data-help-clear>Clear search and filters</button></div>';
}

function renderHelpArticle() {
  const d = helpDoc(HELP.doc) || helpDoc('start');
  HELP.doc = d.id;
  const reader = $('help-reader');
  reader.innerHTML = `<div class="help-article-bar"><div><button class="btn small ghost" data-help-back ${HELP.cursor <= 0 ? 'disabled' : ''} aria-label="Previous article">← Back</button><button class="btn small ghost" data-help-forward ${HELP.cursor >= HELP.history.length - 1 ? 'disabled' : ''} aria-label="Next article">Forward →</button></div><span>${esc(d.group)}</span><span class="spacer"></span>${(d.tabs || []).map((t) => `<button class="btn small" data-help-tab="${esc(t)}">Open ${esc((TABS.find(([id]) => id === t) || [t, t])[1])}</button>`).join('')}</div><article class="help-article"><div class="help-kicker">ZT1 LAB · HELP</div><h1>${esc(d.title)}</h1><p class="help-intro">${esc(d.summary)}</p>${d.notice ? `<aside class="help-notice">${esc(d.notice)} <button data-help-open="${d.legacy ? 'workspace' : 'publish'}">${d.legacy ? 'Current Lab guide' : 'Current publishing guide'} →</button></aside>` : ''}${d.videos.length ? `<section class="help-videos" aria-label="Tutorial recordings">${d.videos.map((v) => `<figure><video controls preload="metadata" src="${esc(v.src)}"${v.poster ? ` poster="${esc(v.poster)}"` : ''} aria-label="${esc(v.title)}"></video><figcaption><strong>${esc(v.title)}</strong> ${esc(v.caption)}</figcaption></figure>`).join('')}</section>` : ''}<div class="help-prose">${d.html}</div><footer class="help-footer"><button class="btn small" data-help-open="start">Start here</button><button class="btn small" data-help-open="troubleshooting">Troubleshooting</button><span>Included with the Lab · Available offline</span></footer></article>`;
  $('help-contents').innerHTML = `<div class="help-contents-title">On this page</div>${d.toc.map((h) => `<button data-help-section="${esc(h.id)}" class="${h.level > 2 ? 'sub' : ''}">${esc(h.title)}</button>`).join('') || '<p class="muted">Read the guide on the left.</p>'}`;
  reader.scrollTop = HELP.positions.get(d.id) || 0;
}

function renderHelp() {
  if (!$('help-reader')) {
    $('v-help').innerHTML = `<aside class="help-sidebar"><div class="help-library-heading"><h2>Help library</h2><span>F1</span></div><label class="help-search-label" for="help-search">Search every guide</label><input type="search" id="help-search" class="search" placeholder="Try: marine, OBJ, theme pack…" autocomplete="off"><div id="help-groups" class="help-groups"><button data-group="">All guides</button>${HELP_GROUPS.map((g) => `<button data-group="${g}">${g}</button>`).join('')}</div><div id="help-count" class="help-count" role="status" aria-live="polite"></div><nav id="help-list" aria-label="Help articles"></nav></aside><main id="help-reader" class="help-reader" aria-label="Help article"></main><nav id="help-contents" class="help-contents" aria-label="Article contents"></nav><dialog id="help-picture"><button class="btn small" data-help-close>Close picture · Esc</button><img alt=""></dialog>`;
    $('help-search').value = HELP.query;
    $('help-search').addEventListener('input', (e) => {
      HELP.query = e.target.value;
      store.set('helpQuery', HELP.query);
      renderHelpList();
    });
    $('v-help').addEventListener('click', helpClick);
    $('v-help').addEventListener('keydown', (e) => {
      if ((e.key === 'Enter' || e.key === ' ') && e.target.matches('.help-prose img')) {
        e.preventDefault();
        helpEnlarge(e.target);
      }
    });
  }
  if (HELP.cursor < 0) HELP.history.push(HELP.doc), HELP.cursor = 0;
  renderHelpList();
  // Keep the article and video state when returning from another Lab tab.
  if (!$('help-reader').firstElementChild) renderHelpArticle();
}

function helpEnlarge(img) {
  const dialog = $('help-picture'), full = dialog.querySelector('img');
  full.src = img.src;
  full.alt = img.alt;
  dialog.showModal();
}

function helpClick(e) {
  const t = e.target.closest('button, a, .help-prose img');
  if (!t) return;
  if (t.tagName === 'A') e.preventDefault();
  if (t.dataset.helpOpen) return helpOpen(t.dataset.helpOpen);
  if (t.dataset.helpLink) {
    const [id, section = ''] = t.dataset.helpLink.split('#');
    return helpOpen(id, section);
  }
  if (t.dataset.helpSection) return helpSection(t.dataset.helpSection);
  if (t.dataset.helpWeb) return lab.helpOpenExternal(t.dataset.helpWeb);
  if (t.dataset.helpTab && TABS.some(([id]) => id === t.dataset.helpTab)) return setTab(t.dataset.helpTab);
  if (t.hasAttribute('data-group')) {
    HELP.group = t.dataset.group;
    store.set('helpGroup', HELP.group);
    return renderHelpList();
  }
  if (t.hasAttribute('data-help-clear')) {
    HELP.query = HELP.group = '';
    store.set('helpQuery', ''); store.set('helpGroup', '');
    $('help-search').value = '';
    renderHelpList();
    return $('help-search').focus();
  }
  if (t.hasAttribute('data-help-back') || t.hasAttribute('data-help-forward')) {
    const next = HELP.cursor + (t.hasAttribute('data-help-back') ? -1 : 1);
    if (next >= 0 && next < HELP.history.length) {
      HELP.cursor = next;
      return helpOpen(HELP.history[next], '', false);
    }
  }
  if (t.hasAttribute('data-help-close')) return $('help-picture').close();
  if (t.matches('.help-prose img')) helpEnlarge(t);
}

document.addEventListener('keydown', (e) => {
  if (e.key === 'F1' && !e.ctrlKey && !e.altKey && !e.metaKey) {
    e.preventDefault();
    e.stopPropagation();
    setTab('help');
  }
}, true);

Object.assign(ACTIONS, { helpOpen: (id) => helpOpen(id), helpSection: (id) => helpSection(id) });
