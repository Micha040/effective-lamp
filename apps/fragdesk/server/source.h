// fragdesk-server – Daten für die Ansichten der Handy-App: eine fdapi::Source pro Konto. Geladen wird erst, wenn ein
// Handy fragt (alle Matches ohne Details, ~150 KB pro Match im Speicher), und nach einer Weile ohne Handy wieder
// freigegeben – im Leerlauf braucht der Server so nur wenige MB.
//
// Alle Ansichten laufen nacheinander unter einer Sperre (wie in FragDesk im UI-Thread). view_common.cpp hält einen
// gemeinsamen Zwischenspeicher (careerFor), der Quellen nur an der Generation unterscheidet. Deshalb vergibt Views
// die Generationen aller Konten aus einem Zähler: Wechselt das Konto, wechselt auch die Generation.
//
// Sperren, immer in dieser Reihenfolge: User::fileMu → UserSource::loadMu → viewMu_ → Store::mu_.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "store.h"

namespace fdsrv {

class Views {
public:
    explicit Views(Store& store) : store_(store) {}
    ~Views();

    // fn mit den Daten des Kontos ausführen (lädt bei Bedarf); false = geht gerade nicht (Konto gelöscht)
    bool with(const UserPtr& u, const std::function<void(fdapi::Source&)>& fn);
    // Match hochgeladen, ersetzt oder gelöscht: geladene Daten nachziehen (sonst nichts zu tun)
    void matchChanged(const UserPtr& u, const std::string& checksum);
    void profileChanged(const UserPtr& u);   // SteamID/Name: zwischengespeicherte Ansichten verwerfen
    void drop(const std::string& userId);    // Konto gelöscht
    int unloadIdle(int64_t maxIdleSeconds);  // liefert die Zahl freigegebener Konten
    int loadedCount();

    class UserSource;

private:
    std::shared_ptr<UserSource> get(const UserPtr& u, bool create);
    Store& store_;
    std::mutex mapMu_;
    std::map<std::string, std::shared_ptr<UserSource>> sources_;
    std::mutex viewMu_;
};

}  // namespace fdsrv
