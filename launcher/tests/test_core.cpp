// efficient lamp – Tests für den plattformunabhängigen Kern (JSON, SHA-256, ZIP/Inflate, Katalog, Versionen).
// Aufruf: tests/run.sh   (baut mit g++, erzeugt die Testdaten mit Python und startet die Prüfungen)
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../src/core/catalog.h"
#include "../src/core/json.h"
#include "../src/core/sha256.h"
#include "../src/core/zip.h"

static int g_checks = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %s %s\n", ok ? "ok   " : "FAIL ", what.c_str());
    if (!ok) ++g_fail;
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static const char* kFiles[] = {"Paket/README.md", "Paket/zufall.bin", "Paket/lang.txt", "Paket/leer.txt", "Paket/Unterordner/klein.txt", "Paket/mix.bin"};

static void testJson() {
    std::printf("JSON\n");
    json::Value v;
    std::string err;
    check(json::parse("{\"a\": [1, 2.5, -3e2, true, false, null], \"b\": {\"c\": \"x\\ny\\u00fc\\ud83d\\ude00\"}}", v, &err), "verschachteltes Dokument");
    check(v["a"].arr.size() == 6 && v["a"].arr[2].num == -300 && v["a"].arr[3].b, "Zahlen und Wahrheitswerte");
    check(v["b"].getString("c") == "x\ny\xc3\xbc\xf0\x9f\x98\x80", "Escapes, Umlaut und Emoji (Ersatzpaar)");
    check(v["fehlt"].type == json::Value::Null && v.getString("fehlt", "std") == "std", "fehlender Schlüssel liefert Standardwert");
    check(json::parse("\xEF\xBB\xBF{\"k\":\"v\"}", v, &err) && v.getString("k") == "v", "UTF-8-BOM am Anfang");
    check(!json::parse("{\"a\":1,}", v, &err), "Komma am Ende abgelehnt");
    check(!json::parse("{\"a\":1} x", v, &err), "Text nach dem Ende abgelehnt");
    check(!json::parse("{\"a\":\"offen}", v, &err), "offene Zeichenkette abgelehnt");
    check(!json::parse(std::string(200, '['), v, &err), "zu tiefe Verschachtelung abgelehnt");
    check(!json::parse("", v, &err), "leerer Text abgelehnt");
}

static void testSha() {
    std::printf("SHA-256\n");
    check(sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "leerer Text");
    check(sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "\"abc\"");
    std::string m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check(sha256Hex(m.data(), m.size()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "zwei Blöcke (56 Zeichen)");
    Sha256 s;
    std::string a(1000, 'a');
    for (int i = 0; i < 1000; ++i) s.update(a.data(), a.size());
    check(s.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "eine Million 'a' in Stücken");
}

static void testZip(const std::string& dir) {
    std::printf("ZIP und Inflate\n");
    for (const char* name : {"deflate1.zip", "deflate6.zip", "deflate9.zip", "stored.zip"}) {
        std::string z = readFile(dir + "/" + name), err;
        std::vector<zip::Entry> entries;
        bool ok = zip::list(z, entries, err);
        int good = 0, files = 0;
        for (const auto& e : entries) {
            if (e.isDir) continue;
            ++files;
            std::string data;
            if (zip::read(z, e, data, err) && data == readFile(dir + "/orig/" + e.name)) ++good;
            else std::printf("        %s: %s\n", e.name.c_str(), err.c_str());
        }
        check(ok && files == 6 && good == 6, std::string(name) + ": alle 6 Dateien stimmen byte-genau");
    }
    for (const char* tag : {"fixed", "level0"}) {
        int good = 0;
        for (const char* f : kFiles) {
            std::string base = f;
            for (char& c : base)
                if (c == '/') c = '_';
            std::string raw = readFile(dir + "/raw-" + tag + "-" + base + ".deflate"), out, err;
            std::string orig = readFile(dir + "/orig/" + f);
            if (zip::inflateRaw(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), out, orig.size(), err) && out == orig) ++good;
            else std::printf("        %s: %s\n", f, err.c_str());
        }
        check(good == 6, std::string("rohes Deflate (") + tag + "): alle 6 Dateien");
    }
    {
        std::string raw = readFile(dir + "/raw-fixed-Paket_lang.txt.deflate"), out, err;
        bool ok = zip::inflateRaw(reinterpret_cast<const uint8_t*>(raw.data()), raw.size(), out, 1000, err);
        check(!ok, "Ausgabegrenze greift (Zip-Bombe): " + err);
        ok = zip::inflateRaw(reinterpret_cast<const uint8_t*>(raw.data()), raw.size() / 2, out, 1u << 20, err);
        check(!ok, "abgeschnittene Daten erkannt: " + err);
    }
    {
        std::string z = readFile(dir + "/broken.zip"), err;
        std::vector<zip::Entry> entries;
        int bad = 0;
        if (zip::list(z, entries, err))
            for (const auto& e : entries) {
                std::string data;
                if (!e.isDir && !zip::read(z, e, data, err)) ++bad;
            }
        else bad = 1;
        check(bad >= 1, "beschädigtes Archiv fällt auf: " + err);
    }
    {
        std::string z = readFile(dir + "/slip.zip"), err;
        std::vector<zip::Entry> entries;
        check(zip::list(z, entries, err) && entries.size() == 1 && !zip::safeName(entries[0].name), "\"../boese.txt\" wird als unsicher erkannt");
        check(zip::safeName("DeskCats/Setup.ps1") && !zip::safeName("C:/Windows/x") && !zip::safeName("/etc/x") && !zip::safeName("a/../../b") &&
                  !zip::safeName("a\\..\\b"),
              "Namensprüfung");
        check(!zip::safeName("COM1.tmp") && !zip::safeName("x/nul") && !zip::safeName("Con.txt") && !zip::safeName("a./b") && !zip::safeName("a /b") &&
                  !zip::safeName("a//b") && zip::safeName("Konsole.txt") && zip::safeName("COM10.txt") && zip::safeName("x/null.txt"),
              "Gerätenamen und Punkte/Leerzeichen am Ende abgelehnt");
    }
    check(zip::crc32("123456789", 9) == 0xCBF43926u, "CRC-32 Prüfwert");
    std::string err;
    std::vector<zip::Entry> e;
    check(!zip::list("kein zip", e, err), "Unsinn ist kein ZIP");
}

