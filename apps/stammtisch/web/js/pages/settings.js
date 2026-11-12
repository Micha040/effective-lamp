// STAMMTISCH – Einstellungen: Profil, Gruppe und Mitglieder, Einladung, Geräte, Speicher, Darstellung; außerdem „Orte“
import {h, fill, icon, card, row, button, avatar, segmented, sheet, actionSheet, confirmSheet, promptSheet, toast, copyText,
  shareText, usageBar, busy, skeleton, emptyState, isIOS, standalone} from '../ui.js';
import {formatCode, inviteLink, PLACE_KINDS} from '../util.js';
import {prepareAvatar} from '../media.js';
import {config, VERSION} from '../../config.js';
import {applyTheme} from '../app.js';
import {KIND_ICON} from './events.js';

const appUrl = () => location.origin + location.pathname;

export async function render(ctx) {
  ctx.title('Einstellungen');
  const gid = ctx.group.id;
  const me = ctx.me;
  const active = ctx.members.filter(m => !m.removed_at);
  const gone = ctx.members.filter(m => m.removed_at);

  // ---- Profil ---------------------------------------------------------------------------------------------------------
  const fileInput = h('input', {type: 'file', accept: 'image/*', class: 'hidden'});
  fileInput.addEventListener('change', async () => {
    const f = fileInput.files[0];
    fileInput.value = '';
    if (!f) return;
    try {
      toast('Profilbild wird hochgeladen …');
      const blob = await prepareAvatar(f);
      const path = `${gid}/avatars/${me.id}/${Date.now()}.jpg`;
      await ctx.be.upload(path, blob);
      await ctx.be.updateMe(me.id, {avatar_path: path});
      if (me.avatar_path) ctx.be.removeFiles([me.avatar_path]).catch(() => {});
      await ctx.refreshMembers();
      ctx.rerender();
    } catch (e) { toast(e.message, true); }
  });
  async function rename() {
    const name = await promptSheet({title: 'Dein Name', label: 'So sehen dich die anderen', value: me.name, maxLength: 40});
    if (!name || name === me.name) return;
    try {
      await ctx.be.updateMe(me.id, {name});
      await ctx.refreshMembers();
      ctx.rerender();
    } catch (e) { toast(e.message, true); }
  }
  const profile = card(null, h('div', {class: 'profile'},
    h('button', {class: 'avatar-edit', type: 'button', 'aria-label': 'Profilbild ändern', onclick: () => fileInput.click()},
      avatar(me, ctx.url, 64), h('span', {class: 'cam'}, icon('camera'))),
    h('div', {class: 'row-main'}, h('b', null, me.name), h('small', null, me.role === 'admin' ? 'Admin' : 'Mitglied')),
    button('Name', {small: true, icon: 'edit', onclick: rename})), fileInput);

  // ---- Gruppe ---------------------------------------------------------------------------------------------------------
  async function renameGroup() {
    const name = await promptSheet({title: 'Name der Gruppe', label: 'Name', value: ctx.group.name, maxLength: 60});
    if (!name || name === ctx.group.name) return;
    try {
      await ctx.be.renameGroup(gid, name);
      await ctx.refreshGroups();
      ctx.rerender();
    } catch (e) { toast(e.message, true); }
  }

  async function inviteSheet() {
    let code;
    try { code = await ctx.be.invite(gid); } catch (e) { toast(e.message, true); return; }
    const draw = () => {
      const link = inviteLink(appUrl(), code);
      sheet({title: 'Einladung', text: `Wer diesen Link öffnet oder den Code eingibt, kann ${ctx.group.name} beitreten. Gib ihn nur an Leute aus eurer Runde.`,
        body: h('div', {class: 'stack'},
          h('div', {class: 'codebox'}, formatCode(code)),
          button('Link teilen', {primary: true, block: true, icon: 'share', onclick: () => shareText('Einladung zu ' + ctx.group.name, `Komm in unsere Gruppe „${ctx.group.name}“: ${link}`)}),
          button('Link kopieren', {block: true, icon: 'copy', onclick: async () => toast((await copyText(link)) ? 'Link kopiert' : 'Kopieren hat nicht geklappt')}),
          button('Neuen Code erzeugen', {block: true, icon: 'refresh', onclick: async ev => {
            if (!(await confirmSheet({title: 'Neuen Code erzeugen?', text: 'Der bisherige Link und Code gelten dann nicht mehr. Wer schon in der Gruppe ist, bleibt drin.', ok: 'Neuen Code erzeugen', danger: true}))) return draw();
            await busy(ev.currentTarget, async () => { code = await ctx.be.rotateInvite(gid); });
            draw();
          }}))});
    };
    draw();
  }

  // Gerätecode für eine Person (für sich selbst: zweites Gerät; für andere: neues Handy)
  async function deviceSheet(member) {
    let code;
    try { code = await ctx.be.deviceCode(member.id); } catch (e) { toast(e.message, true); return; }
    const link = appUrl() + '#geraet=' + formatCode(code);
    const self = member.id === me.id;
    sheet({title: self ? 'Weiteres Gerät verbinden' : `Neues Gerät für ${member.name}`,
      text: (self ? 'Öffne STAMMTISCH auf dem anderen Gerät und gib diesen Code ein.' : `${member.name} öffnet STAMMTISCH auf dem neuen Gerät und gibt diesen Code ein – und ist dann wieder als ${member.name} dabei.`)
        + ' Der Code gilt 15 Minuten und nur einmal.',
      body: h('div', {class: 'stack'},
        h('div', {class: 'codebox'}, formatCode(code)),
        button('Code kopieren', {block: true, icon: 'copy', onclick: async () => toast((await copyText(formatCode(code))) ? 'Code kopiert' : 'Kopieren hat nicht geklappt')}),
        button('Als Link teilen', {block: true, icon: 'share', onclick: () => shareText('STAMMTISCH', link)}))});
  }

  function memberSheet(m) {
    const self = m.id === me.id;
    const act = async (fn, done) => {
      try {
        await fn();
        await ctx.refreshMembers();
        if (done) toast(done);
        ctx.rerender();
      } catch (e) { toast(e.message, true); }
    };
    if (m.removed_at) {
      if (!ctx.isAdmin) return;
      return actionSheet(m.name, [{label: 'Wieder aufnehmen', icon: 'undo', run: () => act(() => ctx.be.restoreMember(m.id), m.name + ' ist wieder dabei')}]);
    }
    actionSheet(m.name, [
      self ? {label: 'Namen ändern', icon: 'edit', run: rename} : null,
      self ? {label: 'Profilbild ändern', icon: 'camera', run: () => fileInput.click()} : null,
      {label: self ? 'Weiteres Gerät verbinden' : 'Gerätecode erzeugen (neues Handy)', icon: 'phone', run: () => deviceSheet(m)},
      ctx.isAdmin && !self && m.role !== 'admin' ? {label: 'Zum Admin machen', icon: 'shield', run: () => act(() => ctx.be.setRole(m.id, 'admin'), m.name + ' ist jetzt Admin')} : null,
      ctx.isAdmin && m.role === 'admin' && active.filter(x => x.role === 'admin').length > 1
        ? {label: self ? 'Meine Admin-Rechte abgeben' : 'Admin-Rechte nehmen', icon: 'shield', run: () => act(() => ctx.be.setRole(m.id, 'member'))} : null,
      ctx.isAdmin && !self ? {label: 'Aus der Gruppe entfernen', icon: 'ban', danger: true, run: async () => {
        if (await confirmSheet({title: `${m.name} entfernen?`, text: 'Die Person sieht die Gruppe dann nicht mehr. Ihre Nachrichten und Bilder bleiben. Tipp: Danach einen neuen Einladungscode erzeugen.', ok: 'Entfernen', danger: true})) {
          act(() => ctx.be.removeMember(m.id), m.name + ' wurde entfernt');
        }
      }} : null,
    ]);
  }

  const memberRows = [...active, ...(ctx.isAdmin ? gone : [])].map(m => row({
    lead: avatar(m, ctx.url, 34), title: m.name + (m.id === me.id ? ' (du)' : ''),
    sub: m.removed_at ? 'entfernt' : m.role === 'admin' ? 'Admin' : null, onclick: () => memberSheet(m),
  }));

  // ---- Speicher -------------------------------------------------------------------------------------------------------
  const usageBox = h('div', {style: {padding: '10px 16px 12px'}}, h('div', {class: 'muted', style: {fontSize: '13px'}}, 'Speicher wird berechnet …'));
  ctx.be.usage(gid).then(u => {
    if (!ctx.current()) return;
    usageBox.replaceChildren(usageBar(u.total_bytes, config.storageLimit, 'Speicher'),
      h('div', {class: 'muted', style: {fontSize: '12.5px', marginTop: '8px'}},
        'Der kostenlose Speicher gilt für alle Gruppen zusammen. Bilder werden vor dem Hochladen verkleinert, Videos gehen bis 50 MB.'));
  }).catch(() => usageBox.replaceChildren(h('div', {class: 'muted', style: {fontSize: '13px'}}, 'Speicher lässt sich gerade nicht abfragen.')));

  // ---- Gruppen ---------------------------------------------------------------------------------------------------------
  async function newGroup() {
    const name = await promptSheet({title: 'Neue Gruppe gründen', text: 'Du wirst Admin der neuen Gruppe und kannst andere per Link einladen. Jede Gruppe hat eigene Treffen, Chat und Bilder.', label: 'Name der Gruppe', placeholder: 'z. B. Kegelclub', ok: 'Gründen', maxLength: 60});
    if (!name) return;
    try {
      const id = await ctx.be.createGroup(name, me.name);
      await ctx.refreshGroups();
      toast('Gruppe gegründet');
      ctx.switchGroup(id);
    } catch (e) { toast(e.message, true); }
  }
  async function leave() {
    if (!(await confirmSheet({title: `${ctx.group.name} verlassen?`, text: 'Du siehst die Gruppe dann nicht mehr. Zurück kommst du nur, wenn dich ein Admin wieder aufnimmt.', ok: 'Gruppe verlassen', danger: true}))) return;
    try {
      await ctx.be.removeMember(me.id);
      const groups = await ctx.refreshGroups();
      if (groups.length) ctx.switchGroup(groups[0].id);
      else ctx.showWelcome({signedIn: true});
    } catch (e) { toast(e.message, true); }
  }
  async function logout() {
    if (!(await confirmSheet({title: 'Dieses Gerät abmelden?', text: 'Danach brauchst du einen Gerätecode von jemandem aus der Gruppe (Einstellungen › Mitglieder › dein Name), um wieder als du selbst reinzukommen.', ok: 'Abmelden', danger: true}))) return;
    ctx.signOut();
  }

  const theme = ctx.store.get('theme', 'auto');
  const themeSeg = segmented([{value: 'auto', label: 'Automatisch'}, {value: 'light', label: 'Hell'}, {value: 'dark', label: 'Dunkel'}], theme, value => {
    ctx.store.set('theme', value);
    applyTheme();
    themeSeg.querySelectorAll('button').forEach(b => b.classList.toggle('on', b.dataset.value === value));
  });

  fill(ctx.main,
    h('div', {class: 'page-head'}, h('div', null, h('h1', {class: 'page-title'}, 'Einstellungen'))),
    profile,
    h('div', {class: 'section'}, h('span', null, ctx.group.name)),
    card({flush: true},
      row({icon: 'link', title: 'Leute einladen', sub: 'Link oder Code für diese Gruppe', onclick: inviteSheet}),
      row({icon: 'edit', title: 'Gruppe umbenennen', onclick: renameGroup}),
      row({icon: 'pin', title: 'Gespeicherte Orte', sub: 'Stammbars, „Bei …“ und andere', href: '#/orte'}),
      row({icon: 'history', title: 'Verlauf', sub: 'Wer hat was geändert – mit Rückgängig', href: '#/verlauf'}),
      row({icon: 'trash', title: 'Papierkorb', sub: 'Gelöschtes bleibt 30 Tage', href: '#/papierkorb'})),
    h('div', {class: 'section'}, h('span', null, `Mitglieder (${active.length})`)),
    card({flush: true}, memberRows),
    card({flush: true}, usageBox),
    h('div', {class: 'section'}, h('span', null, 'Dieses Gerät')),
    card({flush: true},
      row({icon: 'phone', title: 'Weiteres Gerät verbinden', sub: 'z. B. den PC oder ein zweites Handy', onclick: () => deviceSheet(me)}),
      isIOS && !standalone ? row({icon: 'share', title: 'Als App installieren', sub: 'Teilen › „Zum Home-Bildschirm“. Danach dort mit einem Gerätecode verbinden.', chev: false}) : null),
    card({title: 'Darstellung'}, themeSeg),
    h('div', {class: 'section'}, h('span', null, 'Gruppen')),
    card({flush: true},
      ctx.groups.map(g => row({icon: 'group', title: g.name, sub: g.id === gid ? 'gerade geöffnet' : null, value: g.id === gid ? icon('check') : null,
        onclick: g.id === gid ? null : () => ctx.switchGroup(g.id)})),
      row({icon: 'key', title: 'Weiterer Gruppe beitreten', sub: 'mit Einladungscode', onclick: () => ctx.showWelcome({signedIn: true})}),
      row({icon: 'plus', title: 'Neue Gruppe gründen', onclick: newGroup})),
    card({flush: true},
      row({icon: 'logout', title: 'Gruppe verlassen', danger: true, chev: false, onclick: leave}),
      row({icon: 'ban', title: 'Dieses Gerät abmelden', danger: true, chev: false, onclick: logout})),
    h('div', {class: 'about'}, `STAMMTISCH ${VERSION}`, ctx.be.kind === 'demo' ? ' · Demo (nichts wird gespeichert)' : '', h('br'),
      'Teil von ', h('a', {href: 'https://github.com/Micha040/effective-lamp', target: '_blank', rel: 'noopener'}, 'efficient lamp'), ' · Karten © OpenStreetMap'));
}

