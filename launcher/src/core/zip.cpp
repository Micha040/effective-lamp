// efficient lamp – ZIP lesen + Inflate (siehe zip.h). Der Decoder folgt dem Aufbau von "puff" (Mark Adler):
// kanonische Huffman-Codes, bitweise dekodiert – langsam genug egal, dafür kurz und gut prüfbar.
#include "zip.h"

#include <cstring>

namespace zip {

namespace {

uint16_t u16(const std::string& d, size_t o) { return uint16_t((unsigned char)d[o] | ((unsigned char)d[o + 1] << 8)); }
uint32_t u32(const std::string& d, size_t o) { return uint32_t(u16(d, o)) | (uint32_t(u16(d, o + 2)) << 16); }

// ------------------------------------------------------------------ Inflate

constexpr int kMaxBits = 15;

struct OutOfInput {};

struct Huffman {
    short count[kMaxBits + 1];
    short symbol[288];
};

struct Inflater {
    const uint8_t* in;
    size_t inLen;
    size_t inPos = 0;
    uint64_t bitbuf = 0;
    int bitcnt = 0;
    std::string* out;
    size_t maxOut;

    int bits(int need) {
        uint64_t val = bitbuf;
        while (bitcnt < need) {
            if (inPos >= inLen) throw OutOfInput();
            val |= uint64_t(in[inPos++]) << bitcnt;
            bitcnt += 8;
        }
        bitbuf = val >> need;
        bitcnt -= need;
        return int(val & ((uint64_t(1) << need) - 1));
    }

    int stored() {
        bitbuf = 0;   // Rest des angefangenen Bytes verwerfen
        bitcnt = 0;
        if (inPos + 4 > inLen) throw OutOfInput();
        unsigned len = in[inPos] | (in[inPos + 1] << 8);
        unsigned nlen = in[inPos + 2] | (in[inPos + 3] << 8);
        inPos += 4;
        if (len != (~nlen & 0xFFFFu)) return -2;
        if (inPos + len > inLen) throw OutOfInput();
        if (out->size() + len > maxOut) return -12;
        out->append(reinterpret_cast<const char*>(in + inPos), len);
        inPos += len;
        return 0;
    }

    int decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= bits(1);
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -10;   // kein gültiger Code
    }

    static int construct(Huffman& h, const short* length, int n) {
        for (int len = 0; len <= kMaxBits; ++len) h.count[len] = 0;
        for (int s = 0; s < n; ++s) h.count[length[s]]++;
        if (h.count[0] == n) return 0;   // keine Codes – decode() schlägt später fehl
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return left;   // überbelegt
        }
        short offs[kMaxBits + 1];
        offs[1] = 0;
        for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = short(offs[len] + h.count[len]);
        for (int s = 0; s < n; ++s)
            if (length[s] != 0) h.symbol[offs[length[s]]++] = short(s);
        return left;   // > 0: unvollständig
    }

    int codes(const Huffman& lencode, const Huffman& distcode) {
        static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = decode(lencode);
            if (symbol < 0) return symbol;
            if (symbol < 256) {
                if (out->size() >= maxOut) return -12;
                out->push_back(char(symbol));
                continue;
            }
            if (symbol == 256) return 0;
            symbol -= 257;
            if (symbol >= 29) return -10;
            size_t len = size_t(lbase[symbol]) + size_t(bits(lext[symbol]));
            symbol = decode(distcode);
            if (symbol < 0) return symbol;
            if (symbol >= 30) return -10;
            size_t dist = size_t(dbase[symbol]) + size_t(bits(dext[symbol]));
            if (dist > out->size()) return -11;   // Abstand zu weit zurück
            if (out->size() + len > maxOut) return -12;
            size_t from = out->size() - dist;
            for (size_t i = 0; i < len; ++i) out->push_back((*out)[from + i]);   // darf sich überlappen
        }
    }

    struct FixedCodes {
        Huffman lencode, distcode;
        FixedCodes() {
            short lengths[288];
            int s = 0;
            for (; s < 144; ++s) lengths[s] = 8;
            for (; s < 256; ++s) lengths[s] = 9;
            for (; s < 280; ++s) lengths[s] = 7;
            for (; s < 288; ++s) lengths[s] = 8;
            construct(lencode, lengths, 288);
            for (s = 0; s < 30; ++s) lengths[s] = 5;
            construct(distcode, lengths, 30);
        }
    };

    int fixed() {
        static const FixedCodes fc;   // einmalig, thread-sicher (C++11)
        return codes(fc.lencode, fc.distcode);
    }

    int dynamic() {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        short lengths[320];
        int nlen = bits(5) + 257;
        int ndist = bits(5) + 1;
        int ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) return -3;
        int index = 0;
        for (; index < ncode; ++index) lengths[order[index]] = short(bits(3));
        for (; index < 19; ++index) lengths[order[index]] = 0;
        Huffman lencode, distcode;
        if (construct(lencode, lengths, 19) != 0) return -4;   // muss vollständig sein
        index = 0;
        while (index < nlen + ndist) {
            int symbol = decode(lencode);
            if (symbol < 0) return symbol;
            if (symbol < 16) {
                lengths[index++] = short(symbol);
                continue;
            }
            short len = 0;
            if (symbol == 16) {
                if (index == 0) return -5;
                len = lengths[index - 1];
                symbol = 3 + bits(2);
            } else if (symbol == 17) symbol = 3 + bits(3);
            else symbol = 11 + bits(7);
            if (index + symbol > nlen + ndist) return -6;
            while (symbol--) lengths[index++] = len;
        }
        if (lengths[256] == 0) return -9;   // ohne Endcode geht es nicht
        int err = construct(lencode, lengths, nlen);
        if (err && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) return -7;
        err = construct(distcode, lengths + nlen, ndist);
        if (err && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) return -8;
        return codes(lencode, distcode);
    }

    int run() {
        int last;
        do {
            last = bits(1);
            int type = bits(2);
            int err = type == 0 ? stored() : type == 1 ? fixed() : type == 2 ? dynamic() : -1;
            if (err != 0) return err;
        } while (!last);
        return 0;
    }
};

