// STAMMTISCH – kleine DOM-Helfer und Bausteine (ohne Framework, wie in der Handy-App von FragDesk)
import {fmtBytes, initials, colorFor} from './util.js';

// h('div', {class: 'x', onclick: fn}, 'Text', kind, …) – Kinder: Knoten, Text, Arrays, null/false (übersprungen)
export function h(tag, props, ...kids) {
  const el = document.createElement(tag);
  if (props) {
    for (const [k, v] of Object.entries(props)) {
      if (v == null || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'style' && typeof v === 'object') Object.assign(el.style, v);
      else if (k.startsWith('on') && typeof v === 'function') el.addEventListener(k.slice(2), v);
      else if (k === 'value') el.value = v;
      else el.setAttribute(k, v === true ? '' : v);
    }
  }
  add(el, kids);
  return el;
}

// Inhalt eines Elements ersetzen – wie replaceChildren, aber null/false und Arrays sind erlaubt (replaceChildren würde
// aus null den Text „null“ machen)
export function fill(el, ...kids) {
  el.replaceChildren();
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
  calendar: 'M5 5h14a1 1 0 0 1 1 1v13a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V6a1 1 0 0 1 1-1zM4 10h16M8 3v4M16 3v4',
  chat: 'M5 5h14a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-7l-5 4v-4H5a2 2 0 0 1-2-2V7a2 2 0 0 1 2-2z',
  image: 'M5 4h14a1 1 0 0 1 1 1v14a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1zM4 16l5-5 4 4 3-3 4 4M15.5 8.5h.01',
  sliders: 'M4 7h9M17 7h3M4 17h3M11 17h9M15 5v4M9 15v4',
  back: 'M15 5l-7 7 7 7',
  chev: 'M9 6l6 6-6 6',
  down: 'M6 9l6 6 6-6',
  plus: 'M12 5v14M5 12h14',
  close: 'M6 6l12 12M18 6 6 18',
  check: 'M5 12.5l4.5 4.5L19 7',
  pin: 'M12 21s-6.5-6-6.5-11a6.5 6.5 0 0 1 13 0c0 5-6.5 11-6.5 11zM12 7.5a2.5 2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5z',
  beer: 'M6 8h9v10a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2zM15 11h2a2 2 0 0 1 2 2v1a2 2 0 0 1-2 2h-2M9 12v4M12 12v4M6 8a2.5 2.5 0 0 1 1.5-4.5 3.2 3.2 0 0 1 6 0A2.5 2.5 0 0 1 15 8',
  house: 'M4 11 12 4l8 7v9h-5v-6H9v6H4z',
  map: 'M9 4 3 6v14l6-2 6 2 6-2V4l-6 2zM9 4v14M15 6v14',
  clock: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM12 7v5l3 2',
  users: 'M9 11a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7zM3 20c0-3.3 2.7-6 6-6s6 2.7 6 6M16 4.5a3.5 3.5 0 0 1 0 6.5M18 14c2 .7 3 2.6 3 6',
  edit: 'M4 20h4L19 9l-4-4L4 16zM13.5 6.5l4 4',
  trash: 'M5 7h14M10 7V4h4v3M7 7l1 13h8l1-13M10 11v5M14 11v5',
  send: 'M5 12 20 5l-5 15-3-6.5zM12 13.5 20 5',
  mic: 'M12 3a3 3 0 0 0-3 3v5a3 3 0 0 0 6 0V6a3 3 0 0 0-3-3zM6 11a6 6 0 0 0 12 0M12 17v4',
  reply: 'M10 7 5 12l5 5M5 12h9a5 5 0 0 1 5 5v1',
  smile: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM8.5 14a4.5 4.5 0 0 0 7 0M9 9.5v.5M15 9.5v.5',
  copy: 'M9 9h10v11H9zM5 15V4h10',
  more: 'M5 12h.01M12 12h.01M19 12h.01',
  undo: 'M9 6 4 11l5 5M4 11h10a5 5 0 0 1 0 10h-3',
  save: 'M12 4v11M7 10l5 5 5-5M5 20h14',
  video: 'M4 6h11v12H4zM15 10l5-3v10l-5-3',
  camera: 'M4 8h3l2-3h6l2 3h3v11H4zM12 10.5a3 3 0 1 0 0 6 3 3 0 0 0 0-6z',
  logout: 'M10 5H5v14h5M14 8l4 4-4 4M18 12H9',
  shield: 'M12 3l7 3v5c0 4.5-3 8-7 10-4-2-7-5.5-7-10V6z',
  key: 'M15.5 3a5.5 5.5 0 1 0 0 11 5.5 5.5 0 0 0 0-11zM11.6 12.4 3 21M5.5 18.5 8 21M8.5 15.5 11 18M15.5 7v.5',
  phone: 'M8 2h8a2 2 0 0 1 2 2v16a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2zM11 18h2',
  link: 'M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1',
  share: 'M12 15V3M8 7l4-4 4 4M6 11v9h12v-9',
  refresh: 'M20 11a8 8 0 1 0-2.3 5.7M20 5v6h-6',
  info: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM12 11v6M12 7.5v.5',
  warning: 'M12 4 2.5 20h19zM12 10v4.5M12 17.5v.5',
  external: 'M14 4h6v6M20 4l-9 9M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5',
  ban: 'M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM5.6 5.6l12.8 12.8',
  search: 'M11 4a7 7 0 1 0 0 14 7 7 0 0 0 0-14zM16 16l4.5 4.5',
  history: 'M4 12a8 8 0 1 0 2.4-5.7M4 5v5h5M12 8v4.5l3 1.5',
  route: 'M6 19a2 2 0 1 0 0-4 2 2 0 0 0 0 4zM18 9a2 2 0 1 0 0-4 2 2 0 0 0 0 4zM8 17h6a3 3 0 0 0 0-6h-4a3 3 0 0 1 0-6h6',
  group: 'M12 12a3 3 0 1 0 0-6 3 3 0 0 0 0 6zM6 19c0-3 2.7-5 6-5s6 2 6 5M5 9.5a2 2 0 1 0 0-4M19 9.5a2 2 0 1 1 0-4',
};
const FILLED = {
  play: 'M8 5v14l11-7z',
  pause: 'M7 5h4v14H7zM13 5h4v14h-4z',
  stop: 'M7 7h10v10H7z',
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
    path.setAttribute('stroke-width', name === 'more' ? '3' : '2');
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

// Zeile in einer Liste: Symbol/Bild links, Titel + Unterzeile, rechts Wert oder Pfeil. opts: {icon, lead, title, sub, value, href, onclick, danger, chev}
export function row(opts) {
  const o = opts || {};
  const tag = o.href ? 'a' : o.onclick ? 'button' : 'div';
  return h(tag, {class: 'row' + (o.danger ? ' danger' : ''), href: o.href || null, type: tag === 'button' ? 'button' : null, onclick: o.onclick || null},
    o.lead || (o.icon ? h('span', {class: 'row-ic'}, icon(o.icon)) : null),
    h('span', {class: 'row-main'}, h('b', null, o.title), o.sub ? h('small', null, o.sub) : null),
    o.value != null ? h('span', {class: 'row-val'}, o.value) : null,
    (o.href || o.onclick) && o.chev !== false ? icon('chev', 'row-chev') : null);
}

export function emptyState(e, iconName = 'info') {
  return h('div', {class: 'empty'}, icon(iconName), h('b', null, e.title), e.text ? h('p', null, e.text) : null, e.action || null);
}

export function notice(kind, title, text, iconName) {
  return h('div', {class: 'notice ' + kind}, icon(iconName || (kind === 'info' ? 'info' : 'warning')),
    h('div', null, title ? h('b', null, title) : null, text));
}

export function spinner() { return h('div', {class: 'spinner'}); }

export function skeleton(...heights) {
  return heights.map(px => h('div', {class: 'skel', style: {height: px + 'px'}}));
}

export function button(label, opts = {}) {
  const b = h('button', {class: 'btn' + (opts.primary ? ' primary' : '') + (opts.danger ? ' danger' : '') + (opts.block ? ' block' : '') + (opts.small ? ' small' : ''),
    type: opts.type || 'button', disabled: opts.disabled || null}, opts.icon ? icon(opts.icon) : null, label);
  if (opts.onclick) b.addEventListener('click', opts.onclick);
  return b;
}

// Knopf sperren, solange die Aktion läuft; Fehler als Kurzmeldung. Gibt true zurück, wenn es geklappt hat.
export async function busy(btn, fn) {
  if (btn && btn.disabled) return false;
  if (btn) { btn.disabled = true; btn.classList.add('busy'); }
  try {
    await fn();
    return true;
  } catch (e) {
    toast((e && e.message) || String(e), true);
    return false;
  } finally {
    if (btn) { btn.disabled = false; btn.classList.remove('busy'); }
  }
}

// Profilbild: Foto, sonst Anfangsbuchstaben auf fester Farbe. urlFor(path) liefert (irgendwann) die Bildadresse.
export function avatar(member, urlFor, size = 36) {
  const m = member || {};
  const el = h('span', {class: 'avatar' + (m.removed_at ? ' gone' : ''), title: m.name || '',
    style: {width: size + 'px', height: size + 'px', fontSize: Math.round(size * 0.4) + 'px', background: colorFor(m.id)}}, initials(m.name));
  if (m.avatar_path && urlFor) {
    Promise.resolve(urlFor(m.avatar_path)).then(url => {
      if (!url) return;
      const img = h('img', {alt: '', crossorigin: 'anonymous', loading: 'lazy'});
      img.addEventListener('load', () => { el.textContent = ''; el.append(img); });
      img.src = url;
    }).catch(() => {});
  }
  return el;
}

// Formularfeld mit Beschriftung
export function field(label, input, hint) {
  return h('label', {class: 'field'}, h('span', null, label), input, hint ? h('small', null, hint) : null);
}

// Umschalter mit zwei bis vier Möglichkeiten: options [{value, label, icon}], pick(value)
export function segmented(options, value, pick, cls = '') {
  const box = h('div', {class: 'seg ' + cls, role: 'group'});
  for (const o of options) {
    const b = h('button', {type: 'button', class: (o.value === value ? 'on ' : '') + (o.tone || ''), 'data-value': o.value}, o.icon ? icon(o.icon) : null, o.label);
    b.addEventListener('click', () => pick(o.value, b));
    box.append(b);
  }
  return box;
}

// ---- Blatt von unten ---------------------------------------------------------------------------------------------
let openSheet = null;
export function sheet({title, text, rows, body, onClose}) {
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
  openSheet = {bg, panel, onClose};
  return panel;
}

export function closeSheet() {
  if (!openSheet) return;
  const {bg, panel, onClose} = openSheet;
  openSheet = null;
  bg.classList.remove('show');
  panel.classList.remove('show');
  setTimeout(() => { bg.remove(); panel.remove(); }, 260);
  if (onClose) onClose();
}

// Auswahl: actions [{label, icon, danger, run}] – tippt man eine an, schließt das Blatt und run() läuft
export function actionSheet(title, actions, head) {
  const list = h('div', {class: 'actions'});
  for (const a of actions.filter(Boolean)) {
    const b = h('button', {type: 'button', class: 'action' + (a.danger ? ' danger' : '')}, icon(a.icon || 'chev'), h('span', null, a.label));
    b.addEventListener('click', () => { closeSheet(); a.run(); });
    list.append(b);
  }
  return sheet({title, body: [head || null, list]});
}

// Rückfrage: ergibt true (bestätigt) oder false (abgebrochen/weggetippt)
export function confirmSheet({title, text, ok = 'OK', danger}) {
  return new Promise(resolve => {
    let done = false;
    const finish = v => { if (!done) { done = true; resolve(v); } };
    const yes = button(ok, {primary: !danger, danger, block: true, onclick: () => { finish(true); closeSheet(); }});
    const no = button('Abbrechen', {block: true, onclick: () => closeSheet()});
    sheet({title, text, body: h('div', {class: 'stack'}, yes, no), onClose: () => finish(false)});
  });
}

// Eingabe: ergibt den Text oder null (abgebrochen)
export function promptSheet({title, text, label, value = '', placeholder = '', ok = 'Speichern', maxLength = 80, multiline}) {
  return new Promise(resolve => {
    let done = false;
    const finish = v => { if (!done) { done = true; resolve(v); } };
    const input = multiline
      ? h('textarea', {rows: 4, maxlength: maxLength, placeholder}, value)
      : h('input', {type: 'text', maxlength: maxLength, placeholder, value, autocomplete: 'off', enterkeyhint: 'done'});
    const form = h('form', {class: 'stack'}, field(label || '', input), button(ok, {primary: true, block: true, type: 'submit'}));
    form.addEventListener('submit', ev => { ev.preventDefault(); finish(input.value.trim()); closeSheet(); });
    sheet({title, text, body: form, onClose: () => finish(null)});
    setTimeout(() => { input.focus(); if (input.select) input.select(); }, 280);
  });
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
  toastTimer = setTimeout(() => el.classList.remove('show'), err ? 5200 : 3200);
}

// Speicher als Balken: belegt von Limit
export function usageBar(used, limit, label = 'Speicher') {
  const frac = limit > 0 ? Math.min(1, used / limit) : 0;
  return h('div', {class: 'bar usage' + (frac >= 0.9 ? ' full' : '')},
    h('div', {class: 'bt'}, label ? h('b', null, label) : null, h('span', null, `${fmtBytes(used)} von ${fmtBytes(limit)}`)),
    h('div', {class: 'track'}, h('div', {class: 'fill', style: {width: Math.max(frac, used > 0 ? 0.01 : 0) * 100 + '%'}})));
}

// Text in die Zwischenablage (mit Rückfall für ältere Browser); true = geklappt
export async function copyText(text) {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch (e) {
    const ta = h('textarea', {style: {position: 'fixed', opacity: '0'}}, text);
    document.body.append(ta);
    ta.select();
    let ok = false;
    try { ok = document.execCommand('copy'); } catch (e2) { /* geht nicht */ }
    ta.remove();
    return ok;
  }
}

// Teilen über das Menü des Handys; ohne diese Möglichkeit: kopieren
export async function shareText(title, text) {
  if (navigator.share) {
    try { await navigator.share({title, text}); return; } catch (e) { if (e && e.name === 'AbortError') return; }
  }
  toast((await copyText(text)) ? 'Kopiert' : 'Kopieren hat nicht geklappt', false);
}

// Eigenes Stylesheet einer Seite einmal einbinden
export function useCss(href) {
  if (document.querySelector(`link[data-st="${href}"]`)) return;
  document.head.append(h('link', {rel: 'stylesheet', href, 'data-st': href}));
}

const ua = typeof navigator !== 'undefined' ? navigator : {userAgent: '', platform: '', maxTouchPoints: 0};
export const isIOS = /iP(hone|ad|od)/.test(ua.userAgent) || (ua.platform === 'MacIntel' && ua.maxTouchPoints > 1);
export const standalone = typeof window !== 'undefined' && (window.matchMedia('(display-mode: standalone)').matches || ua.standalone === true);
