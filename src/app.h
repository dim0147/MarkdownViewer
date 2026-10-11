#pragma once
// app.h - application logic tying everything together: owns the WebViewHost,
// tracks the current document, and drives the renderer (assets/app.js) via
// JSON messages:
//
//   { type: "config",  config: "<raw config.json text>" }
//   { type: "render",  name: "file.md", path: "<full path>", markdown: "<source>" }
//   { type: "welcome" }
//   { type: "recent",  files: [{name: "file.md", path: "<full path>"}, ...] }
//   { type: "nav",     canBack: <bool>, canForward: <bool> }
//   { type: "files",   files: ["a.md", ...], current: <index | -1> }
//   { type: "edit" }                    - toggle the document editor (Ctrl+E)
//   { type: "save" }                    - ask the editor to post its text back
//   { type: "saved",   ok: <bool> }     - result of writing that text to disk
//
// app.js answers with the string "ready" once loaded ("back"/"forward" from the
// history toolbar; "openfile:<i>"/"reloadfiles" from the file sidebar;
// "openrecent:<i>" from the welcome screen;
// "savedoc:<text>"/"nosave"/"dirty:<0|1>" from the editor — every {type:"save"}
// gets exactly one "savedoc:" or "nosave" answer, which is what completes a
// deferred m_afterSave action); any file opened before "ready" is queued in
// m_current and rendered from OnWebReady().

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
#include "viewerinstance.h"

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
        m_web.onOpenSibling   = [this](int i) { OpenSibling(i); };
        m_web.onOpenRecent    = [this](int i) {
            if (!m_current.empty() || i < 0 || (size_t)i >= m_welcomeRecent.size()) return;
            OpenRecentPath(m_welcomeRecent[i]);
        };
        m_web.onRefreshFiles  = [this]() { SendFiles(); };
        m_web.onSaveDoc    = [this](const std::wstring& text) { OnSaveDoc(text); };
        m_web.onNoSave     = [this]() { OnNoSave(); };
        m_web.onDirty      = [this](bool d) { OnDirty(d); };
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
    // Open Recent, and clicks on in-document .md links. Unsaved editor changes
    // are resolved first (save / discard / cancel) — but never inline: this can
    // be called from an OLE drop callback or a WebView2 event handler, where a
    // modal MessageBox blocks the drag source / risks re-entrancy, so the
    // prompt is deferred through the message queue (WM_APP_OPEN_PENDING).
    void OpenFile(const std::wstring& path) {
        // Reopening never reloads or prompts to discard the existing editor.
        if (IsViewing(path)) { viewerinstance::focus(m_hwnd); Focus(); return; }
        if (viewerinstance::focus_existing(path, m_hwnd)) return;
        if (m_docDirty) {
            m_pendingOpen = path;
            PostMessageW(m_hwnd, cfg::WM_APP_OPEN_PENDING, 0, 0);
            return;
        }
        DoOpenFile(path);
    }

    bool IsViewing(const std::wstring& path) const {
        return viewerinstance::same_path(m_current, path);
    }

    // WM_APP_OPEN_PENDING: run the save/discard/cancel prompt from the message
    // loop (a safe context) and open the file the prompt allows through.
    void OnPendingOpen() {
        std::wstring path;
        path.swap(m_pendingOpen);
        if (path.empty()) return;
        ConfirmLoseChanges([this, path]() { DoOpenFile(path); });
    }

    // Alt+Left / the toolbar's Back button: revisit the previous document.
    void GoBack() {
        if (m_historyIndex <= 0) return;
        ConfirmLoseChanges([this]() {
            if (m_historyIndex > 0) { --m_historyIndex; Show(m_history[m_historyIndex]); }
        });
    }

    // Alt+Right / the toolbar's Forward button: revisit a document we came back
    // from.
    void GoForward() {
        if (m_historyIndex + 1 >= (int)m_history.size()) return;
        ConfirmLoseChanges([this]() {
            if (m_historyIndex + 1 < (int)m_history.size()) { ++m_historyIndex; Show(m_history[m_historyIndex]); }
        });
    }

    // Ctrl+E / File > Edit Document: toggle the renderer's editor pane for the
    // open document (the editor lives entirely in app.js; this just flips it).
    void ToggleEdit() {
        if (m_ready && !m_current.empty()) m_web.PostJson(L"{\"type\":\"edit\"}");
    }

    // Ctrl+S / File > Save: ask the editor to post its current text back
    // ("savedoc:<text>"), which OnSaveDoc() writes to disk. A no-op in app.js
    // when the editor is closed or the text is unchanged.
    void RequestSave() {
        if (m_ready) m_web.PostJson(L"{\"type\":\"save\"}");
    }

    // WM_CLOSE: quit, but let unsaved editor changes be saved/discarded first
    // (Cancel keeps the window open).
    void RequestClose() {
        ConfirmLoseChanges([this]() { DestroyWindow(m_hwnd); });
    }

    // WM_QUERYENDSESSION: logoff/shutdown bypasses WM_CLOSE, so unsaved edits
    // would be destroyed with no prompt. Block the session end the sanctioned
    // way (a shutdown reason Windows shows the user) instead of popping a
    // modal dialog into the shutdown sequence.
    bool IsDirty() const { return m_docDirty; }

    // File sidebar click: open the index-th file from the list we last sent the
    // renderer. We open by index into our own list (never a path supplied by the
    // web side) so a click can only ever open a file we already enumerated; we
    // re-check existence in case it was deleted since, refreshing the list if so.
    void OpenSibling(int index) {
        if (index < 0 || (size_t)index >= m_siblingFiles.size()) return;
        std::wstring path = m_siblingFiles[index];
        if (fileio::file_exists(path)) OpenFile(path);
        else                          SendFiles();   // stale entry: rebuild the sidebar
    }

    // The File > Open Recent submenu (owned by main.cpp). Rebuilt whenever the
    // MRU list changes so it always reflects recent.txt.
    void SetRecentMenu(HMENU h) { m_recentMenu = h; RefreshRecentMenu(); }

    void RefreshRecentMenu() {
        if (m_ready && m_current.empty()) SendRecent();
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
        OpenRecentPath(list[i]);
    }

    void OpenRecentPath(std::wstring path) {
        if (fileio::file_exists(path)) {
            OpenFile(path);                    // re-adds, promoting it to front
        } else {
            std::vector<std::wstring> list = recent::load();
            for (size_t i = 0; i < list.size();) {
                if (viewerinstance::same_path(list[i], path)) list.erase(list.begin() + i);
                else ++i;
            }
            recent::save(list);
            RefreshRecentMenu();
            MessageBoxW(m_hwnd, (L"This file no longer exists:\n" + path).c_str(),
                        cfg::kAppTitle, MB_OK | MB_ICONWARNING);
        }
    }

    void ClearRecent() {
        recent::clear();
        RefreshRecentMenu();
    }

    void ShowWelcome() {
        m_current.clear();
        m_externallyChanged = false;
        m_docDirty = false;
        m_sig = fileio::FileSig{};
        SetWindowTextW(m_hwnd, cfg::kAppTitle);
        if (m_ready) { SendWelcome(); SendFiles(); }
    }

    // F5 / File > Reload: re-read config.json (so settings edits apply live)
    // and the document. SendRender() re-captures the on-disk signature and
    // clears any pending "changed on disk" flag, so this also dismisses the
    // '*' title marker. The config re-read is unconditional (it never touches
    // the editor's text); only the document reload — which replaces that text
    // — waits on the unsaved-changes prompt.
    void Reload() {
        if (!m_ready) return;
        SendConfig();
        ConfirmLoseChanges([this]() {
            if (!m_current.empty()) SendRender();
            else                    SendWelcome();
            SendFiles();           // pick up files added/removed in the folder
        });
    }

    // WM_TIMER: has the open document changed on disk since we rendered it? If
    // so, mark the title with '*' and offer to reload. Called ~once a second.
    void CheckFileChanged() {
        if (!m_ready || m_current.empty() || m_changePromptOpen) return;
        fileio::FileSig now = fileio::file_signature(m_current);
        if (!now.valid) return;            // missing or mid-write — ignore this tick
        if (!m_sig.valid) { m_sig = now; return; }
        if (now == m_sig) return;          // unchanged
        // While the user has unsaved edits, offering "reload?" would throw
        // them away — just flag the conflict in the title. m_sig is left
        // pointing at the version we read, so OnSaveDoc() still detects the
        // external change and asks before overwriting it.
        if (m_docDirty) {
            m_externallyChanged = true;
            UpdateTitle();
            return;
        }
        // Adopt the new signature up front so we prompt exactly once per change
        // (even if the user dismisses the dialog, the next tick stays quiet).
        m_sig = now;
        m_externallyChanged = true;
        UpdateTitle();
        std::wstring dir, name;
        fileio::split_path(m_current, dir, name);
        m_changePromptOpen = true;
        int r = MessageBoxW(m_hwnd,
                            (name + L" has changed on disk.\n\nReload it now?").c_str(),
                            cfg::kAppTitle, MB_YESNO | MB_ICONQUESTION);
        m_changePromptOpen = false;
        if (r == IDYES) Reload();          // SendRender() clears the '*' marker
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

    // External-change watcher (driven by WM_TIMER -> CheckFileChanged): the
    // on-disk fingerprint captured when we last rendered m_current, whether an
    // unreloaded change has been seen (drives the '*' title marker), and a guard
    // so the modal reload prompt is never stacked on itself.
    fileio::FileSig m_sig;
    bool            m_externallyChanged = false;
    bool            m_changePromptOpen  = false;

    // Document editor state: the on-disk format captured when m_current was
    // read (so a save writes the same encoding/EOLs back), whether the editor
    // holds unsaved changes (mirrors app.js's "dirty:" messages; gates
    // navigation/close and drives the '*' title marker), an action to run once
    // a requested save round-trip completes (e.g. "save, then open the next
    // file"), a guard so lose-changes prompts never nest (MessageBox pumps the
    // queue, so a drop/menu command can arrive mid-prompt), and the path a
    // dirty-time OpenFile parked for WM_APP_OPEN_PENDING.
    fileio::TextFormat    m_fmt;
    bool                  m_docDirty = false;
    std::function<void()> m_afterSave;
    bool                  m_confirmOpen = false;
    std::wstring          m_pendingOpen;

    // The non-gated tail of OpenFile (see OpenFile for the deferral story).
    void DoOpenFile(const std::wstring& path) {
        const std::wstring full = fileio::full_path(path);
        recent::add(full);
        RefreshRecentMenu();
        PushHistory(full);
        Show(full);
    }

    // Back/forward history: visited file paths oldest->newest, with m_historyIndex
    // pointing at the current document. Opening a file truncates any forward
    // entries (browser semantics); the renderer's scroll memory is keyed off the
    // path we send, so Back/Forward restores where the user was in each doc.
    std::vector<std::wstring> m_history;
    int                       m_historyIndex = -1;

    // Full paths of the markdown files in the current document's folder, in the
    // same order last sent to the renderer's sidebar. The renderer references
    // them by index ("openfile:<i>") so it can never name an arbitrary path.
    std::vector<std::wstring> m_siblingFiles;
    // Snapshot last shown on the homepage: another window can reorder the MRU
    // on disk, but a click must still open the file the user actually selected.
    std::vector<std::wstring> m_welcomeRecent;

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
        m_externallyChanged = false;   // a fresh display reflects the file on disk
        UpdateTitle();
        if (m_ready) { SendRender(); SendNavState(); SendFiles(); }
    }

    // Window title: "[*]name - Markdown Viewer", where the leading '*' flags
    // unsaved editor changes or a document that has changed on disk but not
    // yet been reloaded.
    void UpdateTitle() {
        if (m_current.empty()) { SetWindowTextW(m_hwnd, cfg::kAppTitle); return; }
        std::wstring dir, name;
        fileio::split_path(m_current, dir, name);
        std::wstring title = (m_docDirty || m_externallyChanged ? L"*" : L"") + name +
                             L" - " + cfg::kAppTitle;
        SetWindowTextW(m_hwnd, title.c_str());
    }

    // Run `then` immediately unless the editor has unsaved changes, in which
    // case ask: Yes saves first (then runs after the save round-trip through
    // app.js completes), No discards, Cancel drops the action. While a prompt
    // is up or a save round-trip is pending, further requests are dropped —
    // MessageBox pumps the message queue, so without the guard a second drop
    // or menu command would nest a second prompt and overwrite m_afterSave.
    void ConfirmLoseChanges(std::function<void()> then) {
        if (!m_docDirty) { then(); return; }
        if (m_confirmOpen || m_afterSave) return;
        std::wstring dir, name;
        fileio::split_path(m_current, dir, name);
        m_confirmOpen = true;
        int r = MessageBoxW(m_hwnd, (L"Save changes to " + name + L"?").c_str(),
                            cfg::kAppTitle, MB_YESNOCANCEL | MB_ICONWARNING);
        m_confirmOpen = false;
        if (r == IDYES) {
            m_afterSave = std::move(then);
            RequestSave();         // answered by exactly one "savedoc:" or "nosave"
        } else if (r == IDNO) {
            m_docDirty = false;    // app.js resets its own state on the next render
            UpdateTitle();
            then();
        }
    }

    // app.js -> host "dirty:" message: the editor gained/lost unsaved changes.
    void OnDirty(bool dirty) {
        m_docDirty = dirty;
        UpdateTitle();
    }

    // app.js -> host "savedoc:" message: write the editor's text back to the
    // document. The destination is always m_current — the file the host itself
    // opened — so web content can never direct a write anywhere else. Any
    // deferred action is taken out of its slot up front (single clearing
    // point) and runs only if the write actually happens.
    void OnSaveDoc(const std::wstring& text) {
        std::function<void()> after = std::move(m_afterSave);
        m_afterSave = nullptr;
        if (m_current.empty()) return;
        // The file changed on disk since we read it: overwriting would lose
        // someone else's edits. m_externallyChanged covers the case where the
        // watcher's reload prompt was declined (it adopts the new signature to
        // avoid re-prompting, so the sig comparison alone would miss it).
        fileio::FileSig now = fileio::file_signature(m_current);
        if ((now.valid && m_sig.valid && now != m_sig) || m_externallyChanged) {
            std::wstring dir, name;
            fileio::split_path(m_current, dir, name);
            if (MessageBoxW(m_hwnd,
                            (name + L" has changed on disk since you started editing.\n\n"
                             L"Overwrite it with your version?").c_str(),
                            cfg::kAppTitle, MB_YESNO | MB_ICONWARNING) != IDYES)
                return;
        }
        if (fileio::write_file_text(m_current, fileio::narrow(text), m_fmt)) {
            m_sig = fileio::file_signature(m_current);
            m_docDirty = false;
            m_externallyChanged = false;
            UpdateTitle();
            m_web.PostJson(L"{\"type\":\"saved\",\"ok\":true}");
            if (after) after();
        } else {
            m_web.PostJson(L"{\"type\":\"saved\",\"ok\":false}");
            MessageBoxW(m_hwnd, (L"Could not save the file:\n" + m_current).c_str(),
                        cfg::kAppTitle, MB_OK | MB_ICONERROR);
        }
    }

    // app.js -> host "nosave": the editor answered a save request with
    // "nothing to write" (it was closed, or the text matches the file again).
    // Whatever waited on the save can proceed — the document is not dirty.
    void OnNoSave() {
        std::function<void()> after = std::move(m_afterSave);
        m_afterSave = nullptr;
        m_docDirty = false;
        UpdateTitle();
        if (after) after();
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
        SendFiles();               // populate the file sidebar
    }

    // Tell the renderer whether Back/Forward are currently possible so it can
    // enable/disable (and show/hide) the history toolbar.
    void SendNavState() {
        if (!m_ready) return;
        bool back = m_historyIndex > 0;
        bool fwd  = m_historyIndex + 1 < (int)m_history.size();
        std::string json = std::string("{\"type\":\"nav\",\"canBack\":") +
                           (back ? "true" : "false") + ",\"canForward\":" +
                           (fwd ? "true" : "false") + "}";
        m_web.PostJson(fileio::widen(json));
    }

    // Scan the open document's folder for sibling markdown files and push the
    // list (plus the index of the current file) to the renderer's sidebar. The
    // renderer shows the sidebar only when there is more than one entry. On the
    // welcome screen (no document) an empty list is sent so the sidebar hides.
    void SendFiles() {
        if (!m_ready) return;
        m_siblingFiles.clear();
        if (m_current.empty()) {
            m_web.PostJson(L"{\"type\":\"files\",\"files\":[],\"current\":-1}");
            return;
        }
        std::wstring dir, name;
        fileio::split_path(m_current, dir, name);
        std::vector<std::wstring> names = fileio::list_markdown_files(dir);
        std::wstring curFull = fileio::full_path(m_current);
        int current = -1;
        std::string arr;
        for (size_t i = 0; i < names.size(); ++i) {
            m_siblingFiles.push_back(dir + L"\\" + names[i]);
            if (current < 0 &&
                lstrcmpiW(fileio::full_path(m_siblingFiles.back()).c_str(), curFull.c_str()) == 0)
                current = (int)i;
            if (!arr.empty()) arr += ",";
            arr += "\"" + fileio::json_escape(fileio::narrow(names[i])) + "\"";
        }
        std::string json = "{\"type\":\"files\",\"files\":[" + arr +
                           "],\"current\":" + std::to_string(current) + "}";
        m_web.PostJson(fileio::widen(json));
    }

    void SendConfig() {
        std::string json = "{\"type\":\"config\",\"config\":\"" +
                           fileio::json_escape(settings::load_or_create()) + "\"}";
        m_web.PostJson(fileio::widen(json));
    }

    void SendWelcome() {
        m_web.PostJson(L"{\"type\":\"welcome\"}");
        SendRecent();
    }

    void SendRecent() {
        if (!m_ready || !m_current.empty()) return;
        m_welcomeRecent = recent::load();
        std::string arr;
        for (const auto& path : m_welcomeRecent) {
            std::wstring dir, name;
            fileio::split_path(path, dir, name);
            if (!arr.empty()) arr += ",";
            arr += "{\"name\":\"" + fileio::json_escape(fileio::narrow(name)) +
                   "\",\"path\":\"" + fileio::json_escape(fileio::narrow(path)) + "\"}";
        }
        m_web.PostJson(fileio::widen("{\"type\":\"recent\",\"files\":[" + arr + "]}"));
    }

    void SendRender() {
        std::string text;
        if (!fileio::read_file_text(m_current, text, &m_fmt)) {
            MessageBoxW(m_hwnd, (L"Could not open file:\n" + m_current).c_str(), cfg::kAppTitle,
                        MB_OK | MB_ICONERROR);
            // Fall back to a consistent empty state instead of returning with
            // m_current pointing at the unreadable file while the renderer
            // still shows (and may be editing) the previous document — in that
            // split state a later Ctrl+S would write the OLD document's text
            // to the NEW path.
            ShowWelcome();
            return;
        }
        // The displayed content now matches what's on disk: capture its
        // fingerprint and drop any pending "changed" marker (UpdateTitle clears
        // the leading '*' that CheckFileChanged may have added).
        m_sig = fileio::file_signature(m_current);
        m_externallyChanged = false;
        UpdateTitle();
        std::wstring dir, name;
        fileio::split_path(m_current, dir, name);
        HRESULT mapped = m_web.SetDocDir(dir);
        if (FAILED(mapped)) {
            MessageBoxW(m_hwnd, L"Could not map the document folder.", cfg::kAppTitle,
                        MB_OK | MB_ICONERROR);
            ShowWelcome();
            return;
        }
        if (mapped == S_OK) {
            m_ready = false;
            if (FAILED(m_web.ReloadPage())) {
                m_ready = true;
                MessageBoxW(m_hwnd, L"Could not reload the viewer page.", cfg::kAppTitle,
                            MB_OK | MB_ICONERROR);
                ShowWelcome();
            }
            return; // OnWebReady re-sends config, document, history and sidebar.
        }
        // `path` is the renderer's stable key for per-document scroll memory
        // (file names alone collide across folders); `name` is the display title.
        std::string json = "{\"type\":\"render\",\"name\":\"" +
                           fileio::json_escape(fileio::narrow(name)) +
                           "\",\"path\":\"" + fileio::json_escape(fileio::narrow(m_current)) +
                           "\",\"markdown\":\"" + fileio::json_escape(text) + "\"}";
        m_web.PostJson(fileio::widen(json));
    }
};
