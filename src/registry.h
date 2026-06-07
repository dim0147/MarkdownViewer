#pragma once
// registry.h - per-user (HKCU, no admin) Explorer right-click integration:
// "View with Markdown Viewer" on .md/.markdown/.mdown/.mkd files.

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <string>
#include "config.h"

namespace reg {

inline bool set_sz(HKEY root, const std::wstring& key, const wchar_t* value_name,
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

inline void install_context_menu(HWND owner, bool quiet) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    bool ok = true;
    for (const wchar_t* ext : cfg::kMarkdownExts) {
        std::wstring base = std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + ext +
                            L"\\shell\\MarkdownViewer";
        ok &= set_sz(HKEY_CURRENT_USER, base, nullptr, L"View with Markdown Viewer");
        ok &= set_sz(HKEY_CURRENT_USER, base, L"Icon", std::wstring(L"\"") + exe + L"\",0");
        ok &= set_sz(HKEY_CURRENT_USER, base + L"\\command", nullptr,
                     std::wstring(L"\"") + exe + L"\" \"%1\"");
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    if (!quiet)
        MessageBoxW(owner,
                    ok ? L"Done! Right-click any .md file in Explorer and choose\n"
                         L"\"View with Markdown Viewer\".\n\n"
                         L"(On Windows 11 it may be under \"Show more options\".)"
                       : L"Some registry entries could not be written.",
                    cfg::kAppTitle, MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
}

inline void uninstall_context_menu(HWND owner, bool quiet) {
    for (const wchar_t* ext : cfg::kMarkdownExts) {
        std::wstring base = std::wstring(L"Software\\Classes\\SystemFileAssociations\\") + ext +
                            L"\\shell\\MarkdownViewer";
        SHDeleteKeyW(HKEY_CURRENT_USER, base.c_str());
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    if (!quiet)
        MessageBoxW(owner, L"The right-click menu entry was removed.", cfg::kAppTitle,
                    MB_OK | MB_ICONINFORMATION);
}

} // namespace reg
