// STAMMTISCH – Übersicht: das nächste Treffen groß, darunter die kommenden und die letzten; außerdem das Archiv
import {h, fill, icon, card, row, emptyState, skeleton, button, segmented, avatar, busy, actionSheet} from '../ui.js';
import {splitEvents, eventTitle, fmtDate, fmtTime, relDay, dayDiff, rsvpGroups, rsvpLine, mapsLinks, EVENT_GRACE_MS} from '../util.js';
import {showMap} from '../map.js';

const WD = ['So', 'Mo', 'Di', 'Mi', 'Do', 'Fr', 'Sa'];
const MON = ['Jan', 'Feb', 'Mär', 'Apr', 'Mai', 'Jun', 'Jul', 'Aug', 'Sep', 'Okt', 'Nov', 'Dez'];
export const KIND_ICON = {bar: 'beer', home: 'house', other: 'pin'};

// "19. Nov" – im laufenden Jahr ohne Jahreszahl
function shortDate(date) {
  const d = new Date(date);
  return `${d.getDate()}. ${MON[d.getMonth()]}` + (d.getFullYear() === new Date().getFullYear() ? '' : ' ' + d.getFullYear());
}

// Kästchen mit Wochentag und Tag (klein in Listen, groß oben)
export function dateBox(date, cls = '') {
  const d = new Date(date);
  return h('span', {class: 'datebox ' + cls}, h('small', null, WD[d.getDay()]), h('b', null, d.getDate()), cls.includes('sm') ? null : h('small', null, MON[d.getMonth()]));
}

export function placeRow(e) {
  return h('div', {class: 'place'},
    h('span', {class: 'row-ic'}, icon(KIND_ICON[e.place_kind] || 'pin')),
    h('span', {class: 'row-main'}, h('b', null, e.place_name || 'Ort noch offen'),
      e.place_address ? h('small', null, e.place_address) : !e.place_name ? h('small', null, 'Wird noch festgelegt – besprecht es im Chat.') : null));
}

// Dabei / Vielleicht / Raus – tippt man die gewählte Antwort nochmal, ist sie wieder offen
export function rsvpControl(ctx, event, mine, changed) {
  const options = [
    {value: 'yes', label: 'Dabei', icon: 'check', tone: 'yes'},
    {value: 'maybe', label: 'Vielleicht', tone: 'maybe'},
    {value: 'no', label: 'Raus', icon: 'close', tone: 'no'},
  ];
  let current = mine || 'none';
  const seg = segmented(options, current, async (value, btn) => {
    const next = value === current ? 'none' : value;
    await busy(btn, async () => {
      await ctx.be.setRsvp(event.id, next);
      current = next;
      seg.querySelectorAll('button').forEach(b => b.classList.toggle('on', b.dataset.value === current));
      if (changed) changed(current);
    });
  }, 'rsvp');
  return seg;
}

export function faces(ctx, list, max = 6) {
  const box = h('div', {class: 'faces'});
  list.slice(0, max).forEach(m => box.append(avatar(m, ctx.url, 28)));
  if (list.length > max) box.append(h('span', {class: 'rest'}, '+' + (list.length - max)));
  return box;
}

// Karten-App öffnen: auf dem iPhone direkt Apple Karten oder Google Maps zur Wahl, sonst Google Maps
export function openRoute(e) {
  const links = mapsLinks(e);
  actionSheet('Route öffnen', [
    {label: 'Apple Karten', icon: 'map', run: () => window.open(links.apple, '_blank')},
    {label: 'Google Maps', icon: 'route', run: () => window.open(links.google, '_blank')},
    {label: 'OpenStreetMap', icon: 'external', run: () => window.open(links.osm, '_blank')},
  ]);
}

