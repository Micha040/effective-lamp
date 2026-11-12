// STAMMTISCH – Start, Navigation, gemeinsamer Zustand
import {h, icon, notice, closeSheet, button} from './ui.js';
import {backend, isDemo} from './backend/index.js';
import {parseCodeInput} from './util.js';

const root = document.getElementById('app');

// ---- Kleiner Speicher auf dem Gerät (Einstellungen, letzter Stand für „ohne Netz“) -----------------------------------
export const store = {
  get(k, def) { try { const v = localStorage.getItem('st.' + k); return v == null ? def : JSON.parse(v); } catch (e) { return def; } },
  set(k, v) { try { localStorage.setItem('st.' + k, JSON.stringify(v)); } catch (e) { /* privat oder voll: egal */ } },
  del(k) { try { localStorage.removeItem('st.' + k); } catch (e) { /* egal */ } },
};

// ---- Darstellung: folgt dem Handy, lässt sich in den Einstellungen festlegen ------------------------------------------
const lightQuery = window.matchMedia('(prefers-color-scheme: light)');
export function applyTheme() {
  const forced = new URLSearchParams(location.search).get('theme');   // nur für Bilder der Oberfläche (tools/render.sh)
  const t = forced || store.get('theme', 'auto');
  const light = t === 'light' || (t === 'auto' && lightQuery.matches);
  document.documentElement.classList.toggle('light', light);
  const meta = document.querySelector('meta[name=theme-color]');
  if (meta) meta.setAttribute('content', light ? '#f3f5f9' : '#0f141b');
}
lightQuery.addEventListener('change', applyTheme);
applyTheme();

// ---- Seiten -------------------------------------------------------------------------------------------------------------
const pages = {
  events: () => import('./pages/events.js'),
  event: () => import('./pages/event.js'),
  'event-edit': () => import('./pages/event-edit.js'),
  chat: () => import('./pages/chat.js'),
  photos: () => import('./pages/photos.js'),
  settings: () => import('./pages/settings.js'),
  changelog: () => import('./pages/changelog.js'),
};
const ID = '([0-9a-f-]{36})';
// detail: Unterseite mit „Zurück“; form: Eingabeseite, die nie von selbst neu gezeichnet wird; fn: andere Funktion der Datei
const routes = [
  {re: /^\/?$/, page: 'events', nav: 0},
  {re: /^\/archiv$/, page: 'events', fn: 'renderArchive', nav: 0, detail: true},
  {re: /^\/treffen\/neu$/, page: 'event-edit', nav: 0, detail: true, form: true},
  {re: new RegExp(`^/treffen/${ID}/bearbeiten$`), page: 'event-edit', nav: 0, detail: true, form: true},
  {re: new RegExp(`^/treffen/${ID}$`), page: 'event', nav: 0, detail: true},
  {re: /^\/chat$/, page: 'chat', nav: 1, chat: true},
  {re: /^\/bilder$/, page: 'photos', nav: 2},
  {re: new RegExp(`^/bilder/(${ID.slice(1, -1)}|ohne)$`), page: 'photos', fn: 'renderAlbum', nav: 2, detail: true},
  {re: /^\/einstellungen$/, page: 'settings', nav: -1, detail: true},
  {re: /^\/orte$/, page: 'settings', fn: 'renderPlaces', nav: -1, detail: true},
  {re: /^\/verlauf$/, page: 'changelog', nav: -1, detail: true},
  {re: /^\/papierkorb$/, page: 'changelog', fn: 'renderTrash', nav: -1, detail: true},
];
const navItems = [
  ['#/', 'beer', 'Treffen'],
  ['#/chat', 'chat', 'Chat'],
  ['#/bilder', 'image', 'Bilder'],
];

const state = {
  be: null,          // die Anbindung (js/backend)
  groups: [],        // meine Gruppen: [{id, name, me}]
  group: null,       // die gerade gezeigte
  members: [],       // ihre Mitglieder (auch ehemalige, für alte Beiträge)
  unread: 0,         // neue Chat-Nachrichten seit dem letzten Blick in den Chat
  offline: false,
};

let shell = null, renderSeq = 0, visited = [], scrollPos = new Map(), route = null;
let pageLive = new Set(), pageCleanup = [];

function buildShell() {
  const tt = h('div', {class: 'tt'});
  const left = h('button', {class: 'iconbtn spacer', type: 'button', 'aria-label': 'Zurück'}, icon('back'));
  const right = h('a', {class: 'iconbtn', href: '#/einstellungen', 'aria-label': 'Einstellungen'}, icon('sliders'));
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
  showUnread();
}

function showUnread() {
  if (!shell) return;
  const tab = shell.tabbar.children[1];
  const old = tab.querySelector('.badge');
  if (old) old.remove();
  if (state.unread > 0) tab.append(h('span', {class: 'badge'}, state.unread > 9 ? '9+' : String(state.unread)));
}

