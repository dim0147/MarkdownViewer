# AGENT.md — Markdown Viewer

Guide for AI agents (and humans) maintaining this codebase. Read this before
changing anything.

## What this is

A small native Windows Markdown viewer: a single portable `MarkdownViewer.exe`
(Win32, C++17) that renders `.md` files with **markdown-it + highlight.js
inside WebView2** (Edge/Chromium). Users open files via Explorer right-click,
drag & drop, `Ctrl+O`, or the command line. An optional per-user Inno Setup
installer (`installer/`) wraps the exe for end users — see "Installer" below.

## Tech stack

| Layer | Technology | Why |
|-------|------------|-----|
| Shell | Pure Win32 (no MFC/Qt), C++17, single translation unit | tiny exe, instant startup |
| Web engine | **WebView2** (Edge/Chromium, evergreen) | modern CSS/JS; runtime preinstalled on Win 10/11 |
| Markdown parser | **markdown-it 14** (+ `task-lists`, `anchor` plugins) | CommonMark + GFM, same parser VS Code uses |
| Syntax highlighting | **highlight.js 11** (common-languages build) | zero config, fenced-block `language-x` classes |
| Styling | **github-markdown-css 5** (light + dark) | authentic GitHub look, auto dark mode |
| WebView2 SDK | nuget `Microsoft.Web.WebView2` (pinned in `tools/get_webview2.ps1`) | headers + `WebView2LoaderStatic.lib` |

Markdown parsing happens **in JavaScript inside the webview** (`assets/app.js`),
not in C++. C++ only reads the file bytes, decodes the text encoding, and ships
the source text to the renderer as a JSON message.

## Directory layout

```
src/                  C++ (header-only modules, all included by main.cpp)
  main.cpp            entry point, window proc, menu — keep this thin
  config.h            ALL compile-time constants (names, IDs, hosts, limits)
  app.h               App class: current document, render flow, dialogs
  webview2host.h      WebViewHost: WebView2 embedding, virtual hosts,
                      navigation policy, ComHandler<> callback template
  fileio.h            encoding-aware file read, paths, JSON-escape, URL-decode
  settings.h          runtime config.json (create defaults / load raw text)
  assets.h            extracts embedded RCDATA assets to disk at startup
  registry.h          per-user (HKCU) Explorer context-menu install/remove
assets/               web UI, embedded into the exe via res/app.rc
  index.html          shell page (absolute https://viewer.assets/ URLs)
  app.js              THE RENDERER: message protocol, markdown-it setup, theme
  app.css             page chrome + welcome screen (doc style comes from vendor)
  vendor/             pinned third-party JS/CSS (committed, do not hand-edit)
res/                  icon, manifest, version info, RCDATA asset entries
tools/
  get_webview2.ps1    downloads the pinned WebView2 SDK into third_party/
  make_icon.ps1       generates res/icon.ico
third_party/webview2/ WebView2 SDK (gitignored; auto-downloaded by build)
installer/
  MarkdownViewer.iss  Inno Setup script — per-user installer (see "Installer")
  output/             built setup exe (gitignored)
test/sample.md        rendering smoke test — open it after any renderer change
build.bat             one-shot CLI build (also: MarkdownViewer.sln for VS)
```

## Architecture / data flow

```
 wWinMain ── creates window ── WM_CREATE ── App::Init
                                              │ assets::extract_all()        (exe resources → %LOCALAPPDATA%\MarkdownViewer\assets)
                                              │ WebViewHost::Create          (async!)
                                              ▼
                       WebView2 controller ready ── Setup()
                          │ virtual host: viewer.assets → extracted assets dir
                          │ Navigate(https://viewer.assets/index.html)
                          ▼
                       app.js loads ── postMessage("ready")
                          ▼
                       App::OnWebReady ── PostWebMessageAsJson:
                          { type:"config",  config:"<raw config.json text>" }
                          { type:"render",  name, markdown }   or  { type:"welcome" }
                          ▼
                       app.js: markdown-it.render() → #content.innerHTML
```

