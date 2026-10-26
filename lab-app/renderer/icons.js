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
  paw: '<circle cx="6.5" cy="10" r="1.7"/><circle cx="10" cy="6" r="1.7"/><circle cx="14.5" cy="6" r="1.7"/><circle cx="18" cy="10" r="1.7"/><path d="M8 17.2c0-3 2-5.2 4.2-5.2s4.2 2.2 4.2 5.2c0 1.7-1.6 2.3-4.2 2.3S8 18.9 8 17.2z"/>',
  map: '<path d="M3.5 6l5.5-2 6 2 5.5-2v14l-5.5 2-6-2-5.5 2z"/><path d="M9 4v14M15 6v14"/>',
  pencil: '<path d="M4 20l1-4L16 5l3 3L8 19z"/><path d="M14 7l3 3"/>',
  up: '<path d="M12 19V5M6 11l6-6 6 6"/>',
  down: '<path d="M12 5v14M6 13l6 6 6-6"/>',
  eyeOff: '<path d="M3 3l18 18"/><path d="M10.6 6.1A9.8 9.8 0 0 1 12 6c6 0 9.5 6 9.5 6a16 16 0 0 1-3 3.6M6.6 6.6C3.9 8.4 2.5 12 2.5 12S6 18 12 18a9 9 0 0 0 4.4-1.1"/>',
  shadow: '<path d="M8.5 4.5h7v8h-7z"/><ellipse cx="13" cy="18" rx="8" ry="2.5"/><path d="M8.5 12.5l-2 4"/>',
  onion: '<rect x="3" y="8" width="11" height="11" rx="2" stroke-dasharray="2 2"/><rect x="10" y="4" width="11" height="11" rx="2"/>',
  mirror: '<path d="M12 3v18" stroke-dasharray="2 2"/><path d="M9 7L4 12l5 5zM15 7l5 5-5 5z"/>',
  shrink: '<path d="M9 4v5H4M15 4v5h5M9 20v-5H4M15 20v-5h5"/>',
  grow: '<path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5"/>',
  bodyShort: '<path d="M3 12h6M21 12h-6"/><path d="M6 9l3 3-3 3M18 9l-3 3 3 3"/>',
  bodyLong: '<path d="M10 12H3M14 12h7"/><path d="M6 9l-3 3 3 3M18 9l3 3-3 3"/>',
  bandShort: '<path d="M12 2.5v6M12 21.5v-6"/><path d="M9 5.5l3 3 3-3M9 18.5l3-3 3 3"/><path d="M5 12h14" stroke-dasharray="2 2"/>',
  bandTall: '<path d="M12 9V3M12 15v6"/><path d="M9 6l3-3 3 3M9 18l3 3 3-3"/><path d="M5 12h14" stroke-dasharray="2 2"/>',
  palette: '<path d="M12 3a9 9 0 1 0 0 18c1.7 0 2-1.3 1.2-2.4-.8-1.1-.2-2.6 1.3-2.6H17a4 4 0 0 0 4-4c0-5-4-9-9-9z"/><circle cx="7.5" cy="11" r="1"/><circle cx="10" cy="7" r="1"/><circle cx="14.5" cy="7" r="1"/>',
  flip: '<path d="M12 3v18" stroke-dasharray="2 2"/><path d="M9 7v10H4zM15 7v10h5z"/>',
  copy: '<rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8V5a1 1 0 0 0-1-1H5a1 1 0 0 0-1 1v10a1 1 0 0 0 1 1h3"/>',
  download: '<path d="M12 4v12M7 11l5 5 5-5M4 16v3a1 1 0 0 0 1 1h14a1 1 0 0 0 1-1v-3"/>',
  zoomIn: '<circle cx="11" cy="11" r="6.5"/><path d="M20 20l-4.3-4.3M8 11h6M11 8v6"/>',
  zoomOut: '<circle cx="11" cy="11" r="6.5"/><path d="M20 20l-4.3-4.3M8 11h6"/>',
  fit: '<path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5"/><rect x="8.5" y="8.5" width="7" height="7" rx="1"/>',
  pause: '<rect x="6" y="5" width="4" height="14" rx="1"/><rect x="14" y="5" width="4" height="14" rx="1"/>',
  prev: '<path d="M15 6l-6 6 6 6"/>',
  next: '<path d="M9 6l6 6-6 6"/>',
  frameAdd: '<rect x="3" y="5" width="12" height="14" rx="2"/><path d="M19.5 9v6M16.5 12h6"/>',
  moveLeft: '<path d="M10 7l-5 5 5 5M5 12h14"/>',
  moveRight: '<path d="M14 7l5 5-5 5M19 12H5"/>',
  outline: '<path d="M6 18c0-5 2.5-8 6-8s6 3 6 8z" stroke-dasharray="2.2 1.8"/><circle cx="12" cy="6" r="2.5" stroke-dasharray="2 1.6"/>',
  inside: '<path d="M5 19c0-6 3-10 7-10s7 4 7 10z"/><path d="M9 16l2-2M11.5 17l3-3"/><circle cx="12" cy="5.5" r="2.2"/>',
  bucket: '<path d="M5 11l7-7 7 7-7 7z"/><path d="M5 11h14"/><path d="M20 15c0 1.5-1 2.5-1 3.5a1 1 0 0 0 2 0c0-1-1-2-1-3.5z"/>',
  stencil: '<rect x="3.5" y="3.5" width="17" height="17" rx="3" stroke-dasharray="3 2.2"/><path d="M7.5 16c1.5-3 3-4.5 4.5-4.5s3 1.5 4.5 4.5"/><circle cx="12" cy="8" r="1.6"/>',
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
