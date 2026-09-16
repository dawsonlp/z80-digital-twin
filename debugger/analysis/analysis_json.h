// Copyright (c) 2026 Larry Dawson. Licensed under the MIT License (see LICENSE).
// Strict, bounded JSON for the analysis schema. No network/build dependency.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace z80::dbg::json {
struct Value {
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;
    std::variant<std::nullptr_t, bool, int64_t, std::string, Array, Object> data = nullptr;
    Value() = default;
    Value(std::nullptr_t) : data(nullptr) {}
    Value(bool x) : data(x) {}
    Value(int64_t x) : data(x) {}
    Value(int x) : data(int64_t{x}) {}
    Value(uint32_t x) : data(int64_t{x}) {}
    Value(std::string x) : data(std::move(x)) {}
    Value(const char *x) : data(std::string(x)) {}
    Value(Array x) : data(std::move(x)) {}
    Value(Object x) : data(std::move(x)) {}
    bool operator==(const Value &) const = default;
    const Value &at(std::string_view key) const;
    const Object &object() const;
    const Array &array() const;
    const std::string &string() const;
    int64_t integer() const;
    bool boolean() const;
    bool null() const { return std::holds_alternative<std::nullptr_t>(data); }
};
// Throws invalid_argument on malformed/unsupported JSON; schema uses only integers.
Value Parse(std::string_view text);
std::string Write(const Value &value);
void Keys(const Value &object, std::initializer_list<std::string_view> keys);
} // namespace z80::dbg::json
