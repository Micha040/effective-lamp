// fragdesk-server – Konten, Einladungen, Schlüssel und Dateien aller Spieler.
//
// Auf der Platte (--data, Standard /var/lib/fragdesk; alles 0600/0700, immer atomar geschrieben):
//   server.json                 Konten (Admin, Limit), offene Einladungen (nur SHA-256 des Codes), Einstellungen,
//                               "steam": Name, SteamID und Ablauf des Steam-Bots, letzter Lauf des Helfers (nichts Geheimes)
//   local.key                   Schlüssel für die Befehle invite/users und den Helfer (sprechen über 127.0.0.1 mit dem Server)
//   steam.json                  Geheimnisse des Steam-Bots: Anmelde-Token und Web-API-Schlüssel (Demos automatisch laden)
//   fetch/                      Ordner des Helfers fragdesk-fetch: wake (der Server schreibt hinein = Helfer starten),
//                               tmp/ und lock gehören dem Helfer
//   users/<id>/account.json     Profil (name, steamId), Konto-Tokens (FragDesk) und Geräte (Handys), nur als SHA-256;
//                               "autodl": Demos automatisch laden (Authentifizierungscode im Klartext – den braucht der Helfer)
//   users/<id>/matches/         <Prüfsumme>.json, .heat und .replay (2D-Ansicht) – genau wie in FragDesk; .noreplay:
//                               FragDesk meldet, dass es für dieses Match keine 2D-Daten geben kann (Grund als JSON)
//   users/<id>/clips/           <clipId>.mp4, .jpg, .part (unfertig) und clips.json (Titel, Match, Moment …)
// Jedes Konto hat seinen eigenen Ordner; gegenseitiges Anschauen lässt sich später über server.json nachrüsten.
//
// Sperren: Store::mu_ schützt alles im Speicher und ist immer die innerste Sperre. User::fileMu reiht alle
// Dateiänderungen eines Kontos hintereinander (Kontingent prüfen, dann schreiben – ohne Wettlauf).
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../src/api/mobile.h"

