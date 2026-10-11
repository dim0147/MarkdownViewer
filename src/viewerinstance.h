#pragma once
// Find a window already showing a document. The IPC only queries its path;
// it never asks another process to open or overwrite a file.
#include <windows.h>
#include <string>
#include "config.h"
#include "fileio.h"

namespace viewerinstance {

constexpr ULONG_PTR kQueryDocument = 0x4d564431; // "MVD1", WM_COPYDATA protocol

inline bool same_path(const std::wstring& a, const std::wstring& b) {
    if (a.empty() || b.empty()) return false;
    return CompareStringOrdinal(fileio::full_path(a).c_str(), -1,
                                fileio::full_path(b).c_str(), -1, TRUE) == CSTR_EQUAL;
}

inline void focus(HWND hwnd) {
    if (IsIconic(hwnd)) ShowWindowAsync(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

inline bool focus_existing(const std::wstring& path, HWND exclude = nullptr) {
    struct Search { std::wstring path; HWND exclude; HWND found = nullptr; };
    Search search { fileio::full_path(path), exclude };
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        auto& s = *reinterpret_cast<Search*>(param);
        if (hwnd == s.exclude) return TRUE;
        wchar_t name[128] = {};
        GetClassNameW(hwnd, name, 128);
        if (lstrcmpW(name, cfg::kWindowClass) != 0) return TRUE;
        COPYDATASTRUCT data = { kQueryDocument,
            static_cast<DWORD>((s.path.size() + 1) * sizeof(wchar_t)),
            const_cast<wchar_t*>(s.path.c_str()) };
        DWORD_PTR viewing = 0;
        if (SendMessageTimeoutW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                                SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &viewing) && viewing == TRUE) {
            s.found = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    if (!search.found) return false;
    focus(search.found);
    return true;
}

// Serialize command-line launches until the first window has claimed its
// document, including launches that arrive before WebView2 is ready.
class LaunchLock {
    HANDLE m_mutex = nullptr;
    bool m_owned = false;
public:
    LaunchLock() {
        m_mutex = CreateMutexW(nullptr, FALSE, L"Local\\MarkdownViewer.DocumentLaunch");
        if (m_mutex) {
            DWORD result = WaitForSingleObject(m_mutex, INFINITE);
            m_owned = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        }
    }
    void Release() {
        if (m_owned) { ReleaseMutex(m_mutex); m_owned = false; }
    }
    ~LaunchLock() { Release(); if (m_mutex) CloseHandle(m_mutex); }
};

} // namespace viewerinstance
