// FragDesk Handy-App – Heatmap wie in FragDesk (heat_view.cpp): Radarbild mit Dichte, einzelnen Punkten und
// Wurflinien; Auswahl von Schichten, Spielern, Seite und – bei Karten mit zwei Ebenen – der Ebene. Rechnen und alle
// Texte übernimmt die Schnittstelle (/api/v1/heat, src/api/view_heat.cpp), hier wird nur gezeichnet.
// Bedienung: zwei Finger vergrößern, ein Finger verschiebt die vergrößerte Karte, doppelt tippen vergrößert bzw.
// zeigt wieder alles; ein Punkt zeigt, was dort war – mit dem Weg in die 2D-Ansicht.
import {h, emptyState, sheet, toast, useCss} from './ui.js';
import * as api from './api.js';
import {store} from './app.js';
import {radarImage} from './radar.js';

const KEYS = {match: 'heatMatch', stats: 'heatStats'};   // Auswahl getrennt für Match und Statistiken (wie in FragDesk)
const MAX_ZOOM = 6;
const HIT = 16;   // Tippen trifft Punkte bis so weit weg (CSS-Pixel)

// ---- Auswahl ----------------------------------------------------------------------------------------------------------
// Gemerkt wird nur, was man selbst gewählt hat; „who“ ohne Wahl: wie die Schnittstelle (Ich, ohne mich beide Teams)
function prefs(kind) {
  const p = store.get(KEYS[kind], null) || {};
  const int = (v, lo, hi, def) => (Number.isInteger(v) && v >= lo && v <= hi ? v : def);
  return {
    layers: int(p.layers, 0, 127, 1),
    who: int(p.who, 1, 7, 0),
    sides: int(p.sides, 1, 3, 3),
    marks: typeof p.marks === 'boolean' ? p.marks : kind === 'match',   // wie FragDesk: Punkte im Match an
    throws: p.throws === true,
  };
}

// Adresse der Schnittstelle mit der gemerkten Auswahl; base: "match=<Prüfsumme>" oder die Filter der Statistiken
export function heatPath(kind, base, level = 0) {
  const p = prefs(kind);
  return `/api/v1/heat?${base}&layers=${p.layers}${p.who ? '&who=' + p.who : ''}&sides=${p.sides}&lower=${level ? 1 : 0}` +
    `&marks=${p.marks ? 1 : 0}&throws=${p.throws ? 1 : 0}`;
}

const fromFilter = f => ({layers: f.layers, who: f.who, sides: f.sides, marks: !!f.marks, throws: !!f.throws});

// ---- Farben wie gradient() und hue() in heat_view.cpp: je Dichte 0..255 vormultipliert (r, g, b mal Deckkraft) ---------
const STOPS = [[0, 0, 60, 255, 0], [0.1, 0, 90, 255, 45], [0.3, 0, 200, 255, 105], [0.5, 40, 230, 120, 150], [0.75, 255, 220, 0, 185],
  [1, 255, 40, 30, 215]];
function premul(out, o, r, g, b, a) {
  a = Math.max(0, Math.min(255, a));
  out[o] = Math.floor(r * a / 255);
  out[o + 1] = Math.floor(g * a / 255);
  out[o + 2] = Math.floor(b * a / 255);
  out[o + 3] = a;
}
let gradLut = null;
function gradient() {   // klassischer Verlauf blau → türkis → grün → gelb → rot, Deckkraft steigt mit
  if (gradLut) return gradLut;
  gradLut = new Uint8Array(256 * 4);
  for (let q = 0; q < 256; q++) {
    const v = q / 255;
    let i = 1;
    while (i + 1 < STOPS.length && v > STOPS[i][0]) i++;
    const a = STOPS[i - 1], b = STOPS[i], t = (v - a[0]) / Math.max(1e-6, b[0] - a[0]);
    const mix = k => Math.round(a[k] + (b[k] - a[k]) * t);
    premul(gradLut, q * 4, mix(1), mix(2), mix(3), mix(4));
  }
  return gradLut;
}
function hue(hex) {   // eine Farbe je Schicht, Deckkraft nach Dichte (wenn mehrere Schichten an sind)
  const lut = new Uint8Array(256 * 4), c = parseInt(String(hex).slice(1), 16) || 0xffffff;
  for (let q = 0; q < 256; q++) premul(lut, q * 4, (c >> 16) & 255, (c >> 8) & 255, c & 255, Math.trunc(20 + 190 * q / 255));
  return lut;
}