// Adresse ersetzen statt eine neue anzulegen (z. B. nach dem Speichern: „Zurück“ soll nicht wieder ins Formular führen)
function replaceRoute(hash) {
  history.replaceState(null, '', hash);
  if (visited.length) visited[visited.length - 1] = location.hash;
  return render();
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

// Daten holen und für „ohne Netz“ merken: Klappt die Anfrage nicht, weil keine Verbindung da ist, kommt der letzte Stand.
async function cached(key, fetcher) {
  const k = `cache.${state.group.id}.${key}`;
  try {
    const value = await fetcher();
    store.set(k, value);
    return value;
  } catch (e) {
    const old = store.get(k, null);
    if (e && e.offline && old != null) { state.offline = true; return old; }
    throw e;
  }
}

async function render(fresh) {
  closeSheet();
  document.querySelectorAll('.viewer').forEach(v => v.remove());   // offene Großansicht eines Bildes
  if (!shell) buildShell();
  const {path, query} = parseHash();
  let m = null;
  route = null;
  for (const r of routes) if ((m = path.match(r.re))) { route = r; break; }
  if (!route) { location.hash = '#/'; return; }
  const seq = ++renderSeq;
  for (const fn of pageCleanup) { try { fn(); } catch (e) { /* Seite räumt nur auf */ } }
  pageCleanup = [];
  pageLive = new Set();
  state.offline = false;
  shell.tabbar.querySelectorAll('a').forEach((a, i) => a.classList.toggle('on', i === route.nav));
  shell.left.classList.toggle('spacer', !route.detail);
  shell.right.classList.toggle('spacer', !!route.detail);
  shell.top.classList.toggle('always', !!route.detail);
  document.body.classList.toggle('st-chat', !!route.chat);
  shell.tt.textContent = '';
  const main = h('main');
  shell.main.replaceWith(main);
  shell.main = main;
  const me = state.members.find(x => x.id === state.group.me.id) || state.group.me;
  const ctx = {
    main, params: m.slice(1), query, fresh: !!fresh,
    be: state.be, group: state.group, groups: state.groups, me, members: state.members, isAdmin: me.role === 'admin',
    member: id => state.members.find(x => x.id === id) || {id, name: 'Unbekannt', removed_at: 'x'},
    url: path => state.be.url(path).catch(() => null),
    current: () => seq === renderSeq,
    title: t => { if (seq === renderSeq) { shell.tt.textContent = t; document.title = t ? t + ' – STAMMTISCH' : 'STAMMTISCH'; } },
    rerender: () => render(true),
    replace: replaceRoute,
    cached,
    offline: () => state.offline,
    onLive: fn => pageLive.add(fn),              // Änderungen anderer, solange diese Seite offen ist
    onLeave: fn => pageCleanup.push(fn),         // aufräumen beim Verlassen (Aufnahme stoppen, Zeitgeber …)
    refreshMembers, refreshGroups, switchGroup, showWelcome, signOut,
    chatSeen: () => { state.unread = 0; store.set('read.' + state.group.id, new Date().toISOString()); showUnread(); },
    store,
  };
  try {
    const mod = await pages[route.page]();
    if (seq !== renderSeq) return;
    await mod[route.fn || 'render'](ctx);
  } catch (e) {
    if (seq !== renderSeq) return;
    main.replaceChildren(errorBox(e, () => render(true)));
  }
  if (seq !== renderSeq) return;
  if (state.offline && !main.querySelector('.offline')) main.prepend(h('div', {class: 'offline'}, 'Keine Verbindung – das ist der letzte gespeicherte Stand'));
  const key = location.hash;
  if (scrollPos.has(key) && restoring) window.scrollTo(0, scrollPos.get(key));
  restoring = false;
}

export function errorBox(e, retry) {
  return h('div', null,
    notice('err', 'Das hat nicht geklappt', (e && e.message) || String(e)),
    retry ? button('Nochmal versuchen', {icon: 'refresh', block: true, onclick: retry}) : null);
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

// ---- Live: Änderungen der anderen -----------------------------------------------------------------------------------
// Verbunden nur, solange die App sichtbar ist. Liegt sie länger als eine halbe Minute im Hintergrund, wird getrennt –
// beim Zurückkommen neu verbunden und die Seite einmal frisch geladen. Kein Abfragen im Takt.
let unsubscribe = null, hideTimer = 0, memberTimer = 0;

function connectLive() {
  if (unsubscribe || !state.group) return;
  unsubscribe = state.be.subscribe(state.group.id, onLiveEvent);
}
function disconnectLive() {
  if (unsubscribe) { unsubscribe(); unsubscribe = null; }
}
function onLiveEvent(ev) {
  if (ev.table === 'members') {
    clearTimeout(memberTimer);
    memberTimer = setTimeout(() => refreshMembers().catch(() => {}), 300);
  } else if (ev.table === 'groups' && ev.row && ev.row.id === state.group.id) {
    state.group.name = ev.row.name;
  } else if (ev.table === 'messages' && ev.row && !(route && route.chat)) {
    const r = ev.row;
    const isNew = !r.deleted_at && !r.edited_at && Date.now() - new Date(r.created_at).getTime() < 60000;
    if (isNew && r.member_id !== state.group.me.id) { state.unread++; showUnread(); }
  }
  for (const fn of pageLive) { try { fn(ev); } catch (e) { console.error(e); } }
}
document.addEventListener('visibilitychange', () => {
  if (!state.group) return;
  if (document.hidden) {
    hideTimer = setTimeout(disconnectLive, 30000);
  } else {
    clearTimeout(hideTimer);
    if (!unsubscribe) {
      connectLive();
      if (shell && route && !route.form) render(true);
    }
  }
});

// ---- Gruppen ------------------------------------------------------------------------------------------------------------
async function refreshMembers() {
  state.members = await cached('members', () => state.be.members(state.group.id));
  const me = state.members.find(x => x.id === state.group.me.id);
  if (me) Object.assign(state.group.me, {name: me.name, role: me.role, avatar_path: me.avatar_path});
  return state.members;
}

async function refreshGroups() {
  state.groups = await state.be.myGroups();
  store.set('groups', state.groups);
  const mine = state.groups.find(g => g.id === (state.group && state.group.id));
  if (mine) state.group = mine;
  return state.groups;
}

async function enterGroup(gid) {
  disconnectLive();
  state.group = state.groups.find(g => g.id === gid) || state.groups[0];
  store.set('group', state.group.id);
  state.unread = 0;
  try {
    await refreshMembers();
  } catch (e) {
    root.replaceChildren(h('div', {class: 'welcome'}, errorBox(e, () => enterGroup(gid))));
    return;
  }
  connectLive();
  shell = null;
  visited = [location.hash];
  window.removeEventListener('hashchange', onHash);
  window.addEventListener('hashchange', onHash);
  await render();
  afterStart();
}

function switchGroup(gid) {
  if (location.hash && location.hash !== '#/') history.replaceState(null, '', location.pathname + location.search + '#/');
  return enterGroup(gid);
}

// Nach dem Start, ohne darauf zu warten: neue Chat-Nachrichten zählen, höchstens einmal am Tag den Papierkorb leeren
async function afterStart() {
  const gid = state.group.id;
  try {
    const since = store.get('read.' + gid, null);
    if (since && !(route && route.chat)) {
      const recent = await state.be.messages(gid, {limit: 30});
      if (state.group.id !== gid) return;
      state.unread = recent.filter(m => m.created_at > since && m.member_id !== state.group.me.id && !m.deleted_at).length;
      showUnread();
    } else if (!since) {
      store.set('read.' + gid, new Date().toISOString());
    }
    const today = new Date().toISOString().slice(0, 10);
    if (store.get('purge.' + gid, '') !== today) {
      store.set('purge.' + gid, today);
      await state.be.purge(gid);
    }
  } catch (e) { /* nicht wichtig genug für eine Meldung */ }
}

async function signOut() {
  disconnectLive();
  await state.be.signOut();
  for (const k of Object.keys(localStorage)) if (k.startsWith('st.') && k !== 'st.theme') localStorage.removeItem(k);
  state.groups = [];
  state.group = null;
  showWelcome({});
}

// ---- Start --------------------------------------------------------------------------------------------------------------
async function showWelcome({pending = '', signedIn = false} = {}) {
  window.removeEventListener('hashchange', onHash);
  shell = null;
  closeSheet();
  document.body.classList.remove('st-chat');
  const mod = await import('./pages/welcome.js');
  mod.renderWelcome(root, {
    be: state.be, pending, signedIn, canCancel: state.groups.length > 0,
    onCancel: () => enterGroup(store.get('group', '')),
    onDone: async gid => {
      sessionStorage.removeItem('st.pending');
      await refreshGroups();
      await enterGroup(gid);
    },
  });
}

async function start() {
  if ('serviceWorker' in navigator && window.isSecureContext && !isDemo()) navigator.serviceWorker.register('sw.js').catch(() => {});

  // Einladungs- oder Gerätelink (…/#einladung=XXXXX-XXXXX): Code merken, Adresse aufräumen
  if (/^#(einladung|geraet)=/.test(location.hash)) {
    sessionStorage.setItem('st.pending', parseCodeInput(location.hash));
    history.replaceState(null, '', location.pathname + location.search);
  }
  const pending = sessionStorage.getItem('st.pending') || '';

  let signedIn = false;
  try {
    state.be = await backend();
    signedIn = await state.be.init();
  } catch (e) {
    root.replaceChildren(h('div', {class: 'welcome'}, errorBox(e, start)));
    return;
  }
  if (signedIn) {
    try {
      state.groups = await state.be.myGroups();
      store.set('groups', state.groups);
    } catch (e) {
      if (!e.offline) { root.replaceChildren(h('div', {class: 'welcome'}, errorBox(e, start))); return; }
      state.groups = store.get('groups', []);
    }
  }
  if (!signedIn || !state.groups.length || pending) return showWelcome({pending, signedIn});
  await enterGroup(store.get('group', ''));
}

start();
