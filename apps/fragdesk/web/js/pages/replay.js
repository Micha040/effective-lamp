// FragDesk Handy-App – 2D-Ansicht einer Runde (wie page_replay.cpp in FragDesk): die Karte von oben mit Spielern,
// Blickrichtung, Granaten, Kills und Bombe, dazu Zeitleiste, Tempo und Runden. Die Daten kommen je Runde von
// /api/v1/replay/<Prüfsumme>?round=<n> (Aufbau in src/api/view_replay.cpp); zwischen zwei Abtastungen wird weich
// verschoben wie in fd::playersAt. Gezeichnet wird nur, solange es abspielt und die Seite zu sehen ist.
//
// Adresse: #/replay/<Prüfsumme>?round=<n>&tick=<t> – mit tick (z. B. ein Kill aus Momenten oder Heatmap) beginnt die
// Ansicht wie in FragDesk 2 Sekunden davor.
import {h, icon, emptyState, useCss, toast} from '../ui.js';
import {radarImage} from '../radar.js';
import * as api from '../api.js';
import {store, setHash, errorBox} from '../app.js';

// ---- Konstanten wie in src/core/replay.h ------------------------------------------------------------------------
const F_CT = 1, F_BOMB = 2, F_DEFUSING = 4, F_PLANTING = 8, F_FLASHED = 64, F_KIT = 128;
const FX_SMOKE = 1, FX_FIRE = 2, FX_FLASH = 3, FX_HE = 4;
const B_PLANTED = 1, B_DEFUSED = 2, B_EXPLODED = 3;
const NO_PLAYER = 255;
const R_SMOKE = 144, R_FIRE = 110, R_FLASH = 220, R_HE = 350;   // Spieleinheiten
// Farben auf der (immer dunklen) Karte wie in FragDesk
const C_T = '#eab45e', C_CT = '#6fa8dc', C_BG = '#0e131b';
const NADE = {1: '#c9d1dc', 2: '#fff3b0', 3: '#f0634f', 4: '#ff8a2a', 5: '#ff8a2a', 6: '#9bc48c'};
const SPEEDS = [[0.5, '½×'], [1, '1×'], [2, '2×'], [4, '4×']];
const FONT = '-apple-system, BlinkMacSystemFont, "SF Pro Text", system-ui, "Segoe UI", Roboto, sans-serif';
const ZOOM = 2;   // Folgen: so viel näher als der ganze Ausschnitt
const reduceMotion = window.matchMedia('(prefers-reduced-motion: reduce)');

let view = null;   // die offene 2D-Ansicht (höchstens eine)

// Abspielen/Anhalten (ui.js kennt kein Anhalten-Symbol)
function playIcon(playing) {
  if (!playing) return icon('play');
  const ns = 'http://www.w3.org/2000/svg', svg = document.createElementNS(ns, 'svg'), path = document.createElementNS(ns, 'path');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('aria-hidden', 'true');
  path.setAttribute('d', 'M7 5h3.5v14H7zM13.5 5H17v14h-3.5z');
  path.setAttribute('fill', 'currentColor');
  svg.append(path);
  return svg;
}
let waitTimer = 0;

export async function render(ctx) {
  dispose();
  const css = cssReady();
  let id = ctx.params[0] || '';
  try { id = decodeURIComponent(id); } catch (e) { /* so lassen */ }
  const round = parseInt(ctx.query.get('round'), 10) || 0;
  const tick = ctx.query.has('tick') ? parseInt(ctx.query.get('tick'), 10) : NaN;
  ctx.title('2D-Ansicht');
  ctx.main.replaceChildren(loading());
  await css;
  if (!ctx.current()) return;
  await open(ctx, id, round, Number.isFinite(tick) ? tick : null, false);
}

// Erst zeichnen, wenn replay.css da ist (beim ersten Besuch kommt es parallel zu den Daten)
let cssWait = null;
function cssReady() {
  if (cssWait) return cssWait;
  useCss('css/replay.css');
  const link = document.querySelector('link[data-fd="css/replay.css"]');
  cssWait = !link || link.sheet ? Promise.resolve() : new Promise(done => {
    link.addEventListener('load', done, {once: true});
    link.addEventListener('error', done, {once: true});
    setTimeout(done, 1500);
  });
  return cssWait;
}

function loading() {
  return h('div', {class: 'rp-wait'}, h('div', {class: 'spinner'}));
}

// ---- Laden ------------------------------------------------------------------------------------------------------
// Eigene Anfrage statt api.get: die großen Antworten sollen nicht im Speicher der App bleiben, und 202 heißt
// „wird gerade erstellt“
async function fetchRound(id, n, retry) {
  const path = `/api/v1/replay/${encodeURIComponent(id)}?round=${n || 0}` + (retry ? '&retry=1' : '');
  const ctl = new AbortController();
  const timer = setTimeout(() => ctl.abort(), 30000);
  let res, data = null;
  try {
    res = await fetch(path, {credentials: 'same-origin', cache: 'no-store', signal: ctl.signal});
    try { data = await res.json(); } catch (e) { /* leer */ }
  } catch (e) {
    throw new api.ApiError(0, api.isServer() ? 'Keine Verbindung zum Server. Bist du online?'
      : 'Keine Verbindung zu FragDesk. Ist dein PC an und FragDesk offen? Unterwegs: Ist Tailscale an?');
  } finally {
    clearTimeout(timer);
  }
  if (!res.ok) {
    if (res.status === 401) window.dispatchEvent(new CustomEvent('fd:unpaired'));
    throw new api.ApiError(res.status, (data && data.error) || `Fehler ${res.status}`);
  }
  if (!data) throw new api.ApiError(res.status, 'Unerwartete Antwort');
  return data;
}

async function open(ctx, id, round, tick, retry) {
  let d;
  try {
    d = await fetchRound(id, round, retry);
  } catch (e) {
    if (!ctx.current() || (e && e.status === 401)) return;
    if (e && e.status === 404) ctx.main.replaceChildren(emptyState({title: 'Match nicht verfügbar', text: e.message}, 'warning'));
    else ctx.main.replaceChildren(errorBox(e, () => { ctx.main.replaceChildren(loading()); open(ctx, id, round, tick, false); }));
    return;
  }
  if (!ctx.current()) return;
  if (d.pending) return showMessage(ctx, d.pending, true, () => open(ctx, id, round, tick, false));
  if (d.missing) return showMessage(ctx, d.missing, false, () => { ctx.main.replaceChildren(loading()); open(ctx, id, round, tick, true); });
  start(ctx, id, decode(d), tick);
}

