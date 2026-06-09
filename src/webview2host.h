#pragma once
// webview2host.h - hosts a WebView2 (Edge/Chromium) control inside a Win32
// window, without WRL/wil dependencies (a small ComHandler template stands in
// for wrl::Callback).
//
// Responsibilities:
//   * async creation of the WebView2 environment + controller
//   * virtual host mappings:
//       viewer.assets -> extracted UI assets (fixed for the process lifetime)
//       viewer.doc    -> directory of the current document (remapped per file)
//   * navigation policy: the webview may only display index.html; clicks on
//     .md links open in the viewer, external links open in the default browser
//   * forwarding Ctrl+O / F5 pressed inside the webview to the main window
//   * JSON messaging with assets/app.js (PostWebMessageAsJson / "ready")

#include <windows.h>
#include <shellapi.h>
#include <ole2.h>
#include <string>
#include <functional>
#include <WebView2.h>
#include "config.h"
#include "fileio.h"

// ---------------------------------------------------------------------------
// Generic COM callback: implements a single-method (Invoke) WebView2 handler
// interface by delegating to a std::function. Self-deletes on final Release.
// ---------------------------------------------------------------------------
template <typename TInterface, typename TArg1, typename TArg2>
class ComHandler : public TInterface {
    LONG m_ref = 1;
    std::function<HRESULT(TArg1, TArg2)> m_fn;
public:
    explicit ComHandler(std::function<HRESULT(TArg1, TArg2)> fn) : m_fn(std::move(fn)) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(TInterface)) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP Invoke(TArg1 a, TArg2 b) override { return m_fn(a, b); }
};

// ---------------------------------------------------------------------------
// FileDropTarget - an OLE drop target that accepts external file drops
// (CF_HDROP) and hands the first dropped path to a callback.
//
// Why this exists: with put_AllowExternalDrop(FALSE) the WebView2 runtime
// registers its OWN drop target on its inner render window that *rejects*
// external drops (the "no-drop" cursor) - it does NOT let the drop fall
// through to the host frame's WM_DROPFILES. So WebViewHost revokes that target
// and registers this one across the WebView2 child windows, restoring "drop a
// .md file anywhere on the page to open it". Self-deletes on final Release.
// ---------------------------------------------------------------------------
class FileDropTarget : public IDropTarget {
    LONG m_ref = 1;
    bool m_hasFiles = false;
    std::function<void(const std::wstring&)> m_onDrop;

    static bool data_has_files(IDataObject* data) {
        FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        return data && data->QueryGetData(&fmt) == S_OK;
    }
    static std::wstring first_file(IDataObject* data) {
        std::wstring out;
        FORMATETC fmt = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM stg = {};
        if (data && SUCCEEDED(data->GetData(&fmt, &stg))) {
            if (HDROP drop = (HDROP)GlobalLock(stg.hGlobal)) {
                wchar_t path[2048] = L"";
                if (DragQueryFileW(drop, 0, path, 2047)) out = path;
                GlobalUnlock(stg.hGlobal);
            }
            ReleaseStgMedium(&stg);
        }
        return out;
    }
public:
    explicit FileDropTarget(std::function<void(const std::wstring&)> onDrop)
        : m_onDrop(std::move(onDrop)) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG r = InterlockedDecrement(&m_ref);
        if (!r) delete this;
        return r;
    }

    STDMETHODIMP DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        m_hasFiles = data_has_files(data);
        *effect = m_hasFiles ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    STDMETHODIMP DragOver(DWORD, POINTL, DWORD* effect) override {
        *effect = m_hasFiles ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    STDMETHODIMP DragLeave() override { m_hasFiles = false; return S_OK; }
    STDMETHODIMP Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        std::wstring path = first_file(data);
        *effect = path.empty() ? DROPEFFECT_NONE : DROPEFFECT_COPY;
        m_hasFiles = false;
        if (!path.empty() && m_onDrop) m_onDrop(path);
        return S_OK;
    }
};

