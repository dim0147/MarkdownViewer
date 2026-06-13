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
    { L"ASSET_KATEX_CSS",        L"vendor\\katex.min.css" },
    { L"ASSET_KATEX_JS",         L"vendor\\katex.min.js" },
    { L"ASSET_MDIT_TEXMATH",     L"vendor\\markdown-it-texmath.min.js" },
    { L"ASSET_DOMPURIFY",        L"vendor\\purify.min.js" },
    { L"ASSET_FONT_KATEX_AMS_REGULAR",         L"vendor\\fonts\\KaTeX_AMS-Regular.woff2" },
    { L"ASSET_FONT_KATEX_CALIGRAPHIC_BOLD",    L"vendor\\fonts\\KaTeX_Caligraphic-Bold.woff2" },
    { L"ASSET_FONT_KATEX_CALIGRAPHIC_REGULAR", L"vendor\\fonts\\KaTeX_Caligraphic-Regular.woff2" },
    { L"ASSET_FONT_KATEX_FRAKTUR_BOLD",        L"vendor\\fonts\\KaTeX_Fraktur-Bold.woff2" },
    { L"ASSET_FONT_KATEX_FRAKTUR_REGULAR",     L"vendor\\fonts\\KaTeX_Fraktur-Regular.woff2" },
    { L"ASSET_FONT_KATEX_MAIN_BOLD",           L"vendor\\fonts\\KaTeX_Main-Bold.woff2" },
    { L"ASSET_FONT_KATEX_MAIN_BOLDITALIC",     L"vendor\\fonts\\KaTeX_Main-BoldItalic.woff2" },
    { L"ASSET_FONT_KATEX_MAIN_ITALIC",         L"vendor\\fonts\\KaTeX_Main-Italic.woff2" },
    { L"ASSET_FONT_KATEX_MAIN_REGULAR",        L"vendor\\fonts\\KaTeX_Main-Regular.woff2" },
    { L"ASSET_FONT_KATEX_MATH_BOLDITALIC",     L"vendor\\fonts\\KaTeX_Math-BoldItalic.woff2" },
    { L"ASSET_FONT_KATEX_MATH_ITALIC",         L"vendor\\fonts\\KaTeX_Math-Italic.woff2" },
    { L"ASSET_FONT_KATEX_SANSSERIF_BOLD",      L"vendor\\fonts\\KaTeX_SansSerif-Bold.woff2" },
    { L"ASSET_FONT_KATEX_SANSSERIF_ITALIC",    L"vendor\\fonts\\KaTeX_SansSerif-Italic.woff2" },
    { L"ASSET_FONT_KATEX_SANSSERIF_REGULAR",   L"vendor\\fonts\\KaTeX_SansSerif-Regular.woff2" },
    { L"ASSET_FONT_KATEX_SCRIPT_REGULAR",      L"vendor\\fonts\\KaTeX_Script-Regular.woff2" },
    { L"ASSET_FONT_KATEX_SIZE1_REGULAR",       L"vendor\\fonts\\KaTeX_Size1-Regular.woff2" },
    { L"ASSET_FONT_KATEX_SIZE2_REGULAR",       L"vendor\\fonts\\KaTeX_Size2-Regular.woff2" },
    { L"ASSET_FONT_KATEX_SIZE3_REGULAR",       L"vendor\\fonts\\KaTeX_Size3-Regular.woff2" },
    { L"ASSET_FONT_KATEX_SIZE4_REGULAR",       L"vendor\\fonts\\KaTeX_Size4-Regular.woff2" },
    { L"ASSET_FONT_KATEX_TYPEWRITER_REGULAR",  L"vendor\\fonts\\KaTeX_Typewriter-Regular.woff2" },
};

inline std::wstring assets_dir() {
    return fileio::local_app_dir() + L"\\assets";
}

// Extract all embedded assets. Returns false if any file could not be written.
inline bool extract_all() {
    std::wstring dir = assets_dir();
    if (!fileio::ensure_dir(dir + L"\\vendor")) return false;
    if (!fileio::ensure_dir(dir + L"\\vendor\\fonts")) return false;
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
