// fragdesk-server – Dateien (POSIX): atomar schreiben (temp + rename), Ordner, Größen, freier Platz.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fdsrv {

bool readFile(const std::string& path, std::string& out, int64_t maxBytes = int64_t(1) << 30);
// Schreibt erst nach <path>.tmp-<zufall>, dann fsync und rename – wer liest, sieht nie eine halbe Datei
bool writeAtomic(const std::string& path, const std::string& data, int mode = 0600);
// Hängt data an (für .part) und schreibt sie sicher auf die Platte; false bei Fehler (Datei dann unverändert lang)
bool appendDurable(const std::string& path, const std::string& data, int64_t expectedSize, int64_t& newSize);
bool makeDirs(const std::string& path, int mode = 0700);
bool removeFile(const std::string& path);      // true auch, wenn es sie nicht gab
bool removeTree(const std::string& path);      // Ordner samt Inhalt (folgt keinen Verknüpfungen)
bool renameFile(const std::string& from, const std::string& to);
int64_t fileSize(const std::string& path);     // -1 = gibt es nicht (oder keine normale Datei)
int64_t fileTime(const std::string& path);     // Änderungszeit, 0 = gibt es nicht
bool isDir(const std::string& path);
std::vector<std::string> listDir(const std::string& path);   // Namen ohne "." und ".."
int64_t freeBytes(const std::string& path);    // für normale Benutzer frei, -1 = unbekannt
std::string joinPath(const std::string& a, const std::string& b);
std::string lastError();                       // strerror(errno) für Meldungen

}  // namespace fdsrv