function eventRow(ctx, e, rsvps, past) {
  const groups = rsvpGroups(rsvps, ctx.members, e.id);
  const mine = (rsvps.find(r => r.event_id === e.id && r.member_id === ctx.me.id) || {}).status;
  const badge = e.canceled ? h('span', {class: 'pill bad'}, 'fällt aus')
    : past ? (groups.yes.length ? h('span', {class: 'row-val'}, groups.yes.length + ' dabei') : null)
    : mine === 'yes' ? h('span', {class: 'pill good'}, icon('check'), 'dabei')
    : mine === 'maybe' ? h('span', {class: 'pill accent'}, 'vielleicht')
    : mine === 'no' ? h('span', {class: 'pill'}, 'raus') : h('span', {class: 'pill'}, 'offen');
  return h('a', {class: 'row', href: '#/treffen/' + e.id},
    dateBox(e.starts_at, 'sm' + (past ? ' past' : '')),
    h('span', {class: 'row-main'},
      h('b', {class: e.canceled ? 'strike' : ''}, e.place_name || eventTitle(e)),
      h('small', null, [shortDate(e.starts_at), fmtTime(e.starts_at) + ' Uhr', e.place_name && e.title ? e.title : null].filter(Boolean).join(' · '))),
    badge, icon('chev', 'row-chev'));
}

async function load(ctx) {
  const now = new Date();
  const cut = new Date(now.getTime() - EVENT_GRACE_MS);
  const [upcoming, past] = await ctx.cached('events', () => Promise.all([ctx.be.events(ctx.group.id, cut), ctx.be.pastEvents(ctx.group.id, cut, 4)]));
  const all = [...upcoming, ...past];
  const rsvps = await ctx.cached('rsvps', () => ctx.be.rsvps(ctx.group.id, all.map(e => e.id)));
  return {parts: splitEvents(all, now), rsvps};
}

export async function render(ctx) {
  const head = h('div', {class: 'page-head'},
    h('div', null,
      h('h1', {class: 'page-title'}, 'Treffen'),
      h('button', {class: 'page-sub', type: 'button', onclick: () => groupSheet(ctx)}, ctx.group.name, ctx.groups.length > 1 ? icon('down') : null)),
    h('a', {class: 'iconbtn accent', href: '#/treffen/neu', 'aria-label': 'Treffen anlegen'}, icon('plus')));
  ctx.title('Treffen');
  fill(ctx.main, head, ...skeleton(330, 120));

  let data = await load(ctx);
  if (!ctx.current()) return;
  let map = null;

  function draw() {
    if (map) { map.remove(); map = null; }
    const {parts, rsvps} = data;
    const out = [head];
    const e = parts.next;
    if (!e) {
      out.push(card(null, emptyState({
        title: 'Noch kein Treffen geplant',
        text: 'Leg das nächste an – Ort und Zeit kann danach jeder aus der Gruppe ändern.',
        action: h('a', {class: 'btn primary', href: '#/treffen/neu'}, icon('plus'), 'Treffen anlegen'),
      }, 'beer')));
    } else {
      const groups = rsvpGroups(rsvps, ctx.members, e.id);
      const mine = (rsvps.find(r => r.event_id === e.id && r.member_id === ctx.me.id) || {}).status;
      const n = dayDiff(e.starts_at);
      const mapEl = typeof e.place_lat === 'number' ? h('a', {class: 'minimap', href: '#/treffen/' + e.id, 'aria-label': 'Karte'}) : null;
      const faceBox = faces(ctx, groups.yes);
      faceBox.append(h('span', {class: 'rest'}, rsvpLine(groups)));
      out.push(h('section', {class: 'card hero' + (e.canceled ? ' canceled' : '')},
        e.canceled ? h('div', {class: 'banner'}, icon('ban'), 'Dieses Treffen fällt aus') : null,
        h('a', {class: 'hero-top', href: '#/treffen/' + e.id, style: {color: 'inherit'}},
          dateBox(e.starts_at),
          h('div', {class: 'h'},
            h('span', {class: 'pill ' + (n <= 1 ? 'accent' : '')}, relDay(e.starts_at)),
            h('h2', {class: e.canceled ? 'strike' : ''}, eventTitle(e)),
            h('div', {class: 'when'}, fmtDate(e.starts_at) + ' · ' + fmtTime(e.starts_at) + ' Uhr')),
          icon('chev', 'row-chev')),
        placeRow(e),
        mapEl,
        e.canceled ? null : rsvpControl(ctx, e, mine, async () => { data = await load(ctx); if (ctx.current()) draw(); }),
        h('div', {class: 'hero-foot'}, faceBox,
          e.place_name ? button('Route', {icon: 'route', small: true, onclick: () => openRoute(e)}) : null)));
      if (mapEl) showMap(mapEl, e.place_lat, e.place_lon).then(m => { if (ctx.current() && mapEl.isConnected) map = m; else m.remove(); }).catch(() => mapEl.remove());
    }
    if (parts.upcoming.length) {
      out.push(h('div', {class: 'section'}, h('span', null, 'Danach')));
      out.push(card({flush: true}, parts.upcoming.map(x => eventRow(ctx, x, rsvps, false))));
    }
    if (parts.past.length) {
      out.push(h('div', {class: 'section'}, h('span', null, 'Zuletzt'), h('a', {href: '#/archiv'}, 'Archiv', icon('chev'))));
      out.push(card({flush: true}, parts.past.slice(0, 3).map(x => eventRow(ctx, x, rsvps, true))));
    }
    fill(ctx.main, ...out);
  }
  draw();
  ctx.onLeave(() => { if (map) map.remove(); });

  // Jemand anderes hat etwas geändert: neu laden (kurz gesammelt, damit nicht jede Zusage einzeln neu zeichnet)
  let timer = 0;
  ctx.onLive(ev => {
    if (ev.table !== 'events' && ev.table !== 'rsvps') return;
    clearTimeout(timer);
    timer = setTimeout(async () => {
      try { data = await load(ctx); } catch (e) { return; }
      if (ctx.current()) draw();
    }, 400);
  });
  ctx.onLeave(() => clearTimeout(timer));
}