// Wird erstellt (Kreisel, fragt von selbst wieder) bzw. gibt es nicht (Hinweis, ggf. Knopf)
function showMessage(ctx, m, pending, again) {
  const kids = pending
    ? [h('div', {class: 'spinner'}), h('b', null, m.title), h('p', null, m.text)]
    : [emptyState({title: m.title, text: m.text}, 'map')];
  if (!pending && m.button) kids.push(h('button', {class: 'btn block', type: 'button', onclick: again}, icon('refresh'), m.button));
  ctx.main.replaceChildren(h('div', {class: pending ? 'rp-wait' : 'rp-missing'}, kids));
  if (!pending) return;
  clearTimeout(waitTimer);
  waitTimer = setTimeout(function poll() {
    if (!ctx.current()) return;
    if (document.hidden) {   // im Hintergrund nicht fragen – weiter, sobald die App wieder zu sehen ist
      document.addEventListener('visibilitychange', () => { if (ctx.current()) poll(); }, {once: true});
      return;
    }
    again();
  }, Math.max(1, m.retry || 3) * 1000);
}

// ---- Daten einer Runde entpacken (Aufbau in src/api/view_replay.cpp) --------------------------------------------
function undelta(list, stride, out) {   // [a, b, Δa, Δb, …] -> Spalten
  const n = list.length / stride;
  for (let c = 0; c < stride; c++) {
    const col = out[c];
    let v = 0;
    for (let i = 0; i < n; i++) {
      v = i ? v + list[i * stride + c] : list[c];
      col[i] = v;
    }
  }
}

function decode(d) {
  const nf = d.frames.length;
  const ticks = new Int32Array(nf);
  undelta(d.frames, 1, [ticks]);
  const tracks = d.tracks.map(tk => {
    const n = tk.x.length;
    const x = new Int16Array(n), y = new Int16Array(n), z = tk.z ? new Int16Array(n) : null, a = new Uint8Array(n);
    undelta(tk.x, 1, [x]);
    undelta(tk.y, 1, [y]);
    if (z) undelta(tk.z, 1, [z]);
    let yaw = 0;
    tk.a.forEach((v, i) => { yaw = i ? (yaw + v + 256) % 256 : v; a[i] = yaw; });
    const expand = list => {   // [Frame, Wert, …] (nur Änderungen) -> Wert je Frame
      const out = new Uint8Array(n);
      for (let k = 0; k < list.length; k += 2) out.fill(list[k + 1], list[k]);
      return out;
    };
    return {p: tk.p, f: tk.f, n, x, y, z, a, hp: expand(tk.hp), w: expand(tk.w), fl: expand(tk.fl)};
  });
  // Bombe je Frame
  const bomb = {s: new Uint8Array(nf), x: new Int16Array(nf), y: new Int16Array(nf), z: new Int16Array(nf)};
  for (let k = 0; k < d.bomb.length; k += 5) {
    const f = d.bomb[k];
    bomb.s.fill(d.bomb[k + 1], f);
    bomb.x.fill(d.bomb[k + 2], f);
    bomb.y.fill(d.bomb[k + 3], f);
    bomb.z.fill(d.bomb[k + 4], f);
  }
  const nades = d.nades.map(g => {
    const n = g.pts.length / 4;
    const cols = [new Int32Array(n), new Int16Array(n), new Int16Array(n), new Int16Array(n)];
    undelta(g.pts, 4, cols);
    return {k: g.k, ct: g.ct, n, t: cols[0], x: cols[1], y: cols[2], z: cols[3]};
  });
  const ns = d.shots.length / 2;
  const shotT = new Int32Array(ns), shotP = new Uint8Array(ns);
  undelta(d.shots, 2, [shotT, shotP]);
  for (let i = 0; i < ns; i++) shotP[i] = d.shots[i * 2 + 1];   // Spieler sind keine Abstände
  const players = new Map(d.players.map(p => [p.i, p]));
  const geo = d.radar || {posX: d.box[0], posY: d.box[1], span: d.box[2]};
  return {
    id: d.id, title: d.title, map: d.map, radar: d.radar, geo, fit: d.fit || [0.5, 0.5, 1],
    lower: !!(d.radar && d.radar.lowerImage), lowerZ: d.radar ? d.radar.lowerZ : 0,
    tickrate: d.tickrate || 64, interval: d.interval || 8, rounds: d.rounds, round: d.round, players,
    roster: d.players.filter(p => !p.x), weapons: d.weapons, ticks, tracks, bomb, nades, shotT, shotP,
    effects: d.effects.map(e => Object.assign(e, {hull: e.hull ? pairs(e.hull) : null})), kills: d.kills, bombev: d.bombev,
  };
}

function pairs(list) {
  const out = [];
  for (let i = 0; i + 1 < list.length; i += 2) out.push([list[i], list[i + 1]]);
  return out;
}

