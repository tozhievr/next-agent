#include "host/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lx::host {

namespace {

const JsonValue& null_value() {
  static const JsonValue kNull{nullptr};
  return kNull;
}

const JsonArray& empty_array() {
  static const JsonArray kEmpty;
  return kEmpty;
}

const JsonObject& empty_object() {
  static const JsonObject kEmpty;
  return kEmpty;
}

void append_utf8(std::string& out, uint32_t cp) {
  if (cp <= 0x7Fu) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFu) {
    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0xFFFFu) {
    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0x10FFFFu) {
    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

void dump_string(std::string& out, std::string_view s) {
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20u) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out.push_back('"');
}

void dump_impl(std::string& out, const JsonValue& v, bool pretty, int indent) {
  if (v.is_null()) {
    out += "null";
  } else if (v.is_bool()) {
    out += v.as_bool() ? "true" : "false";
  } else if (v.is_int()) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v.as_int());
    out.append(buf, static_cast<size_t>(ptr - buf));
  } else if (v.is_double()) {
    double d = v.as_double();
    if (!std::isfinite(d)) {
      out += "null";
    } else {
      char buf[64];
      auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), d);
      out.append(buf, static_cast<size_t>(ptr - buf));
    }
  } else if (v.is_string()) {
    dump_string(out, v.as_string());
  } else if (v.is_array()) {
    const auto& arr = v.as_array();
    if (arr.empty()) {
      out += "[]";
      return;
    }
    out.push_back('[');
    for (size_t i = 0; i < arr.size(); ++i) {
      if (i > 0) out.push_back(',');
      if (pretty) {
        out.push_back('\n');
        out.append(static_cast<size_t>(indent + 2), ' ');
      }
      dump_impl(out, arr[i], pretty, indent + 2);
    }
    if (pretty) {
      out.push_back('\n');
      out.append(static_cast<size_t>(indent), ' ');
    }
    out.push_back(']');
  } else if (v.is_object()) {
    const auto& obj = v.as_object();
    if (obj.empty()) {
      out += "{}";
      return;
    }
    out.push_back('{');
    bool first = true;
    for (const auto& [k, val] : obj) {
      if (!first) out.push_back(',');
      first = false;
      if (pretty) {
        out.push_back('\n');
        out.append(static_cast<size_t>(indent + 2), ' ');
      }
      dump_string(out, k);
      out.push_back(':');
      if (pretty) out.push_back(' ');
      dump_impl(out, val, pretty, indent + 2);
    }
    if (pretty) {
      out.push_back('\n');
      out.append(static_cast<size_t>(indent), ' ');
    }
    out.push_back('}');
  }
}

class Parser {
 public:
  explicit Parser(std::string_view in) : in_(in) {}

  JsonParseResult parse() {
    skip_ws();
    auto val = parse_value();
    if (!val) return {std::nullopt, std::move(err_), pos_};
    skip_ws();
    if (pos_ != in_.size()) {
      return {std::nullopt, "trailing characters after JSON root", pos_};
    }
    return {std::move(val), {}, 0};
  }

 private:
  std::string_view in_;
  size_t pos_{0};
  std::string err_;

