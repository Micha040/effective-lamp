// FragDesk Handy-App – Momente (Vorschläge aus allen Matches) und aufgenommene Videos
import {h, icon, chips, tabs, emptyState, skeleton, notice, stars} from '../ui.js';
import {filterBar, filterQuery} from '../filters.js';
import {openPlayer} from './player.js';
import * as api from '../api.js';
import {store, replaceRoute, replayHref} from '../app.js';

export async function render(ctx) {
  ctx.title('Momente');
  const tab = Number(ctx.params[0] || 0) === 1 ? 1 : 0;
  const who = store.get('momentsWho', 0);
  const path = '/api/v1/moments?' + filterQuery(ctx.filters, false) + (who ? '&who=all' : '');
  const old = api.cached(path);
  if (old && !ctx.fresh) draw(ctx, old, tab, who);
  else ctx.main.replaceChildren(h('h1', {class: 'page-title'}, 'Momente'), h('div', {class: 'page-sub'}, ' '), ...skeleton(44, 72, 72, 72, 72));
  const d = await api.get(path);
  if (!ctx.current()) return;
  draw(ctx, d, tab, who);
}

function draw(ctx, d, tab, who) {
  const out = [h('h1', {class: 'page-title'}, 'Momente'), h('div', {class: 'page-sub'}, d.sub)];
  out.push(tabs(['Vorschläge', `Videos (${d.videos.items.length})`], tab, i => { if (i !== tab) replaceRoute(i ? '#/moments/1' : '#/moments'); }));
  if (d.blocked) out.push(notice('warn', 'Videos gerade nicht verfügbar', d.blocked));
  if (tab === 0) {
    out.push(filterBar(ctx, {period: false}));
    out.push(chips(['Meine Momente', 'Alle Spieler'], who, i => { store.set('momentsWho', i); ctx.rerender(); }));
    const s = d.suggestions;
    if (s.empty) out.push(emptyState(s.empty, 'spark'));
    else {
      // mit Video: abspielen (daneben die 2D-Ansicht); ohne: die 2D-Ansicht genau an dieser Stelle
      out.push(h('section', {class: 'card flush'}, h('div', {class: 'list'}, s.items.map(m => {
        const to = replayHref(m.match, m.round, m.tick);
        const kids = [
          h('span', {class: 'lead', style: {boxShadow: `inset 3px 0 0 ${m.mapColor}`}}, icon(m.clip ? 'play' : m.clutch ? 'star' : 'spark')),
          h('div', {class: 'main'}, h('b', null, m.title), h('small', null, m.meta)),
          h('div', {class: 'side'}, stars(m.stars), h('small', null, m.clip ? 'Video' : '2D-Ansicht')),
        ];
        if (!m.clip) return h('a', {class: 'row', href: to}, kids);
        const play = h('button', {class: 'row', type: 'button', style: {flex: '1', minWidth: '0', paddingRight: '4px'}}, kids);
        play.addEventListener('click', () => openPlayer({id: m.clip, title: m.title, sub: m.meta, match: m.match}));
        return h('div', {class: 'row', style: {padding: '0 8px 0 0'}}, play,
          h('a', {class: 'iconbtn', href: to, 'aria-label': 'In der 2D-Ansicht ansehen', title: '2D-Ansicht'}, icon('map')));
      }))));
      if (s.more) out.push(h('div', {class: 'muted', style: {textAlign: 'center', fontSize: '13px'}}, s.more));
    }
  } else {
    const v = d.videos;
    if (v.empty) out.push(emptyState(v.empty, 'film'));
    else {
      out.push(h('div', {class: 'vgrid'}, v.items.map(c => {
        const img = h('img', {src: '/media/thumb/' + encodeURIComponent(c.id) + '.jpg', alt: '', loading: 'lazy'});
        img.addEventListener('error', () => img.remove());
        const el = h('button', {class: 'vcard', type: 'button'},
          h('div', {class: 'vthumb'}, icon('play'), img, h('span', {class: 'len'}, c.duration)),
          h('div', {class: 'meta'}, h('b', null, c.title), h('small', null, c.matchTitle), h('small', null, c.sub + ' · ' + c.size)));
        el.addEventListener('click', () => openPlayer({id: c.id, title: c.title, sub: c.matchTitle, match: c.match}));
        return el;
      })));
    }
  }
  ctx.main.replaceChildren(...out);
}
