// FragDesk – Valve-KeyValues-Text (loginusers.vdf, libraryfolders.vdf, cs2_video.txt) lesen und einzelne Werte setzen
#pragma once
#include <string>
#include <vector>

namespace fd {

struct VdfNode {
    std::string key, value;
    bool block = false;
    std::vector<VdfNode> children;

    const VdfNode* find(const std::string& k) const;   // ohne Groß-/Kleinschreibung
    std::string get(const std::string& k) const;       // Wert eines Kindes oder leer
};

bool parseVdf(const std::string& text, VdfNode& root);

// "key" "wert" im Text ersetzen; fehlt der Schlüssel, kommt er vor die letzte schließende Klammer.
// Der Rest der Datei (Kommentare, Reihenfolge, Einrückung) bleibt unverändert.
std::string setVdfValue(std::string text, const std::string& key, const std::string& value);

}  // namespace fd