class WebViewHost {
public:
    // All callbacks are invoked on the UI thread.
    std::function<void()>                    onReady;      // app.js loaded and said "ready"
    std::function<void(const std::wstring&)> onOpenFile;   // user activated a link to a markdown file
    std::function<void(const std::wstring&)> onSaveConfig; // settings panel asked to persist config.json
    std::function<void()>                    onBack;       // history toolbar: go back
    std::function<void()>                    onForward;    // history toolbar: go forward
    std::function<void(const std::wstring&)> onFailed;     // creation failed (message for the user)

    // Begin async creation. The control appears in `hwnd`'s client area.
    bool Create(HWND hwnd, const std::wstring& assetsDir, const std::wstring& userDataDir) {
        m_hwnd = hwnd;
        m_assetsDir = assetsDir;
        using EnvHandler  = ComHandler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                                       HRESULT, ICoreWebView2Environment*>;
        using CtrlHandler = ComHandler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                                       HRESULT, ICoreWebView2Controller*>;
        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, userDataDir.c_str(), nullptr,
            new EnvHandler([this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) { Fail(L"Could not start the WebView2 runtime."); return S_OK; }
                env->CreateCoreWebView2Controller(m_hwnd,
                    new CtrlHandler([this](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                        if (FAILED(hr) || !ctrl) { Fail(L"Could not create the WebView2 control."); return S_OK; }
                        m_controller = ctrl;
                        m_controller->AddRef();
                        m_controller->get_CoreWebView2(&m_webview);
                        Setup();
                        Resize();
                        m_webview->Navigate(cfg::kIndexUrl);
                        return S_OK;
                    }));
                return S_OK;
            }));
        if (FAILED(hr)) {
            Fail(L"WebView2 runtime not found.\n\nInstall the \"WebView2 Runtime\" from Microsoft "
                 L"(preinstalled on Windows 11 / recent Windows 10).");
            return false;
        }
        return true;
    }

    void Destroy() {
        if (m_dropTarget) {
            revoke_drop_subtree(m_hwnd);   // release the refs RegisterDragDrop took
            m_dropTarget->Release();
            m_dropTarget = nullptr;
        }
        if (m_webview3)   { m_webview3->Release();   m_webview3 = nullptr; }
        if (m_webview)    { m_webview->Release();    m_webview = nullptr; }
        if (m_controller) { m_controller->Close(); m_controller->Release(); m_controller = nullptr; }
    }

    void Resize() {
        if (!m_controller) return;
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        m_controller->put_Bounds(rc);
    }

    void Focus() {
        if (m_controller)
            m_controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }

    void NotifyMoved() {
        if (m_controller) m_controller->NotifyParentWindowPositionChanged();
    }

    // Map https://viewer.doc/ to the directory of the current document so its
    // relative links and images resolve.
    void SetDocDir(const std::wstring& dir) {
        if (!m_webview3 || dir == m_docDir) return;
        m_docDir = dir;
        m_webview3->ClearVirtualHostNameToFolderMapping(cfg::kDocHost);
        m_webview3->SetVirtualHostNameToFolderMapping(
            cfg::kDocHost, dir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    }

    const std::wstring& DocDir() const { return m_docDir; }

    void PostJson(const std::wstring& json) {
        if (m_webview) m_webview->PostWebMessageAsJson(json.c_str());
    }

private:
    HWND                    m_hwnd       = nullptr;
    ICoreWebView2Controller* m_controller = nullptr;
    ICoreWebView2*           m_webview    = nullptr;
    ICoreWebView2_3*         m_webview3   = nullptr;   // virtual host mapping API
    IDropTarget*             m_dropTarget = nullptr;   // our OS file-drop handler
    std::wstring             m_assetsDir;
    std::wstring             m_docDir;

    void Fail(const std::wstring& msg) {
        if (onFailed) onFailed(msg);
    }

    // (Re)register our file-drop target on every window in the WebView2 child
    // tree, replacing the runtime's rejecting target so OS drops reach us.
    // OLE routes a drop to the exact window under the cursor (no walk-up to the
    // parent), and which inner window that is depends on Chromium internals, so
    // we blanket the whole subtree. Called after creation and each navigation.
    void InstallDropTarget() {
        if (m_dropTarget) register_drop_subtree(m_hwnd, m_dropTarget);
    }
    static void register_drop_subtree(HWND parent, IDropTarget* target) {
        if (!parent) return;
        for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            RevokeDragDrop(c);            // drop the runtime's own (rejecting) target
            RegisterDragDrop(c, target);  // ours accepts external files
            register_drop_subtree(c, target);
        }
    }
    static void revoke_drop_subtree(HWND parent) {
        if (!parent) return;
        for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            RevokeDragDrop(c);
            revoke_drop_subtree(c);
        }
    }

    static bool starts_with(const std::wstring& s, const wchar_t* prefix) {
        size_t n = lstrlenW(prefix);
        return s.size() >= n && _wcsnicmp(s.c_str(), prefix, n) == 0;
    }

    // True if canonical path `p` lies inside directory `base` (case-insensitive).
    // The trailing-separator boundary stops "C:\docs-secret" counting as under
    // "C:\docs".
    static bool path_is_under(std::wstring p, std::wstring base) {
        if (p.empty() || base.empty()) return false;
        if (base.back() != L'\\') base += L'\\';
        return p.size() >= base.size() &&
               _wcsnicmp(p.c_str(), base.c_str(), base.size()) == 0;
    }

    // Take ownership of a CoTaskMem string returned by a WebView2 getter.
    static std::wstring take(LPWSTR s) {
        std::wstring out = s ? s : L"";
        if (s) CoTaskMemFree(s);
        return out;
    }

    // Decide what to do with a URI the page tries to navigate to (or open in
    // a new window). Returns true if the URI is allowed to load in the webview.
    bool HandleNavigation(const std::wstring& uri);

    void Setup() {
        // Settings: keep defaults (context menu, zoom, DevTools) but drop chrome
        // that makes no sense for a viewer.
        ICoreWebView2Settings* s = nullptr;
        if (SUCCEEDED(m_webview->get_Settings(&s)) && s) {
            s->put_IsStatusBarEnabled(FALSE);
            s->put_AreHostObjectsAllowed(FALSE);
            s->Release();
        }

        // Virtual host for UI assets (fixed for the process lifetime).
        m_webview->QueryInterface(IID_PPV_ARGS(&m_webview3));
        if (m_webview3)
            m_webview3->SetVirtualHostNameToFolderMapping(
                cfg::kAssetsHost, m_assetsDir.c_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);

        // Stop the webview from handling external drops as web content; we want
        // dropped .md files to open in the viewer, not navigate the page. This
        // makes the runtime register a drop target that *rejects* OS drops, so
        // we replace it below with our own (see InstallDropTarget / FileDropTarget).
        ICoreWebView2Controller4* c4 = nullptr;
        if (SUCCEEDED(m_controller->QueryInterface(IID_PPV_ARGS(&c4))) && c4) {
            c4->put_AllowExternalDrop(FALSE);
            c4->Release();
        }
        if (!m_dropTarget)
            m_dropTarget = new FileDropTarget([this](const std::wstring& p) { if (onOpenFile) onOpenFile(p); });
        InstallDropTarget();

        EventRegistrationToken tok;

        // String messages from app.js:
        //   "ready"        - the renderer has loaded and is listening
        //   "save:<json>"  - the settings panel wants <json> written to config.json
        //   "back"/"forward" - the history toolbar's navigation buttons
        using MsgHandler = ComHandler<ICoreWebView2WebMessageReceivedEventHandler,
                                      ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>;
        m_webview->add_WebMessageReceived(
            new MsgHandler([this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR msg = nullptr;
                if (SUCCEEDED(args->TryGetWebMessageAsString(&msg)) && msg) {
                    std::wstring s = msg;
                    CoTaskMemFree(msg);
                    if (s == L"ready") { if (onReady) onReady(); }
                    else if (s == L"back") { if (onBack) onBack(); }
                    else if (s == L"forward") { if (onForward) onForward(); }
                    else if (s.rfind(L"save:", 0) == 0) { if (onSaveConfig) onSaveConfig(s.substr(5)); }
                }
                return S_OK;
            }), &tok);

        // Navigation policy: only index.html may load inside the webview.
        using NavHandler = ComHandler<ICoreWebView2NavigationStartingEventHandler,
                                      ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>;
        m_webview->add_NavigationStarting(
            new NavHandler([this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                if (!HandleNavigation(take(uri))) args->put_Cancel(TRUE);
                return S_OK;
            }), &tok);

        // The inner render window that owns the OS drop target is (re)created on
        // every navigation, so re-apply our file-drop target once it settles.
        using NavDoneHandler = ComHandler<ICoreWebView2NavigationCompletedEventHandler,
                                          ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>;
        m_webview->add_NavigationCompleted(
            new NavDoneHandler([this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                InstallDropTarget();
                return S_OK;
            }), &tok);

        // Ctrl+click / target=_blank: never spawn webview windows.
        using NewWinHandler = ComHandler<ICoreWebView2NewWindowRequestedEventHandler,
                                         ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>;
        m_webview->add_NewWindowRequested(
            new NewWinHandler([this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE);
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                HandleNavigation(take(uri));   // open externally / in the viewer
                return S_OK;
            }), &tok);

        // The webview swallows keyboard input; forward the app shortcuts.
        using KeyHandler = ComHandler<ICoreWebView2AcceleratorKeyPressedEventHandler,
                                      ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs*>;
        m_controller->add_AcceleratorKeyPressed(
            new KeyHandler([this](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                COREWEBVIEW2_KEY_EVENT_KIND kind;
                args->get_KeyEventKind(&kind);
                if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN) return S_OK;
                UINT key = 0;
                args->get_VirtualKey(&key);
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (key == VK_F5)               { args->put_Handled(TRUE); PostMessageW(m_hwnd, WM_COMMAND, cfg::ID_RELOAD, 0); }
                else if (ctrl && key == 'O')    { args->put_Handled(TRUE); PostMessageW(m_hwnd, WM_COMMAND, cfg::ID_OPEN, 0); }
                return S_OK;
            }), &tok);
    }
};

