// FragDesk Handy-App – ein Match (wie page_match.cpp): Übersicht, Scoreboard, Runden, Waffen, Duelle, Utility, Momente.
// Jeder Bereich ist eine eigene Anfrage (/api/v1/match/<Prüfsumme>?tab=…&player=…); der Kopf steht beim Wechseln
// sofort da. Was FragDesk beim Darüberfahren erklärt, zeigt die App beim Tippen als Blatt von unten.
import {h, icon, card, chips, statTile, bars, tabs, sheet, emptyState, notice, skeleton, stars, toast, useCss} from '../ui.js';
import {openPlayer} from './player.js';
import * as api from '../api.js';
import {store, replaceRoute} from '../app.js';

const TABS = ['Übersicht', 'Scoreboard', 'Runden', 'Waffen', 'Duelle', 'Utility', 'Momente'];   // Nummern wie in FragDesk
const PER_PLAYER = [true, false, true, true, true, false, false];   // Bereiche mit „Werte von …“
const SKEL = [[34, 430, 150, 200], [62, 430, 430], [34, 640], [34, 400, 280, 230], [34, 300, 640], [520], [40, 74, 74, 74]];
const heads = new Map();   // Kopf je Match aus der letzten Antwort
let live = [];             // sichtbare sortierbare Tabellen (die beiden Teams im Scoreboard sortieren gemeinsam)

export async function render(ctx) {
  const css = cssReady();
  let id = ctx.params[0] || '';
  try { id = decodeURIComponent(id); } catch (e) { /* so lassen */ }
  const n = Number(ctx.params[1] || 0);
  const nav = {id, tab: Number.isInteger(n) && n >= 0 && n < TABS.length ? n : 0, player: ctx.query.get('player') || ''};
  const path = `/api/v1/match/${encodeURIComponent(id)}?tab=${nav.tab}` + (nav.player ? '&player=' + encodeURIComponent(nav.player) : '');
  const known = heads.get(id);
  ctx.title(known ? known.title : 'Match');
  const old = ctx.fresh ? null : api.cached(path);
  if (old) {
    await css;
    if (!ctx.current()) return;
    draw(ctx, nav, old);
  } else {
    ctx.main.replaceChildren(...top(nav, known && known.head), ...skeleton(...SKEL[nav.tab]));
  }
  let d;
  try {
    d = await api.get(path);
  } catch (e) {
    if (!ctx.current()) return;
    if (e && e.status === 404) {
      heads.delete(id);
      ctx.title('Match');
      ctx.main.replaceChildren(emptyState({title: 'Match nicht verfügbar', text: e.message}, 'warning'));
      return;
    }
    if (old && e && e.status !== 401) { toast(e.message, true); return; }   // die gespeicherte Ansicht bleibt stehen
    throw e;
  }
  await css;
  if (!ctx.current()) return;
  if (old && JSON.stringify(old) === JSON.stringify(d)) return;   // unverändert: nichts neu zeichnen (Blätterstand bleibt)
  draw(ctx, nav, d);
}

// Erst zeichnen, wenn match.css da ist (beim ersten Besuch kommt es parallel zu den Daten); gewartet wird nur einmal
let cssWait = null;
function cssReady() {
  if (cssWait) return cssWait;
  useCss('css/match.css');
  const link = document.querySelector('link[data-fd="css/match.css"]');
  cssWait = !link || link.sheet ? Promise.resolve() : new Promise(done => {
    link.addEventListener('load', done, {once: true});
    link.addEventListener('error', done, {once: true});
    setTimeout(done, 1500);
  });
  return cssWait;
}

function href(id, tab, player) {
  return '#/match/' + encodeURIComponent(id) + (tab ? '/' + tab : '') + (player ? '?player=' + encodeURIComponent(player) : '');
}

function draw(ctx, nav, d) {
  heads.set(nav.id, {title: d.title, head: d.head});
  ctx.title(d.title);
  live = [];
  const body = PER_PLAYER[nav.tab] && !d.focus
    ? [emptyState({title: 'Keine Spieler in diesem Match'}, 'users')]
    : [PER_PLAYER[nav.tab] ? focusSel(nav, d) : null, ...PAGES[nav.tab](ctx, nav, d)];
  ctx.main.replaceChildren(...top(nav, d.head), ...body.filter(Boolean));
}

function top(nav, hd) {
  return [
    hd ? header(hd) : h('div', {class: 'skel m-headskel'}),
    tabs(TABS, nav.tab, i => { if (i !== nav.tab) replaceRoute(href(nav.id, i, nav.player)); }),
  ];
}

