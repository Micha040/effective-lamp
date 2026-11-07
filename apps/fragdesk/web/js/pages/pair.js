// FragDesk Handy-App – mit FragDesk koppeln (Code aus FragDesk › Einstellungen › Handy-App)
import {h, icon, notice, isIOS, standalone} from '../ui.js';
import * as api from '../api.js';

export function renderPair(root, {code, onPaired}) {
  const input = h('input', {class: 'code', inputmode: 'numeric', autocomplete: 'one-time-code', maxlength: '6', placeholder: '••••••',
    'aria-label': 'Code aus FragDesk', value: code || null});
  const msg = h('div');
  const btn = h('button', {class: 'btn primary block', type: 'button'}, 'Verbinden');
  const submit = async () => {
    const c = input.value.replace(/\D/g, '');
    if (c.length !== 6) {
      msg.replaceChildren(notice('err', null, 'Der Code hat 6 Ziffern.'));
      return;
    }
    btn.disabled = true;
    btn.textContent = 'Verbinde …';
    try {
      await api.post('/api/v1/pair', {code: c});
      if (isIOS && !standalone) return showInstall(root, onPaired);
      if (standalone && location.search) history.replaceState(null, '', location.pathname + location.hash);
      onPaired();
    } catch (e) {
      msg.replaceChildren(notice('err', null, e.message));
      btn.disabled = false;
      btn.textContent = 'Verbinden';
    }
  };
  btn.addEventListener('click', submit);
  input.addEventListener('input', () => { if (input.value.replace(/\D/g, '').length === 6) submit(); });
  input.addEventListener('keydown', e => { if (e.key === 'Enter') submit(); });
  root.replaceChildren(h('div', {class: 'pair'},
    h('img', {class: 'logo', src: 'icon-180.png', alt: ''}),
    h('h1', null, 'Mit FragDesk verbinden'),
    h('p', null, 'Öffne FragDesk auf deinem PC: Einstellungen › Handy-App › „Handy verbinden“. Scanne den QR-Code oder gib den Code ein.'),
    input, msg, btn));
  if (code && code.length === 6) submit();
  else setTimeout(() => input.focus(), 300);
}

// Nach dem Koppeln in Safari: zum Home-Bildschirm hinzufügen (dann läuft FragDesk wie eine App)
function showInstall(root, onPaired) {
  root.replaceChildren(h('div', {class: 'pair'},
    h('img', {class: 'logo', src: 'icon-180.png', alt: ''}),
    h('h1', null, 'Verbunden!'),
    h('p', null, 'Leg FragDesk jetzt auf deinen Home-Bildschirm – dann startet es wie eine App, im Vollbild:'),
    h('ol', {class: 'steps'},
      h('li', null, 'Unten in Safari auf ', icon('share'), ' „Teilen“ tippen'),
      h('li', null, '„Zum Home-Bildschirm“ wählen (evtl. etwas runterscrollen)'),
      h('li', null, 'Oben rechts „Hinzufügen“ tippen'),
      h('li', null, 'FragDesk über das neue Symbol öffnen. Fragt es nach dem Code, gib ihn noch einmal ein – er gilt 10 Minuten.')),
    notice('info', null, 'Du kannst FragDesk auch einfach hier in Safari weiter benutzen.'),
    h('button', {class: 'btn block', type: 'button', onclick: onPaired}, 'Im Browser weiter')));
}