  void skip_ws() {
    while (pos_ < in_.size()) {
      char c = in_[pos_];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool fail(std::string msg) {
    if (err_.empty()) err_ = std::move(msg);
    return false;
  }

  std::optional<JsonValue> parse_value() {
    skip_ws();
    if (pos_ >= in_.size()) {
      fail("unexpected end of JSON input");
      return std::nullopt;
    }
    char c = in_[pos_];
    if (c == 'n') return parse_literal("null", JsonValue{nullptr});
    if (c == 't') return parse_literal("true", JsonValue{true});
    if (c == 'f') return parse_literal("false", JsonValue{false});
    if (c == '"') {
      auto s = parse_string();
      if (!s) return std::nullopt;
      return JsonValue{std::move(*s)};
    }
    if (c == '[') return parse_array();
    if (c == '{') return parse_object();
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
    fail(std::string("unexpected character: ") + c);
    return std::nullopt;
  }

  std::optional<JsonValue> parse_literal(std::string_view lit, JsonValue val) {
    if (in_.substr(pos_, lit.size()) != lit) {
      fail("invalid literal");
      return std::nullopt;
    }
    pos_ += lit.size();
    return val;
  }

  std::optional<uint32_t> parse_hex4() {
    if (pos_ + 4 > in_.size()) {
      fail("truncated \\u escape");
      return std::nullopt;
    }
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = in_[pos_++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
      else {
        fail("invalid hex digit in \\u escape");
        return std::nullopt;
      }
    }
    return v;
  }

  std::optional<std::string> parse_string() {
    if (pos_ >= in_.size() || in_[pos_] != '"') {
      fail("expected '\"'");
      return std::nullopt;
    }
    ++pos_;
    std::string out;
    while (pos_ < in_.size()) {
      unsigned char c = static_cast<unsigned char>(in_[pos_++]);
      if (c == '"') return out;
      if (c < 0x20u) {
        fail("unescaped control character in string");
        return std::nullopt;
      }
      if (c == '\\') {
        if (pos_ >= in_.size()) {
          fail("unterminated escape sequence");
          return std::nullopt;
        }
        char esc = in_[pos_++];
        switch (esc) {
          case '"':  out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/':  out.push_back('/'); break;
          case 'b':  out.push_back('\b'); break;
          case 'f':  out.push_back('\f'); break;
          case 'n':  out.push_back('\n'); break;
          case 'r':  out.push_back('\r'); break;
          case 't':  out.push_back('\t'); break;
          case 'u': {
            auto cp1 = parse_hex4();
            if (!cp1) return std::nullopt;
            if (*cp1 >= 0xD800u && *cp1 <= 0xDBFFu) {
              if (pos_ + 2 <= in_.size() && in_[pos_] == '\\' && in_[pos_ + 1] == 'u') {
                pos_ += 2;
                auto cp2 = parse_hex4();
                if (!cp2) return std::nullopt;
                if (*cp2 >= 0xDC00u && *cp2 <= 0xDFFFu) {
                  uint32_t combined =
                      0x10000u + (((*cp1 - 0xD800u) << 10) | (*cp2 - 0xDC00u));
                  append_utf8(out, combined);
                } else {
                  append_utf8(out, *cp1);
                  append_utf8(out, *cp2);
                }
              } else {
                append_utf8(out, *cp1);
              }
            } else {
              append_utf8(out, *cp1);
            }
            break;
          }
          default:
            fail("invalid escape character");
            return std::nullopt;
        }
      } else {
        out.push_back(static_cast<char>(c));
      }
    }
    fail("unterminated string");
    return std::nullopt;
  }

  std::optional<JsonValue> parse_number() {
    size_t start = pos_;
    if (in_[pos_] == '-') ++pos_;
    if (pos_ >= in_.size() || !std::isdigit(static_cast<unsigned char>(in_[pos_]))) {
      fail("invalid number");
      return std::nullopt;
    }
    if (in_[pos_] == '0') {
      ++pos_;
    } else {
      while (pos_ < in_.size() && std::isdigit(static_cast<unsigned char>(in_[pos_]))) ++pos_;
    }
    bool is_float = false;
    if (pos_ < in_.size() && in_[pos_] == '.') {
      is_float = true;
      ++pos_;
      if (pos_ >= in_.size() || !std::isdigit(static_cast<unsigned char>(in_[pos_]))) {
        fail("invalid fraction in number");
        return std::nullopt;
      }
      while (pos_ < in_.size() && std::isdigit(static_cast<unsigned char>(in_[pos_]))) ++pos_;
    }
    if (pos_ < in_.size() && (in_[pos_] == 'e' || in_[pos_] == 'E')) {
      is_float = true;
      ++pos_;
      if (pos_ < in_.size() && (in_[pos_] == '+' || in_[pos_] == '-')) ++pos_;
      if (pos_ >= in_.size() || !std::isdigit(static_cast<unsigned char>(in_[pos_]))) {
        fail("invalid exponent in number");
        return std::nullopt;
      }
      while (pos_ < in_.size() && std::isdigit(static_cast<unsigned char>(in_[pos_]))) ++pos_;
    }
    std::string_view slice = in_.substr(start, pos_ - start);
    if (!is_float) {
      int64_t iv = 0;
      auto [ptr, ec] = std::from_chars(slice.data(), slice.data() + slice.size(), iv);
      if (ec == std::errc{} && ptr == slice.data() + slice.size()) {
        return JsonValue{iv};
      }
      uint64_t uv = 0;
      auto [uptr, uec] = std::from_chars(slice.data(), slice.data() + slice.size(), uv);
      if (uec == std::errc{} && uptr == slice.data() + slice.size()) {
        return JsonValue{static_cast<int64_t>(uv)};
      }
    }
    double dv = 0.0;
    auto [ptr, ec] = std::from_chars(slice.data(), slice.data() + slice.size(), dv);
    if (ec != std::errc{}) {
      fail("number out of range");
      return std::nullopt;
    }
    return JsonValue{dv};
  }

  std::optional<JsonValue> parse_array() {
    ++pos_;  // '['
    JsonArray arr;
    skip_ws();
    if (pos_ < in_.size() && in_[pos_] == ']') {
      ++pos_;
      return JsonValue{std::move(arr)};
    }
    while (true) {
      auto v = parse_value();
      if (!v) return std::nullopt;
      arr.push_back(std::move(*v));
      skip_ws();
      if (pos_ >= in_.size()) {
        fail("unterminated array");
        return std::nullopt;
      }
      if (in_[pos_] == ']') {
        ++pos_;
        return JsonValue{std::move(arr)};
      }
      if (in_[pos_] != ',') {
        fail("expected ',' or ']' in array");
        return std::nullopt;
      }
      ++pos_;
    }
  }

  std::optional<JsonValue> parse_object() {
    ++pos_;  // '{'
    JsonObject obj;
    skip_ws();
    if (pos_ < in_.size() && in_[pos_] == '}') {
      ++pos_;
      return JsonValue{std::move(obj)};
    }
    while (true) {
      skip_ws();
      auto key = parse_string();
      if (!key) return std::nullopt;
      skip_ws();
      if (pos_ >= in_.size() || in_[pos_] != ':') {
        fail("expected ':' after object key");
        return std::nullopt;
      }
      ++pos_;
      auto val = parse_value();
      if (!val) return std::nullopt;
      obj.insert_or_assign(std::move(*key), std::move(*val));
      skip_ws();
      if (pos_ >= in_.size()) {
        fail("unterminated object");
        return std::nullopt;
      }
      if (in_[pos_] == '}') {
        ++pos_;
        return JsonValue{std::move(obj)};
      }
      if (in_[pos_] != ',') {
        fail("expected ',' or '}' in object");
        return std::nullopt;
      }
      ++pos_;
    }
  }
};

}  // namespace

bool JsonValue::as_bool(bool def) const {
  if (auto* b = std::get_if<bool>(&data)) return *b;
  return def;
}

int64_t JsonValue::as_int(int64_t def) const {
  if (auto* i = std::get_if<int64_t>(&data)) return *i;
  if (auto* d = std::get_if<double>(&data)) return static_cast<int64_t>(*d);
  return def;
}

uint64_t JsonValue::as_uint64(uint64_t def) const {
  if (auto* i = std::get_if<int64_t>(&data)) return static_cast<uint64_t>(*i);
  if (auto* d = std::get_if<double>(&data)) return static_cast<uint64_t>(*d);
  return def;
}

double JsonValue::as_double(double def) const {
  if (auto* d = std::get_if<double>(&data)) return *d;
  if (auto* i = std::get_if<int64_t>(&data)) return static_cast<double>(*i);
  return def;
}

std::string_view JsonValue::as_string() const {
  if (auto* s = std::get_if<std::string>(&data)) return *s;
  return {};
}

const JsonArray& JsonValue::as_array() const {
  if (auto* a = std::get_if<JsonArray>(&data)) return *a;
  return empty_array();
}

const JsonObject& JsonValue::as_object() const {
  if (auto* o = std::get_if<JsonObject>(&data)) return *o;
  return empty_object();
}

JsonArray& JsonValue::as_array_mut() {
  if (!is_array()) data = JsonArray{};
  return std::get<JsonArray>(data);
}

JsonObject& JsonValue::as_object_mut() {
  if (!is_object()) data = JsonObject{};
  return std::get<JsonObject>(data);
}

const JsonValue* JsonValue::find(std::string_view key) const {
  if (auto* o = std::get_if<JsonObject>(&data)) {
    auto it = o->find(key);
    if (it != o->end()) return &it->second;
  }
  return nullptr;
}

JsonValue* JsonValue::find_mut(std::string_view key) {
  if (auto* o = std::get_if<JsonObject>(&data)) {
    auto it = o->find(key);
    if (it != o->end()) return &it->second;
  }
  return nullptr;
}

const JsonValue& JsonValue::at(std::string_view key) const {
  if (const auto* p = find(key)) return *p;
  return null_value();
}

JsonValue& JsonValue::operator[](const std::string& key) {
  return as_object_mut()[key];
}

std::string JsonValue::dump(bool pretty, int indent) const {
  std::string out;
  dump_impl(out, *this, pretty, indent);
  return out;
}

JsonParseResult parse_json(std::string_view input) {
  return Parser(input).parse();
}

std::optional<uint64_t> json_to_u64(const JsonValue& v) {
  if (v.is_int()) {
    return static_cast<uint64_t>(v.as_int());
  }
  if (v.is_double()) {
    double d = v.as_double();
    if (d >= 0.0 && std::isfinite(d)) {
      return static_cast<uint64_t>(d);
    }
    return std::nullopt;
  }
  if (v.is_string()) {
    std::string_view s = v.as_string();
    if (s.empty()) return std::nullopt;
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
      base = 16;
      s.remove_prefix(2);
    }
    uint64_t out = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out, base);
    if (ec == std::errc{} && ptr == s.data() + s.size()) {
      return out;
    }
  }
  return std::nullopt;
}

}  // namespace lx::host
