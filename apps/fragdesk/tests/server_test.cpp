// Tests für fragdesk-server (Linux): SHA-256, Prüfungen, Grenzen gegen Raten, Konten, Einladungen, Dateien mit
// Kontingent, Kopplung, Ansichten pro Konto. Aufruf: server_test <leerer Ordner> <Ordner mit Test-Matches>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../server/app.h"
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

bool grep(const std::string& dir, const std::string& needle) {   // steht needle irgendwo in einer Datei?
    for (const std::string& n : listDir(dir)) {
        std::string p = joinPath(dir, n), text;
        if (isDir(p) ? grep(p, needle) : readFile(p, text) && text.find(needle) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

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

    std::printf("server_test: %d Prüfungen, %d Fehler\n", checks, fails);
    return fails ? 1 : 0;
}
