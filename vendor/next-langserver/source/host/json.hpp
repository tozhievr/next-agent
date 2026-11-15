#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lx::host {

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue, std::less<>>;

/**
 * Lightweight RFC 8259 JSON value representation for manifests and LSP RPC.
 */
struct JsonValue {
  using Variant = std::variant<
      std::nullptr_t,
      bool,
      int64_t,
      double,
      std::string,
      JsonArray,
      JsonObject>;

  Variant data{nullptr};

  JsonValue() = default;
  JsonValue(std::nullptr_t) : data(nullptr) {}
  JsonValue(bool b) : data(b) {}
  JsonValue(int i) : data(static_cast<int64_t>(i)) {}
  JsonValue(int64_t i) : data(i) {}
  JsonValue(uint64_t u) : data(static_cast<int64_t>(u)) {}
  JsonValue(double d) : data(d) {}
  JsonValue(const char* s) : data(std::string(s ? s : "")) {}
  JsonValue(std::string s) : data(std::move(s)) {}
  JsonValue(std::string_view s) : data(std::string(s)) {}
  JsonValue(JsonArray arr) : data(std::move(arr)) {}
  JsonValue(JsonObject obj) : data(std::move(obj)) {}

  bool is_null() const { return std::holds_alternative<std::nullptr_t>(data); }
  bool is_bool() const { return std::holds_alternative<bool>(data); }
  bool is_int() const { return std::holds_alternative<int64_t>(data); }
  bool is_double() const { return std::holds_alternative<double>(data); }
  bool is_number() const { return is_int() || is_double(); }
  bool is_string() const { return std::holds_alternative<std::string>(data); }
  bool is_array() const { return std::holds_alternative<JsonArray>(data); }
  bool is_object() const { return std::holds_alternative<JsonObject>(data); }

  bool as_bool(bool def = false) const;
  int64_t as_int(int64_t def = 0) const;
  uint64_t as_uint64(uint64_t def = 0) const;
  double as_double(double def = 0.0) const;
  std::string_view as_string() const;
  const JsonArray& as_array() const;
  const JsonObject& as_object() const;
  JsonArray& as_array_mut();
  JsonObject& as_object_mut();

  const JsonValue* find(std::string_view key) const;
  JsonValue* find_mut(std::string_view key);
  const JsonValue& at(std::string_view key) const;
  JsonValue& operator[](const std::string& key);

  std::string dump(bool pretty = false, int indent = 0) const;
};

struct JsonParseResult {
  std::optional<JsonValue> value;
  std::string error;
  size_t error_pos{0};

  explicit operator bool() const { return value.has_value(); }
};

JsonParseResult parse_json(std::string_view input);

/**
 * Parses a uint64 from either a JSON integer, double, or decimal/hex string ("0x...").
 */
std::optional<uint64_t> json_to_u64(const JsonValue& v);

}  // namespace lx::host
