// FragDesk Handy-App – kleine DOM-Helfer und Bausteine (ohne Framework)

// h('div', {class: 'x', onclick: fn}, 'Text', kind, …) – Kinder: Knoten, Text, Arrays, null/false (übersprungen)
export function h(tag, props, ...kids) {
  const el = document.createElement(tag);
  if (props) {
    for (const [k, v] of Object.entries(props)) {
      if (v == null || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
      else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else if (k === 'html') el.innerHTML = v;   // nur für eigene, feste Texte
      else el.setAttribute(k, v === true ? '' : v);
    }
  }
  add(el, kids);
  return el;
}

function add(el, kids) {
  for (const k of kids) {
    if (k == null || k === false) continue;
    if (Array.isArray(k)) add(el, k);
    else el.append(k instanceof Node ? k : document.createTextNode(String(k)));
  }
}

// ---- Symbole (eigene, einfache Linienzeichnungen; 24×24) -------------------------------------------------------
const P = {
  home: 'M4 11 12 4l8 7v9h-5v-6H9v6H4z',
  list: 'M9 7h11M9 12h11M9 17h11M4.5 7h.5M4.5 12h.5M4.5 17h.5',
  stats: 'M5 20v-7M11 20V5M17 20V10M3 20h18',
  film: 'M4 6h16v12H4zM10 9.5l5 2.5-5 2.5z',
  sliders: 'M4 7h9M17 7h3M4 17h3M11 17h9M15 5v4M9 15v4',
  back: 'M15 5l-7 7 7 7',
  chev: 'M9 6l6 6-6 6',
  down: 'M6 9l6 6 6-6',
  save: 'M12 4v11M7 10l5 5 5-5M5 20h14',
  share: 'M12 15V3M8 7l4-4 4 4M6 11v9h12v-9',
  crosshair: 'M12 5a7 7 0 1 0 0 14 7 7 0 0 0 0-14zM12 2v5M12 17v5M2 12h5M17 12h5',
  target: 'M12 4a8 8 0 1 0 0 16 8 8 0 0 0 0-16zM12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8zM12 11.5v1',
  shield: 'M12 3l7 3v5c0 4.5-3 8-7 10-4-2-7-5.5-7-10V6z',
  trophy: 'M8 4h8v5a4 4 0 0 1-8 0zM8 6H5v1a3 3 0 0 0 3 3M16 6h3v1a3 3 0 0 1-3 3M12 13v4M8 20h8M10 17h4',
  flash: 'M13 3 5 13h6l-1 8 8-10h-6z',
  close: 'M6 6l12 12M18 6 6 18',
  info: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM12 11v6M12 7.5v.5',
  warning: 'M12 4 2.5 20h19zM12 10v4.5M12 17.5v.5',
  refresh: 'M20 11a8 8 0 1 0-2.3 5.7M20 5v6h-6',
  spark: 'M12 3v4M12 17v4M3 12h4M17 12h4M6 6l2.5 2.5M15.5 15.5 18 18M6 18l2.5-2.5M15.5 8.5 18 6',
  link: 'M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1',
  phone: 'M8 2h8a2 2 0 0 1 2 2v16a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2zM11 18h2',
  clock: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM12 7v5l3 2',
  map: 'M9 4 3 6v14l6-2 6 2 6-2V4l-6 2zM9 4v14M15 6v14',
  plus: 'M12 5v14M5 12h14',
  bomb: 'M10 7a7 7 0 1 0 4 0M12 7V4h3',
  fire: 'M12 21c-4 0-6-3-6-6 0-4 4-6 4-10 3 2 4 4 4 6 1-1 2-2 2-4 2 2 4 5 4 8 0 3-3 6-8 6z',
  smoke: 'M7 18a4 4 0 0 1 0-8 5 5 0 0 1 9.5-1.5A4 4 0 0 1 17 18z',
  users: 'M9 11a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7zM3 20c0-3.3 2.7-6 6-6s6 2.7 6 6M16 4.5a3.5 3.5 0 0 1 0 6.5M18 14c2 .7 3 2.6 3 6',
  eye: 'M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12zM12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6z',
  coin: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM15 9.5c-.5-1-1.6-1.5-3-1.5-1.7 0-3 .8-3 2s1.3 1.6 3 2 3 .8 3 2-1.3 2-3 2c-1.4 0-2.5-.5-3-1.5M12 6v2M12 16v2',
};
const FILLED = {
  play: 'M8 5v14l11-7z',
  star: 'M12 3.5l2.6 5.3 5.9.9-4.3 4.1 1 5.8L12 16.9l-5.2 2.7 1-5.8-4.3-4.1 5.9-.9z',
};

export function icon(name, cls) {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('aria-hidden', 'true');
  if (cls) svg.setAttribute('class', cls);
  const path = document.createElementNS(ns, 'path');
  if (FILLED[name]) {
    path.setAttribute('d', FILLED[name]);
    path.setAttribute('fill', 'currentColor');
  } else {
    path.setAttribute('d', P[name] || P.info);
    path.setAttribute('fill', 'none');
    path.setAttribute('stroke', 'currentColor');
    path.setAttribute('stroke-width', '2');
    path.setAttribute('stroke-linecap', 'round');
    path.setAttribute('stroke-linejoin', 'round');
  }
  svg.append(path);
  return svg;
}

// ---- Bausteine ---------------------------------------------------------------------------------------------------
// Karte mit Kopf: {title, sub, more: Text, href | onMore, flush: true für Listen ohne Innenabstand}
export function card(opts, ...body) {
  const o = opts || {};
  const head = o.title
    ? h('div', {class: 'card-head'},
        h('div', {class: 'h'}, h('b', null, o.title), o.sub ? h('small', null, o.sub) : null),
        o.more ? h('a', {class: 'more', href: o.href || null, onclick: o.onMore || null}, o.more, icon('chev')) : null)
    : null;
  return h('section', {class: 'card' + (o.flush ? ' flush' : '') + (o.class ? ' ' + o.class : '')}, head, body);
}

// Waagerecht scrollende Auswahl: labels, gewählter Index, pick(i)
export function chips(labels, selected, pick, extra) {
  const row = h('div', {class: 'chips'});
  labels.forEach((l, i) => {
    const b = h('button', {class: 'chip' + (i === selected ? ' on' : ''), type: 'button'}, l);
    b.addEventListener('click', () => pick(i));
    row.append(b);
  });
  if (extra) row.append(extra);
  requestAnimationFrame(() => {
    const on = row.querySelector('.on');
    if (on && on.offsetLeft + on.offsetWidth > row.clientWidth) row.scrollLeft = on.offsetLeft - 16;
  });
  return row;
}

// Auswahl als Chip (natives Auswahlmenü – auf dem iPhone das Rad): options [{value, label}]
export function selectChip(options, value, change, emptyValue = '') {
  const sel = h('select', {'aria-label': 'Auswahl'});
  for (const o of options) sel.append(h('option', {value: o.value, selected: o.value === value || null}, o.label));
  sel.addEventListener('change', () => change(sel.value));
  return h('label', {class: 'selchip' + (value !== emptyValue ? ' on' : '')}, sel, icon('down'));
}

// Kennzahl-Kachel; Tippen zeigt die Erklärung
export function statTile(s) {
  const b = h('button', {class: 'stat', type: 'button'},
    s.icon ? icon(s.icon, 'ic') : null,
    h('div', {class: 'l'}, s.label),
    h('div', {class: 'v ' + toneClass(s.tone)}, s.value),
    s.sub ? h('div', {class: 's'}, s.sub) : null);
  b.addEventListener('click', () => sheet({title: s.label, text: s.hint || '', rows: [['Wert', s.value]].concat(s.sub ? [['', s.sub]] : [])}));
  return b;
}

export function toneClass(t) {
  if (!t) return '';
  if (t === 'muted') return 'tone-muted';
  return t;   // good, bad, win, loss, draw
}

// Balkenliste: items [{name/label, value, sub, frac, color, tip}], onClick(item)
export function bars(items, onClick) {
  const box = h('div', {class: 'bars'});
  for (const it of items) {
    const el = h(onClick ? 'a' : 'div', {class: 'bar'},
      h('div', {class: 'bt'}, it.color ? h('span', {class: 'dot', style: {background: it.color}}) : null,
        h('b', null, it.name || it.label), h('span', null, it.value)),
      it.sub ? h('small', null, it.sub) : null,
      h('div', {class: 'track'}, h('div', {class: 'fill', style: {width: Math.max(0, Math.min(1, it.frac || 0)) * 100 + '%', background: it.color || null}})));
    if (onClick) el.addEventListener('click', () => onClick(it));
    box.append(el);
  }
  return box;
}

export function emptyState(e, iconName = 'info') {
  return h('div', {class: 'empty'}, icon(iconName), h('b', null, e.title), e.text ? h('p', null, e.text) : null);
}

export function notice(kind, title, text, iconName) {
  return h('div', {class: 'notice ' + kind}, icon(iconName || (kind === 'info' ? 'info' : 'warning')),
    h('div', null, title ? h('b', null, title) : null, text));
}

export function spinner() { return h('div', {class: 'spinner'}); }

export function skeleton(...heights) {
  return heights.map(px => h('div', {class: 'skel', style: {height: px + 'px'}}));
}

// Seitenreiter (bleiben oben kleben)
export function tabs(labels, selected, pick) {
  const bar = h('div', {class: 'tabs', role: 'tablist'});
  labels.forEach((l, i) => {
    const b = h('button', {class: i === selected ? 'on' : '', type: 'button', role: 'tab'}, l);
    b.addEventListener('click', () => pick(i));
    bar.append(b);
  });
  requestAnimationFrame(() => {
    const on = bar.querySelector('.on');
    if (on) bar.scrollLeft = Math.max(0, on.offsetLeft - (bar.clientWidth - on.offsetWidth) / 2);
  });
  return bar;
}

// ---- Blatt von unten (statt Tooltips) ----------------------------------------------------------------------------
let openSheet = null;
export function sheet({title, text, rows, body}) {
  closeSheet();
  const bg = h('div', {class: 'sheet-bg'});
  const panel = h('div', {class: 'sheet', role: 'dialog', 'aria-modal': 'true'},
    h('div', {class: 'grab'}),
    title ? h('h3', null, title) : null,
    text ? h('p', null, text) : null,
    (rows || []).filter(r => r && (r[0] || r[1])).map(([k, v]) => h('div', {class: 'kv'}, h('span', null, k), h('b', null, v))),
    body || null);
  bg.addEventListener('click', closeSheet);
  document.body.append(bg, panel);
  requestAnimationFrame(() => { bg.classList.add('show'); panel.classList.add('show'); });
  openSheet = {bg, panel};
  return panel;
}

export function closeSheet() {
  if (!openSheet) return;
  const {bg, panel} = openSheet;
  openSheet = null;
  bg.classList.remove('show');
  panel.classList.remove('show');
  setTimeout(() => { bg.remove(); panel.remove(); }, 260);
}

// ---- Kurzmeldung ---------------------------------------------------------------------------------------------------
let toastTimer = 0;
export function toast(msg, err) {
  let el = document.querySelector('.toast');
  if (!el) {
    el = h('div', {class: 'toast', role: 'status'});
    document.body.append(el);
  }
  el.textContent = msg;
  el.classList.toggle('err', !!err);
  requestAnimationFrame(() => el.classList.add('show'));
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove('show'), 3600);
}