static void testVersions() {
    std::printf("Versionen\n");
    check(cat::compareVersions("1.1.0", "1.0.9") > 0, "1.1.0 > 1.0.9");
    check(cat::compareVersions("1.10.0", "1.9.2") > 0, "1.10.0 > 1.9.2 (numerisch)");
    check(cat::compareVersions("v1.0", "1.0.0") == 0, "v1.0 == 1.0.0");
    check(cat::compareVersions("1.0.0-beta", "1.0.0") < 0, "1.0.0-beta < 1.0.0");
    check(cat::compareVersions("2", "1.99.99") > 0, "2 > 1.99.99");
    check(cat::compareVersions("", "1.0") < 0, "leer < 1.0");
    check(cat::findVersionString("  public const string Version = \"1.1.0\";\n") == "1.1.0", "Version aus C#-Quelltext");
    check(cat::findVersionString("$AppVersion     = '1.0.0'") == "1.0.0", "Version aus PowerShell");
    check(cat::findVersionString("Version: kaputt; Version = \"\"; Version = \"x\"") == "", "nichts Brauchbares gefunden");
}

static std::string sampleCatalog(const std::string& patchApp) {
    return std::string(R"({
  "schema": 1, "name": "efficient lamp", "release": "v1.0.0", "repo": "Micha040/effective-lamp",
  "launcher": {"version": "1.0.0", "file": "EfficientLamp.exe", "url": "https://example.org/EfficientLamp.exe", "size": 1, "sha256": ""},
  "apps": [
    {"id": "deskcats", "name": "DeskCats", "version": "1.0.0", "tagline": "Katzen", "description": "Zwei Pixel-Katzen",
     "download": {"file": "DeskCats.zip", "url": "https://example.org/DeskCats.zip", "size": 600000,
                  "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "folder": "DeskCats"},
     "process": "DeskCats.exe", "installDir": "%LOCALAPPDATA%\\Programs\\DeskCats",
     "detect": {"uninstallKey": "DeskCats", "exe": "%LOCALAPPDATA%\\Programs\\DeskCats\\DeskCats.exe"},
     "install": {"script": "Setup.cmd"}, "uninstall": {"script": "Uninstall.cmd"},
     "open": {"exe": "%LOCALAPPDATA%\\Programs\\DeskCats\\DeskCats.exe", "label": "Öffnen", "wait": false}}
    )") + patchApp + "]}";
}

