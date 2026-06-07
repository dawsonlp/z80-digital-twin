//
// Z80 Digital Twin Debugger - SymbolTable implementation
// Copyright (c) 2025-2026 Larry Dawson
// Licensed under the MIT License (see LICENSE file)
//
// Includes a compact, dependency-free recursive-descent JSON reader/writer
// scoped to the .sym schema, keeping the debugger core self-contained.
//

#include "symbol_table.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace z80::dbg {
namespace {

// ===========================================================================
// Minimal JSON value + recursive-descent parser
// ===========================================================================

struct JValue {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string str;
    std::vector<JValue> array;
    std::vector<std::pair<std::string, JValue>> object;

    [[nodiscard]] const JValue* find(std::string_view key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& [k, v] : object) {
            if (k == key) return &v;
        }
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : s_(text) {}

    JValue parse() {
        skip_ws();
        JValue v = parse_value();
        skip_ws();
        if (i_ != s_.size()) fail("trailing characters after JSON value");
        return v;
    }

private:
    std::string_view s_;
    std::size_t i_ = 0;

    [[noreturn]] void fail(const char* msg) const {
        std::ostringstream os;
        os << "JSON parse error at offset " << i_ << ": " << msg;
        throw std::runtime_error(os.str());
    }

    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
    char get() { return i_ < s_.size() ? s_[i_++] : '\0'; }

    void skip_ws() {
        while (i_ < s_.size()) {
            const char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i_;
            else break;
        }
    }

    bool consume(char c) {
        if (peek() == c) { ++i_; return true; }
        return false;
    }

    JValue parse_value() {
        skip_ws();
        switch (peek()) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': { JValue v; v.type = JValue::Type::String; v.str = parse_string(); return v; }
            case 't': case 'f': return parse_bool();
            case 'n': return parse_null();
            default:  return parse_number();
        }
    }

    JValue parse_object() {
        JValue v; v.type = JValue::Type::Object;
        get();  // '{'
        skip_ws();
        if (consume('}')) return v;
        for (;;) {
            skip_ws();
            if (peek() != '"') fail("expected string key");
            std::string key = parse_string();
            skip_ws();
            if (!consume(':')) fail("expected ':'");
            JValue val = parse_value();
            v.object.emplace_back(std::move(key), std::move(val));
            skip_ws();
            if (consume(',')) continue;
            if (consume('}')) break;
            fail("expected ',' or '}'");
        }
        return v;
    }

    JValue parse_array() {
        JValue v; v.type = JValue::Type::Array;
        get();  // '['
        skip_ws();
        if (consume(']')) return v;
        for (;;) {
            v.array.push_back(parse_value());
            skip_ws();
            if (consume(',')) continue;
            if (consume(']')) break;
            fail("expected ',' or ']'");
        }
        return v;
    }

