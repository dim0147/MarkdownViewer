#pragma once
// markdown.h - small dependency-free Markdown -> HTML converter
// Supports a practical GitHub-flavored subset:
//   ATX + setext headings, fenced/indented code blocks, blockquotes (nested),
//   ordered/unordered/nested lists, task lists, tables with alignment, hr,
//   emphasis (*,_,**,***), strikethrough, inline code, links, images,
//   autolinks, hard line breaks, backslash escapes.
// All raw HTML in the source is escaped (rendered literally) for safety.

#include <string>
#include <vector>
#include <cctype>
#include <cstring>

namespace md {

inline bool is_space_only(const std::string& s) {
    for (char c : s) if (c != ' ' && c != '\t' && c != '\r') return false;
    return true;
}

inline size_t indent_of(const std::string& s) {
    size_t n = 0;
    while (n < s.size() && s[n] == ' ') n++;
    return n;
}

inline std::string ltrim(std::string s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(i);
}

inline std::string rtrim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

inline std::string trim(const std::string& s) { return rtrim(ltrim(s)); }

inline std::string escape_html(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        default:   out += c;
        }
    }
    return out;
}

// Block dangerous URL schemes since output is rendered in a browser control.
inline std::string safe_url(const std::string& u) {
    std::string l;
    for (char c : u) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
        l += (char)tolower((unsigned char)c);
    }
    if (l.rfind("javascript:", 0) == 0 || l.rfind("vbscript:", 0) == 0 || l.rfind("data:", 0) == 0)
        return "#";
    return u;
}

inline std::string slugify(const std::string& text) {
    std::string s;
    for (char c : text) {
        unsigned char u = (unsigned char)c;
        if (isalnum(u)) s += (char)tolower(u);
        else if (u >= 0x80) s += c;                       // keep UTF-8 bytes
        else if (c == ' ' || c == '-' || c == '_') s += '-';
    }
    return s;
}

// ---------------------------------------------------------------- inline ---

std::string parse_inline(const std::string& s);

// Find a closing run of `cnt` chars `ch` at position > from, preceded by non-space.
inline size_t find_close_run(const std::string& s, size_t from, char ch, size_t cnt) {
    size_t i = from;
    while (i < s.size()) {
        if (s[i] == '\\') { i += 2; continue; }
        if (s[i] == ch) {
            size_t run = 1;
            while (i + run < s.size() && s[i + run] == ch) run++;
            if (run >= cnt && i > from && !isspace((unsigned char)s[i - 1])) return i;
            i += run;
        } else {
            i++;
        }
    }
    return std::string::npos;
}

// Parse "[text](url "title")" starting at s[pos] == '['.
inline bool parse_link_body(const std::string& s, size_t pos, std::string& text,
                            std::string& url, std::string& title, size_t& endpos) {
    int depth = 0;
    size_t close_br = std::string::npos;
    for (size_t i = pos; i < s.size(); ++i) {
        if (s[i] == '\\') { i++; continue; }
        if (s[i] == '[') depth++;
        else if (s[i] == ']') { if (--depth == 0) { close_br = i; break; } }
    }
    if (close_br == std::string::npos) return false;
    if (close_br + 1 >= s.size() || s[close_br + 1] != '(') return false;
    int pd = 0;
    size_t close_par = std::string::npos;
    for (size_t i = close_br + 1; i < s.size(); ++i) {
        if (s[i] == '\\') { i++; continue; }
        if (s[i] == '(') pd++;
        else if (s[i] == ')') { if (--pd == 0) { close_par = i; break; } }
    }
    if (close_par == std::string::npos) return false;

    text = s.substr(pos + 1, close_br - pos - 1);
    std::string inner = trim(s.substr(close_br + 2, close_par - close_br - 2));
    title.clear();
    if (!inner.empty() && (inner.back() == '"' || inner.back() == '\'')) {
        char q = inner.back();
        size_t open = inner.rfind(q, inner.size() - 2);
        if (open != std::string::npos && open > 0 && isspace((unsigned char)inner[open - 1])) {
            title = inner.substr(open + 1, inner.size() - open - 2);
            inner = trim(inner.substr(0, open));
        }
    }
    if (inner.size() >= 2 && inner.front() == '<' && inner.back() == '>')
        inner = inner.substr(1, inner.size() - 2);
    url = inner;
    endpos = close_par + 1;
    return true;
}