static void testCatalog(const std::string& real) {
    std::printf("Katalog\n");
    cat::Catalog c;
    std::string err;
    check(cat::parse(sampleCatalog(""), c, err) && c.apps.size() == 1, "Beispielkatalog gelesen " + err);
    if (c.apps.size() == 1) {
        const cat::App& a = c.apps[0];
        check(a.install.script == "Setup.cmd" && a.open.exe.find("DeskCats.exe") != std::string::npos && !a.open.wait && a.open.label == "\xc3\x96" "ffnen",
              "Befehle und Beschriftung");
        check(a.uninstallKey == "DeskCats" && a.folder == "DeskCats" && a.size == 600000, "Erkennung und Paketdaten");
    }
    auto bad = [&](const std::string& extra, const std::string& what) {
        cat::Catalog cc;
        std::string e;
        bool ok = cat::parse(sampleCatalog(extra), cc, e);
        check(ok && cc.apps.size() == 1 && cc.skipped.size() == 1, what + " übersprungen: " + (cc.skipped.empty() ? e : cc.skipped[0]));
    };
    const std::string base0 = R"(,{"id": "x", "name": "X", "version": "1", "download": {"url": "https://e/x.zip", "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}, )";
    const std::string base = base0 + R"("detect": {"exe": "%LOCALAPPDATA%\\X\\x.exe"}, )";
    bad(base + R"("install": {"script": "..\\..\\evil.cmd"}})", "Skript außerhalb des Pakets");
    bad(base + R"("install": {"script": "Setup.cmd"}, "open": {"exe": "C:\\Windows\\System32\\cmd.exe"}})", "Programm außerhalb des Benutzerordners");
    bad(base + R"("installDir": "%LOCALAPPDATA%\\X", "install": {"script": "Setup.cmd"}, "open": {"exe": "%LOCALAPPDATA%\\Microsoft\\WindowsApps\\winget.exe"}})",
        "Programm außerhalb des eigenen Ordners");
    bad(base + R"("install": {"script": "Setup.cmd", "args": "& del *"}})", "Befehlsverkettung in args");
    bad(base + R"("install": {"script": "Setup.cmd", "args": "-Name %USERNAME%"}})", "Umgebungsvariable in args");
    bad(base + R"("install": {"script": "Setup.cmd", "args": "a\u0000b"}})", "Nullzeichen in args");
    bad(base0 + R"("install": {"script": "Setup.cmd"}, "detect": {"exe": "%LOCALAPPDATA%\\..\\x.exe"}})", "\"..\" im Pfad");
    bad(base0 + R"("install": {"script": "Setup.cmd"}})", "fehlende Erkennung");
    bad(R"(,{"id": "y", "name": "Y", "version": "..", "download": {"url": "https://e/y.zip", "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}, "detect": {"exe": "%APPDATA%\\y.exe"}, "install": {"script": "Setup.cmd"}})",
        "Version \"..\"");
    bad(R"(,{"id": "z", "name": "Z", "version": "1", "download": {"url": "https://e/z.zip", "size": -1, "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}, "detect": {"exe": "%APPDATA%\\z.exe"}, "install": {"script": "Setup.cmd"}})",
        "negative Größe");
    bad(R"(,{"id": "deskcats", "name": "D", "version": "1", "download": {"url": "u", "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}, "detect": {"exe": "%APPDATA%\\d.exe"}, "install": {"script": "Setup.cmd"}})",
        "doppelte ID");
    bad(R"(,{"id": "Gross", "name": "D", "version": "1"})", "ungültige ID");
    {
        cat::Catalog cc;
        std::string e;
        bool ok = cat::parse("{\"schema\": 2, \"launcher\": {\"version\": \"2.0.0\", \"url\": \"https://e/l.exe\", \"size\": 5, "
                             "\"sha256\": \"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"}, \"apps\": [{\"neu\": true}]}",
                             cc, e);
        check(ok && !cc.supported && cc.apps.empty() && cc.launcher.version == "2.0.0", "neueres Format: Programme ignoriert, Launcher-Update trotzdem gelesen");
        check(!cat::parse("{\"schema\": 1.9, \"apps\": []}", cc, e), "Format 1.9 abgelehnt: " + e);
        check(!cat::parse("[1, 2]", cc, e), "kein Objekt abgelehnt: " + e);
    }
    check(cat::isValidVersion("1.0.0") && cat::isValidVersion("1.0.0-beta") && !cat::isValidVersion("..") && !cat::isValidVersion("1.") &&
              !cat::isValidVersion("v1") && !cat::isValidVersion("1/2"),
          "gültige und ungültige Versionen");
    if (!real.empty()) {
        std::string text = readFile(real);
        cat::Catalog rc;
        bool ok = cat::parse(text, rc, err);
        check(ok && rc.apps.size() >= 4, "echter Katalog " + real + " (" + std::to_string(rc.apps.size()) + " Programme) " + err);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("Aufruf: test_core <Testdaten-Ordner> [catalog.json]\n");
        return 2;
    }
    testJson();
    testSha();
    testZip(argv[1]);
    testVersions();
    testCatalog(argc > 2 ? argv[2] : "");
    std::printf("\n%d Prüfungen, %d Fehler\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
