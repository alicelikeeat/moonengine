// JSON values, parser and writer.
//
// Used for level files, ACP messages and tool output. The parser treats its
// input as untrusted: it enforces a nesting limit, rejects invalid UTF-16
// escapes and non-finite numbers, and reports errors with line and column.
// Objects keep member order, so files round-trip without reordering.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "aether/foundation/result.hpp"

namespace aether {

class Json {
public:
    struct Member;
    using Array = std::vector<Json>;
    using Object = std::vector<Member>;

    enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };

    // Constructors are defined below Member, where Object is a complete type.
    Json();
    Json(std::nullptr_t);
    Json(bool b);
    Json(double n);
    Json(int n);
    Json(std::int64_t n);
    Json(std::uint64_t n);
    Json(const char* s);
    Json(std::string s);
    Json(std::string_view s);
    Json(Array a);
    Json(Object o);

    [[nodiscard]] static Json array();
    [[nodiscard]] static Json object();

    [[nodiscard]] Type type() const noexcept { return static_cast<Type>(value_.index()); }
    [[nodiscard]] bool is_null() const noexcept { return type() == Type::Null; }
    [[nodiscard]] bool is_bool() const noexcept { return type() == Type::Bool; }
    [[nodiscard]] bool is_number() const noexcept { return type() == Type::Number; }
    [[nodiscard]] bool is_string() const noexcept { return type() == Type::String; }
    [[nodiscard]] bool is_array() const noexcept { return type() == Type::Array; }
    [[nodiscard]] bool is_object() const noexcept { return type() == Type::Object; }

    // Accessors assert on the wrong type; check with is_*() first.
    [[nodiscard]] bool as_bool() const;
    [[nodiscard]] double as_number() const;
    [[nodiscard]] const std::string& as_string() const;
    [[nodiscard]] const Array& as_array() const;
    [[nodiscard]] Array& as_array();
    [[nodiscard]] const Object& as_object() const;
    [[nodiscard]] Object& as_object();

    // Object helpers. find() returns nullptr if absent or not an object.
    [[nodiscard]] const Json* find(std::string_view key) const noexcept;
    // Sets (or replaces) a member; the value must be an object.
    Json& set(std::string key, Json value);
    // Appends to an array.
    void push_back(Json value);

    friend bool operator==(const Json& a, const Json& b);

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value_;
};

struct Json::Member {
    std::string key;
    Json value;
    friend bool operator==(const Member&, const Member&) = default;
};

inline Json::Json() : value_(nullptr) {}
inline Json::Json(std::nullptr_t) : value_(nullptr) {}
inline Json::Json(bool b) : value_(b) {}
inline Json::Json(double n) : value_(n) {}
inline Json::Json(int n) : value_(static_cast<double>(n)) {}
inline Json::Json(std::int64_t n) : value_(static_cast<double>(n)) {}
inline Json::Json(std::uint64_t n) : value_(static_cast<double>(n)) {}
inline Json::Json(const char* s) : value_(std::string(s)) {}
inline Json::Json(std::string s) : value_(std::move(s)) {}
inline Json::Json(std::string_view s) : value_(std::string(s)) {}
inline Json::Json(Array a) : value_(std::move(a)) {}
inline Json::Json(Object o) : value_(std::move(o)) {}
inline Json Json::array() { return Json(Array{}); }
inline Json Json::object() { return Json(Object{}); }

struct JsonParseOptions {
    std::size_t max_depth = 256;
    std::size_t max_bytes = 256u * 1024u * 1024u;
};

[[nodiscard]] Result<Json> parse_json(std::string_view text, const JsonParseOptions& options = {});

// Compact (indent < 0) or pretty-printed with `indent` spaces. Numbers use
// the shortest representation that round-trips exactly.
[[nodiscard]] std::string to_json_string(const Json& value, int indent = -1);

}  // namespace aether