// Raster der Schnittstelle: Base64, Nullen als Paar „0, Anzahl − 1“
function decode(b64, cells) {
  const bin = atob(b64), out = new Uint8Array(cells);
  for (let i = 0, o = 0; i < bin.length && o < cells; i++) {
    const c = bin.charCodeAt(i);
    if (c) out[o++] = c;
    else o += bin.charCodeAt(++i) + 1;
  }
  return out;
}

// Dichte aller Schichten als kleines Bild (n × n), übereinander wie over() in FragDesk
function heatCanvas(d) {
  const g = d.grid;
  if (!g || !g.layers.length) return null;
  const n = g.n, cells = n * n, acc = new Uint8ClampedArray(cells * 4);
  const color = {};
  for (const l of d.ui.layers) color[l.id] = l.color;
  for (const layer of g.layers) {
    const v = decode(layer.data, cells), lut = d.legend.gradient ? gradient() : hue(color[layer.id]);
    for (let i = 0; i < cells; i++) {
      const q = v[i];
      if (!q) continue;
      const o = i * 4, s = q * 4, ia = 255 - lut[s + 3];
      acc[o] = lut[s] + Math.floor(acc[o] * ia / 255);
      acc[o + 1] = lut[s + 1] + Math.floor(acc[o + 1] * ia / 255);
      acc[o + 2] = lut[s + 2] + Math.floor(acc[o + 2] * ia / 255);
      acc[o + 3] = lut[s + 3] + Math.floor(acc[o + 3] * ia / 255);
    }
  }
  const img = new ImageData(n, n), px = img.data;
  for (let o = 0; o < cells * 4; o += 4) {
    const a = acc[o + 3];
    if (!a) continue;
    px[o] = acc[o] * 255 / a;
    px[o + 1] = acc[o + 1] * 255 / a;
    px[o + 2] = acc[o + 2] * 255 / a;
    px[o + 3] = a;
  }
  const cv = document.createElement('canvas');
  cv.width = cv.height = n;
  cv.getContext('2d').putImageData(img, 0, 0);
  return cv;
}

function zoomOutIcon() {   // Lupe mit Minus (fehlt in ui.js)
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('aria-hidden', 'true');
  const p = document.createElementNS(ns, 'path');
  p.setAttribute('d', 'M10.5 4a6.5 6.5 0 1 0 0 13 6.5 6.5 0 0 0 0-13zM20 20l-4.8-4.8M7.5 10.5h6');
  for (const [k, v] of [['fill', 'none'], ['stroke', 'currentColor'], ['stroke-width', '2'], ['stroke-linecap', 'round']]) p.setAttribute(k, v);
  svg.append(p);
  return svg;
}