Key points:

- **WebView2 creation is asynchronous.** A file passed on the command line is
  stored in `App::m_current` and rendered when `OnWebReady()` fires. Anything
  you add that touches the webview must tolerate `m_ready == false`.
- **Two virtual hosts** (`SetVirtualHostNameToFolderMapping`):
  - `viewer.assets` → extracted UI assets (fixed for process lifetime)
  - `viewer.doc` → folder of the current document, remapped on every file open
    so relative images/links resolve (`<base href="https://viewer.doc/">` in
    index.html).
- **Navigation policy** (`WebViewHost::HandleNavigation`): the webview is only
  ever allowed to display `viewer.assets/*`. Clicks on `.md` links under
  `viewer.doc` reopen in the viewer; everything else (http/https/mailto,
  non-md doc files) is cancelled and handed to the OS (`ShellExecute`).
  In-page `#anchor` clicks never navigate — app.js intercepts and scrolls.
- **Keyboard**: the webview owns focus, so `Ctrl+O`/`F5` are caught in
  `add_AcceleratorKeyPressed` and posted to the main window as `WM_COMMAND`.
  The `HACCEL` table in main.cpp only covers the rare main-window-focused case.
- **Drag & drop**: `put_AllowExternalDrop(FALSE)` on the controller makes
  drops fall through to the main window's `WM_DROPFILES`.
- **F5 (Reload)** re-reads `config.json` *and* the document — settings edits
  apply without restarting.

## Configuration

Two distinct things, don't mix them up:

1. **Compile-time** — `src/config.h`: app name, extensions, virtual host
   names, window sizes, command IDs, file-size cap.
2. **Runtime (user)** — `%APPDATA%\MarkdownViewer\config.json`: theme,
   maxWidth, fontSize, syntaxHighlight, linkify, typographer.
   C++ treats it as an **opaque string**; only `assets/app.js` knows the
   options (defaults in `DEFAULTS` must mirror `settings.h`'s
   `kDefaultConfigJson`). Adding a new option = edit those two files only.

## Security invariants (do not weaken)

- `markdown-it` runs with **`html: false`** — raw HTML in documents is escaped,
  so documents cannot inject script. If you ever enable `html: true`, you MUST
  add a sanitizer (e.g. DOMPurify) in front of `innerHTML`.
- The navigation allow-list in `HandleNavigation` keeps arbitrary web content
  out of the (privileged-feeling) app window.
- `put_AreHostObjectsAllowed(FALSE)`; the only host↔web channel is JSON
  messages.
- Explorer integration is strictly per-user `HKCU` (no admin, easy uninstall).

## Build

```bat
build.bat                 rem CLI build → MarkdownViewer.exe in repo root
```
or open `MarkdownViewer.sln` (VS 2022+, output under `build/`). Both routes
auto-download the WebView2 SDK via `tools/get_webview2.ps1` (pinned version)
into `third_party/` on first build. `res/icon.ico` is generated on first build.

There is no test suite; verification is: build, run
`MarkdownViewer.exe test\sample.md`, and eyeball the output (headings, task
checkboxes, highlighted C++ block, table alignment, dark mode if the OS is
dark). Update `test/sample.md` when adding renderer features.

## Installer (installer/MarkdownViewer.iss)

Inno Setup 7 script producing a **per-user** installer (no admin —
`PrivilegesRequired=lowest`, installs to `%LOCALAPPDATA%\Programs\Markdown
Viewer`), matching the app's HKCU-only philosophy.

```bat
build.bat                                rem produces MarkdownViewer.exe (packaged file)
ISCC.exe installer\MarkdownViewer.iss    rem → installer\output\MarkdownViewer-Setup-<ver>.exe
```

Design (full rationale in the .iss header comment):