inline std::string parse_inline(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 32);
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];

        // backslash escapes (also "\<newline>" = hard break)
        if (c == '\\' && i + 1 < n) {
            char d = s[i + 1];
            if (d == '\n') { out += "<br>\n"; i += 2; continue; }
            if (ispunct((unsigned char)d)) {
                out += escape_html(std::string(1, d));
                i += 2;
                continue;
            }
        }

        // newline: hard break if the line ended with two+ spaces
        if (c == '\n') {
            size_t sp = 0;
            while (!out.empty() && out.back() == ' ') { out.pop_back(); sp++; }
            out += (sp >= 2) ? "<br>\n" : "\n";
            i++;
            continue;
        }

        // inline code span
        if (c == '`') {
            size_t ticks = 1;
            while (i + ticks < n && s[i + ticks] == '`') ticks++;
            size_t j = i + ticks, close = std::string::npos;
            while (j < n) {
                if (s[j] == '`') {
                    size_t run = 1;
                    while (j + run < n && s[j + run] == '`') run++;
                    if (run == ticks) { close = j; break; }
                    j += run;
                } else j++;
            }
            if (close != std::string::npos) {
                std::string code = s.substr(i + ticks, close - i - ticks);
                if (code.size() >= 2 && code.front() == ' ' && code.back() == ' ')
                    code = code.substr(1, code.size() - 2);
                out += "<code>" + escape_html(code) + "</code>";
                i = close + ticks;
                continue;
            }
            out += escape_html(std::string(ticks, '`'));
            i += ticks;
            continue;
        }

        // image
        if (c == '!' && i + 1 < n && s[i + 1] == '[') {
            std::string text, url, title; size_t end;
            if (parse_link_body(s, i + 1, text, url, title, end)) {
                out += "<img src=\"" + escape_html(safe_url(url)) + "\" alt=\"" + escape_html(text) + "\"";
                if (!title.empty()) out += " title=\"" + escape_html(title) + "\"";
                out += ">";
                i = end;
                continue;
            }
        }

        // link
        if (c == '[') {
            std::string text, url, title; size_t end;
            if (parse_link_body(s, i, text, url, title, end)) {
                out += "<a href=\"" + escape_html(safe_url(url)) + "\"";
                if (!title.empty()) out += " title=\"" + escape_html(title) + "\"";
                out += ">" + parse_inline(text) + "</a>";
                i = end;
                continue;
            }
        }

        // emphasis / strong
        if (c == '*' || c == '_') {
            size_t run = 1;
            while (i + run < n && s[i + run] == c) run++;
            bool can_open = (i + run < n) && !isspace((unsigned char)s[i + run]);
            if (c == '_' && i > 0 && isalnum((unsigned char)s[i - 1])) can_open = false; // snake_case
            if (can_open) {
                if (run >= 3) {
                    size_t close = find_close_run(s, i + 3, c, 3);
                    if (close != std::string::npos) {
                        out += "<strong><em>" + parse_inline(s.substr(i + 3, close - i - 3)) + "</em></strong>";
                        i = close + 3;
                        continue;
                    }
                }
                if (run >= 2) {
                    size_t close = find_close_run(s, i + 2, c, 2);
                    if (close != std::string::npos) {
                        out += "<strong>" + parse_inline(s.substr(i + 2, close - i - 2)) + "</strong>";
                        i = close + 2;
                        continue;
                    }
                }
                if (run == 1) {
                    size_t close = find_close_run(s, i + 1, c, 1);
                    if (close != std::string::npos) {
                        out += "<em>" + parse_inline(s.substr(i + 1, close - i - 1)) + "</em>";
                        i = close + 1;
                        continue;
                    }
                }
            }
            out += escape_html(std::string(run, c));
            i += run;
            continue;
        }

        // strikethrough
        if (c == '~' && i + 1 < n && s[i + 1] == '~') {
            size_t close = s.find("~~", i + 2);
            if (close != std::string::npos && close > i + 2) {
                out += "<del>" + parse_inline(s.substr(i + 2, close - i - 2)) + "</del>";
                i = close + 2;
                continue;
            }
        }

        // autolink <http://...> / <user@host>
        if (c == '<') {
            size_t close = s.find('>', i + 1);
            if (close != std::string::npos) {
                std::string in = s.substr(i + 1, close - i - 1);
                if (in.find("://") != std::string::npos && in.find(' ') == std::string::npos) {
                    out += "<a href=\"" + escape_html(safe_url(in)) + "\">" + escape_html(in) + "</a>";
                    i = close + 1;
                    continue;
                }
                if (in.find('@') != std::string::npos && in.find(' ') == std::string::npos &&
                    in.find(':') == std::string::npos) {
                    out += "<a href=\"mailto:" + escape_html(in) + "\">" + escape_html(in) + "</a>";
                    i = close + 1;
                    continue;
                }
            }
            out += "&lt;";
            i++;
            continue;
        }

        // bare http(s) autolink
        if (c == 'h' && (s.compare(i, 7, "http://") == 0 || s.compare(i, 8, "https://") == 0) &&
            (i == 0 || !isalnum((unsigned char)s[i - 1]))) {
            size_t j = i;
            while (j < n && !isspace((unsigned char)s[j]) && s[j] != '<' && s[j] != '>' && s[j] != '"') j++;
            while (j > i && strchr(".,;:!?)", s[j - 1])) j--;
            std::string u = s.substr(i, j - i);
            out += "<a href=\"" + escape_html(u) + "\">" + escape_html(u) + "</a>";
            i = j;
            continue;
        }

        switch (c) {
        case '&': out += "&amp;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default:  out += c;
        }
        i++;
    }
    return out;
}

