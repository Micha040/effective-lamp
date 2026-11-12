// STAMMTISCH – Willkommen: mit Einladungscode einer Gruppe beitreten oder dieses Gerät mit einem Gerätecode verbinden
import {h, icon, notice, button, field, busy, isIOS, standalone, copyText, toast} from '../ui.js';
import {cleanCode, codeKind, formatCode, parseCodeInput} from '../util.js';

// opts: {be, pending (Code aus dem Link), signedIn, canCancel, onDone(gruppe), onCancel}
export function renderWelcome(root, opts) {
  const code = h('input', {class: 'code', type: 'text', inputmode: 'text', autocapitalize: 'characters', autocomplete: 'off', autocorrect: 'off',
    spellcheck: 'false', maxlength: 80, placeholder: 'XXXXX-XXXXX', value: formatCode(opts.pending || '')});
  const name = h('input', {type: 'text', maxlength: 40, placeholder: 'So sehen dich die anderen', autocomplete: 'given-name', enterkeyhint: 'go'});
  const nameField = field('Dein Name', name);
  const submit = button('Beitreten', {primary: true, block: true, type: 'submit'});
  const hint = h('small', null, 'Den Code bekommst du von jemandem aus der Gruppe – als Link oder zum Abtippen.');

  // Je nach Code: Einladung (10 Zeichen) braucht einen Namen, ein Gerätecode (8 Zeichen) nicht
  function adapt() {
    const kind = codeKind(code.value);
    nameField.classList.toggle('hidden', kind === 'device');
    submit.lastChild.textContent = kind === 'device' ? 'Gerät verbinden' : 'Beitreten';
    hint.textContent = kind === 'device'
      ? 'Gerätecode erkannt: Dieses Gerät wird mit einer Person verbunden, die es in der Gruppe schon gibt.'
      : 'Den Code bekommst du von jemandem aus der Gruppe – als Link oder zum Abtippen.';
  }
  code.addEventListener('input', () => {
    if (/[#?]/.test(code.value)) code.value = formatCode(parseCodeInput(code.value));   // ganzer Link eingefügt
    adapt();
  });
  code.addEventListener('blur', () => { if (codeKind(code.value)) code.value = formatCode(code.value); });

  const form = h('form', null, field('Code', code, hint), nameField, submit);
  form.addEventListener('submit', ev => {
    ev.preventDefault();
    const c = cleanCode(parseCodeInput(code.value));
    const kind = codeKind(c);
    if (!kind) { toast('Der Code hat 10 Zeichen (Einladung) oder 8 Zeichen (Gerät).', true); code.focus(); return; }
    if (kind === 'invite' && !name.value.trim()) { toast('Bitte gib deinen Namen ein.', true); name.focus(); return; }
    busy(submit, async () => {
      if (!opts.signedIn) {
        await opts.be.signUp(c);          // neues Gerät: Anmeldung anlegen (klappt nur mit gültigem Code)
        opts.signedIn = true;
      }
      const gid = kind === 'device' ? await opts.be.linkDevice(c) : await opts.be.joinGroup(c, name.value.trim());
      await opts.onDone(gid);
    });
  });

  // iPhone im Browser: Die installierte App hat ihren eigenen Speicher. Wer erst hier beitritt und dann installiert,
  // müsste sich in der App nochmal verbinden – also besser erst installieren.
  const install = isIOS && !standalone
    ? h('div', {class: 'notice info'}, icon('phone'), h('div', null,
        h('b', null, 'Tipp fürs iPhone: erst zum Home-Bildschirm'),
        h('ol', {class: 'steps'},
          h('li', null, 'Unten auf ', icon('share'), ' ', h('b', null, 'Teilen'), ' tippen, dann ', h('b', null, '„Zum Home-Bildschirm“'), '.'),
          h('li', null, 'STAMMTISCH vom Home-Bildschirm öffnen.'),
          h('li', null, 'Dort diesen Code eingeben', opts.pending ? [': ', h('b', null, formatCode(opts.pending)), ' '] : '. ',
            opts.pending ? h('a', {href: '#', onclick: async ev => { ev.preventDefault(); toast((await copyText(formatCode(opts.pending))) ? 'Code kopiert' : 'Kopieren hat nicht geklappt'); }}, 'kopieren') : null)),
        h('p', {style: {margin: '8px 0 0'}}, 'So bleibst du angemeldet und bekommst später Mitteilungen. Im Browser weitermachen geht aber auch.')))
    : null;

  root.replaceChildren(h('div', {class: 'welcome'},
    h('img', {class: 'logo', src: 'icon-192.png', alt: ''}),
    h('h1', null, 'STAMMTISCH'),
    h('p', null, opts.canCancel ? 'Mit einem Einladungscode trittst du einer weiteren Gruppe bei.' : 'Treffen, Chat und Bilder für eure Runde. Zum Mitmachen brauchst du einen Einladungscode.'),
    install,
    form,
    opts.canCancel ? h('div', {class: 'alt'}, h('a', {href: '#', onclick: ev => { ev.preventDefault(); sessionStorage.removeItem('st.pending'); opts.onCancel(); }}, 'Abbrechen')) : null,
    !opts.canCancel && opts.signedIn ? notice('info', 'Du bist in keiner Gruppe mehr', 'Mit einem neuen Einladungscode kannst du wieder beitreten.') : null));
  adapt();
  if (!opts.pending) setTimeout(() => code.focus(), 100);
  else if (codeKind(opts.pending) === 'invite') setTimeout(() => name.focus(), 100);
}