// Gruppe wechseln, weitere beitreten oder gründen
export function groupSheet(ctx) {
  actionSheet('Gruppe', [
    ...ctx.groups.map(g => ({label: g.name + (g.id === ctx.group.id ? '  ✓' : ''), icon: 'group', run: () => { if (g.id !== ctx.group.id) ctx.switchGroup(g.id); }})),
    {label: 'Weiterer Gruppe beitreten', icon: 'key', run: () => ctx.showWelcome({signedIn: true})},
    {label: 'Gruppen verwalten', icon: 'sliders', run: () => { location.hash = '#/einstellungen'; }},
  ]);
}

// ---- Archiv: alle vergangenen Treffen --------------------------------------------------------------------------------
export async function renderArchive(ctx) {
  ctx.title('Archiv');
  fill(ctx.main, ...skeleton(300));
  const cut = new Date(Date.now() - EVENT_GRACE_MS);
  const past = await ctx.cached('archive', () => ctx.be.pastEvents(ctx.group.id, cut, 200));
  const rsvps = await ctx.cached('archive-rsvps', () => ctx.be.rsvps(ctx.group.id, past.map(e => e.id)));
  if (!ctx.current()) return;
  if (!past.length) {
    fill(ctx.main, emptyState({title: 'Noch nichts im Archiv', text: 'Hier stehen später alle vergangenen Treffen – mit Ort, wer da war und den Bildern des Abends.'}, 'history'));
    return;
  }
  // nach Jahr gruppiert
  const out = [];
  let year = null, list = null;
  for (const e of past) {
    const y = new Date(e.starts_at).getFullYear();
    if (y !== year) {
      year = y;
      list = card({flush: true});
      out.push(h('div', {class: 'section'}, h('span', null, String(y))), list);
    }
    list.append(eventRow(ctx, e, rsvps, true));
  }
  fill(ctx.main, ...out);
}
