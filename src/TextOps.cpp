// Camelion++ - text transformations used by the Edit menu
// Standard headers used in this file (listed explicitly: newer GCC/libstdc++ no longer include them transitively)
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "TextOps.h"
#include "Editor.h"
#include "Parameters.h"
#include <glib.h>
#include <algorithm>
#include <random>
#include <set>
#include <cstring>

namespace cam {

static std::string eolString(Editor& e)
{
    switch (e.call(SCI_GETEOLMODE)) { case SC_EOL_CRLF: return "\r\n"; case SC_EOL_CR: return "\r"; default: return "\n"; }
}

void transformLines(Editor& e, const std::function<void(std::vector<std::string>&)>& fn, bool wholeDoc)
{
    sptr_t a = e.call(SCI_GETSELECTIONSTART), b = e.call(SCI_GETSELECTIONEND);
    sptr_t l1, l2;
    if (a == b && wholeDoc) { l1 = 0; l2 = e.call(SCI_GETLINECOUNT) - 1; }
    else {
        l1 = e.call(SCI_LINEFROMPOSITION, uptr_t(a));
        l2 = e.call(SCI_LINEFROMPOSITION, uptr_t(b));
        if (l2 > l1 && e.call(SCI_POSITIONFROMLINE, uptr_t(l2)) == b) --l2;  // selection ends at column 0
    }
    std::vector<std::string> lines;
    for (sptr_t l = l1; l <= l2; ++l) lines.push_back(e.lineText(l));
    std::vector<std::string> before = lines;
    fn(lines);
    if (lines == before) return;
    std::string eol = eolString(e), out;
    for (size_t i = 0; i < lines.size(); ++i) { if (i) out += eol; out += lines[i]; }
    sptr_t start = e.call(SCI_POSITIONFROMLINE, uptr_t(l1));
    sptr_t end = e.call(SCI_GETLINEENDPOSITION, uptr_t(l2));
    if (lines.empty() && l2 + 1 < e.call(SCI_GETLINECOUNT)) end = e.call(SCI_POSITIONFROMLINE, uptr_t(l2 + 1));
    e.call(SCI_BEGINUNDOACTION);
    e.call(SCI_SETTARGETRANGE, uptr_t(start), end);
    e.call(SCI_REPLACETARGET, out.size(), reinterpret_cast<sptr_t>(out.c_str()));
    e.call(SCI_ENDUNDOACTION);
    if (a != b) e.call(SCI_SETSEL, uptr_t(start), start + sptr_t(out.size()));
}

void changeCase(Editor& e, CaseMode m)
{
    int n = int(e.call(SCI_GETSELECTIONS));
    e.call(SCI_BEGINUNDOACTION);
    for (int i = 0; i < n; ++i) {
        sptr_t a = e.call(SCI_GETSELECTIONNSTART, uptr_t(i)), b = e.call(SCI_GETSELECTIONNEND, uptr_t(i));
        if (a == b) {   // no selection: act on the word under the caret
            a = e.call(SCI_WORDSTARTPOSITION, uptr_t(a), 1);
            b = e.call(SCI_WORDENDPOSITION, uptr_t(b), 1);
            if (a == b) continue;
        }
        std::string s = e.textRange(a, b);
        gchar* r = nullptr;
        switch (m) {
        case CaseMode::Upper: r = g_utf8_strup(s.c_str(), -1); break;
        case CaseMode::Lower: r = g_utf8_strdown(s.c_str(), -1); break;
        default: {
            std::string out;
            bool newWord = true, newSentence = true;
            std::mt19937 rng(std::random_device{}());
            for (const char* p = s.c_str(); *p; p = g_utf8_next_char(p)) {
                gunichar c = g_utf8_get_char(p);
                gunichar o = c;
                switch (m) {
                case CaseMode::Proper: o = newWord ? g_unichar_totitle(c) : c; break;
                case CaseMode::Sentence: o = newSentence ? g_unichar_toupper(c) : g_unichar_tolower(c); break;
                case CaseMode::Invert: o = g_unichar_isupper(c) ? g_unichar_tolower(c) : g_unichar_toupper(c); break;
                case CaseMode::Random: o = (rng() & 1) ? g_unichar_toupper(c) : g_unichar_tolower(c); break;
                default: break;
                }
                char buf[8]; int len = g_unichar_to_utf8(o, buf); out.append(buf, size_t(len));
                bool alnum = g_unichar_isalnum(c) || c == '\'';
                newWord = !alnum;
                if (c == '.' || c == '!' || c == '?') newSentence = true;
                else if (g_unichar_isalpha(c)) newSentence = false;
            }
            r = g_strdup(out.c_str());
        }
        }
        e.call(SCI_SETTARGETRANGE, uptr_t(a), b);
        e.call(SCI_REPLACETARGET, std::strlen(r), reinterpret_cast<sptr_t>(r));
        g_free(r);
    }
    e.call(SCI_ENDUNDOACTION);
}

void sortLines(Editor& e, SortMode m)
{
    transformLines(e, [m](std::vector<std::string>& v) {
        auto ci = [](const std::string& a, const std::string& b) {
            gchar* x = g_utf8_casefold(a.c_str(), -1); gchar* y = g_utf8_casefold(b.c_str(), -1);
            int r = g_utf8_collate(x, y); g_free(x); g_free(y); return r < 0;
        };
        auto num = [](const std::string& s) { try { return std::stoll(s); } catch (...) { return 0LL; } };
        switch (m) {
        case SortMode::LexAsc: std::stable_sort(v.begin(), v.end()); break;
        case SortMode::LexDesc: std::stable_sort(v.begin(), v.end(), std::greater<>()); break;
        case SortMode::LexAscCI: std::stable_sort(v.begin(), v.end(), ci); break;
        case SortMode::LexDescCI: std::stable_sort(v.begin(), v.end(), [&](auto& a, auto& b) { return ci(b, a); }); break;
        case SortMode::IntAsc: std::stable_sort(v.begin(), v.end(), [&](auto& a, auto& b) { return num(a) < num(b); }); break;
        case SortMode::IntDesc: std::stable_sort(v.begin(), v.end(), [&](auto& a, auto& b) { return num(a) > num(b); }); break;
        case SortMode::Reverse: std::reverse(v.begin(), v.end()); break;
        case SortMode::Random: { std::mt19937 rng(std::random_device{}()); std::shuffle(v.begin(), v.end(), rng); break; }
        }
    });
}

void removeDuplicateLines(Editor& e, bool consecutiveOnly)
{
    transformLines(e, [consecutiveOnly](std::vector<std::string>& v) {
        std::vector<std::string> out;
        std::set<std::string> seen;
        for (auto& l : v) {
            if (consecutiveOnly) { if (out.empty() || out.back() != l) out.push_back(l); }
            else if (seen.insert(l).second) out.push_back(l);
        }
        v.swap(out);
    });
}

void removeEmptyLines(Editor& e, bool includingBlank)
{
    transformLines(e, [includingBlank](std::vector<std::string>& v) {
        std::vector<std::string> out;
        for (auto& l : v) {
            bool empty = includingBlank ? l.find_first_not_of(" \t") == std::string::npos : l.empty();
            if (!empty) out.push_back(l);
        }
        v.swap(out);
    });
}

void blankOperation(Editor& e, BlankOp op)
{
    int tab = int(e.call(SCI_GETTABWIDTH));
    if (op == BlankOp::EolToSpace || op == BlankOp::TrimAll) {
        transformLines(e, [op](std::vector<std::string>& v) {
            std::string joined;
            for (size_t i = 0; i < v.size(); ++i) {
                std::string l = v[i];
                if (op == BlankOp::TrimAll) {
                    l.erase(0, l.find_first_not_of(" \t"));
                    while (!l.empty() && (l.back() == ' ' || l.back() == '\t')) l.pop_back();
                }
                if (i && !(op == BlankOp::TrimAll && l.empty())) joined += ' ';
                joined += l;
            }
            v.assign(1, joined);
        });
        return;
    }
    transformLines(e, [op, tab](std::vector<std::string>& v) {
        for (auto& l : v) {
            switch (op) {
            case BlankOp::TrimTrailing: while (!l.empty() && (l.back() == ' ' || l.back() == '\t')) l.pop_back(); break;
            case BlankOp::TrimLeading: l.erase(0, std::min(l.size(), l.find_first_not_of(" \t"))); break;
            case BlankOp::TrimBoth:
                while (!l.empty() && (l.back() == ' ' || l.back() == '\t')) l.pop_back();
                l.erase(0, std::min(l.size(), l.find_first_not_of(" \t"))); break;
            case BlankOp::TabToSpace: {
                std::string o; int col = 0;
                for (char c : l) {
                    if (c == '\t') { int n = tab - (col % tab); o.append(size_t(n), ' '); col += n; }
                    else { o += c; if ((c & 0xC0) != 0x80) ++col; }
                }
                l = o; break;
            }
            case BlankOp::SpaceToTabLeading:
            case BlankOp::SpaceToTabAll: {
                std::string o; int col = 0; int spaces = 0; bool leading = true;
                auto flush = [&](bool atTabStop) {
                    if (atTabStop && spaces > 1) o += '\t'; else o.append(size_t(spaces), ' ');
                    spaces = 0;
                };
                for (char c : l) {
                    bool convert = op == BlankOp::SpaceToTabAll || leading;
                    if (c == ' ' && convert) {
                        ++spaces; ++col;
                        if (col % tab == 0) flush(true);
                    } else if (c == '\t' && convert) {
                        spaces = 0; o += '\t'; col += tab - (col % tab);
                    } else {
                        flush(false); o += c; leading = false;
                        if ((c & 0xC0) != 0x80) ++col;
                    }
                }
                flush(false);
                l = o; break;
            }
            default: break;
            }
        }
    });
}

void trimTrailingInEditor(Editor& e)
{
    sptr_t a = e.call(SCI_GETSELECTIONSTART), b = e.call(SCI_GETSELECTIONEND);
    e.call(SCI_SETEMPTYSELECTION, uptr_t(e.call(SCI_GETCURRENTPOS)));
    blankOperation(e, BlankOp::TrimTrailing);
    sptr_t len = e.call(SCI_GETLENGTH);
    e.call(SCI_SETSEL, uptr_t(std::min(a, len)), std::min(b, len));
}

void joinLines(Editor& e)
{
    sptr_t a = e.call(SCI_GETSELECTIONSTART), b = e.call(SCI_GETSELECTIONEND);
    if (a == b) {
        sptr_t line = e.call(SCI_LINEFROMPOSITION, uptr_t(a));
        if (line + 1 >= e.call(SCI_GETLINECOUNT)) return;
        a = e.call(SCI_POSITIONFROMLINE, uptr_t(line));
        b = e.call(SCI_GETLINEENDPOSITION, uptr_t(line + 1));
    }
    e.call(SCI_SETTARGETRANGE, uptr_t(a), b);
    e.call(SCI_LINESJOIN);
}

void splitLines(Editor& e)
{
    sptr_t a = e.call(SCI_GETSELECTIONSTART), b = e.call(SCI_GETSELECTIONEND);
    if (a == b) return;
    int edge = int(e.call(SCI_GETEDGECOLUMN));
    int px = edge > 0 ? int(e.call(SCI_TEXTWIDTH, STYLE_DEFAULT, reinterpret_cast<sptr_t>(std::string(size_t(edge), 'x').c_str()))) : 0;
    e.call(SCI_SETTARGETRANGE, uptr_t(a), b);
    e.call(SCI_LINESSPLIT, uptr_t(px));
}

void insertBlankLine(Editor& e, bool above)
{
    sptr_t line = e.call(SCI_LINEFROMPOSITION, uptr_t(e.call(SCI_GETCURRENTPOS)));
    std::string eol = eolString(e);
    e.call(SCI_BEGINUNDOACTION);
    if (above) {
        sptr_t pos = e.call(SCI_POSITIONFROMLINE, uptr_t(line));
        e.call(SCI_INSERTTEXT, uptr_t(pos), reinterpret_cast<sptr_t>(eol.c_str()));
        e.call(SCI_GOTOPOS, uptr_t(pos));
    } else {
        sptr_t pos = e.call(SCI_GETLINEENDPOSITION, uptr_t(line));
        e.call(SCI_INSERTTEXT, uptr_t(pos), reinterpret_cast<sptr_t>(eol.c_str()));
        e.call(SCI_GOTOPOS, uptr_t(pos + sptr_t(eol.size())));
    }
    e.call(SCI_ENDUNDOACTION);
}

static void selectedLineRange(Editor& e, sptr_t& l1, sptr_t& l2)
{
    sptr_t a = e.call(SCI_GETSELECTIONSTART), b = e.call(SCI_GETSELECTIONEND);
    l1 = e.call(SCI_LINEFROMPOSITION, uptr_t(a));
    l2 = e.call(SCI_LINEFROMPOSITION, uptr_t(b));
    if (l2 > l1 && e.call(SCI_POSITIONFROMLINE, uptr_t(l2)) == b) --l2;
}

bool lineComment(Editor& e, const LangInfo* li, bool add)
{
    if (!li || li->commentLine.empty()) return false;
    const std::string tok = li->commentLine;
    sptr_t l1, l2; selectedLineRange(e, l1, l2);
    e.call(SCI_BEGINUNDOACTION);
    for (sptr_t l = l1; l <= l2; ++l) {
        sptr_t indentPos = e.call(SCI_GETLINEINDENTPOSITION, uptr_t(l));
        sptr_t end = e.call(SCI_GETLINEENDPOSITION, uptr_t(l));
        if (add) {
            if (indentPos == end && l1 != l2) continue;   // skip empty lines in multi-line selections
            std::string ins = tok + " ";
            e.callStr(SCI_INSERTTEXT, uptr_t(indentPos), ins.c_str());
        } else {
            std::string t = e.textRange(indentPos, std::min(end, indentPos + sptr_t(tok.size()) + 1));
            if (t.compare(0, tok.size(), tok) == 0) {
                sptr_t len = sptr_t(tok.size()) + ((t.size() > tok.size() && t[tok.size()] == ' ') ? 1 : 0);
                e.call(SCI_DELETERANGE, uptr_t(indentPos), len);
            }
        }
    }
    e.call(SCI_ENDUNDOACTION);
    return true;
}

bool toggleLineComment(Editor& e, const LangInfo* li)
{
    if (!li || li->commentLine.empty()) return streamComment(e, li, true);
    sptr_t l1, l2; selectedLineRange(e, l1, l2);
    bool allCommented = true;
    for (sptr_t l = l1; l <= l2; ++l) {
        sptr_t ip = e.call(SCI_GETLINEINDENTPOSITION, uptr_t(l));
        sptr_t end = e.call(SCI_GETLINEENDPOSITION, uptr_t(l));
        if (ip == end) continue;
        if (e.textRange(ip, std::min(end, ip + sptr_t(li->commentLine.size()))) != li->commentLine) { allCommented = false; break; }
    }
    return lineComment(e, li, !allCommented);
}

bool streamComment(Editor& e, const LangInfo* li, bool add)
{
    if (!li || li->commentStart.empty() || li->commentEnd.empty()) return false;
    sptr_t a = e.call(SCI_GETSELECTIONSTART), b = e.call(SCI_GETSELECTIONEND);
    const std::string& cs = li->commentStart; const std::string& ce = li->commentEnd;
    e.call(SCI_BEGINUNDOACTION);
    if (add) {
        if (a == b) { a = e.call(SCI_WORDSTARTPOSITION, uptr_t(a), 1); b = e.call(SCI_WORDENDPOSITION, uptr_t(b), 1); }
        e.callStr(SCI_INSERTTEXT, uptr_t(b), ce.c_str());
        e.callStr(SCI_INSERTTEXT, uptr_t(a), cs.c_str());
        e.call(SCI_SETSEL, uptr_t(a), b + sptr_t(cs.size() + ce.size()));
    } else {
        // search outward for the enclosing comment markers
        std::string all = e.textRange(0, e.call(SCI_GETLENGTH));
        size_t s = all.rfind(cs, size_t(a));
        size_t t = all.find(ce, size_t(std::max(a, b > 0 ? b - sptr_t(ce.size()) : 0)));
        if (s != std::string::npos && t != std::string::npos && s < t) {
            e.call(SCI_DELETERANGE, t, sptr_t(ce.size()));
            e.call(SCI_DELETERANGE, s, sptr_t(cs.size()));
        }
    }
    e.call(SCI_ENDUNDOACTION);
    return true;
}

std::string expandEscapes(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { o += s[i]; continue; }
        char c = s[++i];
        auto readNum = [&](int base, int maxDigits) -> long {
            long v = 0; int n = 0;
            while (n < maxDigits && i + 1 < s.size()) {
                char d = s[i + 1]; int dv;
                if (d >= '0' && d <= '9') dv = d - '0';
                else if (base == 16 && d >= 'a' && d <= 'f') dv = d - 'a' + 10;
                else if (base == 16 && d >= 'A' && d <= 'F') dv = d - 'A' + 10;
                else break;
                if (dv >= base) break;
                v = v * base + dv; ++i; ++n;
            }
            return n ? v : -1;
        };
        auto putCp = [&](long cp) { if (cp < 0) return; char buf[8]; int n = g_unichar_to_utf8(gunichar(cp), buf); o.append(buf, size_t(n)); };
        switch (c) {
        case 'n': o += '\n'; break;
        case 'r': o += '\r'; break;
        case 't': o += '\t'; break;
        case '0': o += '\0'; break;
        case '\\': o += '\\'; break;
        case 'x': { long v = readNum(16, 2); if (v >= 0) o += char(v); else o += "\\x"; break; }
        case 'u': putCp(readNum(16, 4)); break;
        case 'd': { long v = readNum(10, 3); if (v >= 0) o += char(v); break; }
        case 'o': { long v = readNum(8, 3); if (v >= 0) o += char(v); break; }
        case 'b': { long v = readNum(2, 8); if (v >= 0) o += char(v); break; }
        default: o += '\\'; o += c; break;
        }
    }
    return o;
}

} // namespace cam
