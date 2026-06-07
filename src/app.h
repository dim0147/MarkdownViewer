#pragma once
// app.h - application logic tying everything together: owns the WebViewHost,
// tracks the current document, and drives the renderer (assets/app.js) via
// JSON messages:
//
//   { type: "config",  config: "<raw config.json text>" }
//   { type: "render",  name: "file.md", markdown: "<source text>" }
//   { type: "welcome" }
//
// app.js answers with the string "ready" once loaded; any file opened before
// that is queued in m_current and rendered from OnWebReady().

#include <windows.h>
#include <commdlg.h>
#include <string>
#include "config.h"
#include "fileio.h"
#include "settings.h"
#include "assets.h"
#include "webview2host.h"

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
        m_web.onReady    = [this]() { OnWebReady(); };
        m_web.onOpenFile = [this](const std::wstring& path) { OpenFile(path); };
        m_web.onFailed   = [this](const std::wstring& msg) {
            MessageBoxW(m_hwnd, msg.c_str(), cfg::kAppTitle, MB_OK | MB_ICONERROR);
        };
        return m_web.Create(hwnd, assets::assets_dir(),
                            fileio::local_app_dir() + L"\\WebView2Data");
    }

    void Shutdown()  { m_web.Destroy(); }
    void Resize()    { m_web.Resize(); }
    void Focus()     { m_web.Focus(); }
    void Moved()     { m_web.NotifyMoved(); }

    void OpenFile(const std::wstring& path) {
        m_current = path;
        std::wstring dir, name;
        fileio::split_path(path, dir, name);
        SetWindowTextW(m_hwnd, (name + L" - " + cfg::kAppTitle).c_str());
        if (m_ready) SendRender();
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
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
        if (GetOpenFileNameW(&ofn)) OpenFile(file);
    }

    void OpenConfigFile() {
        settings::load_or_create();
        ShellExecuteW(m_hwnd, L"open", L"notepad.exe",
                      (L"\"" + settings::config_path() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    }

private:
    HWND        m_hwnd = nullptr;
    WebViewHost m_web;
    std::wstring m_current;        // currently displayed file (empty = welcome)
    bool         m_ready = false;  // app.js is loaded and listening

    void OnWebReady() {
        m_ready = true;            // also fires again if the user reloads the page
        SendConfig();
        if (!m_current.empty()) SendRender();
        else                    SendWelcome();
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
        std::string json = "{\"type\":\"render\",\"name\":\"" +
                           fileio::json_escape(fileio::narrow(name)) +
                           "\",\"markdown\":\"" + fileio::json_escape(text) + "\"}";
        m_web.PostJson(fileio::widen(json));
    }
};