// ---- Karte: Radarbild, Dichte und Punkte auf einer Leinwand; Vergrößern, Verschieben, Tippen ---------------------------
function mapView(onTap) {
  const cv = h('canvas', {class: 'hm-cv', role: 'img', 'aria-label': 'Heatmap'});
  const reset = h('button', {class: 'hm-zoom hidden', type: 'button', 'aria-label': 'Ganze Karte zeigen'}, zoomOutIcon());
  const over = h('div', {class: 'hm-over'});
  const box = h('div', {class: 'hm-map'}, cv, over, reset, h('span', {class: 'hm-busy'}));
  const st = {img: null, imgUrl: '', heat: null, marks: [], colors: [], s: 1, x: 0, y: 0, w: 0};
  let raf = 0, anim = 0;
  const redraw = () => { if (!raf) raf = requestAnimationFrame(draw); };
  new ResizeObserver(redraw).observe(box);

  function clamp() {
    st.s = Math.max(1, Math.min(MAX_ZOOM, st.s));
    const min = st.w - st.w * st.s;
    st.x = Math.max(min, Math.min(0, st.x));
    st.y = Math.max(min, Math.min(0, st.y));
  }

  function draw() {
    raf = 0;
    const w = box.clientWidth;
    if (!w) return;
    const dpr = Math.min(3, window.devicePixelRatio || 1), px = Math.round(w * dpr);
    if (cv.width !== px || cv.height !== px) cv.width = cv.height = px;
    if (st.w && st.w !== w) {   // andere Breite (Drehen): gleicher Ausschnitt
      st.x *= w / st.w;
      st.y *= w / st.w;
    }
    st.w = w;
    clamp();
    const g = cv.getContext('2d');
    g.setTransform(1, 0, 0, 1, 0, 0);
    g.clearRect(0, 0, px, px);
    g.imageSmoothingEnabled = true;
    g.imageSmoothingQuality = 'high';
    g.setTransform(dpr * st.s, 0, 0, dpr * st.s, dpr * st.x, dpr * st.y);
    if (st.img) g.drawImage(st.img, 0, 0, w, w);
    if (st.heat) g.drawImage(st.heat, 0, 0, w, w);
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    drawMarks(g, w);
    const zoomed = st.s > 1.01;
    reset.classList.toggle('hidden', !zoomed);
    cv.style.touchAction = zoomed ? 'none' : 'pan-y';   // vergrößert: ein Finger verschiebt die Karte statt der Seite
  }

  // Punkte immer gleich groß (wie in FragDesk in festen Pixeln), auch vergrößert
  function drawMarks(g, w) {
    const ms = st.marks;
    if (!ms.length) return;
    const k = w * st.s / 10000, sx = u => u * k + st.x, sy = v => v * k + st.y;
    const seen = (x, y) => x > -12 && y > -12 && x < w + 12 && y < w + 12;
    g.lineCap = 'round';
    // Wurflinien und Wurfstellen zuerst (darunter)
    for (const [id, col] of st.colors) {
      let lines = null, rings = null;
      for (const m of ms) {
        if (m[2] !== id || !(m[3] & 6)) continue;
        const fx = sx(m[8]), fy = sy(m[9]);
        if (m[3] & 4) {
          lines = lines || new Path2D();
          lines.moveTo(fx, fy);
          lines.lineTo(sx(m[0]), sy(m[1]));
        }
        if (m[3] & 2 && seen(fx, fy)) {
          rings = rings || new Path2D();
          rings.moveTo(fx + 2.6, fy);
          rings.arc(fx, fy, 2.6, 0, 2 * Math.PI);
        }
      }
      g.strokeStyle = col;
      if (lines) {
        g.globalAlpha = 90 / 255;
        g.lineWidth = 1.1;
        g.stroke(lines);
      }
      if (rings) {
        g.globalAlpha = 230 / 255;
        g.lineWidth = 1.3;
        g.stroke(rings);
      }
    }
    g.globalAlpha = 1;
    // Punkte: dunkler Rand, dann Farbe; Tode als Kreuz
    const back = new Path2D(), crossBack = new Path2D(), fills = new Map(), crosses = new Map();
    for (const m of ms) {
      if (!(m[3] & 1)) continue;
      const x = sx(m[0]), y = sy(m[1]);
      if (!seen(x, y)) continue;
      if (m[2] === 2) {
        const p = crosses.get(m[2]) || new Path2D();
        for (const q of [crossBack, p]) {
          q.moveTo(x - 3.3, y - 3.3);
          q.lineTo(x + 3.3, y + 3.3);
          q.moveTo(x + 3.3, y - 3.3);
          q.lineTo(x - 3.3, y + 3.3);
        }
        crosses.set(m[2], p);
      } else {
        const p = fills.get(m[2]) || new Path2D();
        back.moveTo(x + 4, y);
        back.arc(x, y, 4, 0, 2 * Math.PI);
        p.moveTo(x + 2.8, y);
        p.arc(x, y, 2.8, 0, 2 * Math.PI);
        fills.set(m[2], p);
      }
    }
    const color = new Map(st.colors);
    g.fillStyle = g.strokeStyle = 'rgba(0, 0, 0, .78)';
    g.fill(back);
    g.lineWidth = 3.6;
    g.stroke(crossBack);
    for (const [id, p] of fills) {
      g.fillStyle = color.get(id) || '#fff';
      g.fill(p);
    }
    g.lineWidth = 2;
    for (const [id, p] of crosses) {
      g.strokeStyle = color.get(id) || '#fff';
      g.stroke(p);
    }
  }

  // Punkte am Finger: genau getroffen (nur einer ganz nah) dieser, sonst alle in Reichweite, die nächsten zuerst
  function hits(cx, cy) {
    const k = st.w * st.s / 10000, out = [];
    for (const m of st.marks) {
      if (!(m[3] & 1)) continue;
      const dx = m[0] * k + st.x - cx, dy = m[1] * k + st.y - cy, d2 = dx * dx + dy * dy;
      if (d2 <= HIT * HIT) out.push([d2, m]);
    }
    out.sort((a, b) => a[0] - b[0]);
    const exact = out.filter(x => x[0] <= 49);
    return (exact.length === 1 ? exact : out).map(x => x[1]);
  }

  function zoomTo(s, cx, cy) {   // sanft vergrößern; der Punkt unter (cx, cy) bleibt stehen
    cancelAnimationFrame(anim);
    const s0 = st.s, x0 = st.x, y0 = st.y, t0 = performance.now();
    const s1 = Math.max(1, Math.min(MAX_ZOOM, s));
    const x1 = cx - (cx - x0) * s1 / s0, y1 = cy - (cy - y0) * s1 / s0;
    const step = now => {
      const t = Math.min(1, (now - t0) / 180), e = 1 - (1 - t) * (1 - t);
      st.s = s0 + (s1 - s0) * e;
      st.x = x0 + (x1 - x0) * e;
      st.y = y0 + (y1 - y0) * e;
      draw();
      if (t < 1) anim = requestAnimationFrame(step);
    };
    anim = requestAnimationFrame(step);
  }
  reset.addEventListener('click', () => zoomTo(1, 0, 0));

  // Gesten: Zeiger merken; bei jedem Wechsel der Fingerzahl gilt die neue Lage als Ausgang. Getippt wird über „click“ –
  // ein Blatt, das schon bei pointerup aufgeht, würde der Klick des Browsers gleich wieder schließen.
  const ptrs = new Map();
  let base = null, touch = null, lastTap = null;
  const pos = e => {
    const r = cv.getBoundingClientRect();
    return {x: e.clientX - r.left, y: e.clientY - r.top};
  };
  const baseline = () => { base = {s: st.s, x: st.x, y: st.y, p: [...ptrs.values()].map(q => ({x: q.x, y: q.y}))}; };
  cv.addEventListener('pointerdown', e => {
    if (e.pointerType === 'mouse' && e.button !== 0) return;
    cancelAnimationFrame(anim);
    if (!ptrs.size) touch = {moved: false, multi: false};
    ptrs.set(e.pointerId, pos(e));
    if (ptrs.size > 1) touch.multi = true;
    try { cv.setPointerCapture(e.pointerId); } catch (err) { /* egal */ }
    baseline();
  });
  cv.addEventListener('pointermove', e => {
    if (!ptrs.has(e.pointerId) || !base) return;
    ptrs.set(e.pointerId, pos(e));
    const p = [...ptrs.values()];
    if (p.length >= 2 && base.p.length >= 2) {   // zwei Finger: vergrößern um die Mitte, dabei mitschieben
      const [a, b] = base.p, [A, B] = p;
      const s = Math.max(1, Math.min(MAX_ZOOM, base.s * Math.hypot(B.x - A.x, B.y - A.y) / (Math.hypot(b.x - a.x, b.y - a.y) || 1)));
      const mx = (a.x + b.x) / 2, my = (a.y + b.y) / 2, Mx = (A.x + B.x) / 2, My = (A.y + B.y) / 2;
      st.x = Mx - (mx - base.x) * s / base.s;
      st.y = My - (my - base.y) * s / base.s;
      st.s = s;
      touch.moved = true;
    } else if (p.length === 1 && base.p.length === 1) {
      const dx = p[0].x - base.p[0].x, dy = p[0].y - base.p[0].y;
      if (Math.hypot(dx, dy) > 8) touch.moved = true;
      if (base.s <= 1.01) return;   // nicht vergrößert: die Seite scrollt (touch-action)
      st.x = base.x + dx;
      st.y = base.y + dy;
    } else return;
    clamp();
    redraw();
  });
  const up = e => {
    if (!ptrs.has(e.pointerId)) return;
    ptrs.delete(e.pointerId);
    if (ptrs.size) return baseline();
    base = null;
    redraw();
  };
  cv.addEventListener('pointerup', up);
  cv.addEventListener('pointercancel', up);
  cv.addEventListener('click', e => {
    if (touch && (touch.moved || touch.multi)) return;   // nach Verschieben oder Vergrößern ist das kein Tippen
    const at = pos(e), list = hits(at.x, at.y);
    if (list.length) {
      lastTap = null;
      onTap(list);
      return;
    }
    const now = performance.now();   // doppelt tippen: vergrößern bzw. wieder alles zeigen
    if (lastTap && now - lastTap.t < 350 && Math.hypot(at.x - lastTap.x, at.y - lastTap.y) < 30) {
      lastTap = null;
      if (st.s > 1.01) zoomTo(1, 0, 0);
      else zoomTo(2.5, at.x, at.y);
    } else lastTap = {t: now, x: at.x, y: at.y};
  });

  return {
    el: box,
    overlay(el) { over.replaceChildren(...[].concat(el || [])); },
    busy(on) { box.classList.toggle('busy', !!on); },
    set(d, level) {
      const url = d.radar ? (level && d.radar.lowerImage ? d.radar.lowerImage : d.radar.image) : '';
      if (url !== st.imgUrl) {
        st.imgUrl = url;
        st.img = null;
        radarImage(url).then(img => {
          if (st.imgUrl !== url) return;
          st.img = img;
          redraw();
        });
      }
      st.heat = heatCanvas(d);
      st.marks = d.marks || [];
      st.colors = d.ui.layers.map(l => [l.id, l.color]);
      cv.setAttribute('aria-label', 'Heatmap ' + (d.mapName || ''));
      redraw();
    },
  };
}