struct CrcTable {
    uint32_t t[256];
    CrcTable() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
    }
};

}  // namespace

uint32_t crc32(const void* data, size_t len, uint32_t crc) {
    static const CrcTable table;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    crc = ~crc;
    while (len--) crc = table.t[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

bool inflateRaw(const uint8_t* src, size_t len, std::string& out, size_t maxOut, std::string& err) {
    out.clear();
    out.reserve(maxOut < (64u << 20) ? maxOut : 0);
    Inflater f;
    f.in = src;
    f.inLen = len;
    f.out = &out;
    f.maxOut = maxOut;
    int rc;
    try {
        rc = f.run();
    } catch (const OutOfInput&) {
        rc = -100;
    }
    if (rc != 0) {
        err = rc == -100 ? "Daten unvollständig" : rc == -12 ? "Daten größer als angegeben" : "Daten beschädigt (Inflate " + std::to_string(rc) + ")";
        return false;
    }
    return true;
}

bool safeName(const std::string& name) {
    if (name.empty() || name.size() > 400) return false;
    if (name[0] == '/' || name[0] == '\\') return false;
    if (name.size() >= 2 && name[1] == ':') return false;
    size_t start = 0;
    for (size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '/' || name[i] == '\\') {
            std::string part = name.substr(start, i - start);
            if (part == "..") return false;
            start = i + 1;
        } else if ((unsigned char)name[i] < 0x20 || std::strchr("<>:\"|?*", name[i])) {
            return false;
        }
    }
    return true;
}

bool list(const std::string& d, std::vector<Entry>& out, std::string& err) {
    out.clear();
    if (d.size() < 22) {
        err = "keine ZIP-Datei";
        return false;
    }
    // Ende des Inhaltsverzeichnisses suchen (Kommentar höchstens 64 KB)
    size_t eocd = std::string::npos;
    size_t lowest = d.size() > 22 + 65535 ? d.size() - 22 - 65535 : 0;
    for (size_t i = d.size() - 22 + 1; i-- > lowest;) {
        if (u32(d, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        err = "keine ZIP-Datei (Verzeichnisende fehlt)";
        return false;
    }
    uint16_t count = u16(d, eocd + 10);
    uint32_t cdSize = u32(d, eocd + 12), cdOff = u32(d, eocd + 16);
    if (cdOff == 0xFFFFFFFFu || count == 0xFFFF) {
        err = "ZIP64 wird nicht unterstützt";
        return false;
    }
    if (uint64_t(cdOff) + cdSize > eocd) {
        err = "ZIP-Verzeichnis beschädigt";
        return false;
    }
    size_t p = cdOff;
    for (uint16_t i = 0; i < count; ++i) {
        if (p + 46 > d.size() || u32(d, p) != 0x02014b50) {
            err = "ZIP-Verzeichnis beschädigt";
            return false;
        }
        Entry e;
        e.flags = u16(d, p + 8);
        e.method = u16(d, p + 10);
        e.crc = u32(d, p + 16);
        e.compSize = u32(d, p + 20);
        e.size = u32(d, p + 24);
        uint16_t n = u16(d, p + 28), m = u16(d, p + 30), k = u16(d, p + 32);
        e.localOffset = u32(d, p + 42);
        if (p + 46 + n > d.size()) {
            err = "ZIP-Verzeichnis beschädigt";
            return false;
        }
        e.name = d.substr(p + 46, n);
        for (char& c : e.name)
            if (c == '\\') c = '/';
        e.isDir = !e.name.empty() && e.name.back() == '/';
        if (e.compSize == 0xFFFFFFFFu || e.size == 0xFFFFFFFFu || e.localOffset == 0xFFFFFFFFu) {
            err = "ZIP64 wird nicht unterstützt";
            return false;
        }
        out.push_back(std::move(e));
        p += 46 + size_t(n) + m + k;
    }
    return true;
}

bool read(const std::string& d, const Entry& e, std::string& out, std::string& err) {
    out.clear();
    if (e.flags & 1) {
        err = e.name + ": verschlüsselt";
        return false;
    }
    size_t lo = e.localOffset;
    if (lo + 30 > d.size() || u32(d, lo) != 0x04034b50) {
        err = e.name + ": Kopfdaten fehlen";
        return false;
    }
    size_t start = lo + 30 + u16(d, lo + 26) + u16(d, lo + 28);
    if (start + e.compSize > d.size()) {
        err = e.name + ": Datei abgeschnitten";
        return false;
    }
    const uint8_t* src = reinterpret_cast<const uint8_t*>(d.data()) + start;
    if (e.method == 0) {
        if (e.compSize != e.size) {
            err = e.name + ": Größen passen nicht";
            return false;
        }
        out.assign(reinterpret_cast<const char*>(src), e.size);
    } else if (e.method == 8) {
        std::string why;
        if (!inflateRaw(src, e.compSize, out, e.size, why)) {
            err = e.name + ": " + why;
            return false;
        }
        if (out.size() != e.size) {
            err = e.name + ": Größe stimmt nicht";
            return false;
        }
    } else {
        err = e.name + ": Kompression " + std::to_string(e.method) + " wird nicht unterstützt";
        return false;
    }
    if (crc32(out.data(), out.size()) != e.crc) {
        err = e.name + ": Prüfsumme (CRC) falsch";
        return false;
    }
    return true;
}

}  // namespace zip
