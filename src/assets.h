#pragma once
// assets.h - extracts the web UI (HTML/CSS/JS, embedded in the exe as RCDATA
// resources by res\app.rc) to %LOCALAPPDATA%\MarkdownViewer\assets, where
// WebView2 serves them via the viewer.assets virtual host.
//
// Files are re-written on every launch so the on-disk copies always match the
// running exe. To add an asset: put the file under assets\, add an RCDATA
// entry in res\app.rc, and add a row to kAssets below.

#include <windows.h>
#include <string>
#include "fileio.h"

namespace assets {

struct Entry {
    const wchar_t* resource;   // RCDATA resource name in res\app.rc
    const wchar_t* relpath;    // output path relative to the assets dir
};

inline const Entry kAssets[] = {
    { L"ASSET_INDEX_HTML",       L"index.html" },
    { L"ASSET_APP_CSS",          L"app.css" },
    { L"ASSET_APP_JS",           L"app.js" },
    { L"ASSET_MARKDOWN_IT",      L"vendor\\markdown-it.min.js" },
    { L"ASSET_MDIT_TASK_LISTS",  L"vendor\\markdown-it-task-lists.min.js" },
    { L"ASSET_MDIT_ANCHOR",      L"vendor\\markdown-it-anchor.min.js" },
    { L"ASSET_HLJS",             L"vendor\\highlight.min.js" },
    { L"ASSET_MERMAID",          L"vendor\\mermaid.min.js" },
    { L"ASSET_HLJS_CSS_LIGHT",   L"vendor\\hljs-github.min.css" },
    { L"ASSET_HLJS_CSS_DARK",    L"vendor\\hljs-github-dark.min.css" },
    { L"ASSET_GHMD_CSS_LIGHT",   L"vendor\\github-markdown-light.min.css" },
    { L"ASSET_GHMD_CSS_DARK",    L"vendor\\github-markdown-dark.min.css" },
};

inline std::wstring assets_dir() {
    return fileio::local_app_dir() + L"\\assets";
}

// Extract all embedded assets. Returns false if any file could not be written.
inline bool extract_all() {
    std::wstring dir = assets_dir();
    if (!fileio::ensure_dir(dir + L"\\vendor")) return false;
    bool ok = true;
    for (const Entry& a : kAssets) {
        HRSRC res = FindResourceW(nullptr, a.resource, (LPCWSTR)RT_RCDATA);
        HGLOBAL h = res ? LoadResource(nullptr, res) : nullptr;
        const void* data = h ? LockResource(h) : nullptr;
        DWORD size = res ? SizeofResource(nullptr, res) : 0;
        if (!data) { ok = false; continue; }
        ok &= fileio::write_file_bytes(dir + L"\\" + a.relpath, data, size);
    }
    return ok;
}

} // namespace assets
