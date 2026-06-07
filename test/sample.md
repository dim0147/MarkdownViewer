# Markdown Viewer Test

This file exercises **bold**, *italic*, ***bold italic***, ~~strikethrough~~,
`inline code`, and a [link](https://example.com "Example"). Autolink: https://github.com

## Lists

1. First ordered item
2. Second item
   - Nested bullet
   - Another nested bullet
3. Third item

- [x] Completed task
- [ ] Pending task

## Code

```cpp
#include <iostream>
int main() {
    std::cout << "Hello, Markdown!" << std::endl;  // <html> chars & escaped
    return 0;
}
```

## Table

| Feature      | Status | Notes          |
|:-------------|:------:|---------------:|
| Headings     |   OK   | h1 through h6  |
| Code blocks  |   OK   | fenced + indented |
| Tables       |   OK   | with alignment |

## Quote

> Markdown is a lightweight markup language.
> — *John Gruber*
>
> > Nested quotes also work.

---

Setext Heading
==============

Final paragraph with a hard break here.
And the second line after a break.
