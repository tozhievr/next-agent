#pragma once

#include "host/json.hpp"

#include <istream>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>

namespace lx::host {

/**
 * LSP JSON-RPC 2.0 Content-Length framed transport over streams.
 * Thread-safe on writes.
 */
class LspTransport {
 public:
  LspTransport(std::istream& in, std::ostream& out);

  /**
   * Configures stdin/stdout in binary mode on Windows so CRLF headers are not mangled.
   */
  static void set_stdio_binary();

  /**
   * Reads one JSON-RPC message. Returns std::nullopt on EOF or fatal stream error.
   */
  std::optional<JsonValue> read_message(std::string* out_error = nullptr);

  /**
   * Writes one JSON-RPC message framed with `Content-Length: N\r\n\r\n`.
   */
  bool write_message(const JsonValue& msg);

  bool send_response(const JsonValue& id, JsonValue result);
  bool send_error(const JsonValue& id, int code, std::string message);
  bool send_notification(std::string method, JsonValue params);

 private:
  std::istream& in_;
  std::ostream& out_;
  std::mutex write_mu_;
};

}  // namespace lx::host
