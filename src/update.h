#pragma once
// update.h - "Help > Check for Updates": asks the GitHub Releases API for the
// latest published release and compares its tag against cfg::kAppVersion.
//
// All network work happens on a detached background thread (WinHTTP) so the UI
// never blocks on a slow/unreachable network. The thread posts its outcome back
// to the main window as cfg::WM_APP_UPDATE_RESULT; the lParam is a heap-allocated
// update::Result* the message handler takes ownership of (and must delete).
//
// This is the only outbound network request the app ever makes, and only when
// the user explicitly clicks the menu item. It does not touch the webview, so
// the navigation allow-list / CSP are unaffected; the download page is handed to
// the default browser via ShellExecute, exactly like any other external link.

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <thread>
#include "config.h"
#include "fileio.h"

#pragma comment(lib, "winhttp.lib")

namespace update {

enum class Status { UpToDate, Available, Error };

struct Result {
    Status       status = Status::Error;
    std::wstring latest;    // latest release tag (e.g. "v2.2.0"), when Available
    std::wstring message;   // human-readable error text, when Error
};

// Split a version like "v2.2.0" / "2.2" into numeric components, stopping at the
// first non-digit/dot (so a "-beta" pre-release suffix is ignored). A leading
// 'v'/'V' is skipped.
inline std::vector<int> parse_version(const std::wstring& v) {
    std::vector<int> parts;
    size_t i = (!v.empty() && (v[0] == L'v' || v[0] == L'V')) ? 1 : 0;
    int cur = 0;
    for (; i < v.size(); ++i) {
        if (v[i] >= L'0' && v[i] <= L'9')      cur = cur * 10 + (v[i] - L'0');
        else if (v[i] == L'.')               { parts.push_back(cur); cur = 0; }
        else                                   break;   // pre-release / build suffix
    }
    parts.push_back(cur);
    return parts;
}

// True if version string `a` represents a strictly newer release than `b`.
inline bool is_newer(const std::wstring& a, const std::wstring& b) {
    std::vector<int> pa = parse_version(a), pb = parse_version(b);
    size_t n = (pa.size() > pb.size()) ? pa.size() : pb.size();
    for (size_t i = 0; i < n; ++i) {
        int x = (i < pa.size()) ? pa[i] : 0;
        int y = (i < pb.size()) ? pb[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

// Pull the value of a top-level JSON string field ("key":"value") out of `body`.
// Sufficient for GitHub's tag_name, which contains no escaped characters.
inline std::string json_string_field(const std::string& body, const char* key) {
    std::string needle = std::string("\"") + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return "";
    size_t colon = body.find(':', k + needle.size());
    if (colon == std::string::npos) return "";
    size_t q1 = body.find('"', colon);
    if (q1 == std::string::npos) return "";
    size_t q2 = body.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return body.substr(q1 + 1, q2 - q1 - 1);
}

// Minimal HTTPS GET via WinHTTP. Returns true and fills `body` on HTTP 200;
// otherwise returns false with a user-facing reason in `err`.
inline bool https_get(const wchar_t* host, const wchar_t* path,
                      std::string& body, std::wstring& err) {
    // User-Agent is REQUIRED by the GitHub API (it 403s anonymous UAs).
    HINTERNET hSession = WinHttpOpen(L"MarkdownViewer", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) { err = L"Could not initialize the network stack."; return false; }
    WinHttpSetTimeouts(hSession, 8000, 8000, 8000, 8000);   // resolve/connect/send/receive (ms)

    bool ok = false;
    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET hRequest = hConnect ? WinHttpOpenRequest(hConnect, L"GET", path, nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        WINHTTP_FLAG_SECURE)
                                  : nullptr;
    if (!hRequest) {
        err = L"Could not reach GitHub. Check your internet connection.";
    } else {
        const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
        if (WinHttpSendRequest(hRequest, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, nullptr)) {
            DWORD status = 0, len = sizeof(status);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
            if (status == 200) {
                DWORD avail = 0;
                do {
                    if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
                    std::string chunk(avail, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(hRequest, &chunk[0], avail, &read)) break;
                    body.append(chunk.data(), read);
                    if (body.size() > 1024 * 1024) break;   // sanity cap; the payload is ~tens of KB
                } while (avail > 0);
                ok = !body.empty();
                if (!ok) err = L"GitHub returned an empty response.";
            } else {
                err = L"GitHub returned HTTP status " + std::to_wstring(status) + L".";
            }
        } else {
            err = L"The update request failed. Check your internet connection.";
        }
    }
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

// Kick off an asynchronous update check. Result is delivered to `hwnd` via
// cfg::WM_APP_UPDATE_RESULT (lParam = new Result*, handler deletes it).
inline void check_async(HWND hwnd) {
    std::thread([hwnd]() {
        Result* r = new Result();
        std::string body;
        std::wstring err;
        if (!https_get(cfg::kUpdateHost, cfg::kUpdatePath, body, err)) {
            r->status  = Status::Error;
            r->message = err.empty() ? L"The update check failed." : err;
        } else {
            std::string tag = json_string_field(body, "tag_name");
            if (tag.empty()) {
                r->status  = Status::Error;
                r->message = L"Could not read the latest version from GitHub.";
            } else {
                std::wstring latest = fileio::widen(tag);
                if (is_newer(latest, cfg::kAppVersion)) {
                    r->status = Status::Available;
                    r->latest = latest;
                } else {
                    r->status = Status::UpToDate;
                }
            }
        }
        PostMessageW(hwnd, cfg::WM_APP_UPDATE_RESULT, 0, reinterpret_cast<LPARAM>(r));
    }).detach();
}

} // namespace update
