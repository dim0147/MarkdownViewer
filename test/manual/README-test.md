# Markdown Viewer — test page

A quick tour of rendering features and link behavior. Everything here is safe.

---

## 1. Text formatting

**Bold**, *italic*, ***bold italic***, ~~strikethrough~~, `inline code`,
and a footnote-ish superscript via H~2~O is plain text (no sub/sup plugin).

> Blockquote — with a **nested** _emphasis_ and a [link](#9-links).

---

## 2. Lists

- Bullet one
- Bullet two
  - Nested bullet
    - Deeper
1. Ordered
2. Lists
   1. Nested ordered

### Task list (GFM)

- [x] Implemented renderer
- [x] Safe link handling
- [ ] Your test run

---

## 3. Code block (syntax highlighting)

```cpp
#include <iostream>
int main() {
    for (int i = 0; i < 3; ++i)
        std::cout << "Hello " << i << "\n";   // highlighted C++
    return 0;
}
```

```python
def greet(name: str) -> str:
    return f"Hello, {name}!"
```

---

## 4. Table

| Feature      | Status | Notes                  |
|:-------------|:------:|-----------------------:|
| Headings     |   ✅   | h1–h6                  |
| Tables       |   ✅   | alignment works        |
| Code         |   ✅   | highlight.js           |
| Dark mode    |   ✅   | follows OS / config    |

---

## 5. Heading anchors

This heading gets an auto-anchor (markdown-it-anchor). Link #11 below jumps to it.

---

## 6. Images

Inline image rendered from a remote URL (loads if you're online):

![Markdown logo](https://markdown-here.com/img/icon256.png)

---

## 7. Blockquotes / horizontal rule

> Tip: press **F5** to reload after editing this file.

---

## 8. Raw HTML is escaped (security)

The next line must appear as **literal text** — no popup, no bold:

`<script>alert('xss')</script>` and <b>this should not be bold</b>

---

## 9. Links

These OPEN IN THE VIEWER (linked markdown inside this document's folder):

- [Sibling notes.md](notes.md)
- [Subfolder sub/nested.md](sub/nested.md)
- [Encoded slash sub%2Fnested.md](sub%2Fnested.md)

This is intentionally **ignored** — links may not escape the document's
folder (`path_is_under` containment), so nothing happens:

- [Parent ../sample.md](../sample.md)
- [Encoded parent ..%2Fsample.md](..%2Fsample.md)

This OPENS IN YOUR BROWSER:

- [anthropic.com](https://www.anthropic.com)

This SCROLLS within the page (anchor):

- [Jump to Section Two](#section-two)

A link to a non-markdown local file is intentionally **ignored** (nothing
happens, by design — the viewer never launches local files):

- [some-file.txt](some-file.txt)

---

## Section Two

You scrolled here from link #11. ✅
