// Tests für fragdesk-server (Linux): SHA-256, Prüfungen, Grenzen gegen Raten, Konten, Einladungen, Dateien mit
// Kontingent, Kopplung, Ansichten pro Konto, Demos automatisch laden (Codes, Maskieren, Anmeldung des Bots im Speicher,
// Speichern, Auftrag für den Helfer, wake). Aufruf: server_test <leerer Ordner> <Ordner mit Test-Matches>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../server/app.h"
#include "../server/autodl.h"
#include "../server/files.h"
#include "../server/sha256.h"
#include "../server/source.h"
#include "../server/store.h"
#include "../src/core/json.h"

using namespace fdsrv;

static int fails = 0, checks = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        ++checks;                                                        \
        if (!(c)) {                                                      \
            ++fails;                                                     \
            std::printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #c);   \
        }                                                                \
    } while (0)

namespace {

std::string mp4(size_t n) {   // beginnt wie ein echtes MP4
    std::string s = std::string("\0\0\0\x20", 4) + "ftypisom";
    while (s.size() < n) s += char('a' + s.size() % 26);
    s.resize(n);
    return s;
}

int mode(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 ? int(st.st_mode & 0777) : -1;
}

int64_t inode(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 ? int64_t(st.st_ino) : -1;
}

bool grep(const std::string& dir, const std::string& needle) {   // steht needle irgendwo in einer Datei?
    for (const std::string& n : listDir(dir)) {
        std::string p = joinPath(dir, n), text;
        if (isDir(p) ? grep(p, needle) : readFile(p, text) && text.find(needle) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

void autodlTests(const std::string& data, const std::string& matchJson, const std::string& checksum);

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("Aufruf: server_test <leerer Ordner> <Ordner mit Test-Matches>\n");
        return 2;
    }
    std::string dir = argv[1], testMatches = argv[2];
    removeTree(dir);

    // SHA-256 (FIPS 180-4, Beispiele aus dem Standard)
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    {
        Sha256 s;
        std::string a(1000, 'a');
        for (int i = 0; i < 1000; ++i) s.update(a.data(), a.size());
        CHECK(s.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    }

    // Prüfungen
    CHECK(validChecksum("9237b533f777767f") && validChecksum("test-038") && !validChecksum("") && !validChecksum("../x") && !validChecksum("a.b"));
    CHECK(validClipId("0123456789abcdef") && !validClipId("0123456789ABCDEF") && !validClipId("0123") && !validClipId("0123456789abcdeg"));
    CHECK(cleanText("  Mi\ncha\x01  ", 40) == "Micha");
    CHECK(cleanText("\xC3\xA4\xC3\xA4\xC3\xA4", 5) == "\xC3\xA4\xC3\xA4");   // nicht mitten im Zeichen abschneiden
    CHECK(quotaText(15 * kGiB, 15 * kGiB) == "15,0\xC2\xA0GB von 15,0\xC2\xA0GB");

    // Grenze gegen Raten: 5 Fehlversuche, dann gesperrt – andere Adressen nicht
    RateLimit rl(5, 900);
    for (int i = 0; i < 4; ++i) rl.fail("1.2.3.4");
    CHECK(!rl.blocked("1.2.3.4"));
    rl.fail("1.2.3.4");
    CHECK(rl.blocked("1.2.3.4") && !rl.blocked("5.6.7.8"));

    // Atomar schreiben, anhängen
    CHECK(makeDirs(dir + "/f/a/b") && writeAtomic(dir + "/f/a/b/x.json", "{}") && mode(dir + "/f/a/b/x.json") == 0600);
    int64_t ns = 0;
    CHECK(appendDurable(dir + "/f/p", "abc", 0, ns) && ns == 3 && !appendDurable(dir + "/f/p", "x", 1, ns) && appendDurable(dir + "/f/p", "de", 3, ns) && ns == 5);
    CHECK(fileSize(dir + "/f/p") == 5 && removeTree(dir + "/f") && !isDir(dir + "/f"));

    // Konten und Einladungen
    std::string data = dir + "/data";
    Store store;
    std::string err;
    CHECK(store.open(data, err));
    CHECK(mode(data) == 0700 && mode(data + "/local.key") == 0600 && mode(data + "/server.json") == 0600 && store.localKey().size() == 64);
    store.setUrl("https://203-0-113-5.sslip.io");
    CHECK(store.settings().url == "https://203-0-113-5.sslip.io/");
    Invite inv;
    std::string code, tokenA, tokenB;
    CHECK(store.createInvite(true, 7, "", inv, code).ok() && code.size() == 32);
    CHECK(store.inviteLink(code) == "https://203-0-113-5.sslip.io/#invite=" + code);
    UserPtr a, b;
    CHECK(store.registerUser("0000000000000000", "x", "", "", a, tokenA).status == 403);   // falscher Code
    CHECK(store.registerUser(code, " Micha ", "76561190000000001x", "FragDesk 1.4.0", a, tokenA).ok() && a && tokenA.size() == 64);
    CHECK(store.registerUser(code, "Zweiter", "", "", b, tokenB).status == 403);   // ein Code gilt einmal
    UserInfo ia = store.info(a);
    CHECK(ia.admin && ia.limit == 0 && ia.name == "Micha" && ia.steamId == "76561190000000001");
    CHECK(store.createInvite(false, 99, a->id, inv, code).ok() && inv.expires - inv.created == 30 * 86400);   // höchstens 30 Tage
    CHECK(store.registerUser(code, "Freund", "76561190000000002", "", b, tokenB).ok());
    CHECK(!store.info(b).admin && store.info(b).limit == kDefaultLimit);
    CHECK(store.byAccountToken(tokenA) == a && store.byAccountToken(tokenB) == b && !store.byAccountToken(tokenA.substr(1) + "0"));
    CHECK(!store.byDeviceToken(tokenA));   // Konto-Token ist kein Geräte-Token
    CHECK(store.invites().empty());

    // Dateien und Kontingent (zwei beliebige Matches aus den Testdaten)
    std::vector<std::string> names;
    for (const std::string& n : listDir(testMatches))
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0) names.push_back(n);
    std::sort(names.begin(), names.end());
    CHECK(names.size() >= 2);
    if (names.size() < 2) return 1;
    std::string m1, m2;
    json::Value j1, j2;
    CHECK(readFile(testMatches + "/" + names[0], m1) && readFile(testMatches + "/" + names[1], m2) && json::parse(m1, j1) && json::parse(m2, j2));
    const std::string c1 = j1.getString("checksum"), c2 = j2.getString("checksum");
    CHECK(validChecksum(c1) && validChecksum(c2) && c1 != c2);
    CHECK(store.putMatchFile(a, c1, MatchPart::Json, m1).ok() && store.putMatchFile(a, c2, MatchPart::Json, m2).ok());
    CHECK(store.putMatchFile(a, c2, MatchPart::Heat, "FDHM1234").ok());
    CHECK(store.used(a) == int64_t(m1.size() + m2.size() + 8) && store.matchFiles(a).size() == 2);
    CHECK(store.putMatchFile(a, c1, MatchPart::Json, m1).ok() && store.used(a) == int64_t(m1.size() + m2.size() + 8));   // ersetzen zählt nicht doppelt
    // 2D-Daten: nur zu einem Match auf dem Server; sie ersetzen die Markierung „keine 2D-Daten möglich“
    const std::string marker = "{\"reason\":\"demo\"}", rp = "FDRP" + std::string(100, 'r');
    CHECK(store.putMatchFile(a, "nichtda", MatchPart::Replay, rp).status == 404 && store.putMatchFile(a, "nichtda", MatchPart::NoReplay, marker).status == 404);
    int64_t before = store.used(a);
    CHECK(store.putMatchFile(a, c2, MatchPart::NoReplay, marker).ok() && store.matchFiles(a)[c2].noreplay == int64_t(marker.size()));
    CHECK(store.used(a) == before + int64_t(marker.size()));
    CHECK(store.putMatchFile(a, c2, MatchPart::Replay, rp).ok());
    MatchRec r2 = store.matchFiles(a)[c2];
    CHECK(r2.replay == int64_t(rp.size()) && r2.noreplay < 0 && fileSize(store.matchPath(a->id, c2, ".noreplay")) < 0);
    CHECK(store.used(a) == before + int64_t(rp.size()));
    CHECK(store.putMatchFile(a, c2, MatchPart::NoReplay, marker).ok() && store.matchFiles(a)[c2].noreplay < 0);   // neben 2D-Daten: nichts
    CHECK(store.used(a) == before + int64_t(rp.size()));
    std::string clip = mp4(300000);
    int64_t size = 0;
    const std::string cid = "00112233aabbccdd";
    CHECK(store.appendClip(a, cid, 0, int64_t(clip.size()), clip.substr(0, 100000), size).ok() && size == 100000);
    Fail f = store.appendClip(a, cid, 0, int64_t(clip.size()), clip.substr(0, 100000), size);
    CHECK(f.status == 409 && f.size == 100000);   // Stück schon da: aktuelle Größe zurück
    CHECK(store.finishClip(a, cid, int64_t(clip.size()), ClipMeta()).status == 409);   // noch nicht fertig
    CHECK(store.appendClip(a, cid, 100000, int64_t(clip.size()), clip.substr(100000), size).ok() && size == 300000);
    CHECK(store.putThumb(a, cid, "\xFF\xD8\xFF\xE0jpeg").ok());
    ClipMeta meta;
    meta.title = "R7 4 Kills";
    meta.match = c1;
    meta.created = 1794053832;
    meta.seconds = 6;
    CHECK(store.finishClip(a, cid, int64_t(clip.size()), meta).ok());
    CHECK(fileSize(store.clipPath(a->id, cid, ".mp4")) == 300000 && fileSize(store.clipPath(a->id, cid, ".part")) < 0);
    CHECK(store.finishClip(a, cid, int64_t(clip.size()), meta).ok());   // nochmal (Antwort verloren): geht
    CHECK(store.appendClip(a, cid, 0, int64_t(clip.size()), "x", size).status == 409);   // fertig: nicht nochmal
    CHECK(store.clipInfos(a).size() == 1 && store.clipInfos(a)[0].title == "R7 4 Kills" && store.clipInfos(b).empty());
    const std::string bad = "ffffffffffffffff";   // kein MP4
    CHECK(store.appendClip(a, bad, 0, 10, "0123456789", size).ok() && store.finishClip(a, bad, 10, meta).status == 400);
    CHECK(fileSize(store.clipPath(a->id, bad, ".part")) < 0);
    // Limit: Freund bekommt 1 MB, ein Video mit 2 MB passt nicht – schon beim ersten Stück
    CHECK(store.setLimit(b->id, 1 << 20).ok());
    f = store.appendClip(b, cid, 0, 2 << 20, clip.substr(0, 1000), size);
    CHECK(f.status == 507 && f.error.find("Dein Speicher auf dem Server ist voll") == 0);
    CHECK(store.putMatchFile(b, c1, MatchPart::Json, m1).ok());
    std::string big(1 << 20, 'x');
    CHECK(store.putMatchFile(b, "big", MatchPart::Heat, big).status == 507);
    CHECK(store.putMatchFile(b, c1, MatchPart::Replay, "FDRP" + big).status == 507);
    store.minFree = int64_t(1) << 62;   // Platte „voll“
    CHECK(store.putThumb(b, cid, "\xFF\xD8\xFF\xE0").status == 507);
    store.minFree = 0;

    // Kopplung: Code gehört zu genau einem Konto, falsche Codes zählen, nach 5 ist er weg
    int64_t until = 0;
    std::string pa = store.startPairing(a, until), pb = store.startPairing(b, until), dev, dev2;
    CHECK(pa.size() == 6 && pb.size() == 6 && pa != pb && until > fdapi::nowEpoch());
    CHECK(store.pair(pa, "iPhone", dev, err) == a && dev.size() == 64);
    CHECK(store.pair(pa, "iPad", dev2, err) == a);   // gilt 10 Minuten für mehrere Geräte (wie in FragDesk)
    CHECK(store.byDeviceToken(dev) == a && !store.byAccountToken(dev));
    std::string wrong = "000000";
    while (wrong == pa || wrong == pb) wrong[5]++;
    for (int i = 0; i < 5; ++i) CHECK(!store.pair(wrong, "x", dev2, err));
    CHECK(!store.pair(pb, "x", dev2, err));   // nach 5 Fehlversuchen verfallen
    CHECK(store.removeDevice(a, dev) && !store.byDeviceToken(dev));
    // Auf der Platte nur SHA-256 der Schlüssel
    CHECK(!grep(data, tokenA) && !grep(data, tokenB) && !grep(data, dev2) && grep(data, sha256Hex(tokenA)));

    // Ansichten: jedes Konto sieht nur seine Matches; Generationen unterscheiden sich
    Views views(store);
    size_t na = 0, nb = 0;
    int ga = 0, gb = 0;
    std::string meA;
    CHECK(views.with(a, [&](fdapi::Source& s) {
        na = s.matches().size();
        ga = s.generation();
        meA = s.me();
    }));
    CHECK(views.with(b, [&](fdapi::Source& s) {
        nb = s.matches().size();
        gb = s.generation();
    }));
    CHECK(na == 2 && nb == 1 && ga != gb && meA == "76561190000000001" && views.loadedCount() == 2);
    fd::Match d;
    bool detailA = false, detailB = true;
    views.with(a, [&](fdapi::Source& s) { detailA = s.detail(c2, d) && !d.kills.empty(); });
    views.with(b, [&](fdapi::Source& s) { detailB = s.detail(c2, d) || s.detail("../../x", d); });   // c2 hat nur A
    CHECK(detailA && !detailB);
    std::string replayA, markerA, replayB, otherA;
    views.with(a, [&](fdapi::Source& s) {
        replayA = s.matchFile(c2, "replay");
        markerA = s.matchFile(c2, "noreplay");
        otherA = s.matchFile(c2, "json");   // nur heat, replay, noreplay
    });
    views.with(b, [&](fdapi::Source& s) { replayB = s.matchFile(c2, "replay"); });
    CHECK(replayA == store.matchPath(a->id, c2, ".replay") && markerA.empty() && otherA.empty() && replayB.empty());
    CHECK(store.putMatchFile(a, c1, MatchPart::Replay, rp).ok() && fileSize(store.matchPath(a->id, c1, ".replay")) > 0);
    CHECK(store.deleteMatch(a, c1).ok());
    CHECK(fileSize(store.matchPath(a->id, c1, ".replay")) < 0 && fileSize(store.matchPath(a->id, c1, ".json")) < 0);   // samt 2D-Daten
    views.matchChanged(a, c1);
    views.with(a, [&](fdapi::Source& s) {
        na = s.matches().size();
        CHECK(s.generation() != ga);
    });
    CHECK(na == 1);
    CHECK(views.unloadIdle(0) == 2 && views.loadedCount() == 0);

    // Konten entfernen: nicht den letzten Admin
    CHECK(store.removeUser(a->id).status == 409);
    std::string bdir = store.userDir(b->id);
    CHECK(isDir(bdir) && store.removeUser(b->id).ok() && !isDir(bdir) && !store.byAccountToken(tokenB) && b->deleted);
    CHECK(!views.with(b, [](fdapi::Source&) {}));

    // Neu öffnen: alles noch da
    store.flush();
    Store again;
    CHECK(again.open(data, err));
    UserPtr a2 = again.byAccountToken(tokenA);
    CHECK(a2 && again.info(a2).matches == 1 && again.info(a2).clips == 1 && again.used(a2) == store.used(a));
    CHECK(again.matchFiles(a2)[c2].replay == int64_t(rp.size()));   // 2D-Daten auch nach dem Neustart gezählt
    CHECK(again.users().size() == 1 && again.info(a2).devices.size() == 1);

    // Größengrenze vor dem Lesen: große Körper nur mit gültigem Konto-Token
    App app(again, "test");
    fdapi::Request head;
    head.method = "PUT";
    head.path = "/api/v1/sync/clip/" + cid + "/data";
    CHECK(app.bodyLimit(head) == 64 * 1024);
    head.headers.push_back({"authorization", "Bearer " + tokenA});
    CHECK(app.bodyLimit(head) == kMaxChunk);
    head.path = "/api/v1/sync/match/x/json";
    CHECK(app.bodyLimit(head) == kMaxMatchFile);
    head.path = "/api/v1/sync/match/x/replay";
    CHECK(app.bodyLimit(head) == kMaxReplayFile);
    head.method = "POST";
    CHECK(app.bodyLimit(head) == 64 * 1024);
    // Ergebnisse des Helfers: große Körper nur über 127.0.0.1 mit local.key, nicht über Caddy
    fdapi::Request local;
    local.method = "PUT";
    local.path = "/api/v1/local/fetch/" + a2->id + "/match/x/replay";
    local.remote = "127.0.0.1";
    CHECK(app.bodyLimit(local) == 64 * 1024);
    local.headers.push_back({"x-fragdesk-local", again.localKey()});
    CHECK(app.bodyLimit(local) == kMaxReplayFile);
    local.path = "/api/v1/local/fetch/" + a2->id + "/match/x/json";
    CHECK(app.bodyLimit(local) == kMaxMatchFile);
    local.remote = "192.0.2.1";
    CHECK(app.bodyLimit(local) == 64 * 1024);
    local.remote = "127.0.0.1";
    local.headers.push_back({"x-forwarded-for", "192.0.2.1"});   // über Caddy
    CHECK(app.bodyLimit(local) == 64 * 1024);

    autodlTests(dir + "/autodl", m1, c1);

    std::printf("server_test: %d Prüfungen, %d Fehler\n", checks, fails);
    return fails ? 1 : 0;
}

// ---- Demos automatisch laden ---------------------------------------------------------------------------------------
#define NB "\xC2\xA0"   // geschütztes Leerzeichen zwischen Zahl und Einheit
void autodlTests(const std::string& data, const std::string& matchJson, const std::string& checksum) {
    // Codes prüfen und vereinheitlichen
    CHECK(normAuthCode(" abcd-efghi-12ab\n") == "ABCD-EFGHI-12AB" && validAuthCode("ABCD-EFGHI-12AB"));
    CHECK(normAuthCode("ABCD - EFGHI - 12AB") == "ABCD-EFGHI-12AB" && normAuthCode("AB\xC2\xA0" "CD-EFGHI-12AB") == "ABCD-EFGHI-12AB");
    CHECK(!validAuthCode("ABCD-EFGH-12AB") && !validAuthCode("ABCD_EFGHI_12AB") && !validAuthCode("abcd-efghi-12ab") && !validAuthCode(""));
    CHECK(maskAuthCode("ABCD-EFGHI-12AB") == "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2-"
                                            "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2-12AB");
    CHECK(maskAuthCode("") == "" && maskAuthCode("ABCD-EFGHI-12AB").find("ABCD") == std::string::npos);
    const std::string share = "CSGO-rfymV-6yTRU-kvN6S-mkOYF-XTkDA";
    CHECK(validShareCode(share) && normShareCode(" csgo-rfymV-6yTRU-kvN6S-mkOYF-XTkDA ") == share);
    CHECK(!validShareCode("CSGO-abcd-FGHIJ-23456-kmnop-QRSTU") && !validShareCode("CSGO-abcde-FGHIJ-23456-kmnop") &&
          !validShareCode("CSGX-abcde-FGHIJ-23456-kmnop-QRSTU") && !validShareCode(share + "-x") && !validShareCode("CSGO-abcde-FGH!J-23456-kmnop-QRSTU"));
    // wie Valve: ohne 0, 1, I, l, g und höchstens 144 Bit (sonst lehnt fragdesk-fetch den Code ab)
    CHECK(!validShareCode("CSGO-abcde-FGHIJ-23456-kmnop-QRSTU") && !validShareCode("CSGO-rfymV-6yTRU-kvN6S-mkOYF-XTk0A") &&
          !validShareCode("CSGO-ZZZZZ-YYYYY-XXXXX-WWWWW-VVVVV") && validShareCode("CSGO-AAAAA-BBBBB-CCCCC-DDDDD-EEEEE"));
    CHECK(normApiKey("0123456789abcdef0123456789ABCDEF") == "0123456789ABCDEF0123456789ABCDEF" && validApiKey("0123456789ABCDEF0123456789ABCDEF"));
    CHECK(!validApiKey("0123456789ABCDEF0123456789ABCDE") && !validApiKey("0123456789ABCDEF0123456789ABCDEG"));
    CHECK(normGuardCode(" ab12c ") == "AB12C" && validGuardCode("AB12C") && !validGuardCode("AB12") && !validGuardCode("AB12C3"));
    CHECK(validRefreshToken("eyJhbGciOiJFZERTQSJ9.eyJpc3MiOiJzdGVhbSJ9.c2lnbmF0dXJl") && !validRefreshToken("kurz") &&
          !validRefreshToken("eyJhbGciOiJFZERTQSJ9.eyJpc3MiOiJzdGVhbSJ9 c2lnbmF0dXJl"));
    CHECK(validSteamName("meinserver_bot") && !validSteamName("") && !validSteamName("mit leerzeichen"));
    CHECK(epochFrom(1762588800) == 1762588800 && epochFrom(1762588800123.0) == 1762588800 && epochFrom(-5) == 0 && epochFrom(0) == 0);
    CHECK(agoText(5) == "gerade eben" && agoText(185) == "vor 3" NB "min" && agoText(7300) == "vor 2" NB "Std." && agoText(90000) == "vor 1" NB "Tag" &&
          agoText(4 * 86400) == "vor 4" NB "Tagen");
    CHECK(matchesText(1) == "1" NB "Match" && matchesText(4) == "4" NB "Matches");
    CHECK(helperProblem("").empty() && helperProblem("on").empty() && helperProblem("node").find("Node.js") != std::string::npos &&
          !helperProblem("off").empty());

    // Sätze für die Handy-App
    AutodlFacts f;
    std::string tone;
    f.now = 1762588800;
    CHECK(autodlStatus(f, tone).find("Spieler wählen") != std::string::npos && tone == "wait");
    f.hasSteamId = true;
    CHECK(autodlStatus(f, tone) == "Der Admin muss erst den Steam-Bot einrichten. Deine Codes kannst du schon eintragen." && tone == "wait");
    f.admin = true;
    CHECK(autodlStatus(f, tone).find("Server verwalten › Steam-Bot") != std::string::npos);
    f.helperProblem = helperProblem("node");
    CHECK(autodlStatus(f, tone) == f.helperProblem);
    f.admin = false;
    CHECK(autodlStatus(f, tone).find("Sag dem Admin Bescheid") != std::string::npos);
    f.helperProblem.clear();
    f.botReady = true;
    CHECK(autodlStatus(f, tone).find("trag deine beiden Codes ein") != std::string::npos && tone == "off");
    f.enabled = f.hasAuth = f.hasKnown = true;
    CHECK(autodlStatus(f, tone) == "Eingeschaltet – die erste Prüfung kommt gleich." && tone == "ok");
    f.lastCheck = f.now - 185;
    f.fetched = 4;
    CHECK(autodlStatus(f, tone) == "Prüft alle 15" NB "Minuten · zuletzt vor 3" NB "min · 4" NB "Matches geladen" && tone == "ok");
    f.fetched = 0;
    CHECK(autodlStatus(f, tone) == "Prüft alle 15" NB "Minuten · zuletzt vor 3" NB "min · noch kein neues Match");
    f.lastError = "Der Authentifizierungscode passt nicht zu deinem Steam-Konto.";
    CHECK(autodlStatus(f, tone) == f.lastError + " (vor 3" NB "min)" && tone == "error");
    f.checking = true;
    CHECK(autodlStatus(f, tone).rfind("Prüft gerade …", 0) == 0);
    f.full = true;
    CHECK(autodlStatus(f, tone).find("Speicher auf dem Server ist voll") != std::string::npos && tone == "error");
    f.full = f.checking = f.botReady = false;
    f.botNeedsLogin = true;   // Steam nimmt das Token des Bots nicht mehr
    CHECK(autodlStatus(f, tone) == "Der Steam-Bot des Servers muss neu angemeldet werden – sag dem Admin Bescheid." && tone == "error");
    f.enabled = false;
    CHECK(autodlStatus(f, tone).find("neu angemeldet") != std::string::npos && tone == "wait");   // stört nur, wer es eingeschaltet hat
    f.admin = true;
    CHECK(autodlStatus(f, tone) == "Der Steam-Bot muss neu angemeldet werden: Server verwalten › Steam-Bot.");

    // Anmeldung des Bots: Passwort und Code je genau einmal, nach 15 Minuten vergessen
    SteamLogin login;
    int64_t t = 1000000;
    std::string err;
    CHECK(login.view(t).state == "idle" && !login.take(t).active);
    CHECK(!login.code("ABCDE", t, err) && !err.empty());   // ohne Anmeldung kein Code
    login.start("meinbot", "geheim-Passwort-1", t);
    SteamLogin::View lv = login.view(t);
    CHECK(lv.state == "working" && lv.active && !lv.pickedUp);
    SteamLogin::Handout h = login.take(t + 1);
    CHECK(h.active && h.user == "meinbot" && h.hasPassword && h.password == "geheim-Passwort-1" && !h.hasCode);
    h = login.take(t + 2);
    CHECK(h.active && h.user == "meinbot" && !h.hasPassword && h.password.empty() && !h.hasCode);   // nur einmal
    CHECK(login.view(t + 2).pickedUp);
    CHECK(!login.code("ABCDE", t + 3, err));   // noch kein need-code
    CHECK(login.report("need-code", "", "app", t + 4, err));
    lv = login.view(t + 4);
    CHECK(lv.state == "need-code" && lv.codeType == "app" && lv.hint.find("Steam-App") != std::string::npos);
    CHECK(login.code("XY9Z8", t + 5, err));
    CHECK(login.view(t + 5).state == "working" && login.view(t + 5).hint == "Code wird geprüft …");
    h = login.take(t + 6);
    CHECK(h.hasCode && h.code == "XY9Z8" && !h.hasPassword);
    CHECK(!login.take(t + 7).hasCode);   // auch der Code nur einmal
    CHECK(login.report("need-code", "Code falsch – bitte den neuen Code eingeben.", "email", t + 8, err));
    CHECK(login.view(t + 8).codeType == "email" && login.view(t + 8).hint == "Code falsch – bitte den neuen Code eingeben.");
    CHECK(login.report("need-confirm", "", "", t + 9, err) && login.view(t + 9).state == "need-confirm" && login.view(t + 9).codeType.empty());
    CHECK(login.report("need-confirm", "", "app", t + 9, err) && login.view(t + 9).codeType == "app");   // auch der App-Code geht
    CHECK(login.code("QWE2T", t + 9, err) && login.take(t + 9).code == "QWE2T");   // statt Bestätigen: Code aus der App
    CHECK(!login.report("bogus", "", "", t + 9, err));
    CHECK(login.report("done", "Angemeldet als meinbot.", "", t + 10, err));
    lv = login.view(t + 10);
    CHECK(lv.state == "done" && !lv.active && !login.take(t + 11).active);
    CHECK(!login.report("need-code", "", "email", t + 12, err));   // nach dem Ende keine Zwischenstände mehr
    CHECK(login.view(t + 10 + kLoginSeconds).state == "idle");     // Ergebnis verschwindet nach 15 Minuten
    // Ablauf: offene Anmeldung nach 15 Minuten vergessen
    login.start("meinbot", "geheim-Passwort-2", t);
    CHECK(login.report("need-code", "", "email", t + 30, err));
    CHECK(login.view(t + kLoginSeconds - 1).active);
    CHECK(!login.take(t + kLoginSeconds).active);
    lv = login.view(t + kLoginSeconds);
    CHECK(lv.state == "error" && lv.hint.find("Zeit abgelaufen") != std::string::npos);
    CHECK(!login.code("ABCDE", t + kLoginSeconds, err) && err.find("Bot anmelden") != std::string::npos);
    CHECK(login.report("error", "Zeit abgelaufen", "", t + kLoginSeconds + 5, err));   // Ergebnis gilt auch danach
    login.start("meinbot", "geheim-Passwort-3", t);
    login.expire(t + kLoginSeconds + 1);
    CHECK(!login.take(t + kLoginSeconds + 1).hasPassword);
    login.start("meinbot", "geheim-Passwort-4", t);
    login.cancel();
    CHECK(login.view(t).state == "idle" && !login.take(t).active);
    // "error" vom Helfer ohne offene Anmeldung: verspätet (Anmeldung eben abgelaufen) oder aus dem normalen Lauf
    SteamLogin late;
    CHECK(!late.loginError(t));                                  // nie angemeldet: aus dem Lauf (Token abgelehnt)
    late.start("meinbot", "geheim-Passwort-5", t);
    CHECK(late.loginError(t + 1));                               // offen: gehört zur Anmeldung
    CHECK(late.take(t + 2).hasPassword && late.report("need-code", "", "email", t + 3, err));
    CHECK(late.view(t + kLoginSeconds).state == "error");        // beim Server abgelaufen …
    CHECK(late.loginError(t + kLoginSeconds + 600));             // … der Helfer meldet sich 10 Minuten später: kein Token-Problem
    CHECK(late.report("error", "Zeit abgelaufen – bitte noch einmal anmelden.", "", t + kLoginSeconds + 600, err));
    CHECK(!late.loginError(t + 2 * kLoginSeconds + 1));          // viel später: wieder aus dem Lauf
    late.start("meinbot", "geheim-Passwort-6", t);
    CHECK(late.take(t + 1).hasPassword && late.report("done", "Angemeldet als meinbot.", "", t + 20, err));
    CHECK(late.view(t + 30).state == "done" && !late.loginError(t + 30));   // Token abgelehnt: „angemeldet“ gilt nicht mehr
    CHECK(late.view(t + 31).state == "idle");
    late.start("meinbot", "geheim-Passwort-7", t);              // nie abgeholt und abgelaufen: kann nur aus dem Lauf kommen
    CHECK(late.view(t + kLoginSeconds).state == "error" && !late.loginError(t + kLoginSeconds + 5));

    // Speichern, Auftrag, Meldungen des Helfers
    removeTree(data);
    Store s;
    CHECK(s.open(data, err));
    CHECK(mode(s.fetchDir()) == 0700);
    Invite inv;
    std::string code, ta, tb;
    UserPtr a, b;
    CHECK(s.createInvite(true, 7, "", inv, code).ok() && s.registerUser(code, "Micha", "76561190000000001", "", a, ta).ok());
    CHECK(s.createInvite(false, 7, a->id, inv, code).ok() && s.registerUser(code, "Ohne Spieler", "", "", b, tb).ok());
    const std::string auth = "ABCD-EFGHI-12AB", known2 = "CSGO-qAQS4-hUAmk-eOzmO-BzS7q-m3LHA";
    CHECK(s.setAutodl(b, auth, share, 1).status == 400);   // ohne SteamID
    CHECK(s.setAutodl(a, "", "", 1).status == 400);         // zum Einschalten fehlen die Codes
    CHECK(s.setAutodl(a, auth, "", 1).status == 400 && s.autodl(a).authCode.empty());   // nichts halb gespeichert
    CHECK(s.setAutodl(a, auth, share, 1).ok() && s.autodl(a).enabled && s.info(a).autodl && s.autodlCount() == 1);
    std::string acct;
    CHECK(readFile(s.userDir(a->id) + "/account.json", acct) && acct.find("\"autodl\":{\"enabled\":true") != std::string::npos &&
          acct.find(auth) != std::string::npos && mode(s.userDir(a->id) + "/account.json") == 0600);
    CHECK(s.setAutodl(a, "", "", 0).ok() && !s.autodl(a).enabled && s.autodl(a).authCode == auth);   // aus: Codes bleiben
    CHECK(s.fetchAccounts(t).empty());
    CHECK(s.setAutodl(a, "", "", 1).ok());   // wieder an, mit den gespeicherten Codes
    std::vector<FetchAccount> job = s.fetchAccounts(t);
    CHECK(job.size() == 1 && job[0].id == a->id && job[0].steamId == "76561190000000001" && job[0].authCode == auth && job[0].knownCode == share);
    bool ignored = true;
    std::string none;
    CHECK(s.fetchState(a, known2, "", t + 5, 0, &none, ignored).ok() && !ignored && s.autodl(a).knownCode == known2 && s.autodl(a).lastCheck == t + 5);
    std::string e403 = "Der Authentifizierungscode passt nicht.";
    CHECK(s.fetchState(a, known2, "", t + 6, 0, &e403, ignored).ok() && s.autodl(a).lastError == e403);
    CHECK(s.fetchState(a, known2, "", t + 7, 0, nullptr, ignored).ok() && s.autodl(a).lastError == e403);   // ohne lastError: bleibt
    // Codes am Handy geändert, während der Helfer noch mit den alten arbeitet: seine Meldung gilt nicht mehr
    CHECK(s.setAutodl(a, "", share, -1).ok() && s.autodl(a).lastError.empty() && s.autodl(a).lastCheck == 0);
    CHECK(s.fetchState(a, known2, "", t + 8, 0, &none, ignored).ok() && ignored && s.autodl(a).knownCode == share);
    CHECK(s.fetchAccounts(t + 9).size() == 1 && s.fetchState(a, known2, "", t + 9, 0, &none, ignored).ok() && !ignored && s.autodl(a).knownCode == known2);
    // from: der Helfer geht von einem bestimmten Freigabecode aus – passt er nicht mehr, gilt die Meldung nicht
    CHECK(s.fetchState(a, share, share, t + 9, 0, &none, ignored).ok() && ignored && s.autodl(a).knownCode == known2);
    CHECK(s.fetchState(a, known2, known2, t + 9, 0, &none, ignored).ok() && !ignored);
    // neue Matches zählt der Server selbst
    bool isNew = false;
    CHECK(s.putMatchFile(a, checksum, MatchPart::Json, matchJson, &isNew).ok() && isNew);
    s.fetchedMatch(a, t + 10);
    CHECK(s.putMatchFile(a, checksum, MatchPart::Json, matchJson, &isNew).ok() && !isNew);   // ersetzt: nicht neu
    CHECK(s.putMatchFile(a, checksum, MatchPart::Heat, "FDHM1234", &isNew).ok() && !isNew);
    CHECK(s.autodl(a).fetched == 1 && s.autodl(a).lastNew == t + 10);
    // Demo-Quelle (.src): nur zu einem Match auf dem Server, gezählt, nach dem Neustart wieder da, weg mit dem Match
    CHECK(validDemoUrl("http://replay183.valve.net/730/003717052436717584506_1418282143.dem.bz2") && validDemoUrl("https://x.example/a.dem.bz2"));
    CHECK(!validDemoUrl("ftp://replay183.valve.net/a.dem.bz2") && !validDemoUrl("http://") && !validDemoUrl("http://a b/x") &&
          !validDemoUrl("http://user:pw@host/x") && !validDemoUrl("http://host/\"x") && !validDemoUrl("http://host/" + std::string(600, 'a')));
    MatchSource src;
    src.url = "http://replay183.valve.net/730/003717052436717584506_1418282143.dem.bz2";
    src.shareCode = share;
    src.matchtime = t;
    src.until = t + kDemoKeepDays * 86400;
    src.at = t + 10;
    int64_t usedBefore = s.used(a);
    CHECK(s.putMatchSource(a, "nichtda", src).status == 404);
    CHECK(s.putMatchSource(a, checksum, src).ok() && s.matchFiles(a)[checksum].src > 0 && s.matchFiles(a)[checksum].source.url == src.url);
    CHECK(s.used(a) == usedBefore + s.matchFiles(a)[checksum].src && mode(s.matchPath(a->id, checksum, ".src")) == 0600);
    std::string srcText;
    MatchSource back;
    CHECK(readFile(s.matchPath(a->id, checksum, ".src"), srcText) && parseSource(srcText, back) && back.url == src.url && back.matchtime == t &&
          back.until == src.until && back.shareCode == share && back.at == t + 10 && srcText.find("\"fetched\":true") != std::string::npos);
    CHECK(s.putMatchFile(a, checksum, MatchPart::Json, matchJson).ok() && s.matchFiles(a)[checksum].src > 0);   // neue Auswertung: Quelle bleibt
    {
        Store again;
        CHECK(again.open(data, err));
        UserPtr a3 = again.byAccountToken(ta);
        MatchRec rec = a3 ? again.matchFiles(a3)[checksum] : MatchRec();
        CHECK(rec.src > 0 && rec.source.url == src.url && rec.source.until == src.until && again.used(a3) == s.used(a));
    }
    {
        Store tmp;   // eigenes Konto zum Löschen (der Rest des Tests braucht das Match von a)
        std::string d2 = data + "-src", c2, t2;
        removeTree(d2);
        Invite i2;
        UserPtr u2;
        CHECK(tmp.open(d2, err) && tmp.createInvite(true, 7, "", i2, c2).ok() && tmp.registerUser(c2, "X", "76561190000000003", "", u2, t2).ok());
        CHECK(tmp.putMatchFile(u2, checksum, MatchPart::Json, matchJson).ok() && tmp.putMatchSource(u2, checksum, src).ok());
        CHECK(tmp.deleteMatch(u2, checksum).ok() && fileSize(tmp.matchPath(u2->id, checksum, ".src")) < 0 && tmp.used(u2) == 0);
        removeTree(d2);
    }
    // Speicher fast voll: das Konto fehlt im Auftrag (nichts umsonst herunterladen)
    CHECK(s.setLimit(a->id, s.used(a) + (1 << 20)).ok() && s.nearlyFull(a) && s.fetchAccounts(t + 11).empty());
    CHECK(s.setLimit(a->id, 0).ok() && !s.nearlyFull(a) && s.fetchAccounts(t + 12).size() == 1);
    // wake: atomar neu geschrieben, aktuelle Zeit
    CHECK(s.wakeHelper());
    std::string wake = s.fetchDir() + "/wake", w1, w2;
    int64_t ino1 = inode(wake);
    CHECK(readFile(wake, w1) && std::atoll(w1.c_str()) > 1700000000 && w1.find('.') != std::string::npos && mode(wake) == 0600);
    CHECK(s.wakeHelper() && readFile(wake, w2) && inode(wake) != ino1);   // neue Datei (Path-Unit: PathModified)
    bool tmpLeft = false;
    for (const std::string& n : listDir(s.fetchDir())) tmpLeft = tmpLeft || n.find(".tmp-") != std::string::npos;
    CHECK(!tmpLeft);
    // „Jetzt prüfen“ höchstens alle 30 s
    CHECK(s.requestCheck(a, t) && !s.requestCheck(a, t + 29) && s.requestCheck(a, t + 30));
    // aus und Codes löschen: der Helfer bekommt nichts mehr, Meldungen gelten nicht
    CHECK(s.clearAutodl(a).ok() && !s.autodl(a).enabled && s.autodl(a).authCode.empty() && s.autodl(a).knownCode.empty());
    CHECK(s.fetchAccounts(t + 13).empty() && s.fetchState(a, known2, "", t + 13, 0, &none, ignored).status == 403);
    CHECK(s.autodl(a).fetched == 1);   // gezählte Matches bleiben
    CHECK(s.setAutodl(a, auth, share, 1).ok());

    // Steam-Bot: Geheimnisse nur in steam.json (0600), server.json nur Name, SteamID, Ablauf
    const std::string token = "eyJhbGciOiJFZERTQSJ9.eyJzdWIiOiI3NjU2MTE5OCJ9.dGVzdC1zaWduYXR1cmU";
    const std::string key = "0123456789ABCDEF0123456789ABCDEF";
    CHECK(!s.steam().ready(t) && fileSize(data + "/steam.json") < 0);
    CHECK(s.setSteamKey(key).ok() && s.steam().hasKey && !s.steam().ready(t) && mode(data + "/steam.json") == 0600);
    CHECK(s.setSteamLogin(token, "meinbot", "76561199000000001", t + 200 * 86400).ok() && s.steam().ready(t) && !s.steam().ready(t + 201 * 86400));
    std::string rt, ak, sj, srv;
    s.steamSecrets(rt, ak);
    CHECK(rt == token && ak == key);
    CHECK(readFile(data + "/steam.json", sj) && sj.find(token) != std::string::npos && sj.find(key) != std::string::npos);
    CHECK(readFile(data + "/server.json", srv) && srv.find("\"botName\":\"meinbot\"") != std::string::npos && srv.find(token) == std::string::npos &&
          srv.find(key) == std::string::npos);
    CHECK(s.setSteamToken(token + "x", 0).ok() && s.steam().tokenUntil == t + 200 * 86400);   // ohne neuen Ablauf: bleibt
    // Steam lehnt das Token im normalen Lauf ab: Bot braucht eine neue Anmeldung, das Token geht nicht mehr an den Helfer
    s.steamTokenRejected("Steam nimmt das Anmelde-Token nicht mehr an – bitte neu anmelden.");
    SteamBot sb0 = s.steam();
    CHECK(!sb0.loggedIn(t) && sb0.needsLogin(t) && !sb0.ready(t) && sb0.tokenHint.find("Steam nimmt") == 0 && sb0.name == "meinbot");
    s.steamSecrets(rt, ak);
    CHECK(rt.empty() && ak == key);
    CHECK(s.setSteamToken(token + "x", 0).ok() && s.steam().loggedIn(t) && !s.steam().needsLogin(t) && s.steam().tokenHint.empty());
    s.runPhase(true, "", t);
    CHECK(s.steam().running && s.steam().runStart == t);
    s.runPhase(false, "2 neue Matches geladen", t + 60);
    CHECK(!s.steam().running && s.steam().lastRunAt == t + 60 && s.steam().lastRunText == "2 neue Matches geladen");
    s.flush();
    {
        Store again;   // alles wieder da
        CHECK(again.open(data, err));
        UserPtr a2 = again.byAccountToken(ta);
        AutoDl d = a2 ? again.autodl(a2) : AutoDl();
        CHECK(d.enabled && d.authCode == auth && d.knownCode == share && d.fetched == 1);
        SteamBot sb = again.steam();
        CHECK(sb.name == "meinbot" && sb.steamId == "76561199000000001" && sb.hasToken && sb.hasKey && sb.lastRunText == "2 neue Matches geladen");
        std::string rt2, ak2;
        again.steamSecrets(rt2, ak2);
        CHECK(rt2 == token + "x" && ak2 == key);
        CHECK(again.forgetSteamBot().ok() && !again.steam().hasToken && again.steam().name.empty() && again.steam().hasKey);   // Schlüssel bleibt
        CHECK(readFile(data + "/steam.json", sj) && sj.find(token) == std::string::npos && sj.find(key) != std::string::npos);
        CHECK(again.setSteamKey("").ok() && fileSize(data + "/steam.json") < 0);   // nichts Geheimes mehr: Datei weg
    }
    // Passwörter der Anmeldung landen nie auf der Platte
    CHECK(!grep(data, "geheim-Passwort"));
}