// ---------------------------------------------------------------- blocks ---

struct ListMarker {
    bool ok = false;
    bool ordered = false;
    long num = 1;
    size_t indent = 0;
    size_t content = 0;
};

inline ListMarker match_marker(const std::string& line) {
    ListMarker m;
    size_t i = indent_of(line);
    if (i >= line.size()) return m;
    char c = line[i];
    if (c == '-' || c == '*' || c == '+') {
        if (i + 1 < line.size() && line[i + 1] != ' ') return m;
        m.ok = true;
        m.indent = i;
        size_t j = i + 1, lim = i + 5;
        while (j < line.size() && j < lim && line[j] == ' ') j++;
        m.content = (j == i + 1) ? i + 2 : j;
        return m;
    }
    if (isdigit((unsigned char)c)) {
        size_t j = i;
        while (j < line.size() && isdigit((unsigned char)line[j]) && j - i < 9) j++;
        if (j < line.size() && (line[j] == '.' || line[j] == ')')) {
            if (j + 1 < line.size() && line[j + 1] != ' ') return m;
            m.ok = true;
            m.ordered = true;
            m.num = atol(line.substr(i, j - i).c_str());
            m.indent = i;
            size_t k = j + 1, lim = j + 5;
            while (k < line.size() && k < lim && line[k] == ' ') k++;
            m.content = (k == j + 1) ? j + 2 : k;
        }
    }
    return m;
}

inline bool is_hr(const std::string& t) {
    if (t.size() < 3) return false;
    char c = t[0];
    if (c != '-' && c != '*' && c != '_') return false;
    int cnt = 0;
    for (char d : t) {
        if (d == c) cnt++;
        else if (d != ' ') return false;
    }
    return cnt >= 3;
}

inline bool all_char(const std::string& t, char ch) {
    if (t.empty()) return false;
    for (char c : t) if (c != ch) return false;
    return true;
}

inline bool is_table_sep(const std::string& line) {
    std::string t = trim(line);
    if (t.find('-') == std::string::npos) return false;
    if (t.find('|') == std::string::npos) return false;
    for (char c : t)
        if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
    return true;
}

inline std::vector<std::string> split_row(const std::string& line) {
    std::vector<std::string> cells;
    std::string t = trim(line), cur;
    size_t b = 0, e = t.size();
    if (!t.empty() && t.front() == '|') b = 1;
    if (e > b && t.back() == '|' && (e < 2 || t[e - 2] != '\\')) e--;
    for (size_t i = b; i < e; i++) {
        if (t[i] == '\\' && i + 1 < e && t[i + 1] == '|') { cur += '|'; i++; continue; }
        if (t[i] == '|') { cells.push_back(trim(cur)); cur.clear(); }
        else cur += t[i];
    }
    cells.push_back(trim(cur));
    return cells;
}

inline bool starts_block(const std::string& line) {
    if (is_space_only(line)) return true;
    size_t ind = indent_of(line);
    if (ind > 3) return false;
    std::string t = trim(line);
    if (t.compare(0, 3, "```") == 0 || t.compare(0, 3, "~~~") == 0) return true;
    if (t[0] == '#') {
        size_t lv = 0;
        while (lv < t.size() && t[lv] == '#') lv++;
        if (lv <= 6 && (lv == t.size() || t[lv] == ' ')) return true;
    }
    if (is_hr(t)) return true;
    if (t[0] == '>') return true;
    if (match_marker(line).ok) return true;
    return false;
}

void parse_blocks(const std::vector<std::string>& L, size_t b, size_t e, std::string& out);

