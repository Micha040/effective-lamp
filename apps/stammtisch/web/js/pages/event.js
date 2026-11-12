// STAMMTISCH – ein Treffen: Zeit, Ort mit Karte, Zusagen, Bilder des Abends; bearbeiten, absagen, löschen
import {h, fill, icon, card, button, avatar, busy, confirmSheet, toast, notice, skeleton, emptyState} from '../ui.js';
import {eventTitle, fmtDateLong, fmtTime, relDay, rsvpGroups, EVENT_GRACE_MS} from '../util.js';
import {showMap} from '../map.js';
import {placeRow, rsvpControl, openRoute} from './events.js';
import {tile, openViewer} from './photos.js';

export async function render(ctx) {
  const id = ctx.params[0];
  fill(ctx.main, ...skeleton(120, 240, 200));
  let [e, rsvps, media] = await Promise.all([
    ctx.cached('event.' + id, () => ctx.be.event(id)),
    ctx.cached('event-rsvps.' + id, () => ctx.be.rsvps(ctx.group.id, [id])),
    ctx.be.media(ctx.group.id, {eventId: id, limit: 200}).catch(() => []),
  ]);
  if (!ctx.current()) return;
  if (!e || e.group_id !== ctx.group.id) {
    fill(ctx.main, emptyState({title: 'Dieses Treffen gibt es nicht (mehr)', action: h('a', {class: 'btn', href: '#/'}, 'Zur Übersicht')}, 'calendar'));
    return;
  }
  let map = null;

  function draw() {
    if (map) { map.remove(); map = null; }
    ctx.title(eventTitle(e));
    const past = new Date(e.starts_at).getTime() < Date.now() - EVENT_GRACE_MS;
    const groups = rsvpGroups(rsvps, ctx.members, e.id);
    const mine = (rsvps.find(r => r.member_id === ctx.me.id) || {}).status;
    const mapEl = typeof e.place_lat === 'number' ? h('div', {class: 'minimap big'}) : null;

    const who = [];
    const section = (label, list) => {
      if (!list.length) return;
      who.push(h('div', {class: 'who-head'}, `${label} (${list.length})`));
      for (const m of list) who.push(h('div', {class: 'who'}, avatar(m, ctx.url, 30), h('span', null, m.name)));
    };
    section(past ? 'Waren dabei' : 'Dabei', groups.yes);
    if (!past) {
      section('Vielleicht', groups.maybe);
      section('Raus', groups.no);
      section('Noch offen', groups.open);
    }

    const creator = e.created_by ? ctx.member(e.created_by) : null;
    fill(ctx.main,
      e.deleted_at ? notice('warn', 'Im Papierkorb', 'Dieses Treffen wurde gelöscht. Unter Einstellungen › Papierkorb lässt es sich wiederherstellen.') : null,
      e.canceled ? h('div', {class: 'banner'}, icon('ban'), 'Dieses Treffen fällt aus') : null,
      h('div', {class: 'page-head'}, h('div', null,
        h('span', {class: 'pill ' + (past ? '' : 'accent')}, relDay(e.starts_at)),
        h('h1', {class: 'page-title' + (e.canceled ? ' strike' : ''), style: {marginTop: '6px'}}, eventTitle(e)),
        h('div', {class: 'page-sub'}, fmtDateLong(e.starts_at) + ' · ' + fmtTime(e.starts_at) + ' Uhr'))),
      card(null,
        placeRow(e),
        mapEl,
        e.place_name ? button('Route öffnen', {icon: 'route', block: true, onclick: () => openRoute(e)}) : null),
      e.note ? card({title: 'Notiz'}, h('div', {class: 'note'}, e.note)) : null,
      past || e.canceled || e.deleted_at ? null : card({title: 'Bist du dabei?'},
        rsvpControl(ctx, e, mine, async () => { rsvps = await ctx.be.rsvps(ctx.group.id, [id]); if (ctx.current()) draw(); })),
      who.length ? card({title: past ? 'Wer war da' : 'Zusagen'}, who) : null,
      photoCard(),
      e.deleted_at ? null : h('div', {class: 'stack', style: {marginTop: '16px'}},
        h('a', {class: 'btn block', href: `#/treffen/${e.id}/bearbeiten`}, icon('edit'), 'Bearbeiten'),
        past ? null : button(e.canceled ? 'Findet doch statt' : 'Fällt aus', {icon: e.canceled ? 'check' : 'ban', block: true, onclick: ev => toggleCanceled(ev.currentTarget)}),
        button('Löschen', {icon: 'trash', danger: true, block: true, onclick: ev => remove(ev.currentTarget)})),
      h('div', {class: 'about'}, creator ? `Angelegt von ${creator.name}. ` : '', 'Jeder aus der Gruppe kann das Treffen ändern – was sich ändert, steht im Verlauf.'));
    if (mapEl) showMap(mapEl, e.place_lat, e.place_lon, {interactive: true}).then(m => { if (ctx.current() && mapEl.isConnected) map = m; else m.remove(); }).catch(() => mapEl.remove());
  }

  function photoCard() {
    const href = '#/bilder/' + e.id;
    if (!media.length) {
      return card({title: 'Bilder', more: 'Hochladen', href}, h('div', {class: 'muted', style: {fontSize: '14px'}}, 'Noch keine Bilder von diesem Abend.'));
    }
    const strip = h('div', {class: 'strip'});
    media.slice(0, 8).forEach((m, i) => {
      const t = tile(ctx, m, () => openViewer(ctx, media, i, {onChange: () => ctx.rerender()}));
      if (i === 7 && media.length > 8) t.append(h('span', {class: 'more-count'}, '+' + (media.length - 8)));
      strip.append(t);
    });
    return card({title: 'Bilder', sub: media.length === 1 ? '1 Bild' : media.length + ' Bilder und Videos', more: 'Album', href}, strip);
  }

  async function toggleCanceled(btn) {
    const next = !e.canceled;
    if (next && !(await confirmSheet({title: 'Treffen absagen?', text: 'Es bleibt sichtbar, ist aber als „fällt aus“ markiert. Das lässt sich jederzeit zurücknehmen.', ok: 'Fällt aus', danger: true}))) return;
    await busy(btn, async () => {
      await ctx.be.patchEvent(e.id, {canceled: next});
      e = {...e, canceled: next};
      draw();
      toast(next ? 'Als „fällt aus“ markiert' : 'Findet wieder statt');
    });
  }

  async function remove(btn) {
    if (!(await confirmSheet({title: 'Treffen löschen?', text: 'Es landet im Papierkorb und lässt sich 30 Tage lang wiederherstellen. Bilder des Abends bleiben erhalten.', ok: 'Löschen', danger: true}))) return;
    await busy(btn, async () => {
      await ctx.be.patchEvent(e.id, {deleted_at: new Date().toISOString()});
      toast('In den Papierkorb gelegt');
      location.hash = '#/';
    });
  }

  draw();
  ctx.onLeave(() => { if (map) map.remove(); });
  ctx.onLive(async ev => {
    if (ev.table === 'events' && ev.row && ev.row.id === id) { e = ev.row; draw(); }
    if (ev.table === 'rsvps' && ev.row && ev.row.event_id === id) {
      try { rsvps = await ctx.be.rsvps(ctx.group.id, [id]); } catch (err) { return; }
      if (ctx.current()) draw();
    }
  });
}
