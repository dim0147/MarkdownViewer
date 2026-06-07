#pragma once
// webhost.h - minimal OLE container hosting the system WebBrowser control,
// plus an IDropTarget that forwards dropped files to the main window.

#include <windows.h>
#include <ole2.h>
#include <oleidl.h>
#include <exdisp.h>
#include <mshtmhst.h>
#include <shellapi.h>
#include <string>

#define WM_APP_OPENFILE (WM_APP + 1)   // lParam = const wchar_t* path (SendMessage only)

// ---------------------------------------------------------------------------
// Drop target: accepts CF_HDROP and forwards the first file to the window.
// Installed over the browser's own drop target via IDocHostUIHandler::GetDropTarget.
// ---------------------------------------------------------------------------
class FileDropTarget : public IDropTarget {
    LONG m_ref = 1;
    HWND m_notify;
    bool m_ok = false;
public:
    explicit FileDropTarget(HWND notify) : m_notify(notify) {}
    virtual ~FileDropTarget() {}

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

    STDMETHODIMP DragEnter(IDataObject* pdo, DWORD, POINTL, DWORD* eff) override {
        FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        m_ok = pdo && pdo->QueryGetData(&fe) == S_OK;
        if (eff) *eff = m_ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    STDMETHODIMP DragOver(DWORD, POINTL, DWORD* eff) override {
        if (eff) *eff = m_ok ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    STDMETHODIMP DragLeave() override { m_ok = false; return S_OK; }
    STDMETHODIMP Drop(IDataObject* pdo, DWORD, POINTL, DWORD* eff) override {
        if (eff) *eff = DROPEFFECT_NONE;
        FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM stg = {};
        if (pdo && SUCCEEDED(pdo->GetData(&fe, &stg))) {
            HDROP drop = (HDROP)stg.hGlobal;
            wchar_t path[2048] = L"";
            if (drop && DragQueryFileW(drop, 0, path, 2047)) {
                if (eff) *eff = DROPEFFECT_COPY;
                SendMessageW(m_notify, WM_APP_OPENFILE, 0, (LPARAM)path);
            }
            ReleaseStgMedium(&stg);
        }
        m_ok = false;
        return S_OK;
    }
};

// ---------------------------------------------------------------------------
// BrowserHost: hosts the WebBrowser control in-place inside a parent HWND.
// The object is owned by the main window for the lifetime of the process,
// so AddRef/Release never self-delete.
// ---------------------------------------------------------------------------
class BrowserHost : public IOleClientSite,
                    public IOleInPlaceSite,
                    public IOleInPlaceFrame,
                    public IDocHostUIHandler {
    LONG m_ref = 1;
    HWND m_hwnd = nullptr;
    IOleObject* m_ole = nullptr;
    IWebBrowser2* m_web = nullptr;
    IDropTarget* m_drop = nullptr;
public:
    BrowserHost() {}
    virtual ~BrowserHost() {}

    bool Create(HWND hwnd) {
        m_hwnd = hwnd;
        m_drop = new FileDropTarget(hwnd);
        if (FAILED(CoCreateInstance(CLSID_WebBrowser, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IOleObject, (void**)&m_ole)))
            return false;
        m_ole->SetClientSite(this);
        OleSetContainedObject(m_ole, TRUE);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (FAILED(m_ole->DoVerb(OLEIVERB_INPLACEACTIVATE, nullptr, this, 0, hwnd, &rc)))
            return false;
        if (FAILED(m_ole->QueryInterface(IID_IWebBrowser2, (void**)&m_web)))
            return false;
        m_web->put_Silent(VARIANT_TRUE);
        Resize();
        return true;
    }

    void Destroy() {
        if (m_web) { m_web->Release(); m_web = nullptr; }
        if (m_ole) {
            m_ole->Close(OLECLOSE_NOSAVE);
            m_ole->SetClientSite(nullptr);
            m_ole->Release();
            m_ole = nullptr;
        }
        if (m_drop) { m_drop->Release(); m_drop = nullptr; }
    }

    void Navigate(const std::wstring& url) {
        if (!m_web) return;
        BSTR b = SysAllocString(url.c_str());
        VARIANT ve;
        VariantInit(&ve);
        m_web->Navigate(b, &ve, &ve, &ve, &ve);
        SysFreeString(b);
    }

    void Resize() {
        if (!m_ole || !m_hwnd) return;
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        IOleInPlaceObject* ip = nullptr;
        if (SUCCEEDED(m_ole->QueryInterface(IID_IOleInPlaceObject, (void**)&ip)) && ip) {
            ip->SetObjectRects(&rc, &rc);
            ip->Release();
        }
    }

    void Focus() {
        if (!m_ole || !m_hwnd) return;
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        m_ole->DoVerb(OLEIVERB_UIACTIVATE, nullptr, this, 0, m_hwnd, &rc);
    }

    // Let the browser handle navigation keys (arrows, PgUp/PgDn, Ctrl+C, ...).
    bool TranslateKey(MSG* msg) {
        if (!m_web || msg->message < WM_KEYFIRST || msg->message > WM_KEYLAST) return false;
        IOleInPlaceActiveObject* ao = nullptr;
        bool handled = false;
        if (SUCCEEDED(m_web->QueryInterface(IID_IOleInPlaceActiveObject, (void**)&ao)) && ao) {
            handled = (ao->TranslateAccelerator(msg) == S_OK);
            ao->Release();
        }
        return handled;
    }

    // ---- IUnknown -------------------------------------------------------
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (riid == IID_IUnknown || riid == IID_IOleClientSite)
            *ppv = static_cast<IOleClientSite*>(this);
        else if (riid == IID_IOleWindow || riid == IID_IOleInPlaceSite)
            *ppv = static_cast<IOleInPlaceSite*>(this);
        else if (riid == IID_IOleInPlaceUIWindow || riid == IID_IOleInPlaceFrame)
            *ppv = static_cast<IOleInPlaceFrame*>(this);
        else if (riid == IID_IDocHostUIHandler)
            *ppv = static_cast<IDocHostUIHandler*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override { return InterlockedDecrement(&m_ref); } // owned by main window

    // ---- IOleClientSite -------------------------------------------------
    STDMETHODIMP SaveObject() override { return S_OK; }
    STDMETHODIMP GetMoniker(DWORD, DWORD, IMoniker** ppmk) override {
        if (ppmk) *ppmk = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetContainer(IOleContainer** ppc) override {
        if (ppc) *ppc = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP ShowObject() override { return S_OK; }
    STDMETHODIMP OnShowWindow(BOOL) override { return S_OK; }
    STDMETHODIMP RequestNewObjectLayout() override { return E_NOTIMPL; }

    // ---- IOleWindow (base of both IOleInPlaceSite and IOleInPlaceFrame) -
    STDMETHODIMP GetWindow(HWND* phwnd) override {
        if (!phwnd) return E_POINTER;
        *phwnd = m_hwnd;
        return S_OK;
    }
    STDMETHODIMP ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }

    // ---- IOleInPlaceSite ------------------------------------------------
    STDMETHODIMP CanInPlaceActivate() override { return S_OK; }
    STDMETHODIMP OnInPlaceActivate() override { return S_OK; }
    STDMETHODIMP OnUIActivate() override { return S_OK; }
    STDMETHODIMP GetWindowContext(IOleInPlaceFrame** ppFrame, IOleInPlaceUIWindow** ppDoc,
                                  LPRECT prcPos, LPRECT prcClip,
                                  LPOLEINPLACEFRAMEINFO pInfo) override {
        if (ppFrame) { *ppFrame = static_cast<IOleInPlaceFrame*>(this); AddRef(); }
        if (ppDoc) *ppDoc = nullptr;
        if (prcPos) GetClientRect(m_hwnd, prcPos);
        if (prcClip && prcPos) *prcClip = *prcPos;
        if (pInfo) {
            pInfo->cb = sizeof(OLEINPLACEFRAMEINFO);
            pInfo->fMDIApp = FALSE;
            pInfo->hwndFrame = m_hwnd;
            pInfo->haccel = nullptr;
            pInfo->cAccelEntries = 0;
        }
        return S_OK;
    }
    STDMETHODIMP Scroll(SIZE) override { return E_NOTIMPL; }
    STDMETHODIMP OnUIDeactivate(BOOL) override { return S_OK; }
    STDMETHODIMP OnInPlaceDeactivate() override { return S_OK; }
    STDMETHODIMP DiscardUndoState() override { return E_NOTIMPL; }
    STDMETHODIMP DeactivateAndUndo() override { return E_NOTIMPL; }
    STDMETHODIMP OnPosRectChange(LPCRECT prc) override {
        if (m_ole && prc) {
            IOleInPlaceObject* ip = nullptr;
            if (SUCCEEDED(m_ole->QueryInterface(IID_IOleInPlaceObject, (void**)&ip)) && ip) {
                ip->SetObjectRects(prc, prc);
                ip->Release();
            }
        }
        return S_OK;
    }

    // ---- IOleInPlaceUIWindow / IOleInPlaceFrame --------------------------
    STDMETHODIMP GetBorder(LPRECT) override { return E_NOTIMPL; }
    STDMETHODIMP RequestBorderSpace(LPCBORDERWIDTHS) override { return E_NOTIMPL; }
    STDMETHODIMP SetBorderSpace(LPCBORDERWIDTHS) override { return S_OK; }
    STDMETHODIMP SetActiveObject(IOleInPlaceActiveObject*, LPCOLESTR) override { return S_OK; }
    STDMETHODIMP InsertMenus(HMENU, LPOLEMENUGROUPWIDTHS) override { return E_NOTIMPL; }
    STDMETHODIMP SetMenu(HMENU, HOLEMENU, HWND) override { return S_OK; }
    STDMETHODIMP RemoveMenus(HMENU) override { return E_NOTIMPL; }
    STDMETHODIMP SetStatusText(LPCOLESTR) override { return S_OK; }
    STDMETHODIMP EnableModeless(BOOL) override { return S_OK; }   // shared with IDocHostUIHandler
    STDMETHODIMP TranslateAccelerator(LPMSG, WORD) override { return E_NOTIMPL; }

    // ---- IDocHostUIHandler -----------------------------------------------
    STDMETHODIMP ShowContextMenu(DWORD, POINT*, IUnknown*, IDispatch*) override {
        return S_FALSE;   // default browser context menu (copy, select all, ...)
    }
    STDMETHODIMP GetHostInfo(DOCHOSTUIINFO* pInfo) override {
        if (!pInfo) return E_POINTER;
        pInfo->cbSize = sizeof(DOCHOSTUIINFO);
        pInfo->dwFlags = DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_THEME | DOCHOSTUIFLAG_DPI_AWARE;
        pInfo->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;
        pInfo->pchHostCss = nullptr;
        pInfo->pchHostNS = nullptr;
        return S_OK;
    }
    STDMETHODIMP ShowUI(DWORD, IOleInPlaceActiveObject*, IOleCommandTarget*,
                        IOleInPlaceFrame*, IOleInPlaceUIWindow*) override { return S_OK; }
    STDMETHODIMP HideUI() override { return S_OK; }
    STDMETHODIMP UpdateUI() override { return S_OK; }
    STDMETHODIMP OnDocWindowActivate(BOOL) override { return S_OK; }
    STDMETHODIMP OnFrameWindowActivate(BOOL) override { return S_OK; }
    STDMETHODIMP ResizeBorder(LPCRECT, IOleInPlaceUIWindow*, BOOL) override { return S_OK; }
    STDMETHODIMP TranslateAccelerator(LPMSG, const GUID*, DWORD) override { return S_FALSE; }
    STDMETHODIMP GetOptionKeyPath(LPOLESTR* pchKey, DWORD) override {
        if (pchKey) *pchKey = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetDropTarget(IDropTarget*, IDropTarget** ppDropTarget) override {
        if (!ppDropTarget) return E_POINTER;
        if (m_drop) {
            *ppDropTarget = m_drop;
            m_drop->AddRef();
            return S_OK;
        }
        *ppDropTarget = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP GetExternal(IDispatch** ppDispatch) override {
        if (ppDispatch) *ppDispatch = nullptr;
        return E_NOTIMPL;
    }
    STDMETHODIMP TranslateUrl(DWORD, LPWSTR, LPWSTR* ppchURLOut) override {
        if (ppchURLOut) *ppchURLOut = nullptr;
        return S_FALSE;
    }
    STDMETHODIMP FilterDataObject(IDataObject*, IDataObject** ppDORet) override {
        if (ppDORet) *ppDORet = nullptr;
        return S_FALSE;
    }
};