namespace fdsrv {

constexpr int64_t kGiB = int64_t(1024) * 1024 * 1024;
constexpr int64_t kDefaultLimit = 15 * kGiB;     // eingeladene Konten; Admins: 0 = unbegrenzt
constexpr int64_t kMaxMatchFile = 4 << 20;        // .json und .heat
constexpr int64_t kMaxReplayFile = 16 << 20;      // .replay (ein Match hat 0,5–3 MB; Caddy nimmt laut install.sh bis 16 MB)
constexpr int64_t kMaxChunk = 8 << 20;            // ein Stück eines Videos
constexpr int64_t kMaxThumb = 1 << 20;
constexpr int64_t kMaxClip = 4 * kGiB;            // ein Video insgesamt
constexpr int kPairMinutes = 10;
constexpr int kMaxDevices = 20;                   // je Konto, wie in FragDesk (älteste zuerst raus)
constexpr int kMaxTokens = 10;                    // FragDesk-Installationen je Konto
constexpr int kMaxInvites = 50;                   // offene Einladungen
constexpr size_t kMaxMatches = 20000;             // je Konto (gegen sehr viele winzige Dateien)
constexpr size_t kMaxClips = 10000;

struct ClipMeta {
    std::string title, match, highlight, map;
    int64_t created = 0;
    double seconds = 0;
};

struct ClipRec {
    int64_t mp4 = -1, part = -1, thumb = -1;   // Größen in Bytes, -1 = fehlt
    bool hasMeta = false;                      // Eintrag in clips.json
    ClipMeta meta;
    bool complete() const { return mp4 >= 0 && hasMeta; }
};

struct MatchRec {
    int64_t json = -1, heat = -1, replay = -1, noreplay = -1;   // Größen in Bytes, -1 = fehlt
};

// Dateien eines Matches (Endung: .json, .heat, .replay, .noreplay)
enum class MatchPart { Json, Heat, Replay, NoReplay };
const char* matchExt(MatchPart part);

struct TokenRec {
    std::string hash, client;
    int64_t created = 0, lastSeen = 0;
};

struct DeviceRec {
    std::string id, hash, name;   // id: öffentlich (zum Entfernen), hash: SHA-256 des Geräte-Tokens
    int64_t created = 0, lastSeen = 0;
};

// Demos automatisch laden (account.json "autodl"). authCode ist geheim: nie ins Protokoll, an die Handy-App nur maskiert.
struct AutoDl {
    bool enabled = false;
    std::string authCode, knownCode, lastError;   // knownCode: letzter bekannter Freigabecode
    int64_t lastCheck = 0, lastNew = 0, fetched = 0;   // fetched: so viele neue Matches hat der Helfer geladen
    // nur im Speicher
    int64_t rev = 0, jobRev = -1;   // Codes am Handy geändert (zählt hoch) / Stand, den der Helfer zuletzt bekommen hat
    int64_t jobAt = 0, checkAt = 0; // zuletzt an den Helfer gegeben / „Jetzt prüfen“ gedrückt
};

// Steam-Bot des Servers: server.json "steam" (nichts Geheimes) plus Merker für steam.json
struct SteamBot {
    std::string name, steamId;        // Kontoname und SteamID64 des Bots
    int64_t tokenUntil = 0;           // Ablauf des Anmelde-Tokens (aus dem JWT), 0 = unbekannt
    int64_t lastRunAt = 0;            // Ende des letzten Laufs des Helfers
    std::string lastRunText;
    bool tokenBad = false;            // Steam hat das gespeicherte Token abgelehnt: neu anmelden (tokenHint: warum)
    std::string tokenHint;
    bool hasToken = false, hasKey = false;
    bool running = false;             // nur im Speicher: Lauf begonnen, noch nicht beendet
    int64_t runStart = 0;
    bool loggedIn(int64_t now) const { return hasToken && !tokenBad && (tokenUntil <= 0 || tokenUntil > now); }
    bool ready(int64_t now) const { return loggedIn(now) && hasKey; }   // Bot angemeldet und API-Schlüssel da
    bool needsLogin(int64_t now) const { return hasToken && !loggedIn(now); }   // war angemeldet, gilt nicht mehr
};

// Ein Konto im Auftrag für den Helfer
struct FetchAccount {
    std::string id, steamId, authCode, knownCode;
};

struct User {
    std::string id;
    std::string name, steamId;            // Profil
    bool admin = false;
    int64_t limit = 0, created = 0, lastSeen = 0;   // limit in Bytes, 0 = unbegrenzt
    std::vector<TokenRec> tokens;
    std::vector<DeviceRec> devices;
    AutoDl autodl;
    std::map<std::string, MatchRec> matches;   // Prüfsumme ->
    std::map<std::string, ClipRec> clips;      // clipId ->
    int64_t used = 0;                          // Summe aller Dateien
    std::string pairCode;                      // Kopplung eines Handys (10 min)
    int64_t pairUntil = 0;
    int pairFails = 0;
    std::atomic<bool> deleted{false};          // gesetzt mit mu_, ohne Sperre lesbar
    bool dirty = false;                        // nur „zuletzt gesehen“ ungespeichert
    std::mutex fileMu;
};
using UserPtr = std::shared_ptr<User>;

// Kopie für Antworten (ohne Sperre lesbar)
struct UserInfo {
    std::string id, name, steamId;
    bool admin = false, autodl = false;   // autodl: Demos automatisch laden eingeschaltet
    int64_t limit = 0, used = 0, created = 0, lastSeen = 0;
    int matches = 0, clips = 0;
    std::vector<DeviceRec> devices;
};

struct Invite {
    std::string id, hash, by;   // hash: SHA-256 des Codes, by: Konto, das eingeladen hat ("" = Befehlszeile)
    std::string user;           // „PC neu verbinden“: dieses Konto bekommt einen neuen Schlüssel ("" = neues Konto)
    bool admin = false;
    int64_t created = 0, expires = 0;
};

struct Settings {
    std::string url = "http://127.0.0.1:7420/", listen = "127.0.0.1:7420";
    int64_t defaultLimit = kDefaultLimit;
};

// Ergebnis einer Anfrage, die schiefgehen kann: HTTP-Status und deutscher Text
struct Fail {
    int status = 0;
    std::string error;
    int64_t size = -1;   // bei 409: aktuelle Größe
    bool ok() const { return status == 0; }
    static Fail make(int s, std::string e, int64_t size = -1) {
        Fail f;
        f.status = s;
        f.error = std::move(e);
        f.size = size;
        return f;
    }
};

bool validChecksum(const std::string& s);   // [0-9A-Za-z_-]{1,64}
bool validClipId(const std::string& s);     // 16 Hex-Kleinbuchstaben (clipId von FragDesk)
bool validUserId(const std::string& s);     // 8 Hex-Kleinbuchstaben
std::string cleanText(const std::string& s, size_t maxBytes);   // ohne Steuerzeichen, gekürzt (UTF-8-sicher)
std::string quotaText(int64_t used, int64_t limit);             // "14,8 GB von 15,0 GB"

class Store {
public:
    // Ordner anlegen bzw. laden. err: deutscher Grund. minFree: so viel Platz bleibt auf der Platte immer frei.
    bool open(const std::string& dataDir, std::string& err);
    const std::string& dir() const { return dir_; }
    int64_t minFree = 512 << 20;

