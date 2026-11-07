// FragDesk Handy-App – Start, Navigation, gemeinsamer Zustand
import {h, icon, notice, closeSheet} from './ui.js';
import * as api from './api.js';
import {renderPair} from './pages/pair.js';

const root = document.getElementById('app');

// ---- Darstellung ----------------------------------------------------------------------------------------------------
const store = {
  get(k, def) { try { const v = localStorage.getItem('fd.' + k); return v == null ? def : JSON.parse(v); } catch (e) { return def; } },
  set(k, v) { try { localStorage.setItem('fd.' + k, JSON.stringify(v)); } catch (e) { /* privat: egal */ } },
};
export {store};

const darkQuery = window.matchMedia('(prefers-color-scheme: light)');
export function applyTheme() {
  const t = store.get('theme', 'auto');
  const light = t === 'light' || (t === 'auto' && darkQuery.matches);
  document.documentElement.classList.toggle('light', light);
  const meta = document.querySelector('meta[name=theme-color]');
  if (meta) meta.setAttribute('content', light ? '#f3f5f9' : '#0f141b');
}
darkQuery.addEventListener('change', applyTheme);
applyTheme();

// ---- Seiten ---------------------------------------------------------------------------------------------------------
const pages = {
  overview: () => import('./pages/overview.js'),
  matches: () => import('./pages/matches.js'),
  match: () => import('./pages/match.js'),
  stats: () => import('./pages/stats.js'),
  moments: () => import('./pages/moments.js'),
  settings: () => import('./pages/settings.js'),
};
const routes = [
  {re: /^\/?$/, page: 'overview', nav: 0},
  {re: /^\/matches$/, page: 'matches', nav: 1},
  {re: /^\/match\/([^/]+)(?:\/(\d+))?$/, page: 'match', nav: 1, detail: true},
  {re: /^\/stats(?:\/(\d+))?$/, page: 'stats', nav: 2},
  {re: /^\/moments(?:\/(\d+))?$/, page: 'moments', nav: 3},
  {re: /^\/settings$/, page: 'settings', nav: -1, detail: true},
];
const navItems = [
  ['#/', 'home', 'Übersicht'],
  ['#/matches', 'list', 'Matches'],
  ['#/stats', 'stats', 'Statistiken'],
  ['#/moments', 'film', 'Momente'],
];

// Filter wie in FragDesk (Übersicht, Statistiken und Momente teilen sie)
const state = {
  info: null,
  filters: Object.assign({mode: 0, period: 2, map: ''}, store.get('filters', {})),
};

let shell = null, renderSeq = 0, visited = [], scrollPos = new Map();

function buildShell() {
  const tt = h('div', {class: 'tt'});
  const left = h('button', {class: 'iconbtn spacer', type: 'button', 'aria-label': 'Zurück'}, icon('back'));
  const right = h('a', {class: 'iconbtn', href: '#/settings', 'aria-label': 'Einstellungen'}, icon('sliders'));
  const top = h('header', {class: 'top'}, left, tt, right);
  const main = h('main');
  const tabbar = h('nav', {class: 'tabbar'}, navItems.map(([href, ic, label]) => h('a', {href}, icon(ic), label)));
  left.addEventListener('click', goBack);
  tabbar.addEventListener('click', ev => {   // aktiven Reiter nochmal tippen: nach oben und neu laden
    const a = ev.target.closest('a');
    if (a && a.classList.contains('on') && location.hash.replace(/\?.*/, '') === a.getAttribute('href')) {
      ev.preventDefault();
      window.scrollTo({top: 0, behavior: 'smooth'});
      render(true);
    }
  });
  root.replaceChildren(top, main, tabbar);
  window.addEventListener('scroll', () => top.classList.toggle('scrolled', window.scrollY > 36), {passive: true});
  shell = {top, tt, left, right, main, tabbar};
}

export function goBack() {
  if (visited.length > 1) history.back();
  else location.hash = '#/';
}

