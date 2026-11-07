// FragDesk Handy-App – Einladungslink eines eigenen Servers geöffnet (https://…/#invite=<Code>). Der Link gehört in
// FragDesk am PC: die Seite erklärt das und kopiert bzw. teilt ihn.
import {h, icon, notice, toast, isIOS, copyText} from '../ui.js';

const README = 'https://github.com/Micha040/effective-lamp/tree/main/apps/fragdesk#readme';

export function renderInvite(root, {onContinue}) {
  const link = location.href;
  const phone = isIOS || /Android|iPhone|iPad|iPod|Mobile/.test(navigator.userAgent);
  const input = h('input', {class: 'linkbox', readonly: true, value: link, 'aria-label': 'Einladungslink'});
  input.addEventListener('focus', () => input.select());
  const copy = h('button', {class: 'btn primary block', type: 'button'}, icon('link'), 'Link kopieren');
  copy.addEventListener('click', async () => {
    if (await copyText(link, input)) {
      toast('Link kopiert');
      copy.lastChild.textContent = 'Kopiert';
    } else toast('Kopieren ging nicht – tippe auf den Link und kopiere ihn.', true);
  });
  let share = null;
  if (phone && navigator.share) {
    share = h('button', {class: 'btn block', type: 'button'}, icon('share'), 'An den PC schicken …');
    share.addEventListener('click', async () => {
      try {
        await navigator.share({title: 'Einladung zu FragDesk', text: 'Einladung für FragDesk am PC: Einstellungen › Handy-App › Eigener Server › „Einladung einfügen“', url: link});
      } catch (e) {
        if (e.name !== 'AbortError') toast('Teilen ging nicht: ' + e.message, true);
      }
    });
  }
  const steps = [
    phone ? h('li', null, 'Bring den Link auf deinen PC – ' + (share ? 'z. B. mit „An den PC schicken“ über' : 'z. B. per') +
      ' WhatsApp, Discord oder Mail an dich selbst.') : null,
    h('li', null, phone ? 'Am PC den Link kopieren und FragDesk öffnen.' : 'Link kopieren und FragDesk öffnen.', ' Noch kein FragDesk? ',
      h('a', {href: README, target: '_blank', rel: 'noopener'}, 'So installierst du es'), '.'),
    h('li', null, 'In FragDesk: ', h('b', null, 'Einstellungen › Handy-App › Eigener Server › „Einladung einfügen“'), '.'),
    h('li', null, 'Fertig: FragDesk lädt deine Matches und Videos hoch. Mit „Handy verbinden“ in FragDesk kommt dein Handy dazu.'),
  ];
  const cont = h('button', {class: 'btn block', type: 'button', style: {marginTop: '4px', background: 'transparent', color: 'var(--text2)'}}, 'Zur Handy-App');
  cont.addEventListener('click', onContinue);
  root.replaceChildren(h('div', {class: 'pair invite'},
    h('img', {class: 'logo', src: 'icon-180.png', alt: ''}),
    h('h1', null, 'Einladung zu FragDesk'),
    h('p', null, 'Mit diesem Link verbindest du ', h('b', null, 'FragDesk auf deinem PC'),
      ' mit einem eigenen FragDesk-Server. Dort liegen dann deine Stats und Clips – auf dem Handy siehst du sie auch, wenn dein PC aus ist.'),
    h('ol', {class: 'steps'}, steps),
    input, copy, share,
    notice('info', null, 'Der Link gilt nur einmal und nur ein paar Tage. Abgelaufen? Frag nach einem neuen.'),
    cont));
}
