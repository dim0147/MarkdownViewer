// MarkdownViewer - a small dependency-free Markdown viewer for Windows.
//   * Open .md files via Explorer right-click ("View with Markdown Viewer"),
//     drag & drop onto the window, File > Open, or the command line.
//   * Renders through the system browser control; no runtime dependencies.

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include "markdown.h"
#include "webhost.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

#define ID_OPEN      101
#define ID_RELOAD    102
#define ID_EXIT      103
#define ID_INSTALL   201
#define ID_UNINSTALL 202
#define ID_ABOUT     301

static const wchar_t kAppTitle[] = L"Markdown Viewer";
static const wchar_t* kExts[] = { L".md", L".markdown", L".mdown", L".mkd" };

static HWND         g_hwnd = nullptr;
static BrowserHost* g_host = nullptr;
static std::wstring g_current;          // currently displayed file (empty = welcome)
static int          g_tmpToggle = 0;    // alternate temp files so reload bypasses cache
static std::wstring g_tmpFiles[2];

// ---------------------------------------------------------------- helpers --

static std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// Read a text file; handles UTF-8 (with/without BOM), UTF-16 LE/BE, and ANSI.
static bool read_file_text(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    GetFileSizeEx(h, &size);
    if (size.QuadPart > 64LL * 1024 * 1024) { CloseHandle(h); return false; }   // 64 MB cap
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

// file:///C:/dir/ URL for resolving relative links/images in the document.
static std::string file_url_for_dir(const std::wstring& dir) {
    std::string u8 = narrow(dir);
    std::string out = "file:///";
    char hex[8];
    for (unsigned char c : u8) {
        if (c == '\\') out += '/';
        else if (isalnum(c) || strchr("/:._-~!$&'()*+,;=@", c)) out += (char)c;
        else { wsprintfA(hex, "%%%02X", c); out += hex; }
    }
    if (out.back() != '/') out += '/';
    return out;
}

// ------------------------------------------------------------------- page --

static const char kCss[] = R"css(<style>
html,body{margin:0;padding:0;background:#fff;}
body{font-family:'Segoe UI',Arial,sans-serif;font-size:15px;line-height:1.6;color:#1f2328;}
.container{max-width:880px;margin:0 auto;padding:32px 40px 80px;}
h1,h2,h3,h4,h5,h6{margin:1.4em 0 .5em;font-weight:600;line-height:1.25;}
h1{font-size:2em;border-bottom:1px solid #d8dee4;padding-bottom:.3em;}
h2{font-size:1.5em;border-bottom:1px solid #d8dee4;padding-bottom:.3em;}
h3{font-size:1.25em;}h4{font-size:1.05em;}h5{font-size:.95em;}h6{font-size:.9em;color:#59636e;}
p{margin:0 0 1em;}
a{color:#0969da;text-decoration:none;}a:hover{text-decoration:underline;}
code{font-family:Consolas,'Courier New',monospace;font-size:88%;background:#eff1f3;padding:2px 5px;border-radius:4px;}
pre{background:#f6f8fa;border:1px solid #e4e8ec;border-radius:6px;padding:14px 16px;overflow-x:auto;margin:0 0 1em;}
pre code{background:none;padding:0;font-size:13px;line-height:1.45;}
blockquote{margin:0 0 1em;padding:2px 1em;color:#59636e;border-left:4px solid #d0d7de;}
ul,ol{margin:0 0 1em;padding-left:2em;}
li{margin:.15em 0;}
li.task-list-item{list-style:none;margin-left:-1.4em;}
table{border-collapse:collapse;margin:0 0 16px;}
th,td{border:1px solid #d8dee4;padding:6px 13px;}
th{background:#f6f8fa;font-weight:600;}
tr:nth-child(even) td{background:#fafbfc;}
img{max-width:100%;}
hr{border:none;height:3px;background:#d8dee4;margin:24px 0;}
kbd{background:#eff1f3;border:1px solid #d0d7de;border-bottom-width:2px;border-radius:5px;padding:2px 6px;font-family:Consolas,monospace;font-size:85%;}
.welcome{text-align:center;padding-top:14vh;color:#59636e;}
.welcome .logo{display:inline-block;font-size:54px;font-weight:700;color:#fff;background:#24292f;border-radius:18px;padding:14px 34px;margin-bottom:18px;}
.welcome h1{border:none;color:#24292f;margin:.2em 0 .4em;}
.welcome .drop{margin:26px auto 18px;max-width:430px;border:2px dashed #c7ced6;border-radius:12px;padding:34px 26px;font-size:16px;}
.welcome .tip{font-size:13px;max-width:520px;margin:0 auto;}
</style>)css";

static std::string build_page(const std::string& body, const std::string& base_url) {
    std::string page = "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">";
    if (!base_url.empty()) page += "<base href=\"" + base_url + "\">";
    page += kCss;
    page += "</head><body><div class=\"container\">";
    page += body;
    page += "</div></body></html>";
    return page;
}

// Write the page to an alternating temp file and navigate the browser to it.
static void show_page(const std::string& page) {
    const std::wstring& tmp = g_tmpFiles[g_tmpToggle];
    g_tmpToggle ^= 1;
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, "\xEF\xBB\xBF", 3, &written, nullptr);
    WriteFile(h, page.data(), (DWORD)page.size(), &written, nullptr);
    CloseHandle(h);
    if (g_host) g_host->Navigate(tmp);
}

static void show_welcome() {
    std::string body =
        "<div class=\"welcome\">"
        "<div class=\"logo\">M&#8595;</div>"
        "<h1>Markdown Viewer</h1>"
        "<div class=\"drop\"><b>Drag &amp; drop</b> a .md file here<br><br>"
        "or press <kbd>Ctrl</kbd>+<kbd>O</kbd> to browse</div>"
        "<p class=\"tip\">Tip: use <b>Tools &gt; Add to Explorer right-click menu</b> so you can "
        "right-click any .md file and choose <i>View with Markdown Viewer</i>.</p>"
        "</div>";
    g_current.clear();
    SetWindowTextW(g_hwnd, kAppTitle);
    show_page(build_page(body, std::string()));
}

static void show_markdown(const std::wstring& path) {
    std::string text;
    if (!read_file_text(path, text)) {
        MessageBoxW(g_hwnd, (L"Could not open file:\n" + path).c_str(), kAppTitle,
                    MB_OK | MB_ICONERROR);
        return;
    }
    std::string body = md::to_html(text);

    size_t slash = path.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? L"." : path.substr(0, slash);
    std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);

    show_page(build_page(body, file_url_for_dir(dir)));
    g_current = path;
    SetWindowTextW(g_hwnd, (name + L" - " + kAppTitle).c_str());
}

// ---------------------------------------------------------------- registry --

// Render in IE11 standards mode (the control defaults to IE7 quirks otherwise).
static void set_browser_emulation() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const wchar_t* name = PathFindFileNameW(exe);
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Internet Explorer\\Main\\FeatureControl\\FEATURE_BROWSER_EMULATION",
            0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        DWORD v = 11001;
        RegSetValueExW(hk, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
        RegCloseKey(hk);
    }
}

static bool set_reg_sz(HKEY root, const std::wstring& key, const wchar_t* value_name,
                       const std::wstring& data) {
    HKEY hk;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk,
                        nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(hk, value_name, 0, REG_SZ, (const BYTE*)data.c_str(),
                            (DWORD)((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hk);
    return r == ERROR_SUCCESS;
}

// Per-user (no admin) Explorer right-click verb for markdown extensions.
static void install_context_menu(HWND owner, bool quiet) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    bool ok = true;
    for (const wchar_t* ext : kExts) {
        std::wstring base = std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + ext +
                            L"\\shell\\MarkdownViewer";
        ok &= set_reg_sz(HKEY_CURRENT_USER, base, nullptr, L"View with Markdown Viewer");
        ok &= set_reg_sz(HKEY_CURRENT_USER, base, L"Icon", std::wstring(L"\"") + exe + L"\",0");
        ok &= set_reg_sz(HKEY_CURRENT_USER, base + L"\\command", nullptr,
                         std::wstring(L"\"") + exe + L"\" \"%1\"");
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    if (!quiet)
        MessageBoxW(owner,
                    ok ? L"Done! Right-click any .md file in Explorer and choose\n"
                         L"\"View with Markdown Viewer\".\n\n"
                         L"(On Windows 11 it may be under \"Show more options\".)"
                       : L"Some registry entries could not be written.",
                    kAppTitle, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

static void uninstall_context_menu(HWND owner, bool quiet) {
    for (const wchar_t* ext : kExts) {
        std::wstring base = std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + ext +
                            L"\\shell\\MarkdownViewer";
        SHDeleteKeyW(HKEY_CURRENT_USER, base.c_str());
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    if (!quiet)
        MessageBoxW(owner, L"The right-click menu entry was removed.", kAppTitle,
                    MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------- UI --

static void open_file_dialog(HWND hwnd) {
    wchar_t file[4096] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Markdown files (*.md;*.markdown;*.mdown;*.mkd)\0*.md;*.markdown;*.mdown;*.mkd\0"
                      L"All files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = 4096;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) show_markdown(file);
}

static HMENU build_menu() {
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, ID_RELOAD, L"&Reload\tF5");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_EXIT, L"E&xit");
    HMENU tools = CreatePopupMenu();
    AppendMenuW(tools, MF_STRING, ID_INSTALL, L"&Add to Explorer right-click menu");
    AppendMenuW(tools, MF_STRING, ID_UNINSTALL, L"&Remove from Explorer right-click menu");
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, ID_ABOUT, L"&About");
    HMENU bar = CreateMenu();
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tools, L"&Tools");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        g_host = new BrowserHost();
        if (!g_host->Create(hwnd)) {
            MessageBoxW(hwnd, L"Failed to create the embedded browser control.", kAppTitle,
                        MB_OK | MB_ICONERROR);
            return -1;
        }
        DragAcceptFiles(hwnd, TRUE);
        return 0;

    case WM_SIZE:
        if (g_host) g_host->Resize();
        return 0;

    case WM_SETFOCUS:
        if (g_host) g_host->Focus();
        return 0;

    case WM_DROPFILES: {
        wchar_t path[2048] = L"";
        if (DragQueryFileW((HDROP)wp, 0, path, 2047)) show_markdown(path);
        DragFinish((HDROP)wp);
        return 0;
    }

    case WM_APP_OPENFILE:
        show_markdown((const wchar_t*)lp);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 480;
        mmi->ptMinTrackSize.y = 360;
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_OPEN:      open_file_dialog(hwnd); return 0;
        case ID_RELOAD:
            if (!g_current.empty()) show_markdown(g_current);
            else show_welcome();
            return 0;
        case ID_EXIT:      DestroyWindow(hwnd); return 0;
        case ID_INSTALL:   install_context_menu(hwnd, false); return 0;
        case ID_UNINSTALL: uninstall_context_menu(hwnd, false); return 0;
        case ID_ABOUT:
            MessageBoxW(hwnd,
                        L"Markdown Viewer 1.0\n\n"
                        L"A tiny dependency-free Markdown viewer.\n\n"
                        L"\x2022 Drag && drop a .md file onto the window\n"
                        L"\x2022 Ctrl+O to open a file, F5 to reload\n"
                        L"\x2022 Tools menu adds \"View with Markdown Viewer\"\n"
                        L"   to the Explorer right-click menu",
                        kAppTitle, MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        break;

    case WM_DESTROY:
        if (g_host) { g_host->Destroy(); g_host = nullptr; }   // intentionally not deleted: browser may hold refs during teardown
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    // Command-line: [--register|--unregister] | [file.md]
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1) {
        if (lstrcmpiW(argv[1], L"--register") == 0)   { install_context_menu(nullptr, true);  return 0; }
        if (lstrcmpiW(argv[1], L"--unregister") == 0) { uninstall_context_menu(nullptr, true); return 0; }
    }

    set_browser_emulation();
    if (FAILED(OleInitialize(nullptr))) return 1;

    wchar_t tmpdir[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpdir);
    g_tmpFiles[0] = std::wstring(tmpdir) + L"MarkdownViewer_a.html";
    g_tmpFiles[1] = std::wstring(tmpdir) + L"MarkdownViewer_b.html";

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MarkdownViewerWnd";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, kAppTitle, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1060, 800,
                                nullptr, build_menu(), hInst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    if (argc > 1 && GetFileAttributesW(argv[1]) != INVALID_FILE_ATTRIBUTES)
        show_markdown(argv[1]);
    else
        show_welcome();
    LocalFree(argv);

    ACCEL acc[] = {
        { FVIRTKEY | FCONTROL, 'O',   ID_OPEN },
        { FVIRTKEY,            VK_F5, ID_RELOAD },
    };
    HACCEL haccel = CreateAcceleratorTableW(acc, 2);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(hwnd, haccel, &msg)) continue;
        if (g_host && g_host->TranslateKey(&msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyAcceleratorTable(haccel);
    DeleteFileW(g_tmpFiles[0].c_str());
    DeleteFileW(g_tmpFiles[1].c_str());
    OleUninitialize();
    return (int)msg.wParam;
}
