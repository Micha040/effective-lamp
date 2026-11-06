// efficient lamp – der Katalog (catalog.json aus dem GitHub-Release): welche Programme es gibt, wo ihre Pakete
// liegen, woran man eine Installation erkennt und welche Skripte installieren/entfernen/öffnen.
// Plattformunabhängig; alle Texte UTF-8. Aufbau: siehe docs/catalog.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace cat {

struct Command {
    std::string script;   // Datei im entpackten Paket (z. B. "Setup.cmd") – läuft in einem eigenen Konsolenfenster
    std::string exe;      // oder: installiertes Programm (Pfad mit %LOCALAPPDATA% usw.)
    std::string args;
    std::string label;    // Beschriftung im Launcher ("Öffnen", "Einstellungen")
    bool wait = true;     // auf das Ende warten (Installieren/Entfernen) oder nur starten
    bool valid() const { return !script.empty() || !exe.empty(); }
};

struct App {
    std::string id, name, version;
    std::string tagline, description, requirements;
    std::string color;                       // Ersatzfarbe für die Kachel ("#8B5CF6")
    std::string page;                        // Anleitung (README auf GitHub)
    std::string file, url, sha256, folder;   // Paket: Dateiname, Adresse, SHA-256, oberster Ordner im ZIP
    uint64_t size = 0;
    std::string process;                     // "DeskCats.exe" – läuft das Programm?
    std::string installDir;                  // für "Ordner öffnen"
    std::string uninstallKey;                // Erkennung über HKCU\...\Uninstall\<Schlüssel> (DisplayVersion)
    std::string exe;                         // Erkennung: diese Datei existiert
    std::string versionFile;                 // Version aus dieser Datei lesen (Version = "x.y.z")
    Command install, uninstall, open;
};

struct Launcher {
    std::string version, file, url, sha256;
    uint64_t size = 0;
};

struct Catalog {
    int schema = 0;
    bool supported = false;              // Format bekannt? Sonst nur "launcher" gelesen (für das Selbst-Update)
    std::string name, release, published, repo;
    Launcher launcher;                   // leer, wenn ungültig
    std::vector<App> apps;
    std::vector<std::string> skipped;    // übersprungene, fehlerhafte Einträge (Begründungen)
};

constexpr int kSchema = 1;   // der Abschnitt "launcher" bleibt in allen Formaten gleich aufgebaut

// false nur, wenn der Text gar kein lesbarer Katalog ist. Fehlerhafte Programme werden übersprungen.
bool parse(const std::string& text, Catalog& out, std::string& err);

// Versionen vergleichen ("1.10.0" > "1.9.2", "v1.0" == "1.0.0"); Ergebnis < 0, 0 oder > 0
int compareVersions(const std::string& a, const std::string& b);

// "Version = "1.2.3"" o. ä. in einem Quelltext finden (für Programme ohne Registry-Eintrag)
std::string findVersionString(const std::string& text);

// Hilfen für die Prüfung
bool isSafeFileName(const std::string& s);   // nur ein Dateiname, kein Pfad
bool isUserPath(const std::string& s);       // beginnt mit %LOCALAPPDATA%\ oder %APPDATA%\, ohne ".."
bool isValidVersion(const std::string& s);   // "1.2.3", "1.0.0-beta" – nichts, was als Ordnername Ärger macht

}  // namespace cat
