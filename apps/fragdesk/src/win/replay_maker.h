// FragDesk – 2D-Daten erstellen (fdanalyze -replay) für die 2D-Ansicht am PC, die Handy-App direkt vom PC und den
// Abgleich mit dem eigenen Server. fdanalyze schreibt in eine eigene Zwischendatei, erst am Ende wird umbenannt: Zwei
// gleichzeitig (z. B. PC und Handy beim selben Match) stören sich nicht, niemand liest eine halbe Datei.
#pragma once
#include <windows.h>

#include <functional>
#include <string>

namespace fdw {

// matches\<Prüfsumme>.replay
std::wstring replayFile(const std::string& checksum);

// fdanalyze -replay im aufrufenden Thread. priority: BELOW_NORMAL_PRIORITY_CLASS (jemand wartet) oder
// IDLE_PRIORITY_CLASS (im Hintergrund). cancel wird etwa jede halbe Sekunde gefragt; true beendet fdanalyze.
// Liefert "" (fertig), "CANCELLED" oder das Fehler-Kennwort von fdanalyze (Text dazu: analysisErrorText).
std::string makeReplay(const std::wstring& demo, const std::wstring& target, DWORD priority, const std::function<bool()>& cancel = nullptr);

// Kann es aus dieser Demo nie 2D-Daten geben (kaputt, falsches Format …)? Sonst lohnt ein neuer Versuch später.
bool replayErrorFinal(const std::string& code);

}  // namespace fdw