function header(hd) {
  const pill = {win: 'good', loss: 'bad'}[hd.result] || 'neutral';
  return h('div', {class: 'm-head'},
    h('span', {class: 'm-bar', style: {background: hd.mapColor}}),
    h('h1', {class: 'm-map'}, hd.map),
    h('div', {class: 'm-score' + (hd.result ? ' ' + hd.result : '')}, hd.score),
    h('div', {class: 'm-meta' + (hd.resultText ? '' : ' wide')}, hd.meta, hd.share ? h('div', {class: 'm-share'}, hd.share) : null),
    hd.resultText ? h('span', {class: 'pill m-res ' + pill}, hd.resultText) : null);
}

// „Werte von …“ wie in FragDesk; die Wahl steht in der Adresse (?player=), der eigene Spieler ohne
function focusSel(nav, d) {
  const sel = h('select', {'aria-label': 'Werte von Spieler'});
  for (const p of d.players) sel.append(h('option', {value: p.id, selected: p.id === d.focus.id}, p.label));
  sel.addEventListener('change', () => {
    const p = d.players.find(x => x.id === sel.value);
    replaceRoute(href(nav.id, nav.tab, p && p.me ? '' : sel.value));
  });
  return h('label', {class: 'm-focus'}, icon('users'), h('span', null, d.focus.label), icon('down', 'm-chev'), sel);
}

// ---- Übersicht ------------------------------------------------------------------------------------------------------
function overviewTab(ctx, nav, d) {
  const o = d.overview, out = [];
  out.push(h('div', {class: 'grid2'}, o.kpis.map(k => {
    const tile = statTile(k);
    if (k.color) tile.querySelector('.v').style.color = k.color;   // Premier-Farbe
    return tile;
  })));
  if (o.rounds.length) {
    out.push(card({title: 'Runden', sub: 'Grün gewonnen, Rot verloren · Punkte = Kills · Stern = Clutch · × = gestorben · Tippen: Details'},
      strip(o.rounds)));
  }
  if (o.moments.length) {
    out.push(card({title: 'Momente', sub: 'Automatisch erkannt – alle im Tab „Momente“', more: 'Alle', href: href(nav.id, 6, nav.player), flush: true},
      h('div', {class: 'list m-moms'}, o.moments.map(m => momentLink(nav, m)))));
  }
  return out;
}

function strip(rounds) {
  const box = h('div', {class: 'm-strip'});
  let line = null, lineNo = -1, lastG = -1;
  for (const r of rounds) {
    const ln = r.g < 2 ? r.g : 2 + Math.floor((r.g - 2) / 2);   // g: Hälfte (2, 3 … je 3 Runden Verlängerung)
    if (ln !== lineNo) {
      line = h('div', {class: 'm-line'});
      box.append(line);
      lineNo = ln;
    } else if (r.g !== lastG) line.append(h('span', {class: 'm-gap'}));
    lastG = r.g;
    line.append(roundCell(r));
  }
  return box;
}

function roundCell(r) {
  const dots = [];   // Kills als Punkte: bis 3 in einer Reihe, 4 und 5 in zwei Reihen
  for (let done = 0, k = Math.min(r.k, 5); done < k; done += 3) {
    dots.push(h('span', {class: 'm-dr'}, Array.from({length: Math.min(3, k - done)}, () => h('i'))));
  }
  const b = h('button', {class: 'm-rd', type: 'button', 'aria-label': r.tip.split('\n')[0]},
    h('span', {class: 'm-box ' + (r.won ? 'w' : 'l') + (r.side === 'T' ? ' st' : '')},
      dots, r.clutch ? icon('star', 'm-cw') : r.dead ? icon('close', 'm-x') : null),
    h('span', {class: 'm-n'}, r.n));
  b.addEventListener('click', () => tipSheet(r.tip));
  return b;
}

function tipSheet(tip) {   // erste Zeile als Titel
  const i = tip.indexOf('\n');
  sheet(i < 0 ? {title: tip} : {title: tip.slice(0, i), text: tip.slice(i + 1)});
}

function momentLink(nav, m) {
  const kids = [
    h('span', {class: 'lead'}, icon(m.clip ? 'play' : m.clutch ? 'star' : 'spark')),
    h('div', {class: 'main'}, h('b', null, m.title), h('small', null, m.meta)),
    m.clip ? h('span', {class: 'pill neutral'}, 'Video') : icon('chev', 'chev'),
  ];
  if (!m.clip) return h('a', {class: 'row', href: href(nav.id, 6, nav.player)}, kids);
  const row = h('button', {class: 'row', type: 'button'}, kids);
  row.addEventListener('click', () => openPlayer({id: m.clip, title: m.title, sub: m.meta, match: nav.id}));
  return row;
}

