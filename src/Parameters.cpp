// Camelion++ - langs.xml / stylers.xml / theme data (Notepad++ formats)
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
#include "Parameters.h"
#include "Paths.h"
#include "Settings.h"
#include <tinyxml2.h>
#include <gtk/gtk.h>
#include <dirent.h>
#include <algorithm>
#include <set>
#include <sstream>
#include <cstring>

using namespace tinyxml2;

namespace cam {

Parameters& Parameters::get() { static Parameters p; return p; }

const Style* LexerStyler::byId(int id) const
{
    for (auto& s : styles) if (s.id == id) return &s;
    return nullptr;
}

int Parameters::parseColour(const char* hex)
{
    if (!hex || std::strlen(hex) < 6) return -1;
    unsigned v = 0;
    for (int i = 0; i < 6; ++i) {
        char c = hex[i]; v <<= 4;
        if (c >= '0' && c <= '9') v |= unsigned(c - '0');
        else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
        else return -1;
    }
    int r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
    return r | (g << 8) | (b << 16);
}

static std::set<std::string>& fontFamilies()
{
    static std::set<std::string> fams;
    if (fams.empty()) {
        PangoFontMap* map = pango_cairo_font_map_get_default();
        PangoFontFamily** families = nullptr; int n = 0;
        pango_font_map_list_families(map, &families, &n);
        for (int i = 0; i < n; ++i) {
            std::string f = pango_font_family_get_name(families[i]);
            std::transform(f.begin(), f.end(), f.begin(), ::tolower);
            fams.insert(f);
        }
        g_free(families);
        fams.insert("monospace");
    }
    return fams;
}

std::string Parameters::fontOrFallback(const std::string& f)
{
    if (f.empty()) return f;
    std::string l = f; std::transform(l.begin(), l.end(), l.begin(), ::tolower);
    if (fontFamilies().count(l)) return f;
    // Windows fonts used by Notepad++ themes -> sensible Linux equivalents
    static const char* mono[] = {"DejaVu Sans Mono", "Liberation Mono", "Noto Sans Mono", "Ubuntu Mono", "Monospace"};
    for (auto m : mono) {
        std::string ml = m; std::transform(ml.begin(), ml.end(), ml.begin(), ::tolower);
        if (fontFamilies().count(ml)) return m;
    }
    return "Monospace";
}

static void readStyle(XMLElement* e, Style& s)
{
    s.id = e->IntAttribute("styleID", 0);
    if (auto n = e->Attribute("name")) s.name = n;
    s.fg = Parameters::parseColour(e->Attribute("fgColor"));
    s.bg = Parameters::parseColour(e->Attribute("bgColor"));
    if (auto f = e->Attribute("fontName")) s.font = f;
    const char* fs = e->Attribute("fontStyle");
    if (fs && *fs) s.fontStyle = std::atoi(fs);
    const char* fz = e->Attribute("fontSize");
    if (fz && *fz) s.fontSize = std::atoi(fz);
    s.kwClass = keywordClassFromName(e->Attribute("keywordClass"));
    if (const char* t = e->GetText()) s.keywords = t;
}

void Parameters::loadLangs(const std::string& path)
{
    XMLDocument doc;
    if (doc.LoadFile(path.c_str()) != XML_SUCCESS) return;
    XMLElement* root = doc.FirstChildElement("NotepadPlus");
    if (!root) return;
    XMLElement* langs = root->FirstChildElement("Languages");
    if (!langs) return;
    for (XMLElement* l = langs->FirstChildElement("Language"); l; l = l->NextSiblingElement("Language")) {
        LangInfo li;
        li.name = l->Attribute("name") ? l->Attribute("name") : "";
        if (li.name.empty()) continue;
        std::istringstream ss(l->Attribute("ext") ? l->Attribute("ext") : "");
        std::string e;
        while (ss >> e) { std::transform(e.begin(), e.end(), e.begin(), ::tolower); li.exts.push_back(e); }
        if (auto c = l->Attribute("commentLine")) li.commentLine = c;
        if (auto c = l->Attribute("commentStart")) li.commentStart = c;
        if (auto c = l->Attribute("commentEnd")) li.commentEnd = c;
        for (XMLElement* k = l->FirstChildElement("Keywords"); k; k = k->NextSiblingElement("Keywords")) {
            int idx = keywordClassFromName(k->Attribute("name"));
            if (idx >= 0 && k->GetText()) li.keywords[idx] = k->GetText();
        }
        _langs[li.name] = std::move(li);
    }
}

bool Parameters::loadTheme(const std::string& path)
{
    XMLDocument doc;
    if (doc.LoadFile(path.c_str()) != XML_SUCCESS) return false;
    XMLElement* root = doc.FirstChildElement("NotepadPlus");
    if (!root) return false;
    std::map<std::string, LexerStyler> lexers;
    std::vector<Style> globals;
    if (XMLElement* ls = root->FirstChildElement("LexerStyles")) {
        for (XMLElement* lt = ls->FirstChildElement("LexerType"); lt; lt = lt->NextSiblingElement("LexerType")) {
            LexerStyler st;
            st.name = lt->Attribute("name") ? lt->Attribute("name") : "";
            if (auto d = lt->Attribute("desc")) st.desc = d;
            if (auto x = lt->Attribute("ext")) st.ext = x;
            for (XMLElement* w = lt->FirstChildElement("WordsStyle"); w; w = w->NextSiblingElement("WordsStyle")) {
                Style s; readStyle(w, s); st.styles.push_back(s);
            }
            lexers[st.name] = std::move(st);
        }
    }
    if (XMLElement* gs = root->FirstChildElement("GlobalStyles")) {
        for (XMLElement* w = gs->FirstChildElement("WidgetStyle"); w; w = w->NextSiblingElement("WidgetStyle")) {
            Style s; readStyle(w, s); globals.push_back(s);
        }
    }
    if (globals.empty()) return false;
    _lexers = std::move(lexers);
    _globals = std::move(globals);
    _themePath = path;
    return true;
}

void Parameters::load()
{
    const std::string data = paths::dataDir();
    const std::string cfg = paths::configDir();
    std::string langs = paths::join(cfg, "langs.xml");
    loadLangs(paths::fileExists(langs) ? langs : paths::join(data, "langs.model.xml"));

    std::string theme = Settings::get().getString("theme", "");
    bool ok = false;
    if (!theme.empty() && paths::fileExists(theme)) ok = loadTheme(theme);
    if (!ok) {
        std::string stylers = paths::join(cfg, "stylers.xml");
        ok = paths::fileExists(stylers) && loadTheme(stylers);
    }
    if (!ok) loadTheme(paths::join(data, "stylers.model.xml"));
}

std::vector<ThemeInfo> Parameters::themes() const
{
    std::vector<ThemeInfo> v;
    v.push_back({"Default (stylers.xml)", paths::join(paths::dataDir(), "stylers.model.xml")});
    std::string userStylers = paths::join(paths::configDir(), "stylers.xml");
    if (paths::fileExists(userStylers)) v[0].path = userStylers;
    for (const std::string& dir : {paths::join(paths::dataDir(), "themes"), paths::userThemesDir()}) {
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        std::vector<ThemeInfo> part;
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 4 && n.substr(n.size() - 4) == ".xml")
                part.push_back({n.substr(0, n.size() - 4), paths::join(dir, n)});
        }
        closedir(d);
        std::sort(part.begin(), part.end(), [](auto& a, auto& b) {
            return g_ascii_strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
        v.insert(v.end(), part.begin(), part.end());
    }
    return v;
}

const LangInfo* Parameters::lang(const std::string& name) const
{
    auto it = _langs.find(name);
    return it == _langs.end() ? nullptr : &it->second;
}

const LexerStyler* Parameters::lexerStyler(const std::string& name) const
{
    auto it = _lexers.find(name);
    return it == _lexers.end() ? nullptr : &it->second;
}

const Style* Parameters::globalStyle(const std::string& name) const
{
    for (auto& s : _globals) if (s.name == name) return &s;
    return nullptr;
}

const Style* Parameters::globalStyleById(int id) const
{
    for (auto& s : _globals) if (s.id == id && id != 0) return &s;
    return nullptr;
}

int Parameters::defaultFg() const { auto s = globalStyleById(32); return s && s->fg >= 0 ? s->fg : 0x000000; }
int Parameters::defaultBg() const { auto s = globalStyleById(32); return s && s->bg >= 0 ? s->bg : 0xFFFFFF; }
bool Parameters::isDarkTheme() const
{
    int bg = defaultBg();
    int r = bg & 0xFF, g = (bg >> 8) & 0xFF, b = (bg >> 16) & 0xFF;
    return (0.299 * r + 0.587 * g + 0.114 * b) < 128;
}
std::string Parameters::defaultFont() const
{
    std::string override = Settings::get().getString("editor.font", "");
    if (!override.empty()) return override;
    auto s = globalStyleById(32);
    return fontOrFallback(s && !s->font.empty() ? s->font : "Monospace");
}
int Parameters::defaultFontSize() const
{
    int o = Settings::get().getInt("editor.fontSize", 0);
    if (o > 0) return o;
    auto s = globalStyleById(32);
    return s && s->fontSize > 0 ? s->fontSize : 10;
}

int Parameters::langTypeFromFileName(const std::string& path) const
{
    std::string base = paths::baseName(path);
    std::string lbase = base; std::transform(lbase.begin(), lbase.end(), lbase.begin(), ::tolower);
    static const std::map<std::string, std::string> byName = {
        {"makefile", "makefile"}, {"gnumakefile", "makefile"}, {"cmakelists.txt", "cmake"},
        {".bashrc", "bash"}, {".bash_profile", "bash"}, {".profile", "bash"}, {".zshrc", "bash"},
        {"pkgbuild", "bash"}, {"configure", "bash"}, {".gitconfig", "ini"}, {".editorconfig", "ini"},
        {"dockerfile", "bash"}, {"cargo.lock", "toml"}, {"meson.build", "python"},
    };
    auto bn = byName.find(lbase);
    if (bn != byName.end()) { if (auto l = langByName(bn->second)) return l->langType; }

    std::string ext = paths::extension(path);
    if (ext.empty()) return 0;
    if (ext == "md" || ext == "markdown" || ext == "mkd") return 0x1000;
    // user extensions from stylers.xml first, then langs.xml
    for (auto& [n, st] : _lexers) {
        std::istringstream ss(st.ext); std::string e;
        while (ss >> e) if (g_ascii_strcasecmp(e.c_str(), ext.c_str()) == 0) { if (auto l = langByName(n)) return l->langType; }
    }
    for (auto& [n, li] : _langs)
        for (auto& e : li.exts)
            if (e == ext) { if (auto l = langByName(n)) return l->langType; }
    return 0;
}

int Parameters::langTypeFromContent(const std::string& s) const
{
    auto byName = [](const char* n) { auto l = langByName(n); return l ? l->langType : 0; };
    if (s.compare(0, 2, "#!") == 0) {
        std::string first = s.substr(0, s.find('\n'));
        if (first.find("python") != std::string::npos) return byName("python");
        if (first.find("bash") != std::string::npos || first.find("/sh") != std::string::npos ||
            first.find("zsh") != std::string::npos || first.find("ksh") != std::string::npos) return byName("bash");
        if (first.find("perl") != std::string::npos) return byName("perl");
        if (first.find("ruby") != std::string::npos) return byName("ruby");
        if (first.find("node") != std::string::npos) return byName("javascript.js");
        if (first.find("lua") != std::string::npos) return byName("lua");
        if (first.find("php") != std::string::npos) return byName("php");
        if (first.find("tcl") != std::string::npos) return byName("tcl");
        if (first.find("Rscript") != std::string::npos) return byName("r");
    }
    std::string head = s.substr(0, 256);
    std::string lh = head; std::transform(lh.begin(), lh.end(), lh.begin(), ::tolower);
    if (lh.compare(0, 5, "<?xml") == 0) return byName("xml");
    if (lh.find("<!doctype html") != std::string::npos || lh.compare(0, 5, "<html") == 0) return byName("html");
    if (lh.compare(0, 5, "<?php") == 0) return byName("php");
    return 0;
}

LexerStyler Parameters::builtinStyler(const std::string& name) const
{
    LexerStyler st; st.name = name;
    if (name != "markdown") return st;
    bool dark = isDarkTheme();
    auto c = [](unsigned rgb) { return int(((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | ((rgb & 0xFF) << 16)); };
    int head = dark ? c(0x7FB2F0) : c(0x0050A0);
    int emph = dark ? c(0xE5C07B) : c(0x8A4B00);
    int code = dark ? c(0x98C379) : c(0x2E7D32);
    int link = dark ? c(0x56B6C2) : c(0x0070C0);
    int dim  = dark ? c(0x7F848E) : c(0x808080);
    auto add = [&](int id, const char* n, int fg, int fs) { Style s; s.id = id; s.name = n; s.fg = fg; s.fontStyle = fs; st.styles.push_back(s); };
    add(1, "LINE BEGIN", -1, 0);
    add(2, "STRONG", emph, FONTSTYLE_BOLD); add(3, "STRONG2", emph, FONTSTYLE_BOLD);
    add(4, "EM", emph, FONTSTYLE_ITALIC);   add(5, "EM2", emph, FONTSTYLE_ITALIC);
    for (int i = 0; i < 6; ++i) add(6 + i, "HEADER", head, FONTSTYLE_BOLD);
    add(12, "PRECHAR", dim, 0); add(13, "ULIST", link, FONTSTYLE_BOLD); add(14, "OLIST", link, FONTSTYLE_BOLD);
    add(15, "BLOCKQUOTE", dim, FONTSTYLE_ITALIC); add(16, "STRIKEOUT", dim, 0); add(17, "HRULE", dim, FONTSTYLE_BOLD);
    add(18, "LINK", link, FONTSTYLE_UNDERLINE); add(19, "CODE", code, 0); add(20, "CODE2", code, 0); add(21, "CODEBK", code, 0);
    return st;
}

} // namespace cam