// ---- Punkte antippen: was dort war, Weg in die 2D-Ansicht (Route der 2D-Ansicht: #/replay/<Prüfsumme>?round=&tick=) ---
function openMarks(d, kind, list) {
  const match = m => d.matches[m[4]] || {};
  const to2d = m => '#/replay/' + encodeURIComponent(match(m).id || '') + '?round=' + m[5] + '&tick=' + m[6];
  const split = tip => {
    const i = tip.indexOf('\n');
    return i < 0 ? [tip, ''] : [tip.slice(0, i), tip.slice(i + 1)];
  };
  const color = id => (d.ui.layers.find(l => l.id === id) || {}).color;
  if (list.length === 1) {
    const m = list[0], [title, text] = split(m[7] || ''), mt = match(m);
    sheet({
      title, text,
      rows: kind === 'stats' && mt.label ? [['Match', mt.label]] : [],
      body: h('div', {class: 'hm-acts'},
        h('a', {class: 'btn primary block', href: to2d(m)}, 'In 2D ansehen'),
        kind === 'stats' && mt.id ? h('a', {class: 'btn block', href: '#/match/' + encodeURIComponent(mt.id)}, 'Match öffnen') : null),
    });
    return;
  }
  const shown = list.slice(0, 8);
  sheet({
    title: list.length + ' Punkte hier',
    text: list.length > shown.length ? 'Die nächsten ' + shown.length + ' – zum Auseinanderhalten vergrößern.' : 'Tippen öffnet die Stelle in der 2D-Ansicht.',
    body: h('div', {class: 'list hm-list'}, shown.map(m => {
      const [title, text] = split(m[7] || ''), mt = match(m);
      return h('a', {class: 'row', href: to2d(m)},
        h('span', {class: 'dot', style: {background: color(m[2])}}),
        h('div', {class: 'main'}, h('b', null, title), h('small', null, text + (kind === 'stats' && mt.label ? ' · ' + mt.label : ''))),
        h('span', {class: 'hm-2d'}, '2D'));
    })),
  });
}