    std::string parse_string() {
        if (!consume('"')) fail("expected '\"'");
        std::string out;
        for (;;) {
            if (i_ >= s_.size()) fail("unterminated string");
            char c = get();
            if (c == '"') break;
            if (c == '\\') {
                char e = get();
                switch (e) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        // Parse \uXXXX; emit UTF-8 for the basic plane.
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = get();
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                            else fail("invalid \\u escape");
                        }
                        if (cp < 0x80) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: fail("invalid escape character");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    JValue parse_number() {
        const std::size_t start = i_;
        if (peek() == '-' || peek() == '+') ++i_;
        bool any = false;
        while (std::isdigit(static_cast<unsigned char>(peek()))) { ++i_; any = true; }
        if (consume('.')) {
            while (std::isdigit(static_cast<unsigned char>(peek()))) { ++i_; any = true; }
        }
        if (peek() == 'e' || peek() == 'E') {
            ++i_;
            if (peek() == '-' || peek() == '+') ++i_;
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++i_;
        }
        if (!any) fail("invalid number");
        JValue v; v.type = JValue::Type::Number;
        v.number = std::stod(std::string(s_.substr(start, i_ - start)));
        return v;
    }

    JValue parse_bool() {
        JValue v; v.type = JValue::Type::Bool;
        if (s_.substr(i_, 4) == "true")  { i_ += 4; v.boolean = true;  return v; }
        if (s_.substr(i_, 5) == "false") { i_ += 5; v.boolean = false; return v; }
        fail("invalid literal");
    }

    JValue parse_null() {
        if (s_.substr(i_, 4) == "null") { i_ += 4; return JValue{}; }
        fail("invalid literal");
    }
};

// ===========================================================================
// Schema-level helpers
// ===========================================================================

// Parse an address from a JSON value: hex string ("0x1234"), decimal string,
// or a number. Returns nullopt on failure / out of 16-bit range.
std::optional<uint16_t> parse_address(const JValue& v) {
    long value = -1;
    if (v.type == JValue::Type::Number) {
        value = static_cast<long>(v.number);
    } else if (v.type == JValue::Type::String) {
        try {
            const std::string& t = v.str;
            const bool hex = t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X');
            value = std::stol(t, nullptr, hex ? 16 : 10);
        } catch (...) {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    if (value < 0 || value > 0xFFFF) return std::nullopt;
    return static_cast<uint16_t>(value);
}

std::optional<uint16_t> parse_size(const JValue& v) {
    if (v.type == JValue::Type::Number) {
        const long n = static_cast<long>(v.number);
        if (n < 1 || n > 0x10000) return std::nullopt;
        return static_cast<uint16_t>(n > 0xFFFF ? 0xFFFF : n);
    }
    if (v.type == JValue::Type::String) {
        if (auto a = parse_address(v)) return *a == 0 ? std::optional<uint16_t>(1) : a;
    }
    return std::nullopt;
}

std::string json_escape(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 2);
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

} // namespace

// ===========================================================================
// SymbolType <-> string
// ===========================================================================

std::string ToString(SymbolType type) {
    switch (type) {
        case SymbolType::Function:     return "FUNCTION";
        case SymbolType::JumpTarget:   return "JUMP_TARGET";
        case SymbolType::Variable:     return "VARIABLE";
        case SymbolType::DataRegion:   return "DATA_REGION";
        case SymbolType::ByteVariable: return "BYTE_VARIABLE";
        case SymbolType::WordVariable: return "WORD_VARIABLE";
        case SymbolType::Label:        return "LABEL";
    }
    return "LABEL";
}

std::optional<SymbolType> SymbolTypeFromString(std::string_view text) {
    if (text == "FUNCTION")      return SymbolType::Function;
    if (text == "JUMP_TARGET")   return SymbolType::JumpTarget;
    if (text == "VARIABLE")      return SymbolType::Variable;
    if (text == "DATA_REGION")   return SymbolType::DataRegion;
    if (text == "BYTE_VARIABLE") return SymbolType::ByteVariable;
    if (text == "WORD_VARIABLE") return SymbolType::WordVariable;
    if (text == "LABEL")         return SymbolType::Label;
    return std::nullopt;
}

// ===========================================================================
// SymbolTable
// ===========================================================================

void SymbolTable::Define(const Symbol& sym) {
    // Drop any previous symbol at this address (and its old name index entry).
    if (auto it = by_address_.find(sym.address); it != by_address_.end()) {
        by_name_.erase(it->second.name);
    }
    Symbol s = sym;
    if (s.size == 0) s.size = 1;
    by_name_[s.name] = s.address;
    by_address_[s.address] = std::move(s);
}

void SymbolTable::DefineLabel(uint16_t address, std::string name,
                              SymbolType type, std::string description) {
    Define(Symbol{address, std::move(name), type, std::move(description), 1});
}

void SymbolTable::Remove(uint16_t address) {
    if (auto it = by_address_.find(address); it != by_address_.end()) {
        by_name_.erase(it->second.name);
        by_address_.erase(it);
    }
}

void SymbolTable::Clear() noexcept {
    by_address_.clear();
    by_name_.clear();
}

std::optional<Symbol> SymbolTable::Lookup(uint16_t address) const {
    if (auto it = by_address_.find(address); it != by_address_.end()) return it->second;
    return std::nullopt;
}

std::optional<Symbol> SymbolTable::FindContaining(uint16_t address) const {
    if (auto it = by_address_.find(address); it != by_address_.end()) return it->second;
    // Largest symbol with address <= the query; check it covers `address`.
    auto it = by_address_.upper_bound(address);
    if (it == by_address_.begin()) return std::nullopt;
    --it;
    const Symbol& s = it->second;
    const uint32_t end = static_cast<uint32_t>(s.address) + (s.size ? s.size : 1);
    return address < end ? std::optional<Symbol>(s) : std::nullopt;
}

std::optional<std::string> SymbolTable::ResolveName(uint16_t address) const {
    if (auto it = by_address_.find(address); it != by_address_.end()) return it->second.name;
    return std::nullopt;
}

std::optional<uint16_t> SymbolTable::Resolve(std::string_view name) const {
    if (auto it = by_name_.find(std::string(name)); it != by_name_.end()) return it->second;
    return std::nullopt;
}

std::vector<Symbol> SymbolTable::List() const {
    std::vector<Symbol> out;
    out.reserve(by_address_.size());
    for (const auto& [addr, sym] : by_address_) out.push_back(sym);  // map is ordered
    return out;
}

SymbolResolver SymbolTable::MakeResolver() const {
    return [this](uint16_t address) { return ResolveName(address); };
}

bool SymbolTable::LoadFromFile(const std::string& path, std::string* program,
                               std::vector<std::string>* warnings) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    JValue root;
    try {
        root = JsonParser(text).parse();
    } catch (const std::exception& e) {
        if (warnings) warnings->emplace_back(e.what());
        return false;
    }

    if (root.type != JValue::Type::Object) {
        if (warnings) warnings->emplace_back("top-level JSON is not an object");
        return false;
    }
    if (program) {
        if (const JValue* p = root.find("program"); p && p->type == JValue::Type::String) {
            *program = p->str;
        }
    }

    const JValue* syms = root.find("symbols");
    if (!syms || syms->type != JValue::Type::Array) {
        if (warnings) warnings->emplace_back("missing or invalid \"symbols\" array");
        return true;  // a valid object with no symbols is allowed
    }

    std::size_t index = 0;
    for (const JValue& entry : syms->array) {
        const std::size_t at = index++;
        auto warn = [&](const std::string& msg) {
            if (warnings) warnings->emplace_back("symbols[" + std::to_string(at) + "]: " + msg);
        };
        if (entry.type != JValue::Type::Object) { warn("not an object"); continue; }

        const JValue* addr_v = entry.find("address");
        const JValue* name_v = entry.find("name");
        if (!addr_v) { warn("missing \"address\""); continue; }
        if (!name_v || name_v->type != JValue::Type::String || name_v->str.empty()) {
            warn("missing or empty \"name\""); continue;
        }
        auto addr = parse_address(*addr_v);
        if (!addr) { warn("invalid \"address\""); continue; }

        Symbol sym;
        sym.address = *addr;
        sym.name = name_v->str;
        sym.type = SymbolType::Label;
        if (const JValue* t = entry.find("type"); t && t->type == JValue::Type::String) {
            if (auto parsed = SymbolTypeFromString(t->str)) sym.type = *parsed;
            else warn("unknown \"type\" \"" + t->str + "\", defaulting to LABEL");
        }
        if (const JValue* d = entry.find("description"); d && d->type == JValue::Type::String) {
            sym.description = d->str;
        }
        sym.size = 1;
        if (const JValue* sz = entry.find("size")) {
            if (auto s = parse_size(*sz)) sym.size = *s;
            else warn("invalid \"size\", defaulting to 1");
        }
        Define(sym);
    }
    return true;
}

bool SymbolTable::SaveToFile(const std::string& path,
                             const std::string& program) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    out << "{\n";
    out << "  \"version\": \"1.0\",\n";
    out << "  \"program\": \"" << json_escape(program) << "\",\n";
    out << "  \"symbols\": [";

    bool first = true;
    for (const auto& [addr, sym] : by_address_) {
        out << (first ? "\n" : ",\n");
        first = false;
        char addr_buf[8];
        std::snprintf(addr_buf, sizeof(addr_buf), "0x%04X", sym.address);
        out << "    {\n";
        out << "      \"address\": \"" << addr_buf << "\",\n";
        out << "      \"name\": \"" << json_escape(sym.name) << "\",\n";
        out << "      \"type\": \"" << ToString(sym.type) << "\"";
        if (sym.size > 1) out << ",\n      \"size\": " << sym.size;
        if (!sym.description.empty())
            out << ",\n      \"description\": \"" << json_escape(sym.description) << "\"";
        out << "\n    }";
    }
    out << (first ? "" : "\n  ") << "]\n";
    out << "}\n";
    return static_cast<bool>(out);
}

} // namespace z80::dbg
