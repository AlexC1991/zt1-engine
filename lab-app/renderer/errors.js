// The page's own errors (a script that threw, a promise nobody caught,
// console.error), kept so lab_state can say why a view came out empty
const PAGE_ERRORS = [];
function pageError(text) {
  PAGE_ERRORS.push({ at: new Date().toLocaleTimeString(), tab: typeof S !== 'undefined' && S ? S.tab : '', text: String(text).slice(0, 600) });
  if (PAGE_ERRORS.length > 30) PAGE_ERRORS.shift();
}
window.addEventListener('error', (e) => {
  const where = `${(e.filename || '').split('/').pop()}:${e.lineno}`;
  const stack = e.error && e.error.stack ? '\n' + e.error.stack.split('\n').slice(1, 4).join('\n') : '';
  pageError(`${e.message} (${where})${stack}`);
});
window.addEventListener('unhandledrejection', (e) => pageError('Uncaught (in a promise): ' + ((e.reason && (e.reason.stack || e.reason.message)) || e.reason)));
{
  const ce = console.error.bind(console);
  console.error = (...a) => (pageError(a.map((x) => (x && x.stack) || String(x)).join(' ')), ce(...a));
}