    Settings settings() const;
    void setUrl(const std::string& url);
    void setListen(const std::string& listen);
    std::string localKey() const { return localKey_; }

    // ---- Anmeldung (Token im Klartext übergeben; gespeichert ist nur SHA-256) ------------------------------------
    UserPtr byAccountToken(const std::string& token);   // nullptr = ungültig; merkt sich „zuletzt gesehen“
    UserPtr byDeviceToken(const std::string& token);
    bool validAccountToken(const std::string& token) const;   // nur prüfen (für die Größengrenze vor dem Lesen)
    UserPtr byId(const std::string& id) const;
    UserInfo info(const UserPtr& u) const;
    std::vector<UserInfo> users() const;   // älteste zuerst
    int userCount() const;
    std::string profileId(const UserPtr& u) const;     // SteamID
    std::string profileName(const UserPtr& u) const;

    // ---- Konten ---------------------------------------------------------------------------------------------------
    // Einladung einlösen: neues Konto + Konto-Token – bzw. bei „PC neu verbinden“ ein neues Konto-Token für das
    // bestehende Konto (die bisherigen Konto-Tokens gelten dann nicht mehr). Fehler: 403 (Code falsch/abgelaufen/benutzt,
    // Konto weg), 500
    Fail registerUser(const std::string& code, const std::string& name, const std::string& steamId, const std::string& client,
                      UserPtr& user, std::string& token);
    Fail setProfile(const UserPtr& u, const std::string& name, const std::string& steamId);
    bool logoutToken(const UserPtr& u, const std::string& token);
    Fail setLimit(const std::string& id, int64_t limit);
    Fail removeUser(const std::string& id);   // 404, 409 (letzter Admin)

    // ---- Einladungen ----------------------------------------------------------------------------------------------
    // forUser: „PC neu verbinden“ für dieses Konto (admin gilt dann nicht); 404, wenn es das Konto nicht gibt
    Fail createInvite(bool admin, int days, const std::string& by, Invite& out, std::string& code, const std::string& forUser = std::string());
    std::vector<Invite> invites();   // offene, neueste zuerst (abgelaufene fliegen raus)
    bool removeInvite(const std::string& id);
    std::string inviteLink(const std::string& code) const;

    // ---- Handys ---------------------------------------------------------------------------------------------------
    std::string startPairing(const UserPtr& u, int64_t& until);   // neuer 6-stelliger Code, leer = kein Zufall
    void stopPairing(const UserPtr& u);
    // Code prüfen; liefert das Konto und ein neues Geräte-Token. tooMany: Code nach 5 Fehlversuchen verfallen
    UserPtr pair(const std::string& code, const std::string& deviceName, std::string& token, std::string& err);
    bool removeDevice(const UserPtr& u, const std::string& token);    // Handy meldet sich ab
    bool removeDeviceById(const UserPtr& u, const std::string& id);

    // ---- Dateien (alle mit User::fileMu; prüfen Kontingent und freien Platz vor dem Schreiben) --------------------
    std::string userDir(const std::string& id) const;
    std::string matchPath(const std::string& id, const std::string& checksum, const char* ext) const;
    std::string clipPath(const std::string& id, const std::string& clipId, const char* ext) const;
    // Replay/NoReplay nur, wenn das Match schon da ist (404); ein Replay ersetzt die Markierung NoReplay, eine
    // Markierung neben einem Replay wird nicht gespeichert. isNew: das Match (.json) war vorher nicht da
    Fail putMatchFile(const UserPtr& u, const std::string& checksum, MatchPart part, const std::string& body, bool* isNew = nullptr);
    Fail deleteMatch(const UserPtr& u, const std::string& checksum);
    Fail appendClip(const UserPtr& u, const std::string& clipId, int64_t offset, int64_t total, const std::string& body, int64_t& size);
    Fail putThumb(const UserPtr& u, const std::string& clipId, const std::string& jpeg);
    Fail finishClip(const UserPtr& u, const std::string& clipId, int64_t total, const ClipMeta& meta);
    Fail deleteClip(const UserPtr& u, const std::string& clipId);