// Sterne 1–5 (wie in FragDesk bei den Momenten)
export function stars(n) {
  const box = h('span', {class: 'stars', 'aria-label': n + ' von 5'});
  for (let i = 0; i < 5; i++) box.append(icon('star', i < n ? 'on' : ''));
  return box;
}

// ---- Zahlen und Zeiten ------------------------------------------------------------------------------------------
const GiB = 1024 ** 3;
// Bytes wie in FragDesk: 870 KB, 87 MB, 3,2 GB (ganze GB ohne Komma, z. B. für Limits)
export function fmtBytes(n) {
  if (!(n >= 0)) return '–';
  if (n < 1024 * 1024) return Math.ceil(n / 1024).toLocaleString('de-DE') + ' KB';
  if (n < GiB) return Math.round(n / (1024 * 1024)).toLocaleString('de-DE') + ' MB';
  if (n % GiB === 0) return (n / GiB).toLocaleString('de-DE') + ' GB';
  return (n / GiB).toLocaleString('de-DE', {minimumFractionDigits: 1, maximumFractionDigits: 1}) + ' GB';
}

// Heute, 21:13 / Gestern, 21:13 / 06.11., 21:13 / 06.11.2025 (Sekunden seit 1970)
export function fmtDay(epoch) {
  if (!epoch) return '–';
  const d = new Date(epoch * 1000), now = new Date();
  const two = x => String(x).padStart(2, '0');
  const time = two(d.getHours()) + ':' + two(d.getMinutes());
  const day = x => new Date(x.getFullYear(), x.getMonth(), x.getDate()).getTime();
  const diff = Math.round((day(now) - day(d)) / 86400000);
  if (diff === 0) return 'Heute, ' + time;
  if (diff === 1) return 'Gestern, ' + time;
  if (d.getFullYear() === now.getFullYear()) return two(d.getDate()) + '.' + two(d.getMonth() + 1) + '., ' + time;
  return two(d.getDate()) + '.' + two(d.getMonth() + 1) + '.' + d.getFullYear();
}

