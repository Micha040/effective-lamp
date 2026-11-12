// STAMMTISCH – Anbindung auswählen.
//
// Die Seiten reden nie direkt mit Supabase, sondern nur mit dem Objekt, das hier herauskommt. Jede Anbindung bietet
// dieselben Funktionen an (Liste: README, „Eigener Server“):
//   supabase.js – der kostenlose Dienst, mit dem die App startet
//   demo.js     – Beispieldaten im Arbeitsspeicher (Adresse mit ?demo, für Tests und Bilder der Oberfläche)
// Ein eigener Server wird später eine dritte Datei – die Seiten bleiben, wie sie sind.
import {config} from '../../config.js';

let instance = null;

export function isDemo() {
  return config.backend === 'demo' || new URLSearchParams(location.search).has('demo');
}

export async function backend() {
  if (!instance) {
    const mod = isDemo() ? await import('./demo.js') : await import('./supabase.js');
    instance = await mod.create(config);
  }
  return instance;
}
