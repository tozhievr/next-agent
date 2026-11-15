#include "host/uri.hpp"

#include <algorithm>
#include <cctype>

namespace lx::host {

namespace {

int hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string percent_decode(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      int h = hex_val(s[i + 1]);
      int l = hex_val(s[i + 2]);
      if (h >= 0 && l >= 0) {
        out.push_back(static_cast<char>((h << 4) | l));
        i += 2;
        continue;
      }
    }
    out.push_back(s[i]);
  }
  return out;
}

}  // namespace

std::string extract_extension(std::string_view uri_or_path) {
  // Strip query/fragment if URI
  size_t q = uri_or_path.find_first_of("?#");
  if (q != std::string_view::npos) {
    uri_or_path = uri_or_path.substr(0, q);
  }
  size_t slash = uri_or_path.find_last_of("/\\");
  std::string_view filename =
      (slash == std::string_view::npos) ? uri_or_path : uri_or_path.substr(slash + 1);
  size_t dot = filename.rfind('.');
  if (dot == std::string_view::npos || dot == 0) {
    return {};
  }
  std::string ext(filename.substr(dot));
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext;
}

std::string uri_to_path(std::string_view uri) {
  constexpr std::string_view kScheme = "file://";
  if (uri.size() < kScheme.size() || uri.substr(0, kScheme.size()) != kScheme) {
    return std::string(uri);
  }
  std::string_view rest = uri.substr(kScheme.size());
  // Strip authority if empty: file:///C:/... -> /C:/...
  if (!rest.empty() && rest.front() == '/') {
    std::string decoded = percent_decode(rest);
#if defined(_WIN32)
    // "/C:/..." -> "C:/..."
    if (decoded.size() >= 3 && decoded[0] == '/' &&
        std::isalpha(static_cast<unsigned char>(decoded[1])) && decoded[2] == ':') {
      decoded.erase(decoded.begin());
    }
    std::replace(decoded.begin(), decoded.end(), '/', '\\');
#endif
    return decoded;
  }
  // UNC path file://server/share
  std::string decoded = percent_decode(rest);
#if defined(_WIN32)
  std::replace(decoded.begin(), decoded.end(), '/', '\\');
  return "\\\\" + decoded;
#else
  return decoded;
#endif
}

std::string path_to_uri(std::string_view path) {
  std::string p(path);
  std::replace(p.begin(), p.end(), '\\', '/');
  std::string out = "file://";
  if (!p.empty() && p.front() != '/') {
    out.push_back('/');
  }
  for (unsigned char c : p) {
    if (c == ' ') {
      out += "%20";
    } else if (c == '%') {
      out += "%25";
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  return out;
}

}  // namespace lx::host
