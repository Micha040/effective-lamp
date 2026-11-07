// FragDesk Handy-App – alle Matches (wie in FragDesk: Filter nach Modus, sortierbar)
import {h, icon, chips, selectChip, emptyState, skeleton, toneClass} from '../ui.js';
import * as api from '../api.js';
import {store} from '../app.js';

const SORTS = [
  ['date', 'Datum', true], ['rating', 'Rating', true], ['adr', 'ADR', true], ['k', 'Kills', true], ['d', 'Tode', true],
  ['kast', 'KAST', true], ['hs', 'Headshot-Quote', true], ['score', 'Ergebnis', true], ['map', 'Map', false], ['premier', 'Premier', true],
];

export async function render(ctx) {
  ctx.title('Matches');
  const path = '/api/v1/matches';
  const old = api.cached(path);
  if (old && !ctx.fresh) draw(ctx, old);
  else ctx.main.replaceChildren(h('h1', {class: 'page-title'}, 'Matches'), h('div', {class: 'page-sub'}, ' '), ...skeleton(46, 400));
  const d = await api.get(path);
  if (!ctx.current()) return;
  draw(ctx, d);
}

function draw(ctx, d) {
  const mode = store.get('listMode', 0), sort = store.get('listSort', {key: 'date', desc: true});
  const out = [h('h1', {class: 'page-title'}, 'Matches'), h('div', {class: 'page-sub'}, d.sub)];
  if (d.empty) {
    ctx.main.replaceChildren(...out, emptyState(d.empty, 'list'));
    return;
  }
  const sorts = SORTS.filter(s => s[0] !== 'premier' || d.anyPremier);
  const sel = selectChip(sorts.map(s => ({value: s[0], label: 'Sortiert: ' + s[1]})), sort.key, v => {
    const def = sorts.find(s => s[0] === v);
    store.set('listSort', {key: v, desc: def ? def[2] : true});
    draw(ctx, d);
  }, '__none');
  const dir = h('button', {class: 'chip', type: 'button', 'aria-label': 'Reihenfolge umdrehen'}, sort.desc ? '↓' : '↑');
  dir.addEventListener('click', () => { store.set('listSort', {key: sort.key, desc: !sort.desc}); draw(ctx, d); });
  out.push(chips(d.modes, mode, i => { store.set('listMode', i); draw(ctx, d); }));
  out.push(h('div', {class: 'chips'}, sel, dir));

  const key = d.modeKeys[mode] || '';
  const items = d.items.filter(m => !key || m.modeKey === key);
  const val = m => m.sort[sort.key];
  items.sort((a, b) => {
    const x = val(a), y = val(b);
    const nx = x == null || x === '', ny = y == null || y === '';
    if (nx || ny) return nx === ny ? 0 : nx ? 1 : -1;   // ohne Wert immer ans Ende
    const c = typeof x === 'string' ? x.localeCompare(y, 'de') : x - y;
    return sort.desc ? -c : c;
  });
  if (!items.length) {
    out.push(emptyState({title: 'Keine Matches in diesem Modus'}, 'list'));
  } else {
    out.push(h('section', {class: 'card flush'}, h('div', {class: 'list'}, items.map(m => row(m)))));
  }
  ctx.main.replaceChildren(...out);
}

function row(m) {
  // Premier-Matches: statt des Modus das Rating danach (wie die Spalte PREMIER in FragDesk)
  const second = m.premier
    ? [m.date + ' · ', h('span', {style: {color: m.premier.color, fontWeight: '600'}}, m.premier.value), m.premier.delta ? ' ' + m.premier.delta : '']
    : [m.date + ' · ' + m.mode];
  return h('a', {class: 'row', href: '#/match/' + m.id},
    h('div', {class: 'main'},
      h('b', null, h('span', {class: 'dot', style: {background: m.mapColor}}), m.map),
      h('small', null, second)),
    h('div', {class: 'cols'},
      h('div', null, h('b', {class: m.result}, m.score.replace(/ /g, '')), h('small', null, 'Stand')),
      h('div', null, h('b', null, m.k === '–' ? '–' : `${m.k}–${m.d}`), h('small', null, 'K–D')),
      h('div', null, h('b', {class: toneClass(m.ratingTone)}, m.rating), h('small', null, 'Rating'))),
    icon('chev', 'chev'));
}