    // Für /sync/state und die Ansichten (Kopien)
    std::map<std::string, MatchRec> matchFiles(const UserPtr& u) const;
    std::map<std::string, ClipRec> clipFiles(const UserPtr& u) const;
    std::vector<fdapi::ClipInfo> clipInfos(const UserPtr& u) const;   // fertige Videos, neueste zuerst
    int64_t used(const UserPtr& u) const;
    int64_t limit(const UserPtr& u) const;

    // ---- Demos automatisch laden: Konten -------------------------------------------------------------------------
    AutoDl autodl(const UserPtr& u) const;   // Kopie (mit dem Authentifizierungscode – nur für den Server selbst)
    // Codes leer = bisherigen behalten, enabled -1 = unverändert. 400, wenn zum Einschalten etwas fehlt
    Fail setAutodl(const UserPtr& u, const std::string& authCode, const std::string& knownCode, int enabled);
    Fail clearAutodl(const UserPtr& u);   // aus, beide Codes löschen
    bool requestCheck(const UserPtr& u, int64_t now);   // „Jetzt prüfen“: true = Helfer wecken (höchstens alle 30 s)
    // Auftrag: eingeschaltete Konten mit SteamID und beiden Codes, deren Speicher noch ein Match fasst (älteste zuerst)
    std::vector<FetchAccount> fetchAccounts(int64_t now);
    // Meldung des Helfers für ein Konto. lastError nullptr = unverändert; from: der Freigabecode, von dem der Helfer
    // ausgeht (leer = egal). ignored: die Codes wurden am Handy geändert, seit der Helfer seinen Auftrag geholt hat, oder
    // from passt nicht mehr – die Meldung gilt dann nicht. 403, wenn das Laden aus ist
    Fail fetchState(const UserPtr& u, const std::string& knownCode, const std::string& from, int64_t lastCheck, int64_t lastNew,
                    const std::string* lastError, bool& ignored);
    void fetchedMatch(const UserPtr& u, int64_t now);   // ein neues Match vom Helfer: fetched zählt hoch, lastNew
    bool nearlyFull(const UserPtr& u) const;            // passt kein ganzes Match mehr (Kontingent oder Platte)
    int autodlCount() const;                            // Konten mit eingeschaltetem Laden
    std::string fetchDir() const { return dir_ + "/fetch"; }
    bool wakeHelper();   // <data>/fetch/wake neu schreiben (atomar, aktuelle Zeit) – die Path-Unit startet den Helfer

    // ---- Demos automatisch laden: Steam-Bot ----------------------------------------------------------------------
    SteamBot steam() const;
    void steamSecrets(std::string& refreshToken, std::string& apiKey) const;
    Fail setSteamLogin(const std::string& refreshToken, const std::string& name, const std::string& steamId, int64_t until);
    Fail setSteamToken(const std::string& refreshToken, int64_t until);   // von Steam erneuert
    Fail setSteamKey(const std::string& apiKey);   // leer = entfernen
    Fail forgetSteamBot();                          // Token, Name, SteamID weg; der API-Schlüssel bleibt
    void steamTokenRejected(const std::string& hint);   // Steam lehnt das Token ab: bis zur neuen Anmeldung nicht mehr benutzen
    void runPhase(bool start, const std::string& text, int64_t now);   // Lauf des Helfers beginnt / ist zu Ende

    // Aufräumen (alle paar Minuten): abgelaufene Einladungen und Kopplungen, „zuletzt gesehen“ speichern
    void maintain();
    void flush();   // beim Beenden

private:
    mutable std::mutex mu_;
    std::string dir_, localKey_;
    Settings settings_;
    std::map<std::string, UserPtr> users_;
    std::vector<Invite> invites_;
    std::unordered_map<std::string, std::string> accountTokens_, deviceTokens_;   // SHA-256 -> Konto
    bool serverDirty_ = false;
    SteamBot steam_;
    std::string refreshToken_, apiKey_;   // steam.json

    bool loadServer(std::string& err);
    void loadSteam();                      // steam.json (beschädigt: Hinweis, der Bot muss neu angemeldet werden)
    bool saveSteam();                      // mit mu_
    bool nearlyFullLocked(const User& u, int64_t freeBytes) const;
    bool loadAccount(const UserPtr& u, std::string& err);
    void scanFiles(const UserPtr& u);
    void recount(const UserPtr& u);
    bool saveServer();                     // mit mu_
    bool saveAccount(const UserPtr& u);    // mit mu_
    bool saveClips(const UserPtr& u);      // mit mu_
    void touch(User& u, int64_t* tokenSeen);
    Fail checkSpace(const User& u, int64_t grow) const;   // mit mu_
    bool alive(const UserPtr& u) const;    // nicht gelöscht (mit mu_)
};

}  // namespace fdsrv
