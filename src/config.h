#pragma once
// config.h - all compile-time application configuration in one place.
// Runtime (user-editable) settings live in %APPDATA%\MarkdownViewer\config.json
// and are handled by settings.h / assets/app.js.

#include <windows.h>

namespace cfg {

constexpr wchar_t kAppTitle[]    = L"Markdown Viewer";
constexpr wchar_t kAppVersion[]  = L"2.5.0";
constexpr wchar_t kWindowClass[] = L"MarkdownViewerWnd";
constexpr wchar_t kAppDirName[]  = L"MarkdownViewer";   // folder name under %APPDATA% / %LOCALAPPDATA%

// File extensions registered for the Explorer context menu and the Open dialog.
constexpr const wchar_t* kMarkdownExts[] = { L".md", L".markdown", L".mdown", L".mkd" };

// Virtual hosts mapped inside WebView2 (see webview2host.h):
//   viewer.assets -> extracted UI assets (index.html, app.js, vendor libs)
//   viewer.doc    -> directory of the currently open document (relative links/images)
constexpr wchar_t kAssetsHost[] = L"viewer.assets";
constexpr wchar_t kDocHost[]    = L"viewer.doc";
constexpr wchar_t kIndexUrl[]   = L"https://viewer.assets/index.html";
constexpr wchar_t kDocBaseUrl[] = L"https://viewer.doc/";

// GitHub repository backing Help > Check for Updates (see src/update.h). The
// API request is the app's only outbound network call, made solely on user
// request; kReleasesPageUrl is opened in the default browser when an update
// is offered.
constexpr wchar_t kUpdateHost[]      = L"api.github.com";
constexpr wchar_t kUpdatePath[]      = L"/repos/dim0147/MarkdownViewer/releases/latest";
constexpr wchar_t kReleasesPageUrl[] = L"https://github.com/dim0147/MarkdownViewer/releases/latest";

constexpr long long kMaxFileBytes = 64LL * 1024 * 1024;   // refuse files larger than 64 MB

constexpr int kDefaultWindowW = 1060;
constexpr int kDefaultWindowH = 800;
constexpr int kMinWindowW     = 480;
constexpr int kMinWindowH     = 360;

// Most-recently-used file list (File > Open Recent), persisted to
// %APPDATA%\MarkdownViewer\recent.txt.
constexpr int kMaxRecent = 10;

// Menu / accelerator command IDs.
enum : UINT {
    ID_OPEN        = 101,
    ID_RELOAD      = 102,
    ID_EXIT        = 103,
    ID_INSTALL     = 201,
    ID_UNINSTALL   = 202,
    ID_OPENCONFIG  = 203,
    ID_SETTINGS    = 204,
    ID_ABOUT        = 301,
    ID_CHECK_UPDATE = 302,
    // Dynamic File > Open Recent entries occupy [ID_RECENT_BASE,
    // ID_RECENT_BASE + kMaxRecent). Keep ID_RECENT_CLEAR clear of that range.
    ID_RECENT_BASE  = 600,
    ID_RECENT_CLEAR = 700,
};

// Posted by update::check_async() (background thread) to the main window when an
// update check finishes; lParam is a heap-allocated update::Result* that the
// handler takes ownership of and must delete.
constexpr UINT WM_APP_UPDATE_RESULT = WM_APP + 1;

} // namespace cfg
