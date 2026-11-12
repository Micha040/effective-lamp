// STAMMTISCH – Treffen anlegen oder bearbeiten: Zeit, Ort (gespeichert, gesucht oder frei eingetippt), Notiz
import {h, fill, icon, card, button, field, segmented, busy, toast, skeleton, emptyState} from '../ui.js';
import {nextThursday, toDateInput, toTimeInput, fromInputs, PLACE_KINDS} from '../util.js';
import {showMap, searchPlaces} from '../map.js';
import {KIND_ICON} from './events.js';

export async function render(ctx) {
  const id = ctx.params[0] || null;
  ctx.title(id ? 'Treffen bearbeiten' : 'Neues Treffen');
  fill(ctx.main, ...skeleton(140, 320));

  const [existing, places] = await Promise.all([id ? ctx.be.event(id) : null, ctx.be.places(ctx.group.id)]);
  if (!ctx.current()) return;
  if (id && (!existing || existing.group_id !== ctx.group.id)) {
    fill(ctx.main, emptyState({title: 'Dieses Treffen gibt es nicht (mehr)'}, 'calendar'));
    return;
  }

  // Vorschlag für ein neues Treffen: nächster Donnerstag, zur Uhrzeit des letzten Mals (sonst 19 Uhr)
  const last = ctx.store.get('lastTime.' + ctx.group.id, '19:00').split(':').map(Number);
  const start = existing ? new Date(existing.starts_at) : nextThursday(new Date(), last[0], last[1]);
  const place = {
    place_id: existing ? existing.place_id : null,
    kind: (existing && existing.place_kind) || 'bar',
    lat: existing ? existing.place_lat : null,
    lon: existing ? existing.place_lon : null,
  };

  const date = h('input', {type: 'date', value: toDateInput(start), required: true});
  const time = h('input', {type: 'time', value: toTimeInput(start), required: true});
  const title = h('input', {type: 'text', maxlength: 80, placeholder: 'Stammtisch', value: (existing && existing.title) || ''});
  const name = h('input', {type: 'text', maxlength: 80, placeholder: 'z. B. Zum Goldenen Hahn', value: (existing && existing.place_name) || '', autocomplete: 'off'});
  const address = h('input', {type: 'text', maxlength: 200, placeholder: 'Straße, Ort', value: (existing && existing.place_address) || '', autocomplete: 'off', enterkeyhint: 'search'});
  const note = h('textarea', {maxlength: 1000, rows: 3, placeholder: 'z. B. Tisch ist reserviert, bringt Bargeld mit …'}, (existing && existing.note) || '');
  const remember = h('input', {type: 'checkbox'});
  const rememberRow = h('label', {class: 'check'}, remember, h('span', null, 'Ort für die nächsten Male merken'));
  const results = h('div', {class: 'results hidden'});
  const mapEl = h('div', {class: 'minimap pick hidden'});
  const mapHint = h('small', {class: 'muted hidden', style: {display: 'block', margin: '-6px 0 12px'}}, 'Stimmt die Nadel nicht? Tipp auf die richtige Stelle der Karte.');
  const saved = h('div', {class: 'chips'});
  let map = null;

  const namePlaceholders = {bar: 'z. B. Zum Goldenen Hahn', home: 'z. B. Bei ' + ctx.me.name, other: 'z. B. Kegelbahn, Stadtpark …'};
  const kindSeg = h('div');
  function drawKind() {
    kindSeg.replaceChildren(segmented(Object.entries(PLACE_KINDS).map(([value, label]) => ({value, label, icon: KIND_ICON[value]})), place.kind, value => {
      place.kind = value;
      touched();
      drawKind();
    }));
    name.placeholder = namePlaceholders[place.kind];
    drawSaved();
  }

  // Gespeicherte Orte zum Antippen (die der gewählten Art zuerst)
  function drawSaved() {
    const list = [...places].sort((a, b) => (a.kind === place.kind ? 0 : 1) - (b.kind === place.kind ? 0 : 1));
    saved.replaceChildren(...list.map(p => {
      const b = h('button', {type: 'button', class: 'chip' + (p.id === place.place_id ? ' on' : '')}, icon(KIND_ICON[p.kind] || 'pin'), p.name);
      b.addEventListener('click', () => {
        place.place_id = p.id;
        place.kind = p.kind;
        place.lat = p.lat;
        place.lon = p.lon;
        name.value = p.name;
        address.value = p.address || '';
        results.classList.add('hidden');
        drawKind();
        drawMap();
        updateRemember();
      });
      return b;
    }));
    saved.classList.toggle('hidden', !list.length);
  }

  // Von Hand geändert: Das ist dann nicht mehr der gespeicherte Ort
  function touched() {
    place.place_id = null;
    saved.querySelectorAll('.chip.on').forEach(c => c.classList.remove('on'));
    updateRemember();
  }
  function updateRemember() {
    rememberRow.classList.toggle('hidden', !!place.place_id || !name.value.trim());
  }
  name.addEventListener('input', touched);
  address.addEventListener('input', () => { touched(); place.lat = place.lon = null; drawMap(); });

  async function drawMap() {
    const has = typeof place.lat === 'number' && typeof place.lon === 'number';
    mapEl.classList.toggle('hidden', !has);
    mapHint.classList.toggle('hidden', !has);
    if (!has) { if (map) { map.remove(); map = null; } return; }
    if (map) { map.set(place.lat, place.lon); return; }
    try {
      map = await showMap(mapEl, place.lat, place.lon, {interactive: true, onPick: (lat, lon) => { place.lat = lat; place.lon = lon; place.place_id = null; updateRemember(); }});
    } catch (e) { mapEl.classList.add('hidden'); mapHint.classList.add('hidden'); }
  }

  const searchBtn = button('Suchen', {icon: 'search', onclick: () => search()});
  async function search() {
    const q = [name.value.trim(), address.value.trim()].filter(Boolean).join(', ');
    if (q.length < 3) { toast('Gib einen Namen oder eine Adresse ein, dann findet die Suche den Ort.', true); return; }
    await busy(searchBtn, async () => {
      const near = places.find(p => typeof p.lat === 'number');
      const list = await searchPlaces(q, near);
      if (!list.length) { results.classList.add('hidden'); toast('Nichts gefunden. Du kannst den Ort auch ohne Karte eintragen.', true); return; }
      results.replaceChildren(...list.map(r => {
        const b = h('button', {type: 'button'}, h('b', null, r.name), h('small', null, r.address));
        b.addEventListener('click', () => {
          if (!name.value.trim()) name.value = r.name;
          address.value = r.address;
          place.lat = r.lat;
          place.lon = r.lon;
          place.place_id = null;
          results.classList.add('hidden');
          drawMap();
          updateRemember();
        });
        return b;
      }));
      results.classList.remove('hidden');
    });
  }
  address.addEventListener('keydown', ev => { if (ev.key === 'Enter') { ev.preventDefault(); search(); } });

  const submit = button(id ? 'Speichern' : 'Treffen anlegen', {primary: true, block: true, type: 'submit'});
  const form = h('form', null,
    card({title: 'Wann'},
      h('div', {class: 'field2'}, field('Tag', date), field('Uhrzeit', time)),
      field('Titel (freiwillig)', title, 'Leer lassen für „Stammtisch“.')),
    card({title: 'Wo', sub: 'Darf auch offen bleiben – dann steht dort „Ort noch offen“.'},
      kindSeg, h('div', {style: {height: '12px'}}),
      saved,
      field('Name', name),
      field('Adresse', h('div', {class: 'inline-search'}, address, searchBtn)),
      results, mapEl, mapHint, rememberRow),
    card({title: 'Notiz'}, field('', note)),
    submit);

  form.addEventListener('submit', ev => {
    ev.preventDefault();
    const when = fromInputs(date.value, time.value);
    if (!when) { toast('Bitte Tag und Uhrzeit angeben.', true); return; }
    busy(submit, async () => {
      const placeName = name.value.trim();
      let placeId = place.place_id;
      if (!placeId && placeName && remember.checked) {
        const p = await ctx.be.savePlace(ctx.group.id, {name: placeName, address: address.value.trim() || null, lat: place.lat, lon: place.lon, kind: place.kind});
        placeId = p.id;
      }
      const fields = {
        starts_at: when.toISOString(),
        title: title.value.trim() || null,
        place_id: placeName ? placeId : null,
        place_name: placeName || null,
        place_address: placeName ? address.value.trim() || null : null,
        place_lat: placeName ? place.lat : null,
        place_lon: placeName ? place.lon : null,
        place_kind: placeName ? place.kind : null,
        note: note.value.trim() || null,
      };
      const saved = await ctx.be.saveEvent(ctx.group.id, id ? {id, ...fields} : fields);
      ctx.store.set('lastTime.' + ctx.group.id, time.value);
      toast(id ? 'Gespeichert' : 'Treffen angelegt');
      ctx.replace('#/treffen/' + saved.id);
    });
  });

  fill(ctx.main, form);
  drawKind();
  drawMap();
  updateRemember();
  ctx.onLeave(() => { if (map) map.remove(); });
}
