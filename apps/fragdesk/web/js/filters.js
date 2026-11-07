// FragDesk Handy-App – Filterzeile wie in FragDesk: Modus · Zeitraum · Map
import {h, chips, selectChip} from './ui.js';

// opts.period=false: ohne Zeitraum (Momente)
export function filterBar(ctx, opts = {}) {
  const f = ctx.filters, info = ctx.info || {modes: [], periods: [], maps: []};
  const mapOpts = [{value: '', label: 'Alle Maps'}].concat(info.maps.map(m => ({value: m.id, label: `${m.name} (${m.count})`})));
  const mapSel = selectChip(mapOpts, f.map, v => ctx.setFilters({map: v}));
  return h('div', {class: 'filters'},
    chips(info.modes, f.mode, i => ctx.setFilters({mode: i})),
    opts.period === false ? h('div', {class: 'chips'}, mapSel) : chips(info.periods, f.period, i => ctx.setFilters({period: i}), mapSel));
}

export function filterQuery(f, withPeriod = true) {
  return `mode=${f.mode}` + (withPeriod ? `&period=${f.period}` : '') + (f.map ? `&map=${encodeURIComponent(f.map)}` : '');
}