// ---- Scoreboard -----------------------------------------------------------------------------------------------------
function scoreboardTab(ctx, nav, d) {
  const s = d.scoreboard, out = [];
  if (s.notice) out.push(notice('info', s.notice.title, s.notice.text));
  for (const t of s.teams) {
    out.push(h('section', {class: 'card m-team'},
      h('div', {class: 'm-th'}, h('span', {class: 'm-ts ' + t.tone}, t.score), h('div', {class: 'h'}, h('b', null, t.name), h('small', null, t.sub))),
      table(t.table, {onRow: r => playerSheet(nav, t.table, r)})));
  }
  out.push(h('div', {class: 'm-foot'}, h('span', null, 'Tippe auf einen Spieler für Details und seine Werte.'),
    s.teams.length ? glossary(s.teams[0].table) : null));
  return out;
}

// Spieler antippen: was FragDesk beim Darüberfahren zeigt (Fadenkreuz-Code, CP, Reaktion) und seine Übersicht
function playerSheet(nav, t, r) {
  const first = r.c[0];
  const tips = [];
  r.c.forEach((c, i) => {
    if (c && typeof c === 'object' && c.tip) {
      tips.push(h('div', {class: 'm-tip'}, i ? h('small', null, t.cols[i].t + ' · ' + c.t) : null, h('div', null, c.tip)));
    }
  });
  sheet({
    title: typeof first === 'string' ? first : first.t,
    body: h('div', null, tips.length ? h('div', {class: 'm-tips'}, tips) : null,
      h('a', {class: 'btn primary block', href: href(nav.id, 0, r.hl ? '' : r.player)}, icon('users'), 'Werte in der Übersicht')),
  });
}

// ---- Runden, Waffen, Duelle, Utility --------------------------------------------------------------------------------
function roundsTab(ctx, nav, d) {
  const t = d.rounds.table;
  return [card(null, table(t), glossary(t))];
}

function weaponsTab(ctx, nav, d) {
  const w = d.weapons;
  return [
    card(null, table(w.table), w.table.rows.length ? null : h('div', {class: 'm-none'}, w.empty), glossary(w.table)),
    card({title: 'Treffer nach Körperzonen'}, hitFigure(w.zones)),
    card({title: 'Gestorben durch'}, w.deaths.length ? deathBars(w.deaths) : h('div', {class: 'm-none'}, w.deathsEmpty)),
  ];
}

function duelsTab(ctx, nav, d) {
  const u = d.duels;
  return [
    card(null, table(u.table), u.table.rows.length ? null : h('div', {class: 'm-none'}, u.empty), glossary(u.table)),
    card({title: 'Killfeed', sub: 'Alle Kills des Matches – deine sind hervorgehoben'}, table(u.kills)),
  ];
}

function utilityTab(ctx, nav, d) {
  const t = d.utility.table;
  return [card(null, table(t), glossary(t))];
}

function deathBars(items) {
  const el = bars(items.map(x => ({name: x.label, value: x.value, frac: x.frac})));
  el.classList.add('m-deaths');
  return el;
}

// Trefferbild wie hitFigure in FragDesk: Figur 100 × 200, Zonen umso kräftiger, je mehr Treffer, Werte rechts daneben
const NS = 'http://www.w3.org/2000/svg';
function svgEl(tag, attrs, text) {
  const el = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs || {})) el.setAttribute(k, v);
  if (text != null) el.textContent = text;
  return el;
}

