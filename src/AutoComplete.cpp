// Camelion++ - auto-completion using Notepad++ autoCompletion/*.xml files
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
#include "AutoComplete.h"
#include "Editor.h"
#include "LangDefs.h"
#include "Paths.h"
#include "Settings.h"
#include <tinyxml2.h>
#include <glib.h>
#include <dirent.h>
#include <algorithm>
#include <set>
#include <cstring>

using namespace tinyxml2;

namespace cam {

void AutoComplete::setLanguage(int langType)
{
    _lang = langType;
    _ctWord = nullptr;
}

static std::string acFileFor(int langType)
{
    const LangDef* d = langByType(langType);
    std::string n = d->name;
    if (n == "javascript.js" || n == "javascript") n = "javascript";
    else if (n == "baanc") n = "BaanC";
    else if (n == "coffeescript") n = "coffee";
    std::string dir = paths::join(paths::dataDir(), "autoCompletion");
    std::string userDir = paths::join(paths::configDir(), "autoCompletion");
    for (auto& d2 : {userDir, dir}) {
        DIR* dd = opendir(d2.c_str());
        if (!dd) continue;
        std::string found;
        while (dirent* e = readdir(dd)) if (g_ascii_strcasecmp(e->d_name, (n + ".xml").c_str()) == 0) { found = paths::join(d2, e->d_name); break; }
        closedir(dd);
        if (!found.empty()) return found;
    }
    return {};
}

AutoComplete::LangData& AutoComplete::data()
{
    LangData& d = _cache[_lang];
    if (!d.loaded) { d.loaded = true; std::string f = acFileFor(_lang); if (!f.empty()) load(d, f); }
    return d;
}

void AutoComplete::load(LangData& d, const std::string& file)
{
    XMLDocument doc;
    if (doc.LoadFile(file.c_str()) != XML_SUCCESS) return;
    XMLElement* root = doc.FirstChildElement("NotepadPlus");
    XMLElement* ac = root ? root->FirstChildElement("AutoComplete") : nullptr;
    if (!ac) return;
    if (XMLElement* env = ac->FirstChildElement("Environment")) {
        const char* ic = env->Attribute("ignoreCase");
        d.ignoreCase = ic && !std::strcmp(ic, "yes");
        if (const char* s = env->Attribute("startFunc"); s && *s) d.startFunc = s[0];
        if (const char* s = env->Attribute("stopFunc"); s && *s) d.stopFunc = s[0];
        if (const char* s = env->Attribute("paramSeparator"); s && *s) d.paramSep = s[0];
        if (const char* s = env->Attribute("terminal"); s && *s) d.terminal = s[0];
        if (const char* s = env->Attribute("additionalWordChar")) d.additionalWordChars = s;
    }
    for (XMLElement* k = ac->FirstChildElement("KeyWord"); k; k = k->NextSiblingElement("KeyWord")) {
        KeyWord kw;
        if (const char* n = k->Attribute("name")) kw.name = n; else continue;
        const char* f = k->Attribute("func");
        kw.func = f && !std::strcmp(f, "yes");
        for (XMLElement* o = k->FirstChildElement("Overload"); o; o = o->NextSiblingElement("Overload")) {
            Overload ov;
            if (const char* r = o->Attribute("retVal")) ov.retVal = r;
            if (const char* r = o->Attribute("descr")) ov.descr = r;
            for (XMLElement* p = o->FirstChildElement("Param"); p; p = p->NextSiblingElement("Param"))
                if (const char* pn = p->Attribute("name")) ov.params.push_back(pn);
            kw.overloads.push_back(ov);
        }
        d.words.push_back(std::move(kw));
    }
    std::sort(d.words.begin(), d.words.end(), [&](auto& a, auto& b) {
        return d.ignoreCase ? g_ascii_strcasecmp(a.name.c_str(), b.name.c_str()) < 0 : a.name < b.name; });
}

std::string AutoComplete::wordBeforeCaret(Editor& e, sptr_t* start) const
{
    sptr_t pos = e.call(SCI_GETCURRENTPOS);
    sptr_t s = e.call(SCI_WORDSTARTPOSITION, uptr_t(pos), 1);
    if (start) *start = s;
    return e.textRange(s, pos);
}

void AutoComplete::show(Editor& e, bool funcs, bool words)
{
    LangData& d = data();
    sptr_t start = 0;
    std::string prefix = wordBeforeCaret(e, &start);
    if (prefix.empty()) return;
    auto starts = [&](const std::string& w) {
        if (w.size() < prefix.size()) return false;
        return d.ignoreCase || !Settings::get().getBool("autocomplete.matchCase", false)
               ? g_ascii_strncasecmp(w.c_str(), prefix.c_str(), prefix.size()) == 0
               : w.compare(0, prefix.size(), prefix) == 0;
    };
    std::set<std::string> items;
    if (funcs) for (auto& k : d.words) if (starts(k.name)) items.insert(k.name);
    if (words) {
        // words of the current document (like Notepad++ "word completion")
        std::string pat = "\\<" + std::string() ;
        sptr_t len = e.call(SCI_GETLENGTH);
        if (len < 4 * 1024 * 1024) {
            std::string text = e.text();
            size_t i = 0;
            auto isW = [&](unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80 || d.additionalWordChars.find(char(c)) != std::string::npos; };
            while (i < text.size()) {
                while (i < text.size() && !isW((unsigned char)text[i])) ++i;
                size_t j = i;
                while (j < text.size() && isW((unsigned char)text[j])) ++j;
                if (j > i && size_t(sptr_t(i)) != size_t(start)) {
                    std::string w = text.substr(i, j - i);
                    if (w.size() > prefix.size() && starts(w)) items.insert(w);
                }
                i = j;
                if (items.size() > 2000) break;
            }
        }
    }
    items.erase(prefix);
    if (items.empty()) { e.call(SCI_AUTOCCANCEL); return; }
    std::string list;
    for (auto& w : items) { if (!list.empty()) list += ' '; list += w; }
    e.call(SCI_AUTOCSETIGNORECASE, d.ignoreCase || !Settings::get().getBool("autocomplete.matchCase", false));
    e.call(SCI_AUTOCSETCASEINSENSITIVEBEHAVIOUR, SC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE);
    e.call(SCI_AUTOCSHOW, prefix.size(), reinterpret_cast<sptr_t>(list.c_str()));
}

void AutoComplete::showFunctionCompletion(Editor& e) { show(e, true, false); }
void AutoComplete::showWordCompletion(Editor& e) { show(e, false, true); }

void AutoComplete::showPathCompletion(Editor& e)
{
    sptr_t pos = e.call(SCI_GETCURRENTPOS);
    sptr_t line = e.call(SCI_LINEFROMPOSITION, uptr_t(pos));
    std::string before = e.textRange(e.call(SCI_POSITIONFROMLINE, uptr_t(line)), pos);
    size_t s = before.find_last_of(" \t\"'<>(=");
    std::string partial = s == std::string::npos ? before : before.substr(s + 1);
    if (partial.empty() || (partial[0] != '/' && partial[0] != '~' && partial[0] != '.')) return;
    std::string expanded = partial[0] == '~' ? std::string(g_get_home_dir()) + partial.substr(1) : partial;
    std::string dir = expanded.substr(0, expanded.find_last_of('/') + 1);
    std::string base = expanded.substr(dir.size());
    DIR* d = opendir(dir.empty() ? "." : dir.c_str());
    if (!d) return;
    std::set<std::string> items;
    while (dirent* de = readdir(d)) {
        std::string n = de->d_name;
        if (n == "." || n == "..") continue;
        if (n.compare(0, base.size(), base) == 0) items.insert(n);
    }
    closedir(d);
    if (items.empty()) return;
    std::string list;
    for (auto& w : items) { if (!list.empty()) list += '\n'; list += w; }
    e.call(SCI_AUTOCSETSEPARATOR, '\n');
    e.call(SCI_AUTOCSHOW, base.size(), reinterpret_cast<sptr_t>(list.c_str()));
    e.call(SCI_AUTOCSETSEPARATOR, ' ');
}

void AutoComplete::showCallTip(Editor& e)
{
    LangData& d = data();
    // find the function name before the innermost unclosed startFunc
    sptr_t pos = e.call(SCI_GETCURRENTPOS);
    sptr_t lineStart = e.call(SCI_POSITIONFROMLINE, uptr_t(e.call(SCI_LINEFROMPOSITION, uptr_t(pos))));
    std::string text = e.textRange(lineStart, pos);
    int depth = 0;
    long open = -1;
    for (long i = long(text.size()) - 1; i >= 0; --i) {
        if (text[size_t(i)] == d.stopFunc) ++depth;
        else if (text[size_t(i)] == d.startFunc) { if (depth == 0) { open = i; break; } --depth; }
    }
    if (open < 0) { e.call(SCI_CALLTIPCANCEL); _ctWord = nullptr; return; }
    long end = open;
    while (end > 0 && text[size_t(end - 1)] == ' ') --end;
    long beg = end;
    while (beg > 0 && (std::isalnum((unsigned char)text[size_t(beg - 1)]) || text[size_t(beg - 1)] == '_' ||
                       d.additionalWordChars.find(text[size_t(beg - 1)]) != std::string::npos)) --beg;
    std::string name = text.substr(size_t(beg), size_t(end - beg));
    const KeyWord* kw = nullptr;
    for (auto& k : d.words) if (k.func && (d.ignoreCase ? g_ascii_strcasecmp(k.name.c_str(), name.c_str()) == 0 : k.name == name)) { kw = &k; break; }
    if (!kw || kw->overloads.empty()) { e.call(SCI_CALLTIPCANCEL); _ctWord = nullptr; return; }
    if (_ctWord != kw) _ctOverload = 0;
    _ctWord = kw;
    _ctStart = lineStart + open;
    updateCallTip(e);
}

void AutoComplete::updateCallTip(Editor& e)
{
    if (!_ctWord) return;
    LangData& d = data();
    const Overload& ov = _ctWord->overloads[_ctOverload % _ctWord->overloads.size()];
    // which parameter is the caret in?
    sptr_t pos = e.call(SCI_GETCURRENTPOS);
    std::string args = e.textRange(_ctStart + 1, pos);
    int param = 0, depth = 0;
    for (char c : args) {
        if (c == d.startFunc) ++depth; else if (c == d.stopFunc) --depth;
        else if (c == d.paramSep && depth == 0) ++param;
    }
    std::string tip;
    if (_ctWord->overloads.size() > 1) tip += "\001 " + std::to_string(_ctOverload % _ctWord->overloads.size() + 1) + " of " + std::to_string(_ctWord->overloads.size()) + " \002";
    if (!ov.retVal.empty()) tip += ov.retVal + " ";
    tip += _ctWord->name + d.startFunc;
    size_t hlStart = 0, hlEnd = 0;
    for (size_t i = 0; i < ov.params.size(); ++i) {
        if (i) { tip += d.paramSep; tip += ' '; }
        if (int(i) == param) hlStart = tip.size();
        tip += ov.params[i];
        if (int(i) == param) hlEnd = tip.size();
    }
    tip += d.stopFunc;
    if (!ov.descr.empty()) tip += "\n" + ov.descr;
    e.call(SCI_CALLTIPSHOW, uptr_t(_ctStart), reinterpret_cast<sptr_t>(tip.c_str()));
    if (hlEnd > hlStart) e.call(SCI_CALLTIPSETHLT, hlStart, sptr_t(hlEnd));
}

void AutoComplete::onCharAdded(Editor& e, int ch)
{
    Settings& S = Settings::get();
    LangData& d = data();
    if (S.getBool("autocomplete.paramHint", true) && !d.words.empty()) {
        if (ch == d.startFunc) { showCallTip(e); return; }
        if (ch == d.stopFunc) { showCallTip(e); return; }
        if (ch == d.paramSep && _ctWord) { updateCallTip(e); }
    }
    if (!S.getBool("autocomplete.enabled", true)) return;
    if (!(std::isalnum(ch) || ch == '_' || ch >= 0x80)) return;
    if (e.call(SCI_AUTOCACTIVE)) return;
    sptr_t start = 0;
    std::string prefix = wordBeforeCaret(e, &start);
    if (int(prefix.size()) < std::max(1, S.getInt("autocomplete.threshold", 1))) return;
    std::string src = S.getString("autocomplete.source", "both");
    show(e, src != "word", src != "func");
}

} // namespace cam
