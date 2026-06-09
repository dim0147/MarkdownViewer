# Navigation test — Home

This folder exercises the **Back / Forward** history and **per-document scroll
memory** added in v2.4. Open this file in the viewer, then click through the
links below.

## What to check

1. Click **[Chapter One](chapter-one.md)** — it opens in the viewer and the
   floating **◂** (Back) button appears in the top-left corner.
2. From Chapter One, click on to **Chapter Two**, then **Chapter Three**.
3. Press **Back** (or `Alt+Left`) repeatedly — you should retrace
   Three → Two → One → Home, and the **▸** (Forward) button lights up.
4. Press **Forward** (or `Alt+Right`) to walk the chain again.
5. From Chapter One, after going Back to Home, click **Sidebar** instead —
   the old forward history (Two/Three) is discarded, like a web browser.

## Scroll memory

Each chapter is padded with filler so it scrolls. Scroll to the bottom of a
chapter, follow a link, then press **Back** — you should land exactly where you
left off, not at the top.

## Links

- [Chapter One](chapter-one.md)
- [Sidebar (alternate branch)](sidebar.md)

The link below points at the parent folder. It is **intentionally ignored** —
the viewer never follows a link out of the current document's folder
(`path_is_under` containment), so clicking it does nothing:

- [Up to the manual index](../README-test.md)
