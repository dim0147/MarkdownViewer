# Markdown Viewer

A small, fast Markdown viewer for Windows: a single native exe (Win32, C++17)
rendering with **markdown-it + highlight.js inside WebView2** (Edge/Chromium).
No installer, no admin rights.

## Features

- **GitHub-style rendering** (CommonMark + GFM via markdown-it): headings,
  lists, task lists, tables with alignment, fenced code blocks with **syntax
  highlighting**, blockquotes, links, images, strikethrough, autolinks
- **Light / dark theme** — follows Windows automatically, or force via settings
- **Explorer integration** — right-click any `.md` file → *View with Markdown
  Viewer* (on Windows 11 it may be under *Show more options*)
- **Drag & drop** a `.md` file anywhere on the window; `Ctrl+O` to browse;
  or pass a file on the command line
- `F5` reloads the file *and* your settings (handy while editing)
- Clicking a relative `.md` link opens it in the viewer; external links open
  in your default browser
- Handles UTF-8 / UTF-16 / ANSI files; relative images resolve against the
  document's folder

## Settings

*Tools → Settings (config.json)* opens `%APPDATA%\MarkdownViewer\config.json`:

```json
{
  "theme": "auto",          // "auto" | "light" | "dark"
  "maxWidth": 920,          // content width in px, 0 = full width
  "fontSize": 16,
  "syntaxHighlight": true,
  "linkify": true,          // turn bare URLs into links
  "typographer": false      // smart quotes and dashes
}
```

Press `F5` in the viewer to apply changes.

## Building

Requires Visual Studio (Community is fine) with the C++ workload:

```bat
build.bat
```

or open `MarkdownViewer.sln`. The first build downloads the pinned WebView2
SDK into `third_party/` and generates the app icon automatically.

Running requires the **WebView2 Runtime**, preinstalled on Windows 11 and
recent Windows 10.

## Explorer right-click menu

| Action  | How |
|---------|-----|
| Install | *Tools > Add to Explorer right-click menu* (or `MarkdownViewer.exe --register`) |
| Remove  | *Tools > Remove from Explorer right-click menu* (or `MarkdownViewer.exe --unregister`) |

Registration is per-user (`HKCU`), so no administrator rights are needed.
It covers `.md`, `.markdown`, `.mdown`, and `.mkd`.

> **Note:** if you move `MarkdownViewer.exe` to a different folder, run
> *Tools > Add to Explorer right-click menu* again so the registered path is updated.

## Project layout

```
src/          C++ shell: window, WebView2 host, file IO, registry (see AGENT.md)
assets/       web renderer: index.html, app.js (markdown-it), app.css, vendor libs
res/          icon, manifest, version info; embeds assets/ into the exe
tools/        WebView2 SDK fetcher, icon generator
test/         sample.md rendering smoke test
AGENT.md      architecture guide for maintainers (start here)
```
