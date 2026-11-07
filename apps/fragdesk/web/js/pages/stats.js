// FragDesk Handy-App – Statistiken (wie in FragDesk: Filter, Tabs mit Kennzahlen, Tabellen, Balken und Verlauf).
// Der Server schickt je Tab fertige Blöcke (src/api/view_stats.cpp); die Seite zeigt sie nur an.
import {h, icon, card, tabs, bars, sheet, notice, emptyState, skeleton, toneClass, toast, useCss} from '../ui.js';
import {lineChart} from '../chart.js';
import {filterBar, filterQuery} from '../filters.js';
import * as api from '../api.js';
import {store, replaceRoute} from '../app.js';

// Tabs mit den Nummern aus FragDesk; die Heatmaps (11) fehlen noch
const TABS = ['Allgemein', 'Waffen', 'Maps', 'Eröffnung & Trades', 'Clutches', 'Utility', 'Zielen', 'Besondere Kills', 'Schaden & Zeit',
  'Economy & Bombe', 'Verlauf'];

export async function render(ctx) {
  useCss('css/stats.css');
  ctx.title('Statistiken');
  // #/stats ohne Nummer: der zuletzt gewählte Tab – FragDesk merkt ihn sich auch
  let tab = ctx.params[0] != null ? Number(ctx.params[0]) : store.get('statsTab', 0);
  if (!Number.isInteger(tab) || tab < 0 || tab >= TABS.length) tab = 0;
  store.set('statsTab', tab);
  const path = '/api/v1/stats?' + filterQuery(ctx.filters) + '&tab=' + tab;
  const head = d => [
    h('h1', {class: 'page-title'}, 'Statistiken'),
    h('div', {class: 'page-sub'}, d ? d.sub : ' '),
    filterBar(ctx),
  ];
  const tabBar = () => tabs(TABS, tab, i => { if (i !== tab) replaceRoute('#/stats/' + i); });
  const show = d => ctx.main.replaceChildren(...head(d), ...(d.empty ? [emptyState(d.empty, 'stats')] : [tabBar(), ...body(ctx, d)]));
  const old = api.cached(path);
  if (old && !ctx.fresh) show(old);
  else ctx.main.replaceChildren(...head(null), tabBar(), ...skeleton(360, 260, 200));
  const d = await api.get(path);
  if (!ctx.current()) return;
  show(d);
}

function body(ctx, d) {
  const out = [];
  for (const b of d.blocks) {
    const el = b.kind === 'group' ? group(b)
      : b.kind === 'table' ? table(ctx, b)
      : b.kind === 'bars' ? barsCard(b)
      : b.kind === 'chart' ? chartCard(b, d.matches || [])
      : b.kind === 'figure' ? figure(b)
      : b.kind === 'notice' ? notice(b.tone || 'info', b.title, b.text)
      : null;
    out.push(...[].concat(el).filter(Boolean));
  }
  if (d.foot) out.push(h('p', {class: 'st-foot'}, d.foot));
  return out;
}

// ---- Kennzahlen einer Gruppe: Kennzahl · Gesamt · als T · als CT (Tippen zeigt die Erklärung) ---------------------
function group(b) {
  const rows = b.stats.map(s => {
    const side = v => s.sideless || v == null ? h('td', {class: 'sd none'}, '–') : h('td', {class: 'sd'}, v);
    const tr = h('tr', {class: s.hint ? 'tap' : null},
      h('td', null, s.hint ? h('button', {type: 'button'}, s.label, icon('info')) : s.label),
      h('td', {class: 'v'}, s.value), side(s.t), side(s.ct));
    if (s.hint) {
      tr.addEventListener('click', () => sheet({title: s.label, text: s.hint,
        rows: [['Gesamt', s.value]].concat(s.sideless ? [] : [['als T', s.t], ['als CT', s.ct]])}));
    }
    return tr;
  });
  return card({title: b.title, class: 'st-group'}, h('table', {class: 'st-stats'},
    h('thead', null, h('tr', null, h('th', null, 'Kennzahl'), h('th', null, 'Gesamt'), h('th', {class: 't'}, 'als T'), h('th', {class: 'ct'}, 'als CT'))),
    h('tbody', null, rows)));
}

