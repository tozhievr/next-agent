#pragma once

#include <string>
#include <string_view>

namespace lx::host {

/**
 * Extracts lowercased file extension (including leading '.', e.g. ".json")
 * from a file URI or filesystem path.
 */
std::string extract_extension(std::string_view uri_or_path);

/**
 * Converts a file:// URI to a local path (best-effort) or returns input as-is.
 */
std::string uri_to_path(std::string_view uri);

/**
 * Converts a local path to a file:// URI.
 */
std::string path_to_uri(std::string_view path);

}  // namespace lx::host