- **Version** comes from the exe's VERSIONINFO via
  `GetVersionNumbersString` — bump `res/app.rc` (and `cfg::kAppVersion`),
  never the .iss.
- **Install**: exe + README to `{app}`, Start menu shortcut; optional tasks
  for the Explorer context menu (on by default; runs the app's own
  `--register`) and a desktop icon. Warns (non-blocking) if the WebView2
  Runtime is missing.
- **Uninstall leaves NOTHING behind** — this is a hard guarantee, preserve
  it. It removes, in order: the context-menu keys via `[UninstallRun]`
  `--unregister`, then `{app}`, then via `[UninstallDelete]` the runtime
  caches `%LOCALAPPDATA%\MarkdownViewer` (extracted assets + WebView2
  profile) and settings `%APPDATA%\MarkdownViewer` (config.json), and
  finally a `[Code]` fallback sweep deletes the HKCU
  `SystemFileAssociations\<ext>\shell\MarkdownViewer` keys in case
  `--unregister` couldn't run.

**Sync invariants** — if you change any of these in C++, mirror the .iss:

| C++ source | .iss counterpart |
|------------|------------------|
| `cfg::kMarkdownExts` (config.h) | extension list in `CurUninstallStepChanged` |
| `cfg::kAppDirName` (config.h) | `#define MyAppDirName` (used by `[UninstallDelete]`) |
| new on-disk/registry footprint anywhere | new `[UninstallDelete]`/`[Code]` cleanup entry |
| `--register`/`--unregister` CLI (main.cpp) | `[Run]`/`[UninstallRun]` entries |

To verify a clean uninstall: silent-install (`/VERYSILENT`), launch the app
once (creates caches), silent-uninstall, then confirm `{app}`, both appdata
dirs, the Start menu shortcut, the 4 context-menu keys, and
`HKCU\...\Uninstall\{EE354A00-3CF6-4974-8750-B4BD75B21925}_is1` are all gone.

## Common tasks

- **Add a markdown feature (e.g. mermaid, KaTeX):** drop the minified UMD
  build into `assets/vendor/`, add `<script>`/`<link>` in `index.html`
  (absolute `https://viewer.assets/...` URL!), wire it in `app.js`'s
  `buildParser()`/`render()`, then register the file in BOTH `res/app.rc`
  (RCDATA) and `src/assets.h` (`kAssets`). Those two lists must stay in sync.
- **Upgrade a vendor lib:** replace the file in `assets/vendor/` (they're
  pinned, committed copies from jsdelivr), rebuild.
- **Upgrade the WebView2 SDK:** bump `$version` in `tools/get_webview2.ps1`,
  delete `third_party/webview2`, rebuild.
- **New menu item:** ID in `config.h`, item in `build_menu()` (main.cpp),
  handler in the `WM_COMMAND` switch, logic in `app.h`.

## Gotchas

- `assets/` files are **baked into the exe at compile time** and re-extracted
  to `%LOCALAPPDATA%\MarkdownViewer\assets` on every launch. Editing an asset
  requires a rebuild (rc + link) to show up.
- All asset URLs in `index.html` must be absolute (`https://viewer.assets/...`)
  because `<base>` points at `viewer.doc`.
- `ComHandler` (webview2host.h) self-deletes on Release and answers QI only
  for `IUnknown` + its own interface — fine for WebView2's usage; don't reuse
  one instance for two registrations.
- Absolute `file:///` image paths inside documents won't load (cross-origin
  from the `https://viewer.doc` origin). Relative paths are the supported way.
- `WebView2LoaderStatic.lib` needs `version.lib` (linked via `#pragma` in
  main.cpp). The loader is CRT-independent, works with /MT and /MD.
- The legacy v1 (hand-written C++ parser + IE WebBrowser control) was removed
  in v2; if you see references to `markdown.h`/`webhost.h`, they're stale.
