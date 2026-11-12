// STAMMTISCH – wohin sich die App verbindet.
//
// Diese Werte sind öffentlich: Jeder Browser, der die App lädt, bekommt sie zu sehen. Der „publishable key“ ist dafür
// gemacht – er darf nur, was die Zugriffsregeln der Datenbank erlauben (supabase/migrations). Geheime Schlüssel
// (service_role, sb_secret_…) gehören nie hierher und nie ins Repo.
//
// Später ein eigener Server? Dann kommt hier backend: 'server' samt Adresse hin, und js/backend/ bekommt eine dritte
// Datei, die dieselben Funktionen wie supabase.js anbietet (siehe README, „Eigener Server“).
export const config = {
  backend: 'supabase',                    // 'supabase' oder 'demo' (Beispieldaten, nichts wird gespeichert)
  supabaseUrl: 'https://iyyayzruvbjodgfzhpbi.supabase.co',
  supabaseKey: 'sb_publishable_R-44ucrelu_Yo_bbqDASDQ_WeSYEB98',
  storageLimit: 1024 ** 3,                // 1 GB Dateispeicher im kostenlosen Tarif (für alle Gruppen zusammen)
  maxUpload: 50 * 1024 * 1024,            // höchstens 50 MB je Datei
};

export const VERSION = '1.0.0';
