#include "aether/foundation/json.hpp"

#include <charconv>
#include <cmath>
#include <string>

#include "aether/foundation/text.hpp"

namespace aether {

// ---- Value -------------------------------------------------------------------

bool Json::as_bool() const {
    AETHER_ASSERT(is_bool(), "Json is not a bool");
    return std::get<bool>(value_);
}
double Json::as_number() const {
    AETHER_ASSERT(is_number(), "Json is not a number");
    return std::get<double>(value_);
}
const std::string& Json::as_string() const {
    AETHER_ASSERT(is_string(), "Json is not a string");
    return std::get<std::string>(value_);
}
const Json::Array& Json::as_array() const {
    AETHER_ASSERT(is_array(), "Json is not an array");
    return std::get<Array>(value_);
}
Json::Array& Json::as_array() {
    AETHER_ASSERT(is_array(), "Json is not an array");
    return std::get<Array>(value_);
}
const Json::Object& Json::as_object() const {
    AETHER_ASSERT(is_object(), "Json is not an object");
    return std::get<Object>(value_);
}
Json::Object& Json::as_object() {
    AETHER_ASSERT(is_object(), "Json is not an object");
    return std::get<Object>(value_);
}

const Json* Json::find(std::string_view key) const noexcept {
    if (!is_object()) return nullptr;
    for (const Member& m : std::get<Object>(value_))
        if (m.key == key) return &m.value;
    return nullptr;
}

Json& Json::set(std::string key, Json value) {
    Object& obj = as_object();
    for (Member& m : obj)
        if (m.key == key) {
            m.value = std::move(value);
            return m.value;
        }
    obj.push_back(Member{std::move(key), std::move(value)});
    return obj.back().value;
}

void Json::push_back(Json value) { as_array().push_back(std::move(value)); }

bool operator==(const Json& a, const Json& b) { return a.value_ == b.value_; }

// ---- Parser ------------------------------------------------------------------

namespace {

class Parser {
public:
    Parser(std::string_view text, const JsonParseOptions& opt) : s_(text), opt_(opt) {}

    Result<Json> run() {
        if (s_.size() > opt_.max_bytes) return Error{ErrorCode::InvalidArgument, "JSON input exceeds size limit"};
        skip_ws();
        Json value;
        if (!parse_value(value, 0)) return fail_result();
        skip_ws();
        if (pos_ != s_.size()) {
            set_error("unexpected trailing characters");
            return fail_result();
        }
        return value;
    }

private:
    bool parse_value(Json& out, std::size_t depth) {
        if (depth > opt_.max_depth) return set_error("nesting too deep");
        if (pos_ >= s_.size()) return set_error("unexpected end of input");
        switch (s_[pos_]) {
            case '{': return parse_object(out, depth);
            case '[': return parse_array(out, depth);
            case '"': {
                std::string str;
                if (!parse_string(str)) return false;
                out = Json(std::move(str));
                return true;
            }
            case 't': return parse_literal("true", Json(true), out);
            case 'f': return parse_literal("false", Json(false), out);
            case 'n': return parse_literal("null", Json(nullptr), out);
            default: return parse_number(out);
        }
    }

    bool parse_literal(std::string_view word, Json value, Json& out) {
        if (s_.substr(pos_, word.size()) != word) return set_error("invalid literal");
        pos_ += word.size();
        out = std::move(value);
        return true;
    }

    bool parse_number(Json& out) {
        const std::size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size() || !is_digit(s_[pos_])) return set_error("invalid value");
        if (s_[pos_] == '0') ++pos_;
        else while (pos_ < s_.size() && is_digit(s_[pos_])) ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            if (pos_ >= s_.size() || !is_digit(s_[pos_])) return set_error("digit expected after '.'");
            while (pos_ < s_.size() && is_digit(s_[pos_])) ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (pos_ >= s_.size() || !is_digit(s_[pos_])) return set_error("digit expected in exponent");
            while (pos_ < s_.size() && is_digit(s_[pos_])) ++pos_;
        }
        double value = 0;
        const auto [ptr, ec] = std::from_chars(s_.data() + start, s_.data() + pos_, value);
        if (ec != std::errc() || ptr != s_.data() + pos_ || !std::isfinite(value)) {
            pos_ = start;
            return set_error("number out of range");
        }
        out = Json(value);
        return true;
    }

