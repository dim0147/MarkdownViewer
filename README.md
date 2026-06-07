# Markdown Viewer

A tiny, dependency-free Markdown viewer for Windows, written in C++ (pure Win32).
Single ~250 KB exe — no runtime, no installer, no external libraries.

## Features

- **Explorer integration** — right-click any `.md` file → *View with Markdown Viewer*
  (on Windows 11 it may be under *Show more options*)
- **Drag & drop** — drop a `.md` file anywhere on the window
- **Open dialog** — `Ctrl+O`, or pass a file on the command line
- `F5` reloads the current file (handy while editing)
- GitHub-style rendering: headings, lists, task lists, tables with alignment,
  fenced/indented code blocks, blockquotes, links, images, emphasis,
  strikethrough, autolinks, horizontal rules
- Handles UTF-8 / UTF-16 / ANSI files; relative images and links resolve
  against the document's folder

## Building

Requires Visual Studio (Community is fine) with the C++ workload:

```bat
build.bat
```

The script locates `vcvars64.bat`, generates the app icon, compiles resources,
and produces `MarkdownViewer.exe`.

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
src/main.cpp      window, menus, file loading, registry integration
src/markdown.h    Markdown -> HTML converter (GFM subset)
src/webhost.h     embedded browser host + drag & drop target
res/              icon, manifest, version resources
tools/            icon generator (PowerShell)
test/sample.md    rendering smoke test
```