inline void parse_list(const std::vector<std::string>& L, size_t& i, size_t e, std::string& out) {
    ListMarker first = match_marker(L[i]);
    bool ordered = first.ordered;
    size_t base = first.indent;
    size_t cont = first.content;
    bool loose = false, have = false;
    std::vector<std::vector<std::string>> items;
    std::vector<std::string> cur;

    while (i < e) {
        const std::string& line = L[i];
        if (is_space_only(line)) {
            size_t j = i;
            while (j < e && is_space_only(L[j])) j++;
            if (j >= e) { i = j; break; }
            ListMarker m2 = match_marker(L[j]);
            bool next_item = m2.ok && m2.indent >= base && m2.indent < cont;
            bool continuation = indent_of(L[j]) >= cont;
            if (next_item || continuation) {
                if (next_item) loose = true;
                if (continuation && !next_item) cur.push_back("");
                i = j;
                continue;
            }
            i = j;
            break;
        }
        ListMarker m = match_marker(line);
        if (m.ok && m.indent >= base && m.indent < cont) {
            if (have) items.push_back(cur);
            cur.clear();
            cur.push_back(m.content < line.size() ? line.substr(m.content) : std::string());
            cont = m.content;
            have = true;
            i++;
            continue;
        }
        if (indent_of(line) >= cont) {
            cur.push_back(line.substr(cont));
            i++;
            continue;
        }
        if (have && !starts_block(line)) {       // lazy paragraph continuation
            cur.push_back(trim(line));
            i++;
            continue;
        }
        break;
    }
    if (have) items.push_back(cur);

    out += ordered
        ? (first.num != 1 ? "<ol start=\"" + std::to_string(first.num) + "\">\n" : "<ol>\n")
        : "<ul>\n";
    for (auto& item : items) {
        bool task = false, checked = false;
        if (!item.empty() && item[0].size() >= 3 && item[0][0] == '[' && item[0][2] == ']' &&
            (item[0].size() == 3 || item[0][3] == ' ')) {
            char x = item[0][1];
            if (x == ' ' || x == 'x' || x == 'X') {
                task = true;
                checked = (x != ' ');
                item[0] = item[0].size() > 4 ? item[0].substr(4) : std::string();
            }
        }
        std::string ih;
        parse_blocks(item, 0, item.size(), ih);
        if (!loose && ih.compare(0, 3, "<p>") == 0 &&
            ih.find("<p>", 3) == std::string::npos &&
            ih.size() >= 8 && ih.compare(ih.size() - 5, 5, "</p>\n") == 0) {
            ih = ih.substr(3, ih.size() - 8);          // tight list: unwrap single <p>
        }
        if (task)
            out += std::string("<li class=\"task-list-item\"><input type=\"checkbox\" disabled") +
                   (checked ? " checked" : "") + "> " + ih + "</li>\n";
        else
            out += "<li>" + ih + "</li>\n";
    }
    out += ordered ? "</ol>\n" : "</ul>\n";
}