inline bool WebViewHost::HandleNavigation(const std::wstring& uri) {
    // The viewer UI itself.
    if (starts_with(uri, (std::wstring(L"https://") + cfg::kAssetsHost + L"/").c_str()))
        return true;

    // Link into the current document's folder. ONLY linked .md files are acted
    // on - they render in the viewer (safe: html:false, no exfil), which is the
    // whole point of the app. Every other local file is ignored: the viewer
    // never launches or reveals files on disk, so a crafted document cannot
    // turn a click into running/opening an executable or any other local file.
    if (starts_with(uri, cfg::kDocBaseUrl) && !m_docDir.empty()) {
        std::wstring rel = uri.substr(lstrlenW(cfg::kDocBaseUrl));
        size_t hash = rel.find_first_of(L"#?");
        if (hash != std::wstring::npos) rel = rel.substr(0, hash);
        // Decode %XX FIRST, then normalize separators: otherwise an encoded
        // slash ("%2f") would survive separator handling and reappear after
        // decoding, smuggling extra path segments past the browser's own
        // ".."-normalization.
        rel = fileio::url_decode(rel);
        for (wchar_t& c : rel) if (c == L'/') c = L'\\';
        // Canonicalize, then require the result to stay inside the document's
        // folder. Without this an encoded "..%2f" (which the browser's own
        // ".."-normalization never sees) could escape m_docDir and render any
        // .md file on disk.
        std::wstring path = fileio::full_path(m_docDir + L"\\" + rel);
        if (!path.empty() && path_is_under(path, fileio::full_path(m_docDir)) &&
            fileio::has_markdown_ext(path) && fileio::file_exists(path)) {
            if (onOpenFile) onOpenFile(path);      // render linked markdown
        }
        return false;                              // never open/reveal local files
    }

    // Anything external: hand off to the default browser.
    if (starts_with(uri, L"http://") || starts_with(uri, L"https://") ||
        starts_with(uri, L"mailto:"))
        ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return false;
}
