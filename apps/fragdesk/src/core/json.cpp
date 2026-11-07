// efficient lamp – kleiner JSON-Leser (siehe json.h)
#include "json.h"

#include <cstdlib>
#include <cstring>

namespace json {

namespace {

const Value kNull;

struct Parser {
    const char* p;
    const char* end;
    const char* begin;
    std::string err;
    int depth = 0;

    bool fail(const char* what) {
        if (err.empty()) err = std::string(what) + " (Zeichen " + std::to_string(p - begin) + ")";
        return false;
    }
    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool lit(const char* s) {
        size_t n = std::strlen(s);
        if (size_t(end - p) < n || std::memcmp(p, s, n) != 0) return false;
        p += n;
        return true;
    }
    static void putUtf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) {
            o += char(0xC0 | (cp >> 6));
            o += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            o += char(0xE0 | (cp >> 12));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        } else {
            o += char(0xF0 | (cp >> 18));
            o += char(0x80 | ((cp >> 12) & 0x3F));
            o += char(0x80 | ((cp >> 6) & 0x3F));
            o += char(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(uint32_t& v) {
        if (end - p < 4) return fail("\\u ohne 4 Hex-Ziffern");
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else return fail("ungültige Hex-Ziffer");
        }
        return true;
    }
    bool string(std::string& o) {
        ++p;   // "
        o.clear();
        while (p < end) {
            char c = *p++;
            if (c == '"') return true;
            if ((unsigned char)c < 0x20) return fail("Steuerzeichen in Zeichenkette");
            if (c != '\\') {
                o += c;
                continue;
            }
            if (p >= end) break;
            char e = *p++;
            switch (e) {
                case '"': o += '"'; break;
                case '\\': o += '\\'; break;
                case '/': o += '/'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00) {   // Ersatzpaar
                        uint32_t lo = 0;
                        if (end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            if (!hex4(lo)) return false;
                            if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else cp = 0xFFFD;
                        } else cp = 0xFFFD;
                    } else if (cp >= 0xDC00 && cp < 0xE000) cp = 0xFFFD;
                    putUtf8(o, cp);
                    break;
                }
                default: return fail("unbekanntes Escape");
            }
        }
        return fail("Zeichenkette nicht beendet");
    }
    bool number(Value& v) {
        const char* s = p;
        if (p < end && *p == '-') ++p;
        if (p >= end || !(*p >= '0' && *p <= '9')) return fail("Zahl erwartet");
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) ++p;
        std::string t(s, p);
        char* e = nullptr;
        v.type = Value::Number;
        v.num = std::strtod(t.c_str(), &e);
        if (!e || *e) return fail("ungültige Zahl");
        return true;
    }
    bool value(Value& v) {
        ws();
        if (p >= end) return fail("Wert erwartet");
        if (++depth > 64) return fail("zu tief verschachtelt");
        bool ok = true;
        switch (*p) {
            case '{': ok = object(v); break;
            case '[': ok = array(v); break;
            case '"': v.type = Value::String; ok = string(v.str); break;
            case 't': ok = lit("true") ? (v.type = Value::Bool, v.b = true, true) : fail("true erwartet"); break;
            case 'f': ok = lit("false") ? (v.type = Value::Bool, v.b = false, true) : fail("false erwartet"); break;
            case 'n': ok = lit("null") ? (v.type = Value::Null, true) : fail("null erwartet"); break;
            default: ok = number(v); break;
        }
        --depth;
        return ok;
    }
    bool array(Value& v) {
        v.type = Value::Array;
        ++p;
        ws();
        if (p < end && *p == ']') {
            ++p;
            return true;
        }
        for (;;) {
            v.arr.emplace_back();
            if (!value(v.arr.back())) return false;
            ws();
            if (p < end && *p == ',') {
                ++p;
                continue;
            }
            if (p < end && *p == ']') {
                ++p;
                return true;
            }
            return fail("',' oder ']' erwartet");
        }
    }
    bool object(Value& v) {
        v.type = Value::Object;
        ++p;
        ws();
        if (p < end && *p == '}') {
            ++p;
            return true;
        }
        for (;;) {
            ws();
            if (p >= end || *p != '"') return fail("Schlüssel erwartet");
            std::string key;
            if (!string(key)) return false;
            ws();
            if (p >= end || *p != ':') return fail("':' erwartet");
            ++p;
            v.obj.emplace_back(std::move(key), Value());
            if (!value(v.obj.back().second)) return false;
            ws();
            if (p < end && *p == ',') {
                ++p;
                continue;
            }
            if (p < end && *p == '}') {
                ++p;
                return true;
            }
            return fail("',' oder '}' erwartet");
        }
    }
};

}  // namespace

const Value* Value::find(const std::string& key) const {
    if (type != Object) return nullptr;
    for (size_t i = obj.size(); i-- > 0;)   // doppelte Schlüssel: der letzte gilt (wie bei den meisten JSON-Lesern)
        if (obj[i].first == key) return &obj[i].second;
    return nullptr;
}

std::string Value::getString(const std::string& key, const std::string& def) const {
    const Value* v = find(key);
    return v && v->type == String ? v->str : def;
}

double Value::getNumber(const std::string& key, double def) const {
    const Value* v = find(key);
    return v && v->type == Number ? v->num : def;
}

bool Value::getBool(const std::string& key, bool def) const {
    const Value* v = find(key);
    return v && v->type == Bool ? v->b : def;
}

const Value& Value::operator[](const std::string& key) const {
    const Value* v = find(key);
    return v ? *v : kNull;
}

bool parse(const std::string& text, Value& out, std::string* error) {
    Parser ps;
    ps.begin = text.data();
    ps.p = text.data();
    ps.end = text.data() + text.size();
    if (ps.end - ps.p >= 3 && (unsigned char)ps.p[0] == 0xEF && (unsigned char)ps.p[1] == 0xBB && (unsigned char)ps.p[2] == 0xBF) ps.p += 3;
    out = Value();
    bool ok = ps.value(out);
    if (ok) {
        ps.ws();
        if (ps.p != ps.end) ok = ps.fail("Text nach dem Ende");
    }
    if (!ok && error) *error = ps.err;
    return ok;
}

}  // namespace json