inline void parse_blocks(const std::vector<std::string>& L, size_t b, size_t e, std::string& out) {
    size_t i = b;
    while (i < e) {
        const std::string& line = L[i];
        if (is_space_only(line)) { i++; continue; }
        size_t ind = indent_of(line);
        std::string t = trim(line);

        // fenced code block
        if (ind <= 3 && (t.compare(0, 3, "```") == 0 || t.compare(0, 3, "~~~") == 0)) {
            char fc = t[0];
            size_t flen = 0;
            while (flen < t.size() && t[flen] == fc) flen++;
            std::string lang = trim(t.substr(flen));
            size_t sp = lang.find(' ');
            if (sp != std::string::npos) lang = lang.substr(0, sp);
            std::string code;
            size_t j = i + 1;
            for (; j < e; j++) {
                std::string tj = trim(L[j]);
                if (!tj.empty() && tj[0] == fc) {
                    size_t r = 0;
                    while (r < tj.size() && tj[r] == fc) r++;
                    if (r >= flen && trim(tj.substr(r)).empty()) break;
                }
                std::string cl = L[j];
                size_t k = 0;
                while (k < cl.size() && k < ind && cl[k] == ' ') k++;
                code += cl.substr(k);
                code += '\n';
            }
            out += "<pre><code";
            if (!lang.empty()) out += " class=\"language-" + escape_html(lang) + "\"";
            out += ">" + escape_html(code) + "</code></pre>\n";
            i = (j < e) ? j + 1 : e;
            continue;
        }

        // ATX heading
        if (ind <= 3 && t[0] == '#') {
            size_t lv = 0;
            while (lv < t.size() && t[lv] == '#') lv++;
            if (lv <= 6 && (lv == t.size() || t[lv] == ' ')) {
                std::string text = trim(t.substr(lv));
                while (!text.empty() && text.back() == '#') text.pop_back();
                text = rtrim(text);
                std::string lvs = std::to_string(lv);
                out += "<h" + lvs + " id=\"" + escape_html(slugify(text)) + "\">" +
                       parse_inline(text) + "</h" + lvs + ">\n";
                i++;
                continue;
            }
        }

        // horizontal rule
        if (ind <= 3 && is_hr(t)) { out += "<hr>\n"; i++; continue; }

        // blockquote
        if (ind <= 3 && t[0] == '>') {
            std::vector<std::string> q;
            size_t j = i;
            for (; j < e; j++) {
                const std::string& lj = L[j];
                size_t ij = indent_of(lj);
                if (ij <= 3 && ij < lj.size() && lj[ij] == '>') {
                    size_t p = ij + 1;
                    if (p < lj.size() && lj[p] == ' ') p++;
                    q.push_back(lj.substr(p));
                } else if (!is_space_only(lj) && !starts_block(lj) && !q.empty()) {
                    q.push_back(lj);                    // lazy continuation
                } else break;
            }
            std::string qh;
            parse_blocks(q, 0, q.size(), qh);
            out += "<blockquote>\n" + qh + "</blockquote>\n";
            i = j;
            continue;
        }

        // list
        if (match_marker(line).ok) { parse_list(L, i, e, out); continue; }

        // table
        if (line.find('|') != std::string::npos && i + 1 < e && is_table_sep(L[i + 1])) {
            std::vector<std::string> hdr = split_row(line);
            std::vector<std::string> seps = split_row(L[i + 1]);
            std::vector<std::string> aligns;
            for (auto& sc : seps) {
                bool l = !sc.empty() && sc.front() == ':';
                bool r = !sc.empty() && sc.back() == ':';
                aligns.push_back(l && r ? " style=\"text-align:center\""
                                 : r ? " style=\"text-align:right\""
                                 : l ? " style=\"text-align:left\"" : "");
            }
            while (aligns.size() < hdr.size()) aligns.push_back("");
            out += "<table>\n<thead><tr>";
            for (size_t k = 0; k < hdr.size(); k++)
                out += "<th" + aligns[k] + ">" + parse_inline(hdr[k]) + "</th>";
            out += "</tr></thead>\n<tbody>\n";
            size_t j = i + 2;
            for (; j < e; j++) {
                if (is_space_only(L[j]) || L[j].find('|') == std::string::npos) break;
                std::vector<std::string> row = split_row(L[j]);
                out += "<tr>";
                for (size_t k = 0; k < hdr.size(); k++)
                    out += "<td" + aligns[k] + ">" + (k < row.size() ? parse_inline(row[k]) : "") + "</td>";
                out += "</tr>\n";
            }
            out += "</tbody>\n</table>\n";
            i = j;
            continue;
        }

        // indented code block
        if (ind >= 4) {
            std::string code;
            size_t j = i;
            while (j < e) {
                if (is_space_only(L[j])) {
                    size_t k = j;
                    while (k < e && is_space_only(L[k])) k++;
                    if (k < e && indent_of(L[k]) >= 4) {
                        for (size_t q2 = j; q2 < k; q2++) code += '\n';
                        j = k;
                        continue;
                    }
                    break;
                }
                if (indent_of(L[j]) < 4) break;
                code += L[j].substr(4);
                code += '\n';
                j++;
            }
            out += "<pre><code>" + escape_html(code) + "</code></pre>\n";
            i = j;
            continue;
        }

        // paragraph (with setext heading detection)
        {
            std::string para = line;
            size_t j = i + 1;
            bool heading_done = false;
            for (; j < e; j++) {
                const std::string& lj = L[j];
                if (is_space_only(lj)) break;
                std::string tj = trim(lj);
                if (indent_of(lj) <= 3 && (all_char(tj, '=') || all_char(tj, '-'))) {
                    std::string text = trim(para);
                    std::string lvs = all_char(tj, '=') ? "1" : "2";
                    out += "<h" + lvs + " id=\"" + escape_html(slugify(text)) + "\">" +
                           parse_inline(text) + "</h" + lvs + ">\n";
                    i = j + 1;
                    heading_done = true;
                    break;
                }
                if (starts_block(lj)) break;
                para += '\n';
                para += lj;
            }
            if (heading_done) continue;
            out += "<p>" + parse_inline(rtrim(para)) + "</p>\n";
            i = j;
        }
    }
}

// ------------------------------------------------------------------- API ---

inline std::string to_html(const std::string& src) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : src) {
        if (c == '\r') continue;
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else if (c == '\t') cur += "    ";
        else cur += c;
    }
    lines.push_back(cur);
    std::string out;
    parse_blocks(lines, 0, lines.size(), out);
    return out;
}

} // namespace md
