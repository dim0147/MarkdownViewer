#pragma once
// settings.h - runtime user settings (config.json).
//
// The C++ side treats the config as an opaque JSON document: it only creates
// the file with defaults on first run and ships its raw text to the renderer
// (assets/app.js), which parses and applies it. This keeps all knowledge of
// individual options in one place - the JS renderer - and means new options
// never require C++ changes.

#include <string>
#include "config.h"
#include "fileio.h"

namespace settings {

// Default config written on first run.
inline const char kDefaultConfigJson[] =
R"({
  "theme": "auto",
  "maxWidth": 920,
  "fontSize": 16,
  "syntaxHighlight": true,
  "linkify": true,
  "typographer": false
})";
// theme:           "auto" follows Windows light/dark mode; or "light" / "dark"
// maxWidth:        content column width in px (0 = full width)
// fontSize:        base font size in px
// syntaxHighlight: colorize fenced code blocks (highlight.js)
// linkify:         turn bare URLs into links
// typographer:     smart quotes, dashes, ellipsis

inline std::wstring config_path() {
    return fileio::roaming_app_dir() + L"\\config.json";
}

// Returns the raw JSON text of the user's config, creating it with defaults
// on first run. Never fails: falls back to the built-in defaults.
inline std::string load_or_create() {
    std::wstring path = config_path();
    if (!fileio::file_exists(path)) {
        fileio::ensure_dir(fileio::roaming_app_dir());
        fileio::write_file_bytes(path, kDefaultConfigJson, sizeof(kDefaultConfigJson) - 1);
    }
    std::string text;
    if (!fileio::read_file_text(path, text) || text.empty())
        text = kDefaultConfigJson;
    return text;
}

} // namespace settings
