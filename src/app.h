#pragma once
// app.h - application logic tying everything together: owns the WebViewHost,
// tracks the current document, and drives the renderer (assets/app.js) via
// JSON messages:
//
//   { type: "config",  config: "<raw config.json text>" }
//   { type: "render",  name: "file.md", path: "<full path>", markdown: "<source>" }
//   { type: "welcome" }
//   { type: "nav",     canBack: <bool>, canForward: <bool> }
//
// app.js answers with the string "ready" once loaded (and "back"/"forward" from
// the history toolbar); any file opened before "ready" is queued in m_current
// and rendered from OnWebReady().

#include <windows.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include "config.h"
#include "fileio.h"
#include "settings.h"
#include "recent.h"
#include "assets.h"
#include "webview2host.h"
#include "update.h"

class App {
public:
    // Called from WM_CREATE. Extracts assets and starts WebView2 creation.
    bool Init(HWND hwnd) {
        m_hwnd = hwnd;
        if (!assets::extract_all()) {
            MessageBoxW(hwnd, L"Could not extract the viewer assets to %LOCALAPPDATA%.",
                        cfg::kAppTitle, MB_OK | MB_ICONERROR);
            return false;
        }
        m_web.onReady      = [this]() { OnWebReady(); };
        m_web.onOpenFile   = [this](const std::wstring& path) { OpenFile(path); };
        m_web.onSaveConfig = [this](const std::wstring& json) { OnSaveConfig(json); };
        m_web.onBack       = [this]() { GoBack(); };
        m_web.onForward    = [this]() { GoForward(); };
        m_web.onFailed     = [this](const std::wstring& msg) {
            MessageBoxW(m_hwnd, msg.c_str(), cfg::kAppTitle, MB_OK | MB_ICONERROR);
        };
        return m_web.Create(hwnd, assets::assets_dir(),
                            fileio::local_app_dir() + L"\\WebView2Data");
    }

    void Shutdown()  { m_web.Destroy(); }
    void Resize()    { m_web.Resize(); }
    void Focus()     { m_web.Focus(); }
    void Moved()     { m_web.NotifyMoved(); }

    // Open a file as a *fresh* navigation: it joins the MRU list and is pushed
    // onto the back/forward history (any forward entries are discarded, like a
    // web browser). Reached from the command line, drag & drop, File > Open,
    // Open Recent, and clicks on in-document .md links.
    void OpenFile(const std::wstring& path) {
        recent::add(path);
        RefreshRecentMenu();
        PushHistory(path);
        Show(path);
    }

    // Alt+Left / the toolbar's Back button: revisit the previous document.
    void GoBack() {
        if (m_historyIndex > 0) { --m_historyIndex; Show(m_history[m_historyIndex]); }
    }

    // Alt+Right / the toolbar's Forward button: revisit a document we came back
    // from.
    void GoForward() {
        if (m_historyIndex + 1 < (int)m_history.size()) { ++m_historyIndex; Show(m_history[m_historyIndex]); }
    }

    // The File > Open Recent submenu (owned by main.cpp). Rebuilt whenever the
    // MRU list changes so it always reflects recent.txt.
    void SetRecentMenu(HMENU h) { m_recentMenu = h; RefreshRecentMenu(); }

    void RefreshRecentMenu() {
        if (!m_recentMenu) return;
        while (GetMenuItemCount(m_recentMenu) > 0)
            RemoveMenu(m_recentMenu, 0, MF_BYPOSITION);
        std::vector<std::wstring> list = recent::load();
        if (list.empty()) {
            AppendMenuW(m_recentMenu, MF_STRING | MF_GRAYED, 0, L"(no recent files)");
            return;
        }
        for (size_t i = 0; i < list.size(); ++i)
            AppendMenuW(m_recentMenu, MF_STRING, cfg::ID_RECENT_BASE + (UINT)i,
                        RecentLabel((int)i, list[i]).c_str());
        AppendMenuW(m_recentMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m_recentMenu, MF_STRING, cfg::ID_RECENT_CLEAR, L"&Clear recent files");
    }

