// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
#include "analysis_json.h"
#include <charconv>
#include <format>
#include <stdexcept>

namespace z80::dbg::json {
namespace {
[[noreturn]] void invalid(std::string_view message) { throw std::invalid_argument(std::string(message)); }
void utf8(std::string &out, uint32_t cp) {
    if (cp < 0x80)
        out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    } else {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 63));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    }
}
void validate_utf8(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i++]);
        if (c < 128)
            continue;
        int n;
        uint32_t cp, minimum;
        if (c >= 0xc2 && c <= 0xdf) {
            n = 1;
            cp = c & 31;
            minimum = 0x80;
        } else if (c >= 0xe0 && c <= 0xef) {
            n = 2;
            cp = c & 15;
            minimum = 0x800;
        } else if (c >= 0xf0 && c <= 0xf4) {
            n = 3;
            cp = c & 7;
            minimum = 0x10000;
        } else
            invalid("invalid UTF-8");
        while (n--) {
            if (i == text.size())
                invalid("truncated UTF-8");
            auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80)
                invalid("invalid UTF-8 continuation");
            cp = (cp << 6) | (next & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            invalid("invalid UTF-8 scalar");
    }
}
class Parser {
    std::string_view text_;
    size_t pos_ = 0;
    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
    char take() {
        if (pos_ == text_.size())
            invalid("unexpected end of JSON");
        return text_[pos_++];
    }
    void ws() {
        while (peek() == ' ' || peek() == '\n' || peek() == '\r' || peek() == '\t')
            ++pos_;
    }
    bool eat(char c) {
        if (peek() != c)
            return false;
        ++pos_;
        return true;
    }
    uint32_t hex() {
        uint32_t cp = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = take();
            int digit = c >= '0' && c <= '9'   ? c - '0'
                        : c >= 'a' && c <= 'f' ? c - 'a' + 10
                        : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                               : -1;
            if (digit < 0)
                invalid("invalid Unicode escape");
            cp = (cp << 4) | static_cast<uint32_t>(digit);
        }
        return cp;
    }
    std::string string() {
        if (!eat('"'))
            invalid("expected string");
        std::string out;
        for (;;) {
            const char c = take();
            if (c == '"')
                return out;
            if (static_cast<unsigned char>(c) < 0x20)
                invalid("unescaped control character");
            if (c != '\\') {
                out += c;
                continue;
            }
            switch (take()) {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                auto cp = hex();
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if (take() != '\\' || take() != 'u')
                        invalid("missing Unicode low surrogate");
                    const auto low = hex();
                    if (low < 0xdc00 || low > 0xdfff)
                        invalid("invalid Unicode low surrogate");
                    cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
                } else if (cp >= 0xdc00 && cp <= 0xdfff)
                    invalid("unpaired Unicode surrogate");
                utf8(out, cp);
                break;
            }
            default:
                invalid("invalid JSON escape");
            }
        }
    }
    Value value(int depth) {
        if (depth > 64)
            invalid("JSON nesting exceeds 64 levels");
        ws();
        if (peek() == '"')
            return string();
        if (eat('{')) {
            Value::Object out;
            ws();
            if (eat('}'))
                return out;
            for (;;) {
                ws();
                auto key = string();
                ws();
                if (!eat(':'))
                    invalid("expected colon");
                if (!out.emplace(std::move(key), value(depth + 1)).second)
                    invalid("duplicate JSON key");
                ws();
                if (eat('}'))
                    return out;
                if (!eat(','))
                    invalid("expected comma");
            }
        }
        if (eat('[')) {
            Value::Array out;
            ws();
            if (eat(']'))
                return out;
            for (;;) {
                out.push_back(value(depth + 1));
                ws();
                if (eat(']'))
                    return out;
                if (!eat(','))
                    invalid("expected comma");
            }
        }
        for (auto literal : {std::string_view("null"), std::string_view("true"), std::string_view("false")})
            if (text_.substr(pos_, literal.size()) == literal) {
                pos_ += literal.size();
                if (literal == "null")
                    return {};
                return literal == "true";
            }
        const auto start = pos_;
        eat('-');
        if (!eat('0')) {
            if (peek() < '1' || peek() > '9')
                invalid("expected JSON integer");
            while (peek() >= '0' && peek() <= '9')
                ++pos_;
        }
        int64_t number = 0;
        auto [end, error] = std::from_chars(text_.data() + start, text_.data() + pos_, number);
        if (error != std::errc{} || end != text_.data() + pos_)
            invalid("integer out of range");
        return number;
    }

  public:
    explicit Parser(std::string_view text) : text_(text) {}
    Value parse() {
        auto out = value(0);
        ws();
        if (pos_ != text_.size())
            invalid("trailing or unsupported JSON content");
        return out;
    }
};
std::string quote(std::string_view text) {
    validate_utf8(text);
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 32)
            out += std::format("\\u{:04x}", c);
        else
            out += static_cast<char>(c);
    }
    return out + '"';
}
std::string write(const Value &value, int depth) {
    const auto indent = [](int n) { return std::string(static_cast<size_t>(n) * 2, ' '); };
    return std::visit(
        [&](const auto &x) -> std::string {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>)
                return "null";
            else if constexpr (std::is_same_v<T, bool>)
                return x ? "true" : "false";
            else if constexpr (std::is_same_v<T, int64_t>)
                return std::to_string(x);
            else if constexpr (std::is_same_v<T, std::string>)
                return quote(x);
            else {
                constexpr bool object = std::is_same_v<T, Value::Object>;
                std::string out = object ? "{" : "[";
                bool first = true;
                for (const auto &item : x) {
                    out += first ? "\n" : ",\n";
                    first = false;
                    out += indent(depth + 1);
                    if constexpr (object)
                        out += quote(item.first) + ": " + write(item.second, depth + 1);
                    else
                        out += write(item, depth + 1);
                }
                if (!x.empty())
                    out += '\n' + indent(depth);
                return out + (object ? '}' : ']');
            }
        },
        value.data);
}
} // namespace
const Value::Object &Value::object() const {
    if (auto p = std::get_if<Object>(&data))
        return *p;
    invalid("expected object");
}
const Value::Array &Value::array() const {
    if (auto p = std::get_if<Array>(&data))
        return *p;
    invalid("expected array");
}
const std::string &Value::string() const {
    if (auto p = std::get_if<std::string>(&data))
        return *p;
    invalid("expected string");
}
int64_t Value::integer() const {
    if (auto p = std::get_if<int64_t>(&data))
        return *p;
    invalid("expected integer");
}
bool Value::boolean() const {
    if (auto p = std::get_if<bool>(&data))
        return *p;
    invalid("expected boolean");
}
const Value &Value::at(std::string_view key) const {
    const auto &o = object();
    auto it = o.find(std::string(key));
    if (it == o.end())
        invalid("missing field: " + std::string(key));
    return it->second;
}
void Keys(const Value &value, std::initializer_list<std::string_view> keys) {
    const auto &obj = value.object();
    if (obj.size() != keys.size())
        invalid("unknown or missing schema field");
    for (auto key : keys)
        (void)value.at(key);
}
Value Parse(std::string_view text) {
    if (text.size() > 16 * 1024 * 1024)
        invalid("analysis JSON exceeds 16 MiB");
    validate_utf8(text);
    return Parser(text).parse();
}
std::string Write(const Value &value) { return write(value, 0) + '\n'; }
} // namespace z80::dbg::json