function hitFigure(zones) {
  const z = {};
  for (const x of zones) z[x.id] = x;
  const mx = Math.max(1, ...zones.map(x => (Number.isFinite(x.v) ? x.v : 0)));
  const svg = svgEl('svg', {viewBox: '0 0 240 206', class: 'm-fig', role: 'img', 'aria-label': 'Treffer nach Körperzonen'});
  const g = svgEl('g', {transform: 'translate(6 3)'});
  svg.append(g);
  const part = (id, tag, attrs) => {
    const v = z[id] && Number.isFinite(z[id].v) ? z[id].v : 0;
    g.append(svgEl(tag, Object.assign({class: 'm-zone'}, attrs)),
      svgEl(tag, Object.assign({class: 'm-on', 'fill-opacity': (0.15 + 0.85 * v / mx).toFixed(3)}, attrs)));
  };
  part('head', 'ellipse', {cx: 50, cy: 15, rx: 14, ry: 15});
  part('neck', 'rect', {x: 44, y: 30, width: 12, height: 8, rx: 2});
  part('chest', 'rect', {x: 28, y: 39, width: 44, height: 42, rx: 10});
  part('stomach', 'rect', {x: 30, y: 83, width: 40, height: 34, rx: 8});
  part('arms', 'rect', {x: 10, y: 41, width: 15, height: 70, rx: 7});
  part('arms', 'rect', {x: 75, y: 41, width: 15, height: 70, rx: 7});
  part('legs', 'rect', {x: 30, y: 119, width: 18, height: 80, rx: 8});
  part('legs', 'rect', {x: 52, y: 119, width: 18, height: 80, rx: 8});
  // Beschriftung von oben nach unten, ohne sich zu überlappen
  const lx = 112;
  let last = -1e9;
  for (const [id, y] of [['head', 15], ['neck', 34], ['chest', 60], ['arms', 76], ['stomach', 100], ['legs', 160]]) {
    if (!z[id]) continue;
    const yy = Math.max(y - 10, last + 22);
    last = yy;
    g.append(svgEl('line', {x1: 92, y1: y, x2: lx - 6, y2: yy + 10, class: 'm-lead'}),
      svgEl('text', {x: lx, y: yy + 14, class: 'm-zn'}, z[id].name),
      svgEl('text', {x: lx + 50, y: yy + 14, class: 'm-zv'}, z[id].value));
  }
  return svg;
}

// ---- Momente --------------------------------------------------------------------------------------------------------
function momentsTab(ctx, nav, d) {
  const mo = d.moments, who = store.get('momentsWho', 0) === 1 ? 1 : 0;   // wie in FragDesk für Momente und Match gemeinsam
  const items = mo.items.filter(m => who === 1 || m.mine);
  const out = [chips(['Meine Momente', 'Alle Spieler'], who, i => { store.set('momentsWho', i); draw(ctx, nav, d); })];
  if (mo.blocked && items.some(m => m.clip)) out.push(notice('warn', 'Videos gerade nicht verfügbar', mo.blocked));
  if (!items.length) out.push(emptyState(who ? mo.emptyAll : mo.emptyMine, 'film'));
  else out.push(h('section', {class: 'card flush'}, h('div', {class: 'list m-moms'}, items.map(m => momentItem(nav, m, who === 1)))));
  out.push(h('p', {class: 'm-hint'},
    '„In CS2 ansehen“ und „Aufnehmen“ gehen in FragDesk am PC. Aufgenommene Videos spielst du hier ab und speicherst sie aufs Handy.'));
  return out;
}

function momentItem(nav, m, all) {
  const meta = all && m.who ? m.who + ' · ' + m.meta : m.meta;
  const kids = [
    h('span', {class: 'lead'}, icon(m.clip ? 'play' : m.clutch ? 'star' : 'spark')),
    h('div', {class: 'main'}, h('b', null, m.title), h('small', null, meta)),
    h('div', {class: 'side'}, stars(m.stars), m.clip ? h('small', null, 'Video') : null),
  ];
  if (!m.clip) return h('div', {class: 'row m-static'}, kids);
  const row = h('button', {class: 'row', type: 'button'}, kids);
  row.addEventListener('click', () => openPlayer({id: m.clip, title: m.title, sub: meta, match: nav.id}));
  return row;
}

const PAGES = [overviewTab, scoreboardTab, roundsTab, weaponsTab, duelsTab, utilityTab, momentsTab];

// ---- Tabellen: waagerecht blätternd, erste Spalte bleibt stehen; Sortieren wie in FragDesk ------------------------------
const TONES = {good: 'good', bad: 'bad', win: 'win', loss: 'loss', draw: 'draw', muted: 'tone-muted', dim: 'm-dim', t: 'm-t', ct: 'm-ct', acc: 'm-acc'};
const tone = t => (t && TONES[t]) || '';
const align = c => (c.a === 'l' ? 'l' : c.a === 'c' ? 'c' : 'r');

function table(t, opts = {}) {
  const tbl = h('table');
  const scroller = h('div', {class: 'tbl'}, tbl);
  const wrap = h('div', {class: 'm-tw'}, scroller);
  const refresh = () => {
    const st = t.sortKey ? sortOf(t) : null;
    tbl.replaceChildren(thead(t, st), h('tbody', null, (st ? sortRows(t.rows, st.col, st.desc) : t.rows).map(r => tableRow(t, r, opts))));
  };
  refresh();
  if (t.sortKey) live.push({key: t.sortKey, refresh});
  const edges = () => {   // Schatten an der festen Spalte, sobald geblättert; Verlauf rechts, solange es weitergeht
    const x = scroller.scrollLeft, max = scroller.scrollWidth - scroller.clientWidth;
    wrap.classList.toggle('sx', x > 1);
    wrap.classList.toggle('more', max - x > 1);
  };
  scroller.addEventListener('scroll', edges, {passive: true});
  new ResizeObserver(edges).observe(scroller);
  return wrap;
}

