// MarkdownViewer - a small Markdown viewer for Windows.
//   * Open .md files via Explorer right-click ("View with Markdown Viewer"),
//     drag & drop onto the window, File > Open, or the command line.
//   * Renders with markdown-it + highlight.js inside WebView2 (Edge/Chromium).
// See CLAUDE.md for the architecture overview.

#include <windows.h>
#include <shellapi.h>
#include "config.h"
#include "fileio.h"
#include "registry.h"
#include "app.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "version.lib")              // used by WebView2LoaderStatic
#pragma comment(lib, "WebView2LoaderStatic.lib")

static App   g_app;
static HMENU g_recentMenu = nullptr;   // File > Open Recent popup (populated by App)

static HMENU build_menu() {
    g_recentMenu = CreatePopupMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, cfg::ID_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_POPUP, (UINT_PTR)g_recentMenu, L"Open &Recent");
    AppendMenuW(file, MF_STRING, cfg::ID_RELOAD, L"&Reload\tF5");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, cfg::ID_EXIT, L"E&xit");
    HMENU tools = CreatePopupMenu();
    AppendMenuW(tools, MF_STRING, cfg::ID_INSTALL, L"&Add to Explorer right-click menu");
    AppendMenuW(tools, MF_STRING, cfg::ID_UNINSTALL, L"&Remove from Explorer right-click menu");
    AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(tools, MF_STRING, cfg::ID_SETTINGS, L"&Settings...");
    AppendMenuW(tools, MF_STRING, cfg::ID_OPENCONFIG, L"Edit config.json in &editor");
    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, cfg::ID_CHECK_UPDATE, L"Check for &Updates...");
    AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(help, MF_STRING, cfg::ID_ABOUT, L"&About");
    HMENU bar = CreateMenu();
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tools, L"&Tools");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        if (!g_app.Init(hwnd)) return -1;
        DragAcceptFiles(hwnd, TRUE);
        return 0;

    case WM_SIZE:
        g_app.Resize();
        return 0;

    case WM_MOVE:
        g_app.Moved();
        return 0;

    case WM_SETFOCUS:
        g_app.Focus();
        return 0;

    case WM_DROPFILES: {
        wchar_t path[2048] = L"";
        if (DragQueryFileW((HDROP)wp, 0, path, 2047)) g_app.OpenFile(path);
        DragFinish((HDROP)wp);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = cfg::kMinWindowW;
        mmi->ptMinTrackSize.y = cfg::kMinWindowH;
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case cfg::ID_OPEN:       g_app.OpenDialog(); return 0;
        case cfg::ID_RELOAD:     g_app.Reload(); return 0;
        case cfg::ID_EXIT:       DestroyWindow(hwnd); return 0;
        case cfg::ID_INSTALL:    reg::install_context_menu(hwnd, false); return 0;
        case cfg::ID_UNINSTALL:  reg::uninstall_context_menu(hwnd, false); return 0;
        case cfg::ID_SETTINGS:   g_app.OpenSettings(); return 0;
        case cfg::ID_OPENCONFIG: g_app.OpenConfigFile(); return 0;
        case cfg::ID_CHECK_UPDATE: g_app.CheckForUpdates(); return 0;
        case cfg::ID_RECENT_CLEAR: g_app.ClearRecent(); return 0;
        case cfg::ID_ABOUT:
            MessageBoxW(hwnd,
                        (std::wstring(L"Markdown Viewer ") + cfg::kAppVersion + L"\n\n"
                        L"Rendering: markdown-it + highlight.js in WebView2.\n\n"
                        L"\x2022 Drag && drop a .md file onto the window\n"
                        L"\x2022 Ctrl+O to open a file, F5 to reload\n"
                        L"\x2022 Tools menu adds \"View with Markdown Viewer\"\n"
                        L"   to the Explorer right-click menu\n"
                        L"\x2022 Tools > Settings to customize theme, fonts, ...").c_str(),
                        cfg::kAppTitle, MB_OK | MB_ICONINFORMATION);
            return 0;
        default:
            if (LOWORD(wp) >= cfg::ID_RECENT_BASE &&
                LOWORD(wp) <  cfg::ID_RECENT_BASE + cfg::kMaxRecent) {
                g_app.OpenRecent(LOWORD(wp) - cfg::ID_RECENT_BASE);
                return 0;
            }
        }
        break;

    case WM_DESTROY:
        g_app.Shutdown();
        PostQuitMessage(0);
        return 0;

    default:
        // Update-check result delivered from update::check_async's worker thread.
        if (msg == cfg::WM_APP_UPDATE_RESULT) {
            update::Result* r = reinterpret_cast<update::Result*>(lp);
            if (r) { g_app.OnUpdateResult(*r); delete r; }
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    // Command-line: [--register|--unregister] | [file.md]
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc > 1) {
        if (lstrcmpiW(argv[1], L"--register") == 0)   { reg::install_context_menu(nullptr, true);  return 0; }
        if (lstrcmpiW(argv[1], L"--unregister") == 0) { reg::uninstall_context_menu(nullptr, true); return 0; }
    }

    if (FAILED(OleInitialize(nullptr))) return 1;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = cfg::kWindowClass;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, cfg::kAppTitle, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                cfg::kDefaultWindowW, cfg::kDefaultWindowH,
                                nullptr, build_menu(), hInst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    g_app.SetRecentMenu(g_recentMenu);   // populate File > Open Recent

    if (argc > 1 && GetFileAttributesW(argv[1]) != INVALID_FILE_ATTRIBUTES)
        g_app.OpenFile(argv[1]);     // queued until the renderer reports ready
    else
        g_app.ShowWelcome();
    LocalFree(argv);

    // Shortcuts while the (rarely focused) main window has focus; the same keys
    // inside the webview are forwarded by WebViewHost's AcceleratorKeyPressed.
    ACCEL acc[] = {
        { FVIRTKEY | FCONTROL, 'O',   cfg::ID_OPEN },
        { FVIRTKEY,            VK_F5, cfg::ID_RELOAD },
    };
    HACCEL haccel = CreateAcceleratorTableW(acc, 2);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(hwnd, haccel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DestroyAcceleratorTable(haccel);
    OleUninitialize();
    return (int)msg.wParam;
}
