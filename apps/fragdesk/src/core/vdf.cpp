// FragDesk – Valve-KeyValues (siehe vdf.h)
#include "vdf.h"

#include <cctype>

namespace fd {

namespace {

bool sameKey(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

struct Lexer {
    const std::string& s;
    size_t i = 0;
    explicit Lexer(const std::string& t) : s(t) {}

    void skip() {
        for (;;) {
            while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
            if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {
                while (i < s.size() && s[i] != '\n') ++i;
                continue;
            }
            return;
        }
    }
    // 0 = Ende, '{', '}', 's' = Zeichenkette
    char next(std::string& out) {
        skip();
        if (i >= s.size()) return 0;
        char c = s[i];
        if (c == '{' || c == '}') {
            ++i;
            return c;
        }
        out.clear();
        if (c == '"') {
            ++i;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    char e = s[i + 1];
                    out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                    i += 2;
                    continue;
                }
                out += s[i++];
            }
            if (i < s.size()) ++i;
            return 's';
        }
        while (i < s.size() && !std::isspace((unsigned char)s[i]) && s[i] != '{' && s[i] != '}' && s[i] != '"') out += s[i++];
        return 's';
    }
};

bool parseBlock(Lexer& lx, VdfNode& node, int depth) {
    if (depth > 32) return false;
    std::string key, value;
    for (;;) {
        char t = lx.next(key);
        if (t == 0) return depth == 0;
        if (t == '}') return depth > 0;
        if (t == '{') return false;
        VdfNode child;
        child.key = key;
        char u = lx.next(value);
        if (u == 's') child.value = value;
        else if (u == '{') {
            child.block = true;
            if (!parseBlock(lx, child, depth + 1)) return false;
        } else return false;
        node.children.push_back(std::move(child));
    }
}

}  // namespace

const VdfNode* VdfNode::find(const std::string& k) const {
    for (const auto& c : children)
        if (sameKey(c.key, k)) return &c;
    return nullptr;
}

std::string VdfNode::get(const std::string& k) const {
    const VdfNode* n = find(k);
    return n && !n->block ? n->value : std::string();
}

bool parseVdf(const std::string& text, VdfNode& root) {
    root = VdfNode();
    root.block = true;
    Lexer lx(text);
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) lx.i = 3;
    return parseBlock(lx, root, 0);
}

std::string setVdfValue(std::string text, const std::string& key, const std::string& value) {
    const std::string quoted = "\"" + key + "\"";
    size_t pos = 0;
    while ((pos = text.find(quoted, pos)) != std::string::npos) {
        size_t i = pos + quoted.size();
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        if (i < text.size() && text[i] == '"') {
            size_t end = text.find('"', i + 1);
            if (end == std::string::npos) break;
            text.replace(i + 1, end - i - 1, value);
            return text;
        }
        pos = i;
    }
    size_t close = text.rfind('}');
    std::string line = "\t" + quoted + "\t\t\"" + value + "\"\n";
    if (close == std::string::npos) return text + line;
    text.insert(close, line);
    return text;
}

}  // namespace fd