    // Open the i-th recent file. A file that has since been deleted/moved is
    // pruned from the list rather than throwing an error dialog at the user.
    void OpenRecent(int i) {
        std::vector<std::wstring> list = recent::load();
        if (i < 0 || (size_t)i >= list.size()) return;
        if (fileio::file_exists(list[i])) {
            OpenFile(list[i]);                 // re-adds, promoting it to front
        } else {
            std::wstring missing = list[i];
            list.erase(list.begin() + i);
            recent::save(list);
            RefreshRecentMenu();
            MessageBoxW(m_hwnd, (L"This file no longer exists:\n" + missing).c_str(),
                        cfg::kAppTitle, MB_OK | MB_ICONWARNING);
        }
    }

    void ClearRecent() {
        recent::clear();
        RefreshRecentMenu();
    }

    void ShowWelcome() {
        m_current.clear();
        SetWindowTextW(m_hwnd, cfg::kAppTitle);
        if (m_ready) SendWelcome();
    }

    // F5: re-read config.json (so settings edits apply live) and the document.
    void Reload() {
        if (!m_ready) return;
        SendConfig();
        if (!m_current.empty()) SendRender();
        else                    SendWelcome();
    }

    void OpenDialog() {
        wchar_t file[4096] = L"";
        OPENFILENAMEW ofn = { sizeof(ofn) };
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFilter = L"Markdown files (*.md;*.markdown;*.mdown;*.mkd)\0*.md;*.markdown;*.mdown;*.mkd\0"
                          L"All files (*.*)\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = 4096;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn)) OpenFile(file);
    }

    // Tools > Settings...: open the in-app settings panel (rendered by app.js).
    // Push the on-disk config first so the panel reflects external edits.
    void OpenSettings() {
        if (!m_ready) return;
        SendConfig();
        m_web.PostJson(L"{\"type\":\"settings\"}");
    }

    // Help > Check for Updates...: query the GitHub Releases API on a background
    // thread; OnUpdateResult() handles the answer back on the UI thread.
    void CheckForUpdates() {
        update::check_async(m_hwnd);
    }

    void OnUpdateResult(const update::Result& r) {
        switch (r.status) {
        case update::Status::UpToDate:
            MessageBoxW(m_hwnd,
                        (std::wstring(L"You're up to date.\n\nMarkdown Viewer ") +
                         cfg::kAppVersion + L" is the latest version.").c_str(),
                        cfg::kAppTitle, MB_OK | MB_ICONINFORMATION);
            break;
        case update::Status::Available: {
            std::wstring msg = L"A new version of Markdown Viewer is available.\n\n"
                               L"Installed: " + std::wstring(cfg::kAppVersion) + L"\n"
                               L"Latest: " + r.latest + L"\n\n"
                               L"Open the download page in your browser?";
            if (MessageBoxW(m_hwnd, msg.c_str(), cfg::kAppTitle,
                            MB_YESNO | MB_ICONINFORMATION) == IDYES)
                ShellExecuteW(m_hwnd, L"open", cfg::kReleasesPageUrl, nullptr, nullptr,
                              SW_SHOWNORMAL);
            break;
        }
        case update::Status::Error:
            MessageBoxW(m_hwnd, (L"Could not check for updates.\n\n" + r.message).c_str(),
                        cfg::kAppTitle, MB_OK | MB_ICONWARNING);
            break;
        }
    }