function parseHash() {
  const raw = location.hash.replace(/^#/, '') || '/';
  const q = raw.indexOf('?');
  return {path: q < 0 ? raw : raw.slice(0, q), query: new URLSearchParams(q < 0 ? '' : raw.slice(q + 1))};
}

async function render(fresh) {
  closeSheet();
  if (!shell) buildShell();
  const {path, query} = parseHash();
  let route = null, m = null;
  for (const r of routes) if ((m = path.match(r.re))) { route = r; break; }
  if (!route) { location.hash = '#/'; return; }
  const seq = ++renderSeq;
  shell.tabbar.querySelectorAll('a').forEach((a, i) => a.classList.toggle('on', i === route.nav));
  shell.left.classList.toggle('spacer', !route.detail);
  shell.right.classList.toggle('spacer', !!route.detail);
  shell.top.classList.toggle('always', !!route.detail);
  shell.tt.textContent = '';
  const main = h('main');
  shell.main.replaceWith(main);
  shell.main = main;
  const ctx = {
    main, params: m.slice(1), query, info: state.info, filters: state.filters, fresh: !!fresh,
    current: () => seq === renderSeq,
    title: t => { if (seq === renderSeq) { shell.tt.textContent = t; document.title = t ? t + ' – FragDesk' : 'FragDesk'; } },
    setFilters: patch => { Object.assign(state.filters, patch); store.set('filters', state.filters); render(); },
    rerender: () => render(true),
    refreshInfo: async () => { state.info = await api.get('/api/v1/info'); return state.info; },
  };
  try {
    const mod = await pages[route.page]();
    if (seq !== renderSeq) return;
    await mod.render(ctx);
  } catch (e) {
    if (seq !== renderSeq) return;
    if (e && e.status === 401) return;   // Kopplung weg: fd:unpaired zeigt die Kopplung
    main.replaceChildren(errorBox(e, () => render(true)));
  }
  if (seq !== renderSeq) return;
  const key = location.hash;
  if (scrollPos.has(key) && restoring) window.scrollTo(0, scrollPos.get(key));
  restoring = false;
}

export function errorBox(e, retry) {
  return h('div', null,
    notice('err', 'Das hat nicht geklappt', (e && e.message) || String(e)),
    retry ? h('button', {class: 'btn block', type: 'button', onclick: retry}, icon('refresh'), 'Nochmal versuchen') : null);
}

let restoring = false;
function onHash() {
  const prev = visited[visited.length - 1];
  if (prev) scrollPos.set(prev, lastScroll);
  if (visited.length > 1 && visited[visited.length - 2] === location.hash) {
    visited.pop();
    restoring = true;
  } else {
    visited.push(location.hash);
    window.scrollTo(0, 0);
  }
  render();
}
let lastScroll = 0;
window.addEventListener('scroll', () => { lastScroll = window.scrollY; }, {passive: true});

// ---- Start ----------------------------------------------------------------------------------------------------------
async function start() {
  if ('serviceWorker' in navigator && window.isSecureContext) navigator.serviceWorker.register('sw.js').catch(() => {});
  const code = new URLSearchParams(location.search).get('pair') || '';
  try {
    await api.get('/api/v1/session');
  } catch (e) {
    if (e.status === 401) return showPair(code);
    root.replaceChildren(h('div', {class: 'pair'}, errorBox(e, start)));
    return;
  }
  try {
    state.info = await api.get('/api/v1/info');
  } catch (e) {
    root.replaceChildren(h('div', {class: 'pair'}, errorBox(e, start)));
    return;
  }
  if (!visited.length) visited.push(location.hash);
  window.addEventListener('hashchange', onHash);
  render();
}

function showPair(code) {
  window.removeEventListener('hashchange', onHash);
  shell = null;
  api.forget();
  renderPair(root, {code, onPaired: () => { visited = []; start(); }});
}

window.addEventListener('fd:unpaired', () => showPair(''));
start();