// ---- Heatmap mit Auswahl, Legende und Schaltern ------------------------------------------------------------------------
// d: Antwort von /api/v1/heat; kind: "match" | "stats"; base: Teil der Adresse ohne Auswahl (siehe heatPath)
export function heatView(d, {kind, base}) {
  useCss('css/heat.css');
  const root = h('div', {class: 'hm'});
  if (d.empty) {
    root.append(emptyState(d.empty, d.empty.icon || 'map'));
    return root;
  }
  let cur = d, level = d.filter.lower ? 1 : 0, seq = 0;
  const sel = fromFilter(d.filter);
  const map = mapView(list => openMarks(cur, kind, list));
  const top = h('div'), legend = h('div'), bottom = h('div');
  root.append(top, map.el, legend, bottom);
  show(d);
  return root;

  function show(nd) {
    if (nd.empty) {
      root.replaceChildren(emptyState(nd.empty, nd.empty.icon || 'map'));
      return;
    }
    cur = nd;
    Object.assign(sel, fromFilter(nd.filter));
    level = nd.filter.lower ? 1 : 0;
    controls();
    map.set(nd, level);
    levels();
    legendPart();
    switches();
  }

  function chip(dot, label, on, click) {
    const b = h('button', {class: 'chip' + (on ? ' on' : ''), type: 'button', 'aria-pressed': on ? 'true' : 'false'}, dot, label);
    b.addEventListener('click', click);
    return b;
  }

  function controls() {   // „Zeigen“, „Spieler“, „Seite“ wie rechts neben der Karte in FragDesk
    const u = cur.ui, visible = u.who.reduce((a, w) => a | w.bit, 0);
    top.replaceChildren(
      h('div', {class: 'hm-cap'}, u.layersTitle),
      h('div', {class: 'hm-chips'}, u.layers.map(l => chip(h('span', {class: 'dot', style: {background: l.color}}), l.label,
        (sel.layers >> l.id) & 1, () => change({layers: sel.layers ^ (1 << l.id)})))),
      h('div', {class: 'hm-groups'},
        h('div', null, h('div', {class: 'hm-cap'}, u.whoTitle), h('div', {class: 'hm-chips'}, u.who.map(w => chip(null, w.label, sel.who & w.bit, () => {
          const who = sel.who & visible;
          if ((who & ~w.bit) || !(who & w.bit)) change({who: who ^ w.bit});   // eine Auswahl bleibt immer
        })))),
        h('div', null, h('div', {class: 'hm-cap'}, u.sidesTitle), h('div', {class: 'hm-chips'},
          u.sides.map(s => chip(null, s.label, sel.sides === s.value, () => { if (sel.sides !== s.value) change({sides: s.value}); }))))));
  }

  function levels() {   // Karten mit zwei Ebenen: Ebene oben links auf der Karte wählen
    const names = cur.ui.levels;
    map.overlay(names ? h('div', {class: 'hm-lv'}, names.map((name, i) => {
      const b = h('button', {class: i === level ? 'on' : '', type: 'button', 'aria-pressed': i === level ? 'true' : 'false'}, name);
      b.addEventListener('click', () => {
        if (i === level) return;
        level = i;
        levels();
        load();
      });
      return b;
    })) : null);
  }

  function legendPart() {
    const lg = cur.legend, out = [];
    if (cur.marksNote) out.push(h('p', {class: 'hm-note'}, cur.marksNote));
    if (lg && lg.items.length) {
      out.push(h('div', {class: 'hm-legend'},
        lg.gradient ? [h('div', {class: 'hm-grad'}), h('div', {class: 'hm-gl'}, h('span', null, cur.ui.low), h('span', null, cur.ui.high))] : null,
        lg.items.map(it => h('div', {class: 'hm-li'}, h('span', {class: 'dot', style: {background: it.color}}), h('span', null, it.label), h('b', null, it.value)))));
    }
    legend.replaceChildren(...out);
  }

  function switches() {
    const sw = (key, o) => {
      const on = !!sel[key];
      const b = h('button', {class: 'hm-sw', type: 'button', role: 'switch', 'aria-checked': on ? 'true' : 'false'},
        h('span', {class: 'tx'}, h('b', null, o.label), h('small', null, o.sub)), h('span', {class: 'tg'}));
      b.addEventListener('click', () => change({[key]: !on}));
      return b;
    };
    bottom.replaceChildren(h('div', {class: 'hm-sws'}, sw('marks', cur.ui.marks), sw('throws', cur.ui.throws)),
      cur.foot ? h('p', {class: 'hm-foot'}, cur.foot) : null);
  }

  // Auswahl ändern: sofort zeigen, merken, neu laden
  function change(patch) {
    Object.assign(sel, patch);
    store.set(KEYS[kind], Object.assign(store.get(KEYS[kind], null) || {}, patch));
    controls();
    switches();
    load();
  }

  async function load() {
    const path = heatPath(kind, base, level), my = ++seq;
    const old = api.cached(path);
    if (old) show(old);
    map.busy(true);
    let nd;
    try {
      nd = await api.get(path);
    } catch (e) {
      if (my !== seq) return;
      map.busy(false);
      if (e && e.status === 401) return;
      toast(e.message, true);
      if (!old) {   // nichts Neues: Auswahl zurück auf das, was die Karte zeigt
        const shown = fromFilter(cur.filter), saved = store.get(KEYS[kind], null) || {};
        for (const k of Object.keys(saved)) if (k in shown) saved[k] = shown[k];
        store.set(KEYS[kind], saved);
        Object.assign(sel, shown);
        level = cur.filter.lower ? 1 : 0;
        controls();
        levels();
        switches();
      }
      return;
    }
    if (my !== seq || !root.isConnected) return;
    map.busy(false);
    if (!old || JSON.stringify(old) !== JSON.stringify(nd)) show(nd);
  }
}