// ---- Tabellen (Waffen, Maps): waagerecht scrollbar, Spaltenkopf antippen sortiert wie in FragDesk ------------------
function table(ctx, b) {
  if (!b.rows.length) return card({class: 'st-tcard'}, h('div', {class: 'st-none'}, 'Noch keine Werte'));
  const wrap = h('div', {class: 'tbl'});
  const draw = () => {
    const s = sortOf(b), left = wrap.scrollLeft;
    const head = b.cols.map((c, i) => {
      const on = i === s.col;
      const btn = h('button', {class: 'st-sort' + (on ? ' on' : ''), type: 'button'}, c.label, on ? icon('down', s.desc ? null : 'up') : null);
      btn.addEventListener('click', () => {
        const next = {col: i, desc: on ? !s.desc : i !== 0};   // neue Spalte: Zahlen absteigend, Namen aufsteigend
        saveSort(b.id, next);
        draw();
        toast(`Sortiert nach ${c.tip || c.label} (${next.desc ? 'absteigend' : 'aufsteigend'})`);
      });
      return h('th', {'aria-sort': on ? (s.desc ? 'descending' : 'ascending') : null}, btn);
    });
    const rows = sorted(b.rows, s).map(r => {
      const tr = h('tr', {class: r.on ? 'tap on' : 'tap'}, r.cells.map(c => h('td', null, cell(c))));
      tr.addEventListener('click', () => rowTap(ctx, b, r));
      return tr;
    });
    wrap.replaceChildren(h('table', null, h('thead', null, h('tr', null, head)), h('tbody', null, rows)));
    wrap.scrollLeft = left;
  };
  draw();
  return [card({class: 'st-tcard'}, wrap), b.note ? h('p', {class: 'st-note'}, b.note) : null];
}

function cell(c) {
  return h(c.b ? 'b' : 'span', {class: toneClass(c.tone) || null}, c.dot ? h('span', {class: 'dot', style: {background: c.dot}}) : null, c.text);
}

function rowTap(ctx, b, r) {
  if (b.tap === 'map') ctx.setFilters({map: r.on ? '' : r.map});   // wie in FragDesk: Map filtern, noch mal tippen hebt auf
  else if (b.tap === 'details') {   // alle Werte einer Zeile untereinander, mit den Erklärungen der Spalten
    sheet({title: r.cells[0].text, rows: b.cols.slice(1).map((c, i) => [c.tip || c.label, (r.cells[i + 1] || {}).text || '–'])});
  }
}

function sortOf(b) {
  const s = store.get('statsSort', {})[b.id];
  return s && Number.isInteger(s.col) && s.col >= 0 && s.col < b.cols.length ? s : b.sort || {col: -1, desc: true};
}

function saveSort(id, s) {
  const all = store.get('statsSort', {});
  all[id] = s;
  store.set('statsSort', all);
}

// Wie sortRows in FragDesk: nach Rohwert, Zellen ohne Wert immer ans Ende; ohne Rohwerte nach Text
function sorted(rows, {col, desc}) {
  if (col < 0) return rows;
  const num = c => (c && Number.isFinite(c.v) ? c.v : null);
  const txt = c => (c && c.text) || '';
  return rows.slice().sort((a, b) => {
    const x = num(a.cells[col]), y = num(b.cells[col]);
    if (x != null || y != null) {
      if (x == null || y == null) return x == null ? 1 : -1;
      return desc ? y - x : x - y;
    }
    const c = txt(a.cells[col]).localeCompare(txt(b.cells[col]), 'de', {sensitivity: 'accent'});
    return desc ? -c : c;
  });
}

// ---- Balken ----------------------------------------------------------------------------------------------------------
function barsCard(b) {
  const list = b.items.length ? bars(b.items) : h('div', {class: 'st-none'}, 'Noch keine Werte');
  if (b.tone && b.items.length) list.classList.add('st-' + b.tone);
  return card({title: b.title, sub: b.sub}, list);
}

