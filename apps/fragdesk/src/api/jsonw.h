// FragDesk – JSON schreiben (für die Handy-App). Zahlen ohne Gebietsschema, NaN/unendlich werden null.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace fdapi {

class JsonW {
public:
    JsonW& obj() { return open('{'); }
    JsonW& arr() { return open('['); }
    JsonW& end() {
        out_ += stack_.back() ? '}' : ']';
        stack_.pop_back();
        first_.pop_back();
        return *this;
    }
    JsonW& key(const char* k) {
        comma();
        quote(k);
        out_ += ':';
        afterKey_ = true;
        return *this;
    }
    JsonW& str(const std::string& s) {
        comma();
        quote(s);
        return *this;
    }
    JsonW& num(double v) {
        comma();
        if (!std::isfinite(v)) out_ += "null";
        else if (v == std::floor(v) && std::fabs(v) < 1e15) out_ += std::to_string(int64_t(v));
        else {
            char b[32];
            snprintf(b, sizeof b, "%.6g", v);
            for (char* p = b; *p; ++p)
                if (*p == ',') *p = '.';   // falls ein Gebietsschema gesetzt ist
            out_ += b;
        }
        return *this;
    }
    JsonW& integer(int64_t v) {
        comma();
        out_ += std::to_string(v);
        return *this;
    }
    JsonW& boolean(bool b) {
        comma();
        out_ += b ? "true" : "false";
        return *this;
    }
    JsonW& null() {
        comma();
        out_ += "null";
        return *this;
    }
    // Kurzformen: Schlüssel und Wert
    JsonW& kv(const char* k, const std::string& v) { return key(k).str(v); }
    JsonW& kv(const char* k, const char* v) { return key(k).str(v ? v : ""); }
    JsonW& kn(const char* k, double v) { return key(k).num(v); }
    JsonW& ki(const char* k, int64_t v) { return key(k).integer(v); }
    JsonW& kb(const char* k, bool v) { return key(k).boolean(v); }
    // eingebettetes, fertiges JSON
    JsonW& raw(const std::string& json) {
        comma();
        out_ += json;
        return *this;
    }

    const std::string& str() const { return out_; }
    std::string take() { return std::move(out_); }

private:
    std::string out_;
    std::vector<bool> stack_;   // true = Objekt
    std::vector<bool> first_;
    bool afterKey_ = false;

    JsonW& open(char ch) {
        comma();
        out_ += ch;
        stack_.push_back(ch == '{');
        first_.push_back(true);
        return *this;
    }
    void comma() {
        if (afterKey_) {
            afterKey_ = false;
            return;
        }
        if (!first_.empty()) {
            if (!first_.back()) out_ += ',';
            first_.back() = false;
        }
    }
    void quote(const std::string& s) {
        out_ += '"';
        for (unsigned char ch : s) {
            switch (ch) {
                case '"': out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\n': out_ += "\\n"; break;
                case '\r': out_ += "\\r"; break;
                case '\t': out_ += "\\t"; break;
                default:
                    if (ch < 0x20) {
                        char b[8];
                        snprintf(b, sizeof b, "\\u%04x", ch);
                        out_ += b;
                    } else out_ += char(ch);
            }
        }
        out_ += '"';
    }
};

}  // namespace fdapi