    void OpenConfigFile() {
        settings::load_or_create();
        // Full path: a bare "notepad.exe" would let ShellExecute search the
        // current directory, which can be attacker-controlled (doc's folder).
        wchar_t sysdir[MAX_PATH] = L"";
        GetSystemDirectoryW(sysdir, MAX_PATH);
        ShellExecuteW(m_hwnd, L"open", (std::wstring(sysdir) + L"\\notepad.exe").c_str(),
                      (L"\"" + settings::config_path() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    }

private:
    HWND        m_hwnd = nullptr;
    HMENU       m_recentMenu = nullptr;   // File > Open Recent popup (owned by main.cpp)
    WebViewHost m_web;
    std::wstring m_current;        // currently displayed file (empty = welcome)
    bool         m_ready = false;  // app.js is loaded and listening

    // Back/forward history: visited file paths oldest->newest, with m_historyIndex
    // pointing at the current document. Opening a file truncates any forward
    // entries (browser semantics); the renderer's scroll memory is keyed off the
    // path we send, so Back/Forward restores where the user was in each doc.
    std::vector<std::wstring> m_history;
    int                       m_historyIndex = -1;

    // Append `path` as the new current document, dropping any forward history.
    // A no-op when it equals the current entry (e.g. Open Recent on the open
    // file) so we never stack duplicates.
    void PushHistory(const std::wstring& path) {
        if (m_historyIndex >= 0 && m_history[m_historyIndex] == path) return;
        if (m_historyIndex + 1 < (int)m_history.size())
            m_history.erase(m_history.begin() + m_historyIndex + 1, m_history.end());
        m_history.push_back(path);
        m_historyIndex = (int)m_history.size() - 1;
    }

    // Display `path` (already in m_history): update window title and, once the
    // renderer is up, push the document and the back/forward button state.
    void Show(const std::wstring& path) {
        m_current = path;
        std::wstring dir, name;
        fileio::split_path(path, dir, name);
        SetWindowTextW(m_hwnd, (name + L" - " + cfg::kAppTitle).c_str());
        if (m_ready) { SendRender(); SendNavState(); }
    }

    // Build a menu label for the i-th recent file: an "&1".."&9","&0" mnemonic
    // plus a path compacted to fit, with literal '&' doubled so it shows.
    static std::wstring RecentLabel(int i, const std::wstring& path) {
        wchar_t buf[MAX_PATH] = L"";
        std::wstring shown = PathCompactPathExW(buf, path.c_str(), 64, 0) ? std::wstring(buf) : path;
        wchar_t mn = (i < 9) ? (wchar_t)(L'1' + i) : L'0';
        std::wstring label = L"&";
        label += mn;
        label += L"  ";
        for (wchar_t c : shown) { if (c == L'&') label += L'&'; label += c; }
        return label;
    }

    // app.js -> host "save:" message: persist the panel's JSON verbatim. The
    // C++ side keeps treating config as an opaque string (see settings.h).
    void OnSaveConfig(const std::wstring& jsonText) {
        std::string u8 = fileio::narrow(jsonText);
        fileio::ensure_dir(fileio::roaming_app_dir());
        fileio::write_file_bytes(settings::config_path(), u8.data(), u8.size());
    }

    void OnWebReady() {
        m_ready = true;            // also fires again if the user reloads the page
        SendConfig();
        if (!m_current.empty()) SendRender();
        else                    SendWelcome();
        SendNavState();            // restore the toolbar after a page (re)load
    }

    // Tell the renderer whether Back/Forward are currently possible so it can
    // enable/disable (and show/hide) the history toolbar.
    void SendNavState() {
        bool back = m_historyIndex > 0;
        bool fwd  = m_historyIndex + 1 < (int)m_history.size();
        std::string json = std::string("{\"type\":\"nav\",\"canBack\":") +
                           (back ? "true" : "false") + ",\"canForward\":" +
                           (fwd ? "true" : "false") + "}";
        m_web.PostJson(fileio::widen(json));
    }

    void SendConfig() {
        std::string json = "{\"type\":\"config\",\"config\":\"" +
                           fileio::json_escape(settings::load_or_create()) + "\"}";
        m_web.PostJson(fileio::widen(json));
    }

    void SendWelcome() {
        m_web.PostJson(L"{\"type\":\"welcome\"}");
    }

    void SendRender() {
        std::string text;
        if (!fileio::read_file_text(m_current, text)) {
            MessageBoxW(m_hwnd, (L"Could not open file:\n" + m_current).c_str(), cfg::kAppTitle,
                        MB_OK | MB_ICONERROR);
            return;
        }
        std::wstring dir, name;
        fileio::split_path(m_current, dir, name);
        m_web.SetDocDir(dir);
        // `path` is the renderer's stable key for per-document scroll memory
        // (file names alone collide across folders); `name` is the display title.
        std::string json = "{\"type\":\"render\",\"name\":\"" +
                           fileio::json_escape(fileio::narrow(name)) +
                           "\",\"path\":\"" + fileio::json_escape(fileio::narrow(m_current)) +
                           "\",\"markdown\":\"" + fileio::json_escape(text) + "\"}";
        m_web.PostJson(fileio::widen(json));
    }
};
