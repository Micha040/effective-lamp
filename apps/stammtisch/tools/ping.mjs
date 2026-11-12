// STAMMTISCH – Lebenszeichen an die Datenbank (läuft zweimal pro Woche in GitHub Actions, siehe
// .github/workflows/stammtisch-wecker.yml). Supabase legt kostenlose Projekte nach einer Woche ohne Zugriffe schlafen;
// dieser eine kleine Aufruf verhindert das, auch wenn mal Ferien sind. Aufruf: node tools/ping.mjs
import {config} from '../web/config.js';

const res = await fetch(config.supabaseUrl + '/rest/v1/rpc/ping', {
  method: 'POST',
  headers: {'Content-Type': 'application/json', apikey: config.supabaseKey},
  body: '{}',
});
const text = await res.text();
if (!res.ok) {
  console.error(`Die Datenbank antwortet nicht wie erwartet (${res.status}): ${text.slice(0, 300)}`);
  console.error('Schläft das Projekt schon? Dann im Supabase-Dashboard auf „Restore project“ klicken.');
  process.exit(1);
}
console.log('Datenbank wach, Uhrzeit dort:', text);
