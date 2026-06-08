#pragma once
// recent.h - the "Open Recent" most-recently-used (MRU) file list.
//
// Persisted as plain UTF-8 text (one absolute path per line, newest first) to
// %APPDATA%\MarkdownViewer\recent.txt. It lives alongside config.json so the
// installer's existing %APPDATA%\MarkdownViewer cleanup removes it on uninstall
// (no extra footprint to track). The C++ side owns this list; the menu in
// main.cpp / app.h renders it.

#include <windows.h>
#include <string>
#include <vector>
#include "config.h"
#include "fileio.h"

namespace recent {

inline std::wstring store_path() {
    return fileio::roaming_app_dir() + L"\\recent.txt";
}

// Load the MRU list (newest first), capped at cfg::kMaxRecent. Missing file or
// read error yields an empty list.
inline std::vector<std::wstring> load() {
    std::vector<std::wstring> out;
    std::string text;
    if (!fileio::read_file_text(store_path(), text)) return out;
    std::wstring w = fileio::widen(text);
    size_t start = 0;
    while (start < w.size() && out.size() < (size_t)cfg::kMaxRecent) {
        size_t nl = w.find_first_of(L"\r\n", start);
        std::wstring line = (nl == std::wstring::npos) ? w.substr(start)
                                                       : w.substr(start, nl - start);
        if (!line.empty()) out.push_back(line);
        if (nl == std::wstring::npos) break;
        start = nl + 1;
    }
    return out;
}

inline void save(const std::vector<std::wstring>& list) {
    fileio::ensure_dir(fileio::roaming_app_dir());
    std::wstring w;
    for (size_t i = 0; i < list.size() && i < (size_t)cfg::kMaxRecent; ++i) {
        w += list[i];
        w += L"\r\n";
    }
    std::string u8 = fileio::narrow(w);
    fileio::write_file_bytes(store_path(), u8.data(), u8.size());
}

// Promote `path` to the front of the list (canonicalized, case-insensitive
// dedupe), trim to the cap, and persist.
inline void add(const std::wstring& path) {
    std::wstring full = fileio::full_path(path);
    if (full.empty()) full = path;
    std::vector<std::wstring> list = load();
    for (size_t i = 0; i < list.size();) {
        if (lstrcmpiW(list[i].c_str(), full.c_str()) == 0) list.erase(list.begin() + i);
        else ++i;
    }
    list.insert(list.begin(), full);
    if (list.size() > (size_t)cfg::kMaxRecent) list.resize(cfg::kMaxRecent);
    save(list);
}

inline void clear() {
    DeleteFileW(store_path().c_str());
}

} // namespace recent
