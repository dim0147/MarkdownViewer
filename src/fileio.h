#pragma once
// fileio.h - text encoding helpers, file reading/writing, well-known paths,
// and small string utilities (JSON escaping, URL decoding) shared by the app.

#include <windows.h>
#include <shlobj.h>
#include <string>
#include "config.h"

namespace fileio {

inline std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

inline std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// Read a text file as UTF-8; handles UTF-8 (with/without BOM), UTF-16 LE/BE,
// and falls back to the system ANSI code page for non-UTF-8 content.
inline bool read_file_text(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    GetFileSizeEx(h, &size);
    if (size.QuadPart > cfg::kMaxFileBytes) { CloseHandle(h); return false; }
    std::string buf((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    BOOL ok = buf.empty() ? TRUE : ReadFile(h, &buf[0], (DWORD)buf.size(), &read, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    buf.resize(read);

    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF) {
        out = buf.substr(3);                                       // UTF-8 BOM
        return true;
    }
    if (buf.size() >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE) {
        std::wstring w((const wchar_t*)(buf.data() + 2), (buf.size() - 2) / 2);   // UTF-16 LE
        out = narrow(w);
        return true;
    }
    if (buf.size() >= 2 && (unsigned char)buf[0] == 0xFE && (unsigned char)buf[1] == 0xFF) {
        std::wstring w;                                            // UTF-16 BE
        for (size_t i = 2; i + 1 < buf.size(); i += 2)
            w += (wchar_t)(((unsigned char)buf[i] << 8) | (unsigned char)buf[i + 1]);
        out = narrow(w);
        return true;
    }
    // Validate as UTF-8; fall back to the system ANSI code page.
    if (!buf.empty() &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf.c_str(), (int)buf.size(), nullptr, 0) == 0) {
        int n = MultiByteToWideChar(CP_ACP, 0, buf.c_str(), (int)buf.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, buf.c_str(), (int)buf.size(), &w[0], n);
        out = narrow(w);
        return true;
    }
    out = buf;
    return true;
}

inline bool write_file_bytes(const std::wstring& path, const void* data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = size == 0 ? TRUE : WriteFile(h, data, (DWORD)size, &written, nullptr);
    CloseHandle(h);
    return ok && written == size;
}

inline bool file_exists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Canonicalize a path: resolve "." / ".." and make it fully qualified.
// Returns an empty string on failure (treat that as "reject").
inline std::wstring full_path(const std::wstring& path) {
    wchar_t buf[4096];
    DWORD n = GetFullPathNameW(path.c_str(), 4096, buf, nullptr);
    if (n == 0 || n >= 4096) return std::wstring();
    return std::wstring(buf, n);
}

// Create a directory tree (no-op if it already exists).
inline bool ensure_dir(const std::wstring& path) {
    int r = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || r == ERROR_FILE_EXISTS;
}

// %LOCALAPPDATA%\MarkdownViewer  (caches: extracted assets, WebView2 profile)
inline std::wstring local_app_dir() {
    wchar_t buf[MAX_PATH] = L"";
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf);
    return std::wstring(buf) + L"\\" + cfg::kAppDirName;
}

// %APPDATA%\MarkdownViewer  (user settings: config.json)
inline std::wstring roaming_app_dir() {
    wchar_t buf[MAX_PATH] = L"";
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf);
    return std::wstring(buf) + L"\\" + cfg::kAppDirName;
}

// Split "C:\dir\file.md" into directory and file name.
inline void split_path(const std::wstring& path, std::wstring& dir, std::wstring& name) {
    size_t slash = path.find_last_of(L"\\/");
    dir  = (slash == std::wstring::npos) ? L"." : path.substr(0, slash);
    name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
}

inline bool has_markdown_ext(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot);
    for (const wchar_t* e : cfg::kMarkdownExts)
        if (lstrcmpiW(ext.c_str(), e) == 0) return true;
    return false;
}

// Escape a UTF-8 string for embedding inside a JSON string literal.
inline std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 64);
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                wsprintfA(buf, "\\u%04X", c);
                out += buf;
            } else {
                out += (char)c;
            }
        }
    }
    return out;
}

// Decode %XX escapes of a URL path component (UTF-8) into a wide string.
inline std::wstring url_decode(const std::wstring& in) {
    std::string u8;
    u8.reserve(in.size());
    auto hex = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == L'%' && i + 2 < in.size() && hex(in[i + 1]) >= 0 && hex(in[i + 2]) >= 0) {
            int byte = hex(in[i + 1]) * 16 + hex(in[i + 2]);
            // Drop NUL / control bytes: a decoded path must never contain them
            // (e.g. "%00" truncating a path, or smuggled control chars).
            if (byte >= 0x20) u8 += (char)byte;
            i += 2;
        } else {
            u8 += narrow(std::wstring(1, in[i]));
        }
    }
    return widen(u8);
}

} // namespace fileio