// ---- Verlauf ---------------------------------------------------------------------------------------------------------
function chartCard(b, matches) {
  const f = b.fmt || {dec: 0, unit: ''};
  const fmtY = v => v.toLocaleString('de-DE', {minimumFractionDigits: f.dec, maximumFractionDigits: f.dec}) + (f.unit || '');
  const pts = b.points.map(p => {
    const m = matches[p.i] || {};
    return {y: p.y, label: (m.label || '').replace(/\.$/, ''), color: p.color, tip: p.value + (m.info ? '\n' + m.info : ''),
      href: m.id ? '#/match/' + m.id : null};
  });
  let content;
  if (!pts.length) content = h('div', {class: 'st-chartempty'}, b.empty || 'Noch keine Daten');
  else {
    content = lineChart(pts, {fmtY, bands: b.bands, refY: b.ref, area: b.area, color: b.area ? 'var(--accent)' : undefined,
      height: b.id === 'premier' ? 180 : 170, avg: b.avg || 0});
  }
  return card({title: b.title, sub: b.sub, class: 'st-chart'}, content);
}

const NS = 'http://www.w3.org/2000/svg';
function svg(tag, attrs, text) {
  const el = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs || {})) el.setAttribute(k, v);
  if (text != null) el.textContent = text;
  return el;
}

// ---- Treffer nach Körperzonen (wie hitFigure in FragDesk): Figur 100 × 200, je Zone von der Spur zum Akzent ----------
const BODY = {
  head: [['ellipse', {cx: 50, cy: 15, rx: 14, ry: 15}]],
  neck: [['rect', {x: 44, y: 30, width: 12, height: 8, rx: 2}]],
  chest: [['rect', {x: 28, y: 39, width: 44, height: 42, rx: 10}]],
  stomach: [['rect', {x: 30, y: 83, width: 40, height: 34, rx: 8}]],
  arms: [['rect', {x: 10, y: 41, width: 15, height: 70, rx: 7}], ['rect', {x: 75, y: 41, width: 15, height: 70, rx: 7}]],
  legs: [['rect', {x: 30, y: 119, width: 18, height: 80, rx: 8}], ['rect', {x: 52, y: 119, width: 18, height: 80, rx: 8}]],
};
const ANCHOR = {head: 15, neck: 34, chest: 60, arms: 76, stomach: 100, legs: 160};   // Höhe der Beschriftung an der Figur

function figure(b) {
  const fx = 16, fy = 5, lx = fx + 110;
  const mx = Math.max(1, ...b.zones.map(z => z.v).filter(Number.isFinite));
  const root = svg('svg', {viewBox: '0 0 260 210', class: 'st-figure', role: 'img',
    'aria-label': b.title + ': ' + b.zones.map(z => z.label + ' ' + z.value).join(', ')});
  const fig = svg('g', {transform: `translate(${fx} ${fy})`});
  for (const z of b.zones) {
    const t = Number.isFinite(z.v) ? z.v / mx : 0;
    for (const [tag, a] of BODY[z.id] || []) {
      fig.append(svg(tag, Object.assign({class: 'base'}, a)), svg(tag, Object.assign({class: 'heat', 'fill-opacity': (0.15 + 0.85 * t).toFixed(3)}, a)));
    }
  }
  root.append(fig);
  // Beschriftung rechts, von oben nach unten, mindestens 22 auseinander
  let last = -1e9;
  for (const z of b.zones.filter(z => z.id in ANCHOR).sort((p, q) => ANCHOR[p.id] - ANCHOR[q.id])) {
    const ay = fy + ANCHOR[z.id], yy = Math.max(ay - 10, last + 22);
    last = yy;
    root.append(svg('line', {x1: fx + 92, y1: ay, x2: lx - 6, y2: yy + 10, class: 'lead'}),
      svg('text', {x: lx, y: yy + 14, class: 'name'}, z.label), svg('text', {x: lx + 64, y: yy + 14, class: 'val'}, z.value));
  }
  return card({title: b.title, class: 'st-fig'}, root);
}
