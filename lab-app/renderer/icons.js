// ZT1 Lab: the buttons' icons (24px line icons, drawn in the button's own
// colour). ic(name) gives one as markup; every .btn with a data-act listed in
// BTN_ICONS gets its icon put in front of its label when it's drawn, so a new
// button needs only a line here. (loaded after code.js)

const ICONS = {
  play: '<path d="M7 4.5v15l12-7.5z"/>',
  hammer: '<path d="M14 6l4 4-9.5 9.5a2.1 2.1 0 0 1-3-3z"/><path d="M13 3h3l5 5-2.5 2.5L13 5z"/>',
  stop: '<rect x="6" y="6" width="12" height="12" rx="2"/>',
  layout: '<rect x="3.5" y="3.5" width="17" height="17" rx="2.5"/><path d="M9.5 3.5v17M9.5 12h11"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  search: '<circle cx="11" cy="11" r="6.5"/><path d="M20 20l-4.3-4.3"/>',
  external: '<path d="M14 4h6v6M20 4l-9 9"/><path d="M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5"/>',
  package: '<path d="M12 3l8 4.5v9L12 21l-8-4.5v-9z"/><path d="M4 7.5l8 4.5 8-4.5M12 12v9"/>',
  flask: '<path d="M9.5 3h5M10.5 3v6L5 18.5A1.8 1.8 0 0 0 6.6 21h10.8a1.8 1.8 0 0 0 1.6-2.5L13.5 9V3"/><path d="M7.5 15h9"/>',
  cpu: '<rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9.5 9.5h5v5h-5zM9 2v3M15 2v3M9 19v3M15 19v3M2 9h3M2 15h3M19 9h3M19 15h3"/>',
  eye: '<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="3"/>',
  check: '<path d="M5 12.5l4.5 4.5L19 7.5"/>',
  save: '<path d="M5 4h11l3 3v12a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1z"/><path d="M8 4v5h7V4M8 20v-6h8v6"/>',
  code: '<path d="M8.5 7L3.5 12l5 5M15.5 7l5 5-5 5"/>',
  keyboard: '<rect x="2.5" y="6" width="19" height="12" rx="2"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M7 14h10"/>',
  file: '<path d="M14 3H7a1 1 0 0 0-1 1v16a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1V7z"/><path d="M14 3v4h4"/>',
  back: '<path d="M10 6l-6 6 6 6M4 12h16"/>',
  trash: '<path d="M4 7h16M9 7V4h6v3M6.5 7l1 13h9l1-13"/>',
  refresh: '<path d="M20 12a8 8 0 1 1-2.3-5.6M20 4v5h-5"/>',
  sync: '<path d="M4 12a8 8 0 0 1 13.7-5.7L20 8.5M20 4v4.5h-4.5M20 12a8 8 0 0 1-13.7 5.7L4 15.5M4 20v-4.5h4.5"/>',
  x: '<path d="M6 6l12 12M18 6L6 18"/>',
  upload: '<path d="M12 16V4M7 9l5-5 5 5M4 16v3a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-3"/>',
  rec: '<circle cx="12" cy="12" r="6"/>',
  volume: '<path d="M4 9.5h3.5L12 5.5v13l-4.5-4H4z"/><path d="M15.5 9a4 4 0 0 1 0 6M18 6.5a7.5 7.5 0 0 1 0 11"/>',
};
const ic = (name) =>
  ICONS[name]
    ? `<svg class="ic" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICONS[name]}</svg>`
    : '';

// data-act -> its icon
const BTN_ICONS = {
  run: () => ({ engine: 'hammer', mods: 'flask' }[S.tab] || 'play'), engBuild: 'hammer', engRelease: 'package', engNewFile: 'plus', engFind: 'search', engOpenOutside: 'external',
  engNewReader: 'plus', engNewScript: 'plus', scriptRun: 'play', scriptToggle: 'play', scriptTestZoo: 'eye', engRunReader: 'play', layoutMenu: 'layout',
  tryFn: 'cpu', seeLive: 'eye', modFor: 'flask', newMod: 'flask', newModFromFile: 'flask', openCode: 'code',
  saveFn: 'save', makeFn: 'code', openModFile: 'external', modsGameClose: 'back',
  kbTools: 'plus', memOpen: 'search', recIdle: 'eye', recStart: 'rec', recStop: 'stop', recList: 'file',
  playStart: 'play', playOriginal: 'play', playStop: 'stop', playFocus: 'keyboard',
  watchClear: 'trash', watchSel: 'eye', restartEngine: 'refresh', syncGo: 'upload',
};
// (the old text glyphs the icons replace)
const GLYPHS = /^[\s▶＋+▦⟳✓]+/;
function iconButtons(root) {
  for (const b of root.querySelectorAll('.btn[data-act]:not(.has-ic)')) {
    let name = BTN_ICONS[b.dataset.act];
    if (typeof name === 'function') name = name();
    if (!name) continue;
    const first = b.firstChild;
    if (first && first.nodeType === 3) first.textContent = first.textContent.replace(GLYPHS, '');
    b.insertAdjacentHTML('afterbegin', ic(name));
    b.classList.add('has-ic');
  }
}
new MutationObserver((list) => {
  for (const m of list)
    for (const n of m.addedNodes) if (n.nodeType === 1) iconButtons(n.matches && n.matches('.btn') ? n.parentNode || n : n);
  // (a button whose label was redrawn: its text changed, its icon went)
  for (const m of list) if (m.type === 'characterData' || m.type === 'childList') {
    const b = m.target.nodeType === 1 ? m.target.closest && m.target.closest('.btn.has-ic') : m.target.parentElement && m.target.parentElement.closest('.btn.has-ic');
    if (b && !b.querySelector('svg.ic')) b.classList.remove('has-ic'), iconButtons(b.parentNode);
  }
}).observe(document.documentElement, { childList: true, subtree: true, characterData: true });
document.addEventListener('DOMContentLoaded', () => iconButtons(document));
