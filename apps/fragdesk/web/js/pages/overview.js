// FragDesk Handy-App – Übersicht (wie in FragDesk: Premier, Form, Kennzahlen, letzte Matches, Waffen, Maps, Momente)
import {h, icon, card, statTile, bars, emptyState, skeleton, toneClass, isIOS, standalone} from '../ui.js';
import {lineChart, fmtThousands} from '../chart.js';
import {filterBar, filterQuery} from '../filters.js';
import {openPlayer} from './player.js';
import * as api from '../api.js';
import {store} from '../app.js';

export async function render(ctx) {
  const path = '/api/v1/overview?' + filterQuery(ctx.filters);
  const head = d => [
    h('h1', {class: 'page-title'}, 'Übersicht'),
    h('div', {class: 'page-sub'}, d ? d.sub : ' '),
    installHint(),
    filterBar(ctx),
  ];
  ctx.title('Übersicht');
  const old = api.cached(path);
  if (old && !ctx.fresh) ctx.main.replaceChildren(...head(old), ...body(ctx, old));
  else ctx.main.replaceChildren(...head(null), ...skeleton(300, 250, 230, 230));
  const d = await api.get(path);
  if (!ctx.current()) return;
  ctx.main.replaceChildren(...head(d), ...body(ctx, d));
}

function installHint() {
  if (!isIOS || standalone || store.get('installHidden', false)) return null;
  const box = h('div', {class: 'install'},
    icon('phone'),
    h('div', {style: {flex: '1'}}, h('b', null, 'Als App nutzen'),
      h('div', {class: 'sub', style: {fontSize: '13.5px', marginTop: '2px'}}, 'In Safari auf „Teilen“ › „Zum Home-Bildschirm“ – dann startet FragDesk im Vollbild.')),
    h('button', {class: 'iconbtn x', type: 'button', 'aria-label': 'Ausblenden', onclick: () => { store.set('installHidden', true); box.remove(); }}, icon('close')));
  box.querySelector('svg').style.cssText = 'width:22px;height:22px;color:var(--accent-text);flex:none;margin-top:2px';
  return box;
}

function body(ctx, d) {
  if (d.empty) return [emptyState(d.empty, 'stats')];
  const out = [];

  // Premier
  if (d.premier) {
    const p = d.premier;
    out.push(card({title: 'Premier-Rating'},
      h('div', {class: 'premier-top'},
        h('span', {class: 'premier-num', style: {color: p.color}}, p.value),
        p.delta ? h('span', {class: 'pill ' + p.deltaTone}, p.delta) : null,
        h('span', {class: 'muted', style: {fontSize: '13px'}}, p.best)),
      lineChart(p.points.map(pt => ({y: pt.y, label: pt.label.replace(/\.$/, ''), color: pt.color, tip: pt.tip, href: '#/match/' + pt.match})),
        {fmtY: fmtThousands, bands: p.bands, height: 180})));
  } else if (d.premierEmpty) {
    out.push(card({title: 'Premier-Rating'}, h('div', {class: 'sub'}, d.premierEmpty.title),
      h('div', {class: 'muted', style: {fontSize: '13px', marginTop: '6px'}}, d.premierEmpty.text)));
  }

  // Form
  out.push(card({title: 'Form', sub: 'Die letzten 10 Matches'},
    h('div', {class: 'form'}, d.form.items.map(f => h('a', {class: f.result, href: '#/match/' + f.match, title: f.tip},
      h('span', {class: f.result}, f.letter), h('span', {class: 'dot', style: {background: f.mapColor}})))),
    h('div', {class: 'minis'}, d.form.minis.map(m => h('div', null, h('div', {class: 'l'}, m.label), h('div', {class: 'v ' + toneClass(m.tone)}, m.value))))));

  // Kennzahlen
  out.push(h('div', {class: 'grid2'}, d.kpis.map(k => statTile(k))));

  // Letzte Matches
  out.push(card({title: 'Letzte Matches', more: 'Alle', href: '#/matches', flush: true},
    h('div', {class: 'list'}, d.recent.map(r => h('a', {class: 'row', href: '#/match/' + r.match},
      h('div', {class: 'main'}, h('b', null, h('span', {class: 'dot', style: {background: r.mapColor}}), r.map), h('small', null, r.sub)),
      h('div', {class: 'cols'},
        h('div', null, h('b', {class: r.result}, r.score), h('small', null, 'Stand')),
        h('div', null, h('b', null, r.kd), h('small', null, 'K–D')),
        h('div', null, h('b', {class: toneClass(r.ratingTone)}, r.rating), h('small', null, 'Rating'))))))));

  // Waffen
  if (d.weapons.length) out.push(card({title: 'Waffen', sub: 'Kills und Headshot-Quote'}, bars(d.weapons)));

  // Maps (Tippen: Statistiken dieser Map)
  if (d.maps.length) {
    out.push(card({title: 'Maps', sub: 'Siegquote und Rating'}, bars(d.maps, m => {
      ctx.filters.map = m.id;
      store.set('filters', ctx.filters);
      location.hash = '#/stats';
    })));
  }

  // Beste Momente
  out.push(card({title: 'Beste Momente', more: 'Alle', href: '#/moments', flush: true},
    d.moments.length
      ? h('div', {class: 'list'}, d.moments.map(m => momentRow(m)))
      : h('div', {class: 'muted', style: {padding: '4px 16px 10px', fontSize: '13.5px'}}, 'Noch keine Momente – sie werden beim Auswerten erkannt.')));
  return out;
}

export function momentRow(m) {
  const row = h('a', {class: 'row', href: m.clip ? null : '#/match/' + m.match + '/6'},
    h('span', {class: 'lead'}, icon(m.clip ? 'play' : m.clutch ? 'star' : 'spark')),
    h('div', {class: 'main'}, h('b', null, m.title), h('small', null, m.sub || m.meta)),
    m.clip ? h('span', {class: 'pill neutral'}, 'Video') : icon('chev', 'chev'));
  if (m.clip) row.addEventListener('click', () => openPlayer({id: m.clip, title: m.title, sub: m.sub || m.meta, match: m.match}));
  return row;
}