// ---- Positionen zu einem Zeitpunkt (wie fd::playersAt) -----------------------------------------------------------
function frameAt(ticks, t) {   // letzter Frame mit Tick <= t (0, wenn t davor liegt)
  let lo = 0, hi = ticks.length - 1;
  if (hi < 0 || t < ticks[0]) return 0;
  while (lo < hi) {
    const mid = (lo + hi + 1) >> 1;
    if (ticks[mid] <= t) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

function playersAt(R, t) {
  const out = [], n = R.ticks.length;
  if (!n) return out;
  const i = frameAt(R.ticks, t), a = R.ticks[i];
  let b = i + 1 < n ? R.ticks[i + 1] : -1;
  if (b >= 0 && b - a > R.interval * 2 + 2) b = -1;   // Lücke: nicht über mehr als zwei Abstände hinweg schieben
  const f = b >= 0 ? Math.min(1, Math.max(0, (t - a) / (b - a))) : 0;
  for (const tk of R.tracks) {
    const k = i - tk.f;
    if (k < 0 || k >= tk.n) continue;
    const p = {p: tk.p, x: tk.x[k], y: tk.y[k], z: tk.z ? tk.z[k] : 0, yaw: tk.a[k] * 360 / 256, hp: tk.hp[k], w: tk.w[k], fl: tk.fl[k]};
    if (b >= 0 && k + 1 < tk.n) {
      const dx = tk.x[k + 1] - p.x, dy = tk.y[k + 1] - p.y;
      if (dx * dx + dy * dy < 600 * 600) {   // größere Sprünge sind Neustarts, nicht Bewegung
        p.x += f * dx;
        p.y += f * dy;
        if (tk.z) p.z += f * (tk.z[k + 1] - tk.z[k]);
        let dyaw = (tk.a[k + 1] - tk.a[k]) * 360 / 256;
        if (dyaw > 180) dyaw -= 360;
        if (dyaw < -180) dyaw += 360;
        p.yaw = (p.yaw + f * dyaw + 360) % 360;
        if (f > 0.5) {   // nur im selben Leben übernehmen
          p.hp = tk.hp[k + 1];
          p.w = tk.w[k + 1];
          p.fl = tk.fl[k + 1];
        }
      }
    }
    out.push(p);
  }
  return out;
}

// ---- Seite --------------------------------------------------------------------------------------------------------
function start(ctx, id, R, tick) {
  const v = view = {
    ctx, id, R, cache: new Map([[R.round.n, R]]), t: 0, playing: false, follow: -1, zoom: true, level: 0,
    speed: SPEEDS.some(s => s[0] === store.get('rpSpeed', 1)) ? store.get('rpSpeed', 1) : 1, names: store.get('rpNames', true) !== false,
    cam: null, raf: 0, last: 0, animUntil: 0, listAt: 0, feedKey: '', killKey: '', ps: [], hits: [], imgs: [null, null], el: {}, off: [],
    preload: 0,
  };
  build(v);
  setRound(v, R, tick == null ? null : tick - 2 * R.tickrate);
  const on = (target, type, fn, opts) => {
    target.addEventListener(type, fn, opts);
    v.off.push(() => target.removeEventListener(type, fn, opts));
  };
  // weg von der Seite: alles anhalten (hashchange kommt erst nach dem Wechsel der Seite an)
  on(window, 'hashchange', () => { if (!ctx.current()) dispose(); });
  on(document, 'visibilitychange', () => {
    if (!alive(v)) return dispose();
    if (document.hidden) pause(v);   // im Hintergrund nichts zeichnen (Akku)
    else redraw(v);
  });
  on(document, 'keydown', ev => {
    if (!alive(v)) return dispose();
    if (ev.target && /^(INPUT|SELECT|TEXTAREA)$/.test(ev.target.tagName)) return;
    const tr = v.R.tickrate;
    if (ev.key === ' ') {
      if (ev.target && ev.target.tagName === 'BUTTON') return;   // der Knopf selbst reagiert schon
      togglePlay(v);
    }
    else if (ev.key === 'ArrowLeft') seek(v, v.t - 5 * tr);
    else if (ev.key === 'ArrowRight') seek(v, v.t + 5 * tr);
    else if (ev.key === 'ArrowUp' && v.R.round.prev) gotoRound(v, v.R.round.prev, v.playing);
    else if (ev.key === 'ArrowDown' && v.R.round.next) gotoRound(v, v.R.round.next, v.playing);
    else return;
    ev.preventDefault();
  });
  const ro = new ResizeObserver(() => { if (alive(v)) resize(v); });
  ro.observe(v.el.stage);
  v.off.push(() => ro.disconnect());
  resize(v);
}

function alive(v) { return view === v && v.ctx.current() && v.el.canvas.isConnected; }

function dispose() {
  clearTimeout(waitTimer);
  const v = view;
  view = null;
  if (!v) return;
  if (v.raf) cancelAnimationFrame(v.raf);
  v.raf = 0;
  v.playing = false;
  for (const off of v.off) off();
  v.off = [];
  v.cache.clear();
}

function build(v) {
  const el = v.el;
  el.title = h('b');
  el.result = h('small');
  el.prev = h('button', {class: 'iconbtn', type: 'button', 'aria-label': 'Vorige Runde'}, icon('back'));
  el.next = h('button', {class: 'iconbtn', type: 'button', 'aria-label': 'Nächste Runde'}, icon('chev'));
  el.prev.addEventListener('click', () => { if (v.R.round.prev) gotoRound(v, v.R.round.prev, v.playing); });
  el.next.addEventListener('click', () => { if (v.R.round.next) gotoRound(v, v.R.round.next, v.playing); });
  const head = h('div', {class: 'rp-head'}, h('div', {class: 'rp-ttl'}, el.title, el.result), el.prev, el.next);
  el.strip = h('div', {class: 'rp-strip', 'aria-label': 'Runden'});

  el.canvas = h('canvas', {class: 'rp-canvas', role: 'img', 'aria-label': 'Karte der Runde von oben – tippen: abspielen oder anhalten, auf einen Spieler: ihm folgen'});
  el.canvas.addEventListener('click', ev => tapMap(v, ev));
  el.feed = h('div', {class: 'rp-feed'});
  el.note = h('div', {class: 'rp-note hidden'});
  el.empty = h('div', {class: 'rp-empty hidden'}, 'Für diesen Zeitpunkt gibt es keine Positionsdaten');
  el.upper = h('button', {type: 'button', class: 'on'}, 'Oben');
  el.lower = h('button', {type: 'button'}, 'Unten');
  el.upper.addEventListener('click', () => setLevel(v, 0));
  el.lower.addEventListener('click', () => setLevel(v, 1));
  el.levels = h('div', {class: 'rp-levels hidden', role: 'group', 'aria-label': 'Ebene'}, el.upper, el.lower);
  el.stage = h('div', {class: 'rp-stage'}, el.canvas, el.feed, el.levels, el.note, el.empty);

  el.play = h('button', {class: 'rp-play', type: 'button', 'aria-label': 'Abspielen'}, icon('play'));
  el.play.addEventListener('click', () => togglePlay(v));
  el.time = h('b');
  el.sub = h('small');
  el.fill = h('div', {class: 'rp-fill'});
  el.freeze = h('div', {class: 'rp-frz'});
  el.marks = h('div', {class: 'rp-marks'});
  el.thumb = h('div', {class: 'rp-thumb'});
  el.tli = h('div', {class: 'rp-tli'}, h('div', {class: 'rp-track'}, el.fill), el.freeze, el.marks, el.thumb);
  el.tl = h('div', {class: 'rp-tl', role: 'slider', tabindex: '0', 'aria-label': 'Zeit in der Runde'}, el.tli);
  scrubber(v);
  const bar = h('div', {class: 'rp-bar'}, el.play, h('div', {class: 'rp-clock'}, el.time, el.sub), el.tl);

  el.speeds = SPEEDS.map(([s, label]) => {
    const b = h('button', {type: 'button', class: s === v.speed ? 'on' : ''}, label);
    b.addEventListener('click', () => {
      v.speed = s;
      store.set('rpSpeed', s);
      el.speeds.forEach((x, i) => x.classList.toggle('on', SPEEDS[i][0] === s));
    });
    return b;
  });
  el.names = h('button', {type: 'button', class: 'rp-chip' + (v.names ? ' on' : ''), 'aria-pressed': String(v.names)}, 'Namen');
  el.names.addEventListener('click', () => {
    v.names = !v.names;
    store.set('rpNames', v.names);
    el.names.classList.toggle('on', v.names);
    el.names.setAttribute('aria-pressed', String(v.names));
    redraw(v);
  });
  el.zoomBtn = h('button', {type: 'button', class: 'rp-chip', disabled: true, 'aria-pressed': 'false'}, icon('target'), 'Nah');
  el.zoomBtn.addEventListener('click', () => {
    v.zoom = !v.zoom;
    syncFollow(v);
    animate(v);
  });
  const opts = h('div', {class: 'rp-opts'}, h('div', {class: 'rp-seg', role: 'group', 'aria-label': 'Tempo'}, el.speeds), el.zoomBtn, el.names);

  el.teams = h('div', {class: 'rp-teams'});
  el.kills = h('div', {class: 'rp-kills'});
  const more = h('div', {class: 'rp-more'},
    el.teams,
    h('section', {class: 'card rp-kcard'}, h('div', {class: 'card-head'}, h('div', {class: 'h'}, h('b', null, 'Kills'),
      h('small', null, 'Tippen: 2 Sekunden davor ansehen'))), el.kills),
    h('p', {class: 'rp-hint'}, 'Tippe auf die Karte zum Abspielen oder Anhalten, auf einen Spieler, um ihm zu folgen. Positionen gibt es 8-mal pro '
      + 'Sekunde, dazwischen wird weich verschoben – für genaue Pixel-Duelle ist die Demo in CS2 das Original.'));
  v.ctx.main.replaceChildren(h('div', {class: 'rp'}, head, el.strip, el.stage, bar, opts, more));
  el.g = el.canvas.getContext('2d');
}

// ---- Runde wechseln -------------------------------------------------------------------------------------------------
function setRound(v, R, at) {
  const el = v.el, old = v.R;
  v.R = R;
  v.preload = 0;
  const r = R.round, tr = R.tickrate;
  v.t = at == null ? r.tick : Math.max(r.start, Math.min(r.end, at));
  v.last = 0;
  v.feedKey = v.killKey = '';
  v.ctx.title('2D-Ansicht · ' + R.map);
  setHash('#/replay/' + encodeURIComponent(v.id) + '?round=' + r.n);
  el.title.textContent = r.title;
  el.result.textContent = r.result + ' · ' + r.score;
  el.prev.disabled = !r.prev;
  el.next.disabled = !r.next;
  // Radarbilder (einmal je Karte)
  if (!old || old.map !== R.map || el.strip.childElementCount === 0) {
    v.imgs = [null, null];
    el.levels.classList.toggle('hidden', !R.lower);
    if (R.radar) {
      radarImage(R.radar.image).then(img => { if (view === v) { v.imgs[0] = img; redraw(v); } });
      if (R.lower) radarImage(R.radar.lowerImage).then(img => { if (view === v) { v.imgs[1] = img; redraw(v); } });
    }
  }
  roundStrip(v);
  // Zeitleiste: Ende der Kaufzeit, Kills (oben, Farbe des Schützen), Bombe (unten)
  const span = Math.max(1, r.end - r.start), pos = tick => (Math.max(0, Math.min(1, (tick - r.start) / span)) * 100).toFixed(2) + '%';
  el.freeze.style.left = pos(r.freeze);
  el.marks.replaceChildren(
    ...R.kills.map(k => h('i', {class: 'rp-mk ' + (k.kct ? 'ct' : 't'), style: {left: pos(k.t)}, title: k.time + '  ' + k.text})),
    ...R.bombev.map(b => h('i', {class: 'rp-mb b' + b.k, style: {left: pos(b.t)}, title: b.time + '  ' + b.text})));
  el.tl.setAttribute('aria-valuemin', '0');
  el.tl.setAttribute('aria-valuemax', String(Math.round(span / tr)));
  teamList(v);
  note(v, R.radar ? '' : 'Kein Radarbild für diese Karte – Positionen auf Raster');
  redraw(v);
}

function roundStrip(v) {
  const R = v.R, strip = v.el.strip;
  if (strip.childElementCount !== R.rounds.length) {
    strip.replaceChildren();
    let g = -1;
    for (const r of R.rounds) {
      if (g >= 0 && r.g !== g) strip.append(h('span', {class: 'rp-gap'}));
      g = r.g;
      const b = h('button', {type: 'button', class: 'rp-r ' + r.win, 'aria-label': r.tip, title: r.tip, 'data-n': r.n}, String(r.n));
      b.addEventListener('click', () => { if (r.n !== v.R.round.n) gotoRound(v, r.n, v.playing); });
      strip.append(b);
    }
  }
  let sel = null;
  for (const b of strip.querySelectorAll('.rp-r')) {
    const on = Number(b.dataset.n) === R.round.n;
    b.classList.toggle('on', on);
    b.setAttribute('aria-current', on ? 'true' : 'false');
    if (on) sel = b;
  }
  if (sel) {
    const want = sel.offsetLeft - (strip.clientWidth - sel.offsetWidth) / 2;
    if (sel.offsetLeft < strip.scrollLeft || sel.offsetLeft + sel.offsetWidth > strip.scrollLeft + strip.clientWidth) strip.scrollLeft = want;
  }
}

async function loadRound(v, n) {
  if (v.cache.has(n)) return v.cache.get(n);
  const d = await fetchRound(v.id, n, false);
  if (!d || !d.round) return null;   // Datei inzwischen weg (z. B. Match neu ausgewertet)
  const R = decode(d);
  if (view !== v) return R;
  v.cache.set(R.round.n, R);
  while (v.cache.size > 3) {   // die aktuelle, die nächste und die vorige reichen
    const drop = [...v.cache.keys()].find(k => k !== v.R.round.n && k !== R.round.n);
    v.cache.delete(drop);
  }
  return R;
}

// Runde wechseln; andThen: danach abspielen (wie in FragDesk läuft es weiter, wenn es lief)
async function gotoRound(v, n, andThen) {
  pause(v);
  v.el.strip.classList.add('busy');
  try {
    const R = await loadRound(v, n);
    if (!alive(v)) return;
    if (!R) return v.ctx.rerender();
    setRound(v, R, null);
    if (andThen) play(v);
  } catch (e) {
    if (alive(v) && e.status !== 401) toast((e && e.message) || String(e), true);
  } finally {
    v.el.strip.classList.remove('busy');
  }
}


// ---- Abspielen ------------------------------------------------------------------------------------------------------
function togglePlay(v) {
  if (v.playing) pause(v);
  else play(v);
}

function play(v) {
  const r = v.R.round;
  if (v.t >= r.end - 1) v.t = r.tick;   // am Ende: von vorn
  v.playing = true;
  v.last = 0;
  syncPlay(v);
  loop(v);
}

function pause(v) {
  if (!v.playing) return;
  v.playing = false;
  syncPlay(v);
  redraw(v);
}

function syncPlay(v) {
  const b = v.el.play;
  b.replaceChildren(playIcon(v.playing));
  b.setAttribute('aria-label', v.playing ? 'Anhalten' : 'Abspielen');
  b.classList.toggle('on', v.playing);
}

function seek(v, t) {
  const r = v.R.round;
  v.t = Math.max(r.start, Math.min(r.end, t));
  v.last = 0;
  redraw(v);
}

// Ein Bild zeichnen, sobald der Browser so weit ist (mehrere Anstöße werden eins)
function redraw(v) {
  if (!v.raf && alive(v)) v.raf = requestAnimationFrame(now => tick(v, now));
}

function loop(v) { redraw(v); }

function animate(v) {   // kurze Bewegung der Karte (Folgen, Nah), auch wenn nichts abspielt
  v.animUntil = performance.now() + (reduceMotion.matches ? 0 : 450);
  redraw(v);
}

function tick(v, now) {
  v.raf = 0;
  if (!alive(v)) return dispose();
  const dt = v.last ? Math.min(0.25, (now - v.last) / 1000) : 0;
  v.last = now;
  const R = v.R, r = R.round;
  if (v.playing) {
    v.t += dt * R.tickrate * v.speed;
    // gegen Ende schon die nächste Runde holen, damit es ohne Pause weitergeht
    if (r.next && !v.preload && v.t > r.end - 12 * R.tickrate) {
      v.preload = r.next;
      loadRound(v, r.next).catch(() => {});
    }
    if (v.t >= r.end) {
      v.t = r.end;
      const next = r.next && v.cache.get(r.next);
      if (next) {   // weiter mit der nächsten Runde
        setRound(v, next, null);
        v.playing = true;
      } else {
        v.playing = false;
        syncPlay(v);
        if (r.next) gotoRound(v, r.next, true);   // noch nicht da: holen und dann weiter
      }
    }
  }
  draw(v, dt);
  if ((v.playing || now < v.animUntil) && !v.raf) v.raf = requestAnimationFrame(n => tick(v, n));
  else if (!v.raf) v.last = 0;
}

// ---- Größe ----------------------------------------------------------------------------------------------------------
function resize(v) {
  const c = v.el.canvas, w = Math.max(1, Math.round(v.el.stage.clientWidth));
  const dpr = Math.min(3, window.devicePixelRatio || 1);
  if (w === v.W && dpr === v.dpr) return;
  v.W = w;
  v.dpr = dpr;
  c.width = Math.round(w * dpr);
  c.height = Math.round(w * dpr);
  c.style.width = c.style.height = w + 'px';
  redraw(v);
}

// ---- Folgen, Ebene, Antippen -----------------------------------------------------------------------------------------
function setFollow(v, p) {
  v.follow = p;
  v.zoom = true;
  syncFollow(v);
  animate(v);
}

function syncFollow(v) {
  const b = v.el.zoomBtn, on = v.follow >= 0;
  b.disabled = !on;
  b.classList.toggle('on', on && v.zoom);
  b.setAttribute('aria-pressed', String(on && v.zoom));
  v.listAt = 0;   // Liste sofort auffrischen (Hervorhebung)
}

function setLevel(v, level) {
  if (v.level === level) return;
  v.level = level;
  v.el.upper.classList.toggle('on', level === 0);
  v.el.lower.classList.toggle('on', level === 1);
  redraw(v);
}

function tapMap(v, ev) {
  const rect = v.el.canvas.getBoundingClientRect();
  const x = ev.clientX - rect.left, y = ev.clientY - rect.top;
  let best = -1, bd = 26 * 26;
  for (const hp of v.hits) {
    const d = (hp.x - x) ** 2 + (hp.y - y) ** 2;
    if (d < bd) { bd = d; best = hp.p; }
  }
  if (best >= 0) setFollow(v, v.follow === best ? -1 : best);
  else togglePlay(v);
}

// ---- Zeitleiste: ziehen oder tippen springt ---------------------------------------------------------------------------
function scrubber(v) {
  const tl = v.el.tl;
  let drag = false, was = false;
  const at = ev => {
    const r = v.el.tli.getBoundingClientRect(), round = v.R.round;
    const k = Math.max(0, Math.min(1, (ev.clientX - r.left) / Math.max(1, r.width)));
    seek(v, round.start + k * (round.end - round.start));
  };
  tl.addEventListener('pointerdown', ev => {
    drag = true;
    was = v.playing;
    if (was) pause(v);
    tl.setPointerCapture(ev.pointerId);
    tl.classList.add('drag');
    at(ev);
  });
  tl.addEventListener('pointermove', ev => { if (drag) at(ev); });
  const end = () => {
    if (!drag) return;
    drag = false;
    tl.classList.remove('drag');
    if (was) play(v);
  };
  tl.addEventListener('pointerup', end);
  tl.addEventListener('pointercancel', end);
  tl.addEventListener('keydown', ev => {
    const tr = v.R.tickrate;
    if (ev.key === 'ArrowLeft' || ev.key === 'ArrowRight') {
      seek(v, v.t + (ev.key === 'ArrowLeft' ? -5 : 5) * tr);
      ev.preventDefault();
      ev.stopPropagation();
    }
  });
}

// ---- Zeichnen -----------------------------------------------------------------------------------------------------
function clock(secs) {
  const s = Math.floor(Math.max(0, secs));
  return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
}

function shortName(n, max) { return n.length > max ? n.slice(0, max - 1) + '…' : n; }

function rgba(hex, a) {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${n >> 16},${(n >> 8) & 255},${n & 255},${a})`;
}

function onLevel(v, z) {   // liegt die Höhe z auf der gezeigten Ebene?
  return !v.R.lower || (z < v.R.lowerZ) === (v.level === 1);
}

function draw(v, dt) {
  const R = v.R, r = R.round, g = v.el.g, W = v.W, t = v.t, tr = R.tickrate;
  if (!g || !W) return;
  const ps = t >= r.start - tr ? playersAt(R, t) : [];
  v.ps = ps;
  // Kamera: der Ausschnitt, in dem sich im Match jemand bewegt (fit); beim Folgen näher ran und mit
  const geo = R.geo;
  const uvOf = (x, y) => [(x - geo.posX) / geo.span, (geo.posY - y) / geo.span];
  let [tu, tv] = R.fit, tz = 1 / R.fit[2];
  if (v.follow >= 0) {
    const fp = ps.find(p => p.p === v.follow);
    if (fp && R.lower) {
      const lv = fp.z < R.lowerZ ? 1 : 0;
      if (lv !== v.level) setLevel(v, lv);
    }
    if (v.zoom) {
      tz *= ZOOM;
      if (fp) [tu, tv] = uvOf(fp.x, fp.y);
      else if (v.cam) [tu, tv] = [v.cam.u, v.cam.v];   // tot: Karte bleibt, wo sie war
    }
  }
  const lim = 0.5 / tz;
  tu = Math.max(lim, Math.min(1 - lim, tu));
  tv = Math.max(lim, Math.min(1 - lim, tv));
  if (!v.cam || reduceMotion.matches || dt === 0 || dt > 0.2) v.cam = {u: tu, v: tv, z: tz};
  else {
    const k = 1 - Math.exp(-dt * 7);
    v.cam.u += (tu - v.cam.u) * k;
    v.cam.v += (tv - v.cam.v) * k;
    v.cam.z += (tz - v.cam.z) * k;
  }
  const cam = v.cam, S = W * cam.z;
  const X = u => W / 2 + (u - cam.u) * S, Y = w => W / 2 + (w - cam.v) * S;
  const P = (x, y) => [X((x - geo.posX) / geo.span), Y((geo.posY - y) / geo.span)];
  const px = units => units / geo.span * S;

  g.setTransform(v.dpr, 0, 0, v.dpr, 0, 0);
  g.fillStyle = C_BG;
  g.fillRect(0, 0, W, W);
  // Radarbild (ohne: Raster)
  const img = v.imgs[R.lower ? v.level : 0];
  if (img) {
    g.imageSmoothingQuality = 'high';
    g.drawImage(img, X(0), Y(0), S, S);
  } else {
    g.strokeStyle = 'rgba(255,255,255,0.1)';
    g.lineWidth = 1;
    g.beginPath();
    for (let i = 0; i <= 8; i++) {
      const gx = X(i / 8), gy = Y(i / 8);
      g.moveTo(gx, Y(0));
      g.lineTo(gx, Y(1));
      g.moveTo(X(0), gy);
      g.lineTo(X(1), gy);
    }
    g.stroke();
  }

  // Wirkung der Granaten
  for (const e of R.effects) {
    if (e.s > t || t > e.e || !onLevel(v, e.z)) continue;
    const age = (t - e.s) / tr, dur = Math.max(0.01, (e.e - e.s) / tr);
    const [ex, ey] = P(e.x, e.y);
    if (e.k === FX_SMOKE) {
      const fade = Math.max(0, Math.min(1, age / 0.8 + 0.3, (dur - age) / 1.5));
      const rad = px(R_SMOKE) * Math.min(1, 0.55 + age / 1.2);
      circle(g, ex, ey, rad, `rgba(196,203,212,${(0.67 * fade).toFixed(3)})`);
      ring(g, ex, ey, rad, `rgba(230,235,242,${(0.78 * fade).toFixed(3)})`, 1.2);
    } else if (e.k === FX_FIRE) {
      const fade = Math.max(0, Math.min(1, age / 0.5 + 0.2, (dur - age) / 1.0));
      const fill = `rgba(255,120,40,${(0.47 * fade).toFixed(3)})`, edge = `rgba(255,150,60,${(0.86 * fade).toFixed(3)})`;
      if (e.hull && e.hull.length >= 3) {
        g.beginPath();
        e.hull.forEach(([hx, hy], i) => {
          const [qx, qy] = P(hx, hy);
          if (i) g.lineTo(qx, qy);
          else g.moveTo(qx, qy);
        });
        g.closePath();
        g.fillStyle = fill;
        g.fill();
        g.strokeStyle = edge;
        g.lineWidth = 1.2;
        g.stroke();
      } else circle(g, ex, ey, px(R_FIRE), fill);
    } else if (e.k === FX_FLASH) {
      const k = Math.min(1, age / dur);
      circle(g, ex, ey, px(R_FLASH) * (0.3 + 0.7 * k), `rgba(255,250,225,${(0.75 * (1 - k)).toFixed(3)})`);
    } else if (e.k === FX_HE) {
      const k = Math.min(1, age / dur);
      ring(g, ex, ey, px(R_HE) * (0.2 + 0.8 * k), `rgba(255,110,70,${(0.9 * (1 - k)).toFixed(3)})`, 3);
      circle(g, ex, ey, px(90), `rgba(255,140,80,${(0.63 * (1 - k)).toFixed(3)})`);
    } else ring(g, ex, ey, 5, 'rgba(155,196,140,0.78)', 1.5);   // Decoy
  }

  // Granaten im Flug mit Spur
  for (const n of R.nades) {
    if (!n.n || n.t[0] > t || n.t[n.n - 1] + tr * 0.15 < t) continue;
    if (!onLevel(v, n.z[0])) continue;
    const col = NADE[n.k] || NADE[6];
    g.beginPath();
    let hx = 0, hy = 0;
    for (let i = 0; i < n.n; i++) {
      let [qx, qy] = P(n.x[i], n.y[i]);
      if (n.t[i] > t && i > 0) {   // Kopf zwischen zwei Punkten
        const [ax, ay] = P(n.x[i - 1], n.y[i - 1]), k = (t - n.t[i - 1]) / Math.max(1, n.t[i] - n.t[i - 1]);
        qx = ax + (qx - ax) * k;
        qy = ay + (qy - ay) * k;
        g.lineTo(qx, qy);
        hx = qx;
        hy = qy;
        break;
      }
      if (i) g.lineTo(qx, qy);
      else g.moveTo(qx, qy);
      hx = qx;
      hy = qy;
    }
    g.strokeStyle = rgba(col, 0.6);
    g.lineWidth = 1.4;
    g.stroke();
    circle(g, hx, hy, 3.6, col);
    ring(g, hx, hy, 3.6, 'rgba(0,0,0,0.63)', 1);
  }

  // Bombe (liegt oder gelegt) laut Frame, Explosion
  if (R.ticks.length) {
    const fi = frameAt(R.ticks, t), state = R.bomb.s[fi];
    if (state && R.ticks[fi] >= r.start && onLevel(v, R.bomb.z[fi])) {
      const [bx, by] = P(R.bomb.x[fi], R.bomb.y[fi]);
      if (state === 2) {
        const pulse = reduceMotion.matches ? 0.5 : 0.5 + 0.5 * Math.sin(t / tr * 6);
        ring(g, bx, by, 9 + pulse * 4, `rgba(255,70,60,${((120 + 100 * pulse) / 255).toFixed(3)})`, 2);
        rect(g, bx - 7, by - 5, 14, 10, 2, '#e5483b');
      } else rect(g, bx - 6, by - 4.5, 12, 9, 2, '#e8a23a');
      label(g, 'C4', bx, by + 7, '#ffffff', 10);
    }
  }
  for (const b of R.bombev) {
    if (b.k !== B_EXPLODED || b.t > t) continue;
    const age = (t - b.t) / tr;
    if (age > 1.2 || !onLevel(v, b.z)) continue;
    const [bx, by] = P(b.x, b.y);
    circle(g, bx, by, px(600) * (age / 1.2), `rgba(255,160,60,${(0.67 * (1 - age / 1.2)).toFixed(3)})`);
  }

  const rad = Math.max(6.5, W / 80);   // Spielerpunkt

  // Tode dieser Runde: Kreuz, kurz danach eine Linie vom Schützen
  const xs = rad * 0.8;
  for (const k of R.kills) {
    if (k.t > t) break;
    if (!onLevel(v, k.vp[2])) continue;
    const [vx, vy] = P(k.vp[0], k.vp[1]);
    const age = (t - k.t) / tr;
    if (age < 1.5 && k.k !== NO_PLAYER) {
      const [kx, ky] = P(k.kp[0], k.kp[1]);
      g.beginPath();
      g.moveTo(kx, ky);
      g.lineTo(vx, vy);
      g.strokeStyle = `rgba(255,255,255,${(0.78 * (1 - age / 1.5)).toFixed(3)})`;
      g.lineWidth = 1.3;
      g.stroke();
    }
    g.beginPath();
    g.moveTo(vx - xs, vy - xs);
    g.lineTo(vx + xs, vy + xs);
    g.moveTo(vx + xs, vy - xs);
    g.lineTo(vx - xs, vy + xs);
    g.strokeStyle = rgba(k.vct ? C_CT : C_T, age < 3 ? 1 : 0.63);
    g.lineWidth = 2.4;
    g.lineCap = 'round';
    g.stroke();
    g.lineCap = 'butt';
  }

  // wer hat gerade geschossen?
  const shot = new Set();
  {
    const from = t - 0.12 * tr;
    let lo = 0, hi = R.shotT.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (R.shotT[mid] < from) lo = mid + 1;
      else hi = mid;
    }
    for (let i = lo; i < R.shotT.length && R.shotT[i] <= t; i++) shot.add(R.shotP[i]);
  }

  // Spieler (die auf der anderen Ebene nur als blasser Ring)
  const hits = [];
  const pulse = reduceMotion.matches ? 0.5 : 0.5 + 0.5 * Math.sin(t / tr * 8);
  for (const p of ps) {
    const info = R.players.get(p.p);
    if (!info) continue;
    const [x, y] = P(p.x, p.y);
    const ct = (p.fl & F_CT) !== 0, col = ct ? C_CT : C_T;
    if (!onLevel(v, p.z)) {
      ring(g, x, y, rad * 0.7, rgba(col, 0.45), 1.5);
      continue;
    }
    hits.push({p: p.p, x, y});
    const focus = v.follow === p.p, me = !!info.me;
    // Blickrichtung: Spielwinkel gegen den Uhrzeigersinn ab Osten (Bildschirm: y nach unten)
    const yaw = -p.yaw * Math.PI / 180, cone = rad * 4.2, half = 22 * Math.PI / 180;
    g.beginPath();
    g.moveTo(x, y);
    g.arc(x, y, cone, yaw - half, yaw + half);
    g.closePath();
    g.fillStyle = rgba(col, focus ? 0.59 : 0.41);
    g.fill();
    if (shot.has(p.p)) {
      g.beginPath();
      g.moveTo(x + Math.cos(yaw) * rad, y + Math.sin(yaw) * rad);
      g.lineTo(x + Math.cos(yaw) * rad * 8, y + Math.sin(yaw) * rad * 8);
      g.strokeStyle = 'rgba(255,236,140,0.9)';
      g.lineWidth = 1.6;
      g.stroke();
    }
    if (p.fl & F_FLASHED) circle(g, x, y, rad + 5, 'rgba(255,255,255,0.59)');
    if (focus || me) ring(g, x, y, rad + 3.5, me && !focus ? 'rgba(230,234,240,0.8)' : '#ffffff', 1.6);
    circle(g, x, y, rad, col);
    // HP als Ring um den Punkt: voll = 100, der Rest dunkel
    ring(g, x, y, rad, 'rgba(0,0,0,0.75)', 2);
    if (p.hp > 0) {
      g.beginPath();
      g.arc(x, y, rad, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * Math.min(p.hp, 100) / 100);
      g.strokeStyle = p.hp > 50 ? '#eaf2ff' : p.hp > 20 ? '#f5c04a' : '#f0634f';
      g.lineWidth = 2;
      g.stroke();
    }
    if (p.fl & F_BOMB) rect(g, x + rad - 2, y - rad - 4, 8, 6, 1.5, '#e5483b');
    if (p.fl & (F_DEFUSING | F_PLANTING)) ring(g, x, y, rad + 6 + pulse * 3, 'rgba(255,210,90,0.86)', 1.6);
    if (v.names || focus) label(g, shortName(info.name, 12), x, y + rad + 2, focus ? '#ffffff' : '#e6eaf0', 11);
  }
  v.hits = hits;
  const empty = !ps.length && (t >= r.freeze || !R.ticks.length);
  if (v.el.empty.classList.contains('hidden') === empty) v.el.empty.classList.toggle('hidden', !empty);

  status(v);
}

function circle(g, x, y, r, fill) {
  if (r <= 0) return;
  g.beginPath();
  g.arc(x, y, r, 0, 2 * Math.PI);
  g.fillStyle = fill;
  g.fill();
}

function ring(g, x, y, r, stroke, width) {
  if (r <= 0) return;
  g.beginPath();
  g.arc(x, y, r, 0, 2 * Math.PI);
  g.strokeStyle = stroke;
  g.lineWidth = width;
  g.stroke();
}

function rect(g, x, y, w, hh, r, fill) {
  g.beginPath();
  if (g.roundRect) g.roundRect(x, y, w, hh, r);
  else g.rect(x, y, w, hh);
  g.fillStyle = fill;
  g.fill();
}

// Text mit dunklem Schatten (auf der Karte lesbar)
function label(g, text, x, y, color, size) {
  g.font = `600 ${size}px ${FONT}`;
  g.textAlign = 'center';
  g.textBaseline = 'top';
  g.fillStyle = 'rgba(0,0,0,0.82)';
  g.fillText(text, x + 1, y + 1);
  g.fillStyle = color;
  g.fillText(text, x, y);
}

function note(v, text) {
  const n = v.el.note;
  n.textContent = text;
  n.classList.toggle('hidden', !text);
}

// ---- Uhr, Zeitleiste, Killliste, Spielerliste ------------------------------------------------------------------------
function status(v) {
  const R = v.R, r = R.round, t = v.t, tr = R.tickrate, el = v.el;
  const buy = t < r.freeze;
  el.time.textContent = buy ? 'Kaufzeit' : clock((t - r.freeze) / tr);
  // gelegte Bombe: Zeit bis zur Explosion (40 s wie in Premier, Wettkampf und FACEIT)
  let plant = -1;
  for (const b of R.bombev) {
    if (b.t > t) break;
    plant = b.k === B_PLANTED ? b.t : -1;
  }
  const c4 = plant >= 0 && t - plant < 40 * tr;
  el.sub.textContent = c4 ? 'C4 · noch ' + clock(40 - (t - plant) / tr + 0.999) : buy ? 'Runde ' + r.n : 'nach der Kaufzeit';
  el.sub.classList.toggle('c4', c4);
  const k = Math.max(0, Math.min(1, (t - r.start) / Math.max(1, r.end - r.start)));
  el.fill.style.width = (k * 100).toFixed(3) + '%';
  el.thumb.style.left = (k * 100).toFixed(3) + '%';
  el.tl.setAttribute('aria-valuenow', String(Math.round((t - r.start) / tr)));
  el.tl.setAttribute('aria-valuetext', el.time.textContent);
  feed(v);
  const now = performance.now();
  if (!v.playing || now - v.listAt > 125) {   // Listen höchstens 8-mal pro Sekunde
    v.listAt = now;
    killList(v);
    teamState(v);
  }
}

// Killfeed oben rechts auf der Karte: was in den letzten 5 Sekunden passiert ist
function feed(v) {
  const R = v.R, t = v.t, list = [];
  for (const k of R.kills) {
    if (k.t > t) break;
    if (t - k.t <= 5 * R.tickrate) list.push(k);
  }
  const shown = list.slice(-5), key = shown.map(k => k.t + ':' + k.v).join(',');
  if (key === v.feedKey) return;
  v.feedKey = key;
  v.el.feed.replaceChildren(...shown.map(k => {
    const killer = R.players.get(k.k), victim = R.players.get(k.v);
    return h('div', {class: 'rp-kf'},
      killer ? h('b', {class: k.kct ? 'ct' : 't'}, shortName(killer.name, 14)) : null,
      h('span', null, k.w || (killer ? '' : '†')), k.hs ? h('i', {'aria-label': 'Headshot'}, icon('crosshair')) : null,
      h('b', {class: k.vct ? 'ct' : 't'}, shortName(victim ? victim.name : '?', 14)));
  }));
}

function killList(v) {
  const R = v.R, t = v.t, tr = R.tickrate;
  let n = 0;
  while (n < R.kills.length && R.kills[n].t <= t) n++;
  const key = R.round.n + ':' + n;
  if (key === v.killKey) return;
  v.killKey = key;
  if (!n) {
    v.el.kills.replaceChildren(h('div', {class: 'rp-none'}, 'Noch keine'));
    return;
  }
  v.el.kills.replaceChildren(...R.kills.slice(0, n).map(k => {
    const killer = R.players.get(k.k), victim = R.players.get(k.v);
    const b = h('button', {class: 'rp-kl', type: 'button', 'aria-label': k.time + ' ' + k.text},
      h('span', {class: 'rp-kt'}, k.time),
      killer ? h('b', {class: k.kct ? 'ct' : 't'}, killer.name) : null,
      h('span', {class: 'rp-kw'}, (k.w || '') + (k.hs ? ' HS' : '')),
      h('b', {class: k.vct ? 'ct' : 't'}, victim ? victim.name : '?'));
    b.addEventListener('click', () => seek(v, k.t - 2 * tr));
    return b;
  }));
}

// Teams wie in FragDesk: CT zuerst, je Spieler Name, Waffe, HP; Tippen folgt ihm auf der Karte
function teamList(v) {
  const R = v.R, el = v.el;
  v.rows = new Map();
  v.heads = [];
  const blocks = [true, false].map(ct => {
    const members = R.roster.filter(p => p.ct === ct);
    if (!members.length) return null;
    const count = h('span', {class: 'rp-cnt'});
    v.heads.push({ct, count, members});
    return h('section', {class: 'card flush rp-team ' + (ct ? 'ct' : 't')},
      h('div', {class: 'rp-th'}, h('span', {class: 'rp-dot'}), h('b', null, ct ? 'Counter-Terroristen' : 'Terroristen'), count),
      members.map(p => {
        const name = h('b', null, p.name + (p.me ? ' (du)' : ''));
        const wep = h('small');
        const fill = h('i');
        const hp = h('span', {class: 'rp-hpv'});
        const row = h('button', {class: 'rp-pl', type: 'button'}, h('div', {class: 'rp-pm'}, name, wep), h('span', {class: 'rp-hp'}, fill), hp);
        row.addEventListener('click', () => setFollow(v, v.follow === p.i ? -1 : p.i));
        v.rows.set(p.i, {row, name, wep, fill, hp, p, state: ''});
        return row;
      }));
  }).filter(Boolean);
  el.teams.replaceChildren(...blocks);
}

function teamState(v) {
  const R = v.R, byP = new Map(v.ps.map(p => [p.p, p]));
  for (const [i, x] of v.rows) {
    const p = byP.get(i);
    let wep = '';
    if (p) {
      wep = R.weapons[p.w] || '';
      if (p.fl & F_BOMB) wep += (wep ? ' · ' : '') + 'C4';
      if (p.fl & F_KIT) wep += (wep ? ' · ' : '') + 'Kit';
    }
    const state = (p ? p.hp + '|' + wep : 'tot') + (v.follow === i ? '|f' : '');
    if (state === x.state) continue;
    x.state = state;
    x.row.classList.toggle('dead', !p);
    x.row.classList.toggle('on', v.follow === i);
    x.row.setAttribute('aria-pressed', String(v.follow === i));
    x.wep.textContent = p ? wep || '–' : 'tot';
    x.hp.textContent = p ? String(p.hp) : '';
    x.fill.style.width = (p ? Math.min(100, p.hp) : 0) + '%';
    x.fill.classList.toggle('low', !!p && p.hp <= 20);
  }
  for (const hd of v.heads) {
    const alive = hd.members.filter(m => byP.has(m.i)).length;
    hd.count.textContent = alive + '/' + hd.members.length;
  }
}
