// FragDesk Handy-App – Liniendiagramm (SVG). Tippen zeigt den Wert, nochmal tippen öffnet das Match.
import {h} from './ui.js';

const NS = 'http://www.w3.org/2000/svg';
function s(tag, attrs) {
  const el = document.createElementNS(NS, tag);
  for (const [k, v] of Object.entries(attrs || {})) el.setAttribute(k, v);
  return el;
}

function niceStep(raw) {
  const p = Math.pow(10, Math.floor(Math.log10(raw)));
  for (const m of [1, 2, 2.5, 5, 10]) if (raw <= m * p) return m * p;
  return 10 * p;
}

// points: [{y, label, color, tip, href}] – opts: {height, fmtY, bands: [{from, color}], area, minY, maxY, refY, color}
export function lineChart(points, opts = {}) {
  const wrap = h('div', {class: 'chartwrap'});
  const tip = h('div', {class: 'chart-tip hidden'});
  wrap.append(tip);
  const draw = () => {
    const old = wrap.querySelector('svg');
    if (old) old.remove();
    const W = Math.max(240, wrap.clientWidth || 340), H = opts.height || 190;
    const fmt = opts.fmtY || (v => String(Math.round(v)));
    const ys = points.map(p => p.y).filter(v => Number.isFinite(v));
    if (!ys.length) return;
    let lo = opts.minY != null ? opts.minY : Math.min(...ys), hi = opts.maxY != null ? opts.maxY : Math.max(...ys);
    if (opts.refY != null) { lo = Math.min(lo, opts.refY); hi = Math.max(hi, opts.refY); }
    if (hi - lo < 1e-9) { hi += 1; lo -= 1; }
    const step = niceStep((hi - lo) / 4);
    lo = Math.floor(lo / step) * step;
    hi = Math.ceil(hi / step) * step;
    const left = 44, right = 10, top = 10, bottom = 22;
    const x = i => left + (points.length === 1 ? (W - left - right) / 2 : i * (W - left - right) / (points.length - 1));
    const y = v => top + (hi - v) / (hi - lo) * (H - top - bottom);
    const svg = s('svg', {class: 'chart', viewBox: `0 0 ${W} ${H}`, width: W, height: H, role: 'img'});
    // Farbbänder (z. B. Premier-Stufen)
    if (opts.bands) {
      opts.bands.forEach((b, i) => {
        const from = Math.max(b.from, lo), to = Math.min(i + 1 < opts.bands.length ? opts.bands[i + 1].from : hi, hi);
        if (to <= from) return;
        svg.append(s('rect', {x: left, y: y(to), width: W - left - right, height: y(from) - y(to), fill: b.color, 'fill-opacity': '0.10'}));
      });
    }
    // Raster und Achse
    for (let v = lo; v <= hi + step / 2; v += step) {
      svg.append(s('line', {x1: left, x2: W - right, y1: y(v), y2: y(v), stroke: 'currentColor', 'stroke-opacity': '0.10'}));
      const t = s('text', {x: left - 6, y: y(v) + 3.5, 'text-anchor': 'end'});
      t.textContent = fmt(v);
      svg.append(t);
    }
    if (opts.refY != null) svg.append(s('line', {x1: left, x2: W - right, y1: y(opts.refY), y2: y(opts.refY), stroke: 'currentColor', 'stroke-opacity': '0.35', 'stroke-dasharray': '4 4'}));
    const nLabels = Math.min(points.length, Math.max(2, Math.floor((W - left) / 64)));
    for (let k = 0; k < nLabels; k++) {
      const i = nLabels === 1 ? 0 : Math.round(k * (points.length - 1) / (nLabels - 1));
      const t = s('text', {x: x(i), y: H - 5, 'text-anchor': k === 0 ? 'start' : k === nLabels - 1 ? 'end' : 'middle'});
      t.textContent = points[i].label || '';
      svg.append(t);
    }
    // Linie, Fläche, Punkte (NaN-Punkte werden übersprungen)
    const valid = points.map((p, i) => [p, i]).filter(([p]) => Number.isFinite(p.y));
    const d = valid.map(([p, i], k) => (k ? 'L' : 'M') + x(i).toFixed(1) + ' ' + y(p.y).toFixed(1)).join(' ');
    if (opts.area && valid.length > 1) {
      const a = d + ` L${x(valid[valid.length - 1][1]).toFixed(1)} ${H - bottom} L${x(valid[0][1]).toFixed(1)} ${H - bottom} Z`;
      svg.append(s('path', {d: a, fill: opts.color || 'var(--accent)', 'fill-opacity': '0.12'}));
    }
    svg.append(s('path', {d, fill: 'none', stroke: opts.color || 'var(--text2)', 'stroke-width': '2', 'stroke-linejoin': 'round'}));
    const dots = [];
    for (const [p, i] of valid) {
      const c = s('circle', {cx: x(i), cy: y(p.y), r: points.length > 40 ? 2.2 : 3.2, fill: p.color || opts.color || 'var(--accent)'});
      svg.append(c);
      dots[i] = c;
    }
    const hot = s('circle', {r: 6, fill: 'none', stroke: 'var(--text)', 'stroke-width': '2', class: 'hidden'});
    svg.append(hot);
    let active = -1;
    svg.addEventListener('click', ev => {
      const r = svg.getBoundingClientRect();
      const px = (ev.clientX - r.left) * W / r.width;
      let best = -1, bd = 1e9;
      for (const [, i] of valid) {
        const dd = Math.abs(x(i) - px);
        if (dd < bd) { bd = dd; best = i; }
      }
      if (best < 0) return;
      const p = points[best];
      if (best === active && p.href) { location.hash = p.href; return; }
      active = best;
      hot.setAttribute('cx', x(best));
      hot.setAttribute('cy', y(p.y));
      hot.classList.remove('hidden');
      tip.textContent = (p.tip || fmt(p.y)) + (p.href ? '\nNochmal tippen: Match öffnen' : '');
      tip.classList.remove('hidden');
      const tx = x(best) / W * r.width, tw = Math.min(230, r.width - 12);
      tip.style.left = Math.max(6, Math.min(r.width - tw - 6, tx - tw / 2)) + 'px';
      tip.style.maxWidth = tw + 'px';
      tip.style.top = Math.max(0, y(p.y) / H * r.height - 12 - tip.offsetHeight) + 'px';
      if (parseFloat(tip.style.top) < 4) tip.style.top = (y(p.y) / H * r.height + 14) + 'px';
    });
    wrap.prepend(svg);
  };
  requestAnimationFrame(draw);
  let lastW = 0;
  new ResizeObserver(() => {
    if (Math.abs(wrap.clientWidth - lastW) > 4) { lastW = wrap.clientWidth; draw(); }
  }).observe(wrap);
  return wrap;
}

export function fmtThousands(v) {
  return Math.round(v).toLocaleString('de-DE');
}