function thead(t, st) {
  return h('thead', null, h('tr', null, t.cols.map((c, i) => {
    const on = !!st && st.col === i;
    const tap = st ? !!c.t : !!c.tip;
    const cls = [align(c), on ? 'on' : '', on && !st.desc ? 'up' : '', tap ? 'tap' : '', !st && c.tip ? 'm-tipped' : ''].filter(Boolean).join(' ');
    const th = h('th', {class: cls, 'aria-sort': on ? (st.desc ? 'descending' : 'ascending') : null}, c.t, on ? icon('down', 'm-arr') : null);
    if (st && c.t) th.addEventListener('click', () => setSort(t, i));
    else if (c.tip) th.addEventListener('click', () => sheet({title: c.t, text: c.tip}));
    return th;
  })));
}

function tableRow(t, r, opts) {
  const tap = !!(r.player && opts.onRow);
  const cls = [r.hl ? 'me' : '', r.bar ? 'm-' + r.bar : '', tap ? 'tap' : ''].filter(Boolean).join(' ');
  const tr = h('tr', {class: cls || null}, r.c.map((c, i) => tableCell(c, t.cols[i] || {})));
  if (tap) tr.addEventListener('click', () => opts.onRow(r));
  return tr;
}

function tableCell(c, col) {
  if (typeof c === 'string') return h('td', {class: align(col)}, c);
  const cls = [tone(c.tone), c.b ? 'm-b' : '', c.sm ? 'm-sm' : ''].filter(Boolean).join(' ');
  const txt = h('span', {class: cls || null, style: c.color ? {color: c.color} : null}, c.t);
  let inner = c.sub ? h('div', {class: 'm-2'}, txt, h('small', null, c.sub)) : txt;
  if (c.icon) inner = h('div', {class: 'm-ic'}, icon(c.icon, tone(c.it) || null), inner);
  return h('td', {class: align(col)}, inner);
}

// Wie ui::sortRows: Zahlen vor leeren Zellen (in beiden Richtungen), ohne Zahl nach dem Text (ohne Groß-/Kleinschreibung
// und Satzzeichen wie die Wortsortierung von Windows: „-jason“ steht bei „jason“); gleiche bleiben in Reihe
const collator = new Intl.Collator('de', {sensitivity: 'base', ignorePunctuation: true});
function sortRows(rows, col, desc) {
  const key = r => {
    const c = r.c[col];
    if (c == null) return null;
    return typeof c === 'string' ? {t: c, s: null} : {t: c.t, s: typeof c.s === 'number' ? c.s : null};
  };
  return rows.map((r, i) => ({r, i, k: key(r)})).sort((a, b) => {
    if (!a.k || !b.k) return a.i - b.i;
    const an = a.k.s == null, bn = b.k.s == null;
    let c;
    if (!an || !bn) {
      if (an !== bn) return an ? 1 : -1;
      c = a.k.s - b.k.s;
    } else c = collator.compare(a.k.t, b.k.t);
    if (c === 0) return a.i - b.i;
    return desc ? -c : c;
  }).map(x => x.r);
}

function sortOf(t) {
  const s = (store.get('matchSort', {}) || {})[t.sortKey];
  if (s && Number.isInteger(s.col) && s.col >= 0 && s.col < t.cols.length) return {col: s.col, desc: !!s.desc};
  return {col: t.sortCol, desc: !!t.sortDesc};
}

function setSort(t, col) {   // wie in FragDesk: gleiche Spalte dreht um, neue Spalte absteigend (Namen aufsteigend)
  const s = sortOf(t);
  if (s.col === col) s.desc = !s.desc;
  else {
    s.col = col;
    s.desc = col !== 0;
  }
  const all = store.get('matchSort', {}) || {};
  all[t.sortKey] = s;
  store.set('matchSort', all);
  for (const x of live) if (x.key === t.sortKey) x.refresh();
}

// „Spalten erklärt“: die Erklärungen der Spaltenköpfe (in FragDesk Tooltips)
function glossary(t) {
  const cols = t.cols.filter(c => c.tip);
  if (!cols.length) return null;
  const b = h('button', {class: 'm-gl', type: 'button'}, icon('info'), 'Spalten erklärt');
  b.addEventListener('click', () => sheet({title: 'Spalten', body: h('dl', {class: 'm-gloss'}, cols.map(c => h('div', null, h('dt', null, c.t), h('dd', null, c.tip))))}));
  return b;
}