// ---- Gespeicherte Orte -----------------------------------------------------------------------------------------------
export async function renderPlaces(ctx) {
  ctx.title('Gespeicherte Orte');
  fill(ctx.main, ...skeleton(200));
  const places = await ctx.be.places(ctx.group.id);
  if (!ctx.current()) return;
  if (!places.length) {
    fill(ctx.main, emptyState({title: 'Noch keine Orte gespeichert', text: 'Beim Anlegen eines Treffens kannst du „Ort für die nächsten Male merken“ ankreuzen. Dann steht er hier und lässt sich mit einem Tipp auswählen.'}, 'pin'));
    return;
  }
  const edit = p => actionSheet(p.name, [
    {label: 'Umbenennen', icon: 'edit', run: async () => {
      const name = await promptSheet({title: 'Ort umbenennen', label: 'Name', value: p.name, maxLength: 80});
      if (!name || name === p.name) return;
      try { await ctx.be.patchPlace(p.id, {name}); ctx.rerender(); } catch (e) { toast(e.message, true); }
    }},
    {label: 'Adresse ändern', icon: 'pin', run: async () => {
      const address = await promptSheet({title: 'Adresse', label: 'Straße, Ort', value: p.address || '', maxLength: 200});
      if (address == null || address === (p.address || '')) return;
      try { await ctx.be.patchPlace(p.id, {address: address || null, lat: null, lon: null}); ctx.rerender(); } catch (e) { toast(e.message, true); }
    }},
    {label: 'Löschen', icon: 'trash', danger: true, run: async () => {
      if (!(await confirmSheet({title: `„${p.name}“ löschen?`, text: 'Der Ort verschwindet aus der Auswahl. Treffen, die dort waren, behalten ihn. Er bleibt 30 Tage im Papierkorb.', ok: 'Löschen', danger: true}))) return;
      try { await ctx.be.patchPlace(p.id, {deleted_at: new Date().toISOString()}); toast('In den Papierkorb gelegt'); ctx.rerender(); } catch (e) { toast(e.message, true); }
    }},
  ]);
  fill(ctx.main,
    h('div', {class: 'page-head'}, h('div', null, h('h1', {class: 'page-title'}, 'Orte'), h('div', {class: 'page-sub'}, 'Beim Anlegen eines Treffens mit einem Tipp auswählbar'))),
    card({flush: true}, places.map(p => row({icon: KIND_ICON[p.kind] || 'pin', title: p.name, sub: [PLACE_KINDS[p.kind], p.address].filter(Boolean).join(' · '), onclick: () => edit(p)}))));
}