// Speicher als Balken: belegt von Limit (0 = unbegrenzt)
export function usageBar(used, limit, label = 'Speicher') {
  const frac = limit > 0 ? Math.min(1, used / limit) : 0;
  const text = limit > 0 ? `${fmtBytes(used)} von ${fmtBytes(limit)}` : `${fmtBytes(used)} · unbegrenzt`;
  return h('div', {class: 'bar usage' + (frac >= 0.9 ? ' full' : '')},
    h('div', {class: 'bt'}, label ? h('b', null, label) : null, h('span', null, text)),
    limit > 0 ? h('div', {class: 'track'}, h('div', {class: 'fill', style: {width: Math.max(frac, used > 0 ? 0.01 : 0) * 100 + '%'}})) : null);
}

// Text in die Zwischenablage (mit Rückfall für ältere Browser); true = geklappt
export async function copyText(text, input) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch (e) {
    if (!input) return false;
    input.focus();
    input.select();
    try { return document.execCommand('copy'); } catch (e2) { return false; }
  }
}

// Eigenes Stylesheet einer Seite einmal einbinden (z. B. useCss('css/match.css'))
export function useCss(href) {
  if (document.querySelector(`link[data-fd="${href}"]`)) return;
  document.head.append(h('link', {rel: 'stylesheet', href, 'data-fd': href}));
}

export const isIOS = /iP(hone|ad|od)/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
export const standalone = window.matchMedia('(display-mode: standalone)').matches || navigator.standalone === true;