    bool parse_hex4(std::uint32_t& out) {
        if (pos_ + 4 > s_.size()) return set_error("truncated \\u escape");
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return set_error("invalid \\u escape");
        }
        return true;
    }

    static void append_utf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool parse_string(std::string& out) {
        ++pos_;  // opening quote
        while (true) {
            if (pos_ >= s_.size()) return set_error("unterminated string");
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return set_error("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) return set_error("unterminated escape");
            const char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!parse_hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (pos_ + 2 > s_.size() || s_[pos_] != '\\' || s_[pos_ + 1] != 'u')
                            return set_error("unpaired surrogate in \\u escape");
                        pos_ += 2;
                        std::uint32_t low = 0;
                        if (!parse_hex4(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) return set_error("invalid low surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return set_error("unpaired surrogate in \\u escape");
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: return set_error("invalid escape");
            }
        }
    }

    bool parse_array(Json& out, std::size_t depth) {
        ++pos_;
        Json::Array arr;
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            out = Json(std::move(arr));
            return true;
        }
        while (true) {
            skip_ws();
            Json item;
            if (!parse_value(item, depth + 1)) return false;
            arr.push_back(std::move(item));
            skip_ws();
            if (pos_ >= s_.size()) return set_error("unterminated array");
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; break; }
            return set_error("expected ',' or ']'");
        }
        out = Json(std::move(arr));
        return true;
    }

    bool parse_object(Json& out, std::size_t depth) {
        ++pos_;
        Json::Object obj;
        skip_ws();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            out = Json(std::move(obj));
            return true;
        }
        while (true) {
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != '"') return set_error("expected string key");
            std::string key;
            if (!parse_string(key)) return false;
            for (const auto& m : obj)
                if (m.key == key) return set_error("duplicate key \"" + key + "\"");
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != ':') return set_error("expected ':'");
            ++pos_;
            skip_ws();
            Json value;
            if (!parse_value(value, depth + 1)) return false;
            obj.push_back(Json::Member{std::move(key), std::move(value)});
            skip_ws();
            if (pos_ >= s_.size()) return set_error("unterminated object");
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; break; }
            return set_error("expected ',' or '}'");
        }
        out = Json(std::move(obj));
        return true;
    }

    void skip_ws() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }
    static bool is_digit(char c) { return c >= '0' && c <= '9'; }

    bool set_error(std::string message) {
        if (error_.empty()) {
            std::size_t line = 1, col = 1;
            for (std::size_t i = 0; i < pos_ && i < s_.size(); ++i) {
                if (s_[i] == '\n') { ++line; col = 1; } else { ++col; }
            }
            error_ = "JSON error at line " + std::to_string(line) + ", column " + std::to_string(col) + ": " + message;
        }
        return false;
    }
    Error fail_result() const { return Error{ErrorCode::InvalidArgument, error_}; }

    std::string_view s_;
    JsonParseOptions opt_;
    std::size_t pos_ = 0;
    std::string error_;
};

// ---- Writer ------------------------------------------------------------------

void write_number(std::string& out, double v) {
    if (v == 0) { out += "0"; return; }  // also normalises -0
    char buf[32];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof buf, v);
    out.append(buf, ec == std::errc() ? ptr : buf);
}

void write(std::string& out, const Json& v, int indent, int level) {
    const auto newline = [&](int lvl) {
        if (indent < 0) return;
        out += '\n';
        out.append(static_cast<std::size_t>(indent * lvl), ' ');
    };
    switch (v.type()) {
        case Json::Type::Null: out += "null"; break;
        case Json::Type::Bool: out += v.as_bool() ? "true" : "false"; break;
        case Json::Type::Number: write_number(out, v.as_number()); break;
        case Json::Type::String:
            out += '"';
            append_json_escaped(out, v.as_string());
            out += '"';
            break;
        case Json::Type::Array: {
            const auto& a = v.as_array();
            if (a.empty()) { out += "[]"; break; }
            // Short arrays of numbers (vectors, colours) stay on one line.
            bool flat = a.size() <= 4;
            for (const Json& item : a) flat = flat && item.is_number();
            out += '[';
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (i) out += flat && indent >= 0 ? ", " : ",";
                if (!flat) newline(level + 1);
                write(out, a[i], indent, level + 1);
            }
            if (!flat) newline(level);
            out += ']';
            break;
        }
        case Json::Type::Object: {
            const auto& o = v.as_object();
            if (o.empty()) { out += "{}"; break; }
            out += '{';
            for (std::size_t i = 0; i < o.size(); ++i) {
                if (i) out += ',';
                newline(level + 1);
                out += '"';
                append_json_escaped(out, o[i].key);
                out += indent >= 0 ? "\": " : "\":";
                write(out, o[i].value, indent, level + 1);
            }
            newline(level);
            out += '}';
            break;
        }
    }
}

}  // namespace

Result<Json> parse_json(std::string_view text, const JsonParseOptions& options) {
    return Parser(text, options).run();
}

std::string to_json_string(const Json& value, int indent) {
    std::string out;
    write(out, value, indent, 0);
    return out;
}

}  // namespace aether
