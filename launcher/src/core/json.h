// efficient lamp – kleiner JSON-Leser (nur Lesen, UTF-8). Reicht für catalog.json; kein externes Paket nötig.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace json {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b = false;
    double num = 0;
    std::string str;                                  // UTF-8
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;   // Reihenfolge wie in der Datei

    bool isObject() const { return type == Object; }
    bool isArray() const { return type == Array; }
    bool isString() const { return type == String; }
    bool isNumber() const { return type == Number; }

    // Zugriff mit Rückfall: fehlende Schlüssel oder falsche Typen liefern den Standardwert
    const Value* find(const std::string& key) const;
    std::string getString(const std::string& key, const std::string& def = std::string()) const;
    double getNumber(const std::string& key, double def = 0) const;
    bool getBool(const std::string& key, bool def = false) const;
    const Value& operator[](const std::string& key) const;   // fehlend -> Null-Wert
};

// Liest text komplett. Bei Fehler: false, error enthält Position und Grund.
bool parse(const std::string& text, Value& out, std::string* error = nullptr);

}  // namespace json
