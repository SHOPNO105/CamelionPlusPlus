// Camelion++ - Function List panel driven by Notepad++ functionList/*.xml parsers
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
#include "FunctionList.h"
#include "MainWindow.h"
#include "LangDefs.h"
#include "Paths.h"
#include <tinyxml2.h>
#include <glib.h>
#include <algorithm>
#include <map>
#include <memory>
#include <cstring>

using namespace tinyxml2;

namespace cam {

// ------------------------------------------------------------------ regex helpers (PCRE via GRegex)
// Notepad++ runs these expressions with Boost in Perl mode: '^'/'$' match at line
// boundaries and '.' matches newlines unless a pattern says (?-s).
struct Rx {
    GRegex* re = nullptr;
    explicit Rx(const std::string& pattern) {
        if (pattern.empty()) return;
        GError* err = nullptr;
        re = g_regex_new(pattern.c_str(), GRegexCompileFlags(G_REGEX_MULTILINE | G_REGEX_DOTALL | G_REGEX_RAW | G_REGEX_OPTIMIZE), GRegexMatchFlags(0), &err);
        if (err) { g_error_free(err); re = nullptr; }
    }
    ~Rx() { if (re) g_regex_unref(re); }
    Rx(const Rx&) = delete;
    Rx& operator=(const Rx&) = delete;
    bool ok() const { return re != nullptr; }
    // first match at or after `from`, within [0, limit)
    bool find(const std::string& s, size_t from, size_t limit, size_t& a, size_t& b) const {
        if (!re || from > limit) return false;
        GMatchInfo* mi = nullptr;
        bool found = false;
        if (g_regex_match_full(re, s.c_str(), gssize(limit), gint(from), GRegexMatchFlags(0), &mi, nullptr) && g_match_info_matches(mi)) {
            gint x = 0, y = 0;
            g_match_info_fetch_pos(mi, 0, &x, &y);
            a = size_t(x); b = size_t(y); found = true;
        }
        g_match_info_free(mi);
        return found;
    }
};

struct ParserDef {
    std::string displayName;
    std::string commentExpr;
    struct Func { std::string mainExpr; std::vector<std::string> nameExprs; std::vector<std::string> classExprs; };
    struct ClassRange { std::string mainExpr, open, close; std::vector<std::string> classNameExprs; Func func; };
    std::vector<ClassRange> classRanges;
    std::vector<Func> functions;
    bool valid = false;
};

static std::string findParserFile(int langType)
{
    const LangDef* d = langByType(langType);
    std::vector<std::string> dirs = {paths::join(paths::configDir(), "functionList"), paths::join(paths::dataDir(), "functionList")};
    std::string id;
    // overrideMap.xml may redirect a langID to another parser file
    for (auto& dir : dirs) {
        XMLDocument doc;
        if (doc.LoadFile(paths::join(dir, "overrideMap.xml").c_str()) != XML_SUCCESS) continue;
        XMLElement* root = doc.FirstChildElement("NotepadPlus");
        XMLElement* fl = root ? root->FirstChildElement("functionList") : nullptr;
        XMLElement* am = fl ? fl->FirstChildElement("associationMap") : nullptr;
        for (XMLElement* a = am ? am->FirstChildElement("association") : nullptr; a; a = a->NextSiblingElement("association"))
            if (a->IntAttribute("langID", -1) == langType && a->Attribute("id")) { id = a->Attribute("id"); break; }
        if (!id.empty()) break;
    }
    if (id.empty()) id = std::string(d->name) + ".xml";
    for (auto& dir : dirs) { std::string p = paths::join(dir, id); if (paths::fileExists(p)) return p; }
    return {};
}

static std::vector<std::string> exprList(XMLElement* parent, const char* child, const char* elem)
{
    std::vector<std::string> v;
    XMLElement* c = parent ? parent->FirstChildElement(child) : nullptr;
    for (XMLElement* e = c ? c->FirstChildElement(elem) : nullptr; e; e = e->NextSiblingElement(elem))
        if (const char* x = e->Attribute("expr")) v.push_back(x);
    return v;
}

static ParserDef loadParser(const std::string& file)
{
    static std::map<std::string, ParserDef> cache;
    auto it = cache.find(file);
    if (it != cache.end()) return it->second;
    ParserDef p;
    XMLDocument doc;
    if (doc.LoadFile(file.c_str()) == XML_SUCCESS) {
        XMLElement* root = doc.FirstChildElement("NotepadPlus");
        XMLElement* fl = root ? root->FirstChildElement("functionList") : nullptr;
        XMLElement* pe = fl ? fl->FirstChildElement("parser") : nullptr;
        if (pe) {
            p.valid = true;
            if (auto s = pe->Attribute("displayName")) p.displayName = s;
            if (auto s = pe->Attribute("commentExpr")) p.commentExpr = s;
            for (XMLElement* cr = pe->FirstChildElement("classRange"); cr; cr = cr->NextSiblingElement("classRange")) {
                ParserDef::ClassRange r;
                if (auto s = cr->Attribute("mainExpr")) r.mainExpr = s;
                if (auto s = cr->Attribute("openSymbole")) r.open = s;
                if (auto s = cr->Attribute("closeSymbole")) r.close = s;
                r.classNameExprs = exprList(cr, "className", "nameExpr");
                if (XMLElement* f = cr->FirstChildElement("function")) {
                    if (auto s = f->Attribute("mainExpr")) r.func.mainExpr = s;
                    r.func.nameExprs = exprList(f, "functionName", "funcNameExpr");
                }
                p.classRanges.push_back(r);
            }
            for (XMLElement* f = pe->FirstChildElement("function"); f; f = f->NextSiblingElement("function")) {
                ParserDef::Func fn;
                if (auto s = f->Attribute("mainExpr")) fn.mainExpr = s;
                fn.nameExprs = exprList(f, "functionName", "nameExpr");
                fn.classExprs = exprList(f, "className", "nameExpr");
                p.functions.push_back(fn);
            }
        }
    }
    cache[file] = p;
    return p;
}

// apply a chain of nameExpr, each one searching inside the previous result
static std::string applyChain(const std::string& s, const std::vector<std::string>& chain)
{
    std::string cur = s;
    for (auto& e : chain) {
        Rx rx(e);
        size_t a, b;
        if (!rx.ok() || !rx.find(cur, 0, cur.size(), a, b)) return chain.size() == 1 ? std::string() : cur;
        cur = cur.substr(a, b - a);
    }
    return cur;
}

std::vector<FunctionListPanel::Entry> FunctionListPanel::parse(const std::string& text, int langType, std::string* parserName)
{
    std::vector<Entry> out;
    std::string file = findParserFile(langType);
    if (file.empty()) return out;
    ParserDef p = loadParser(file);
    if (!p.valid) return out;
    if (parserName) *parserName = p.displayName;

    // comment zones
    std::vector<std::pair<size_t, size_t>> comments;
    {
        Rx rx(p.commentExpr);
        size_t pos = 0, a, b;
        while (rx.ok() && rx.find(text, pos, text.size(), a, b)) { comments.emplace_back(a, b); pos = b > a ? b : a + 1; }
    }
    auto inComment = [&](size_t pos) {
        auto it = std::upper_bound(comments.begin(), comments.end(), std::make_pair(pos, size_t(-1)));
        if (it == comments.begin()) return false;
        --it;
        return pos >= it->first && pos < it->second;
    };

    std::vector<std::pair<size_t, size_t>> classZones;
    for (auto& cr : p.classRanges) {
        Rx main(cr.mainExpr), open(cr.open), close(cr.close), fmain(cr.func.mainExpr);
        size_t pos = 0, a, b;
        while (main.ok() && main.find(text, pos, text.size(), a, b)) {
            pos = b > a ? b : a + 1;
            if (inComment(a)) continue;
            // find the opening symbol, then the matching closing symbol.
            // Without symbols (e.g. Python) the class body is the match itself.
            size_t oa = a, ob = a;
            size_t zoneEnd = b;
            int depth = 0;
            if (!cr.open.empty()) {
                size_t scan = a;
                bool haveOpen = false;
                while (open.find(text, scan, text.size(), oa, ob)) { if (!inComment(oa)) { haveOpen = true; break; } scan = ob > oa ? ob : oa + 1; }
                if (!haveOpen) continue;
                depth = 1;
                zoneEnd = text.size();
            }
            size_t cursor = ob;
            while (depth > 0) {
                size_t na = 0, nb = 0, ca = 0, cb = 0;
                bool fo = open.find(text, cursor, text.size(), na, nb);
                bool fc = close.find(text, cursor, text.size(), ca, cb);
                if (!fc) { zoneEnd = text.size(); break; }
                if (fo && na < ca) { if (!inComment(na)) ++depth; cursor = nb > na ? nb : na + 1; }
                else { if (!inComment(ca)) --depth; cursor = cb > ca ? cb : ca + 1; zoneEnd = cb; }
            }
            std::string cls = applyChain(text.substr(a, b - a), cr.classNameExprs);
            classZones.emplace_back(a, zoneEnd);
            // methods inside the class body
            size_t fp = ob, fa, fb;
            while (fmain.ok() && fmain.find(text, fp, zoneEnd, fa, fb)) {
                fp = fb > fa ? fb : fa + 1;
                if (inComment(fa)) continue;
                std::string name = applyChain(text.substr(fa, fb - fa), cr.func.nameExprs);
                if (name.empty()) continue;
                out.push_back({name, cls, long(fa), 0});
            }
            if (cls.empty()) cls = "?";
            out.push_back({"", cls, long(a), 0});   // class header entry (empty name)
            pos = std::max(pos, ob);
        }
    }
    auto inClass = [&](size_t pos) {
        for (auto& z : classZones) if (pos >= z.first && pos < z.second) return true;
        return false;
    };
    for (auto& fn : p.functions) {
        Rx main(fn.mainExpr);
        size_t pos = 0, a, b;
        while (main.ok() && main.find(text, pos, text.size(), a, b)) {
            pos = b > a ? b : a + 1;
            if (inComment(a) || inClass(a)) continue;
            std::string m = text.substr(a, b - a);
            std::string name = applyChain(m, fn.nameExprs);
            if (name.empty()) continue;
            std::string cls = fn.classExprs.empty() ? "" : applyChain(m, fn.classExprs);
            if (!cls.empty() && name.size() > cls.size() && name.compare(0, cls.size(), cls) == 0) {
                // "Class::method" style names: strip the class prefix
                size_t k = cls.size();
                while (k < name.size() && (name[k] == ':' || name[k] == '.' || name[k] == '-' || name[k] == '>')) ++k;
                name = name.substr(k);
            }
            out.push_back({name, cls, long(a), 0});
            if (out.size() > 50000) break;
        }
    }
    // line numbers
    std::sort(out.begin(), out.end(), [](auto& x, auto& y) { return x.pos < y.pos; });
    long line = 0; size_t lastPos = 0;
    for (auto& e : out) {
        line += long(std::count(text.begin() + long(lastPos), text.begin() + e.pos, '\n'));
        lastPos = size_t(e.pos);
        e.line = line;
    }
    return out;
}

// ------------------------------------------------------------------ panel
FunctionListPanel::FunctionListPanel(MainWindow* mw) : _mw(mw)
{
    _box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    _search = gtk_search_entry_new();
    gtk_widget_set_hexpand(_search, TRUE);
    _sortBtn = gtk_toggle_button_new();
    gtk_button_set_image(GTK_BUTTON(_sortBtn), gtk_image_new_from_icon_name("view-sort-ascending", GTK_ICON_SIZE_MENU));
    gtk_widget_set_tooltip_text(_sortBtn, "Sort alphabetically");
    GtkWidget* reload = gtk_button_new_from_icon_name("view-refresh", GTK_ICON_SIZE_MENU);
    gtk_widget_set_tooltip_text(reload, "Reload");
    gtk_box_pack_start(GTK_BOX(bar), _search, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bar), _sortBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), reload, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(_box), bar, FALSE, FALSE, 0);
    _label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(_label), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(_label), "dim-label");
    gtk_box_pack_start(GTK_BOX(_box), _label, FALSE, FALSE, 0);
    GtkTreeStore* store = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_LONG, G_TYPE_LONG);
    _tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(_tree), FALSE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(_tree), FALSE);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(_tree), -1, "", gtk_cell_renderer_text_new(), "text", 0, nullptr);
    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(sw), _tree);
    gtk_box_pack_start(GTK_BOX(_box), sw, TRUE, TRUE, 0);

    g_signal_connect_swapped(_search, "search-changed", CAM_CB(+[](FunctionListPanel* p) { p->populate(); }), this);
    g_signal_connect_swapped(_sortBtn, "toggled", CAM_CB(+[](FunctionListPanel* p) { p->populate(); }), this);
    g_signal_connect_swapped(reload, "clicked", CAM_CB(+[](FunctionListPanel* p) { p->refreshNow(); }), this);
    g_signal_connect(_tree, "row-activated", CAM_CB(+[](GtkTreeView* tv, GtkTreePath* path, GtkTreeViewColumn*, gpointer self) {
        auto* p = static_cast<FunctionListPanel*>(self);
        GtkTreeModel* m = gtk_tree_view_get_model(tv);
        GtkTreeIter it;
        if (!gtk_tree_model_get_iter(m, &it, path)) return;
        glong pos = -1, line = -1;
        gtk_tree_model_get(m, &it, 1, &pos, 2, &line, -1);
        if (line < 0) return;
        Buffer* b = p->_mw->current();
        p->_mw->jumpTo(b, line, -1, -1);
    }), this);
    // single click also jumps (like Notepad++)
    g_signal_connect(_tree, "button-release-event", CAM_CB(+[](GtkWidget* w, GdkEventButton* ev, gpointer) -> gboolean {
        GtkTreePath* path = nullptr;
        if (ev->button == 1 && gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w), int(ev->x), int(ev->y), &path, nullptr, nullptr, nullptr)) {
            gtk_tree_view_row_activated(GTK_TREE_VIEW(w), path, nullptr);
            gtk_tree_path_free(path);
        }
        return FALSE;
    }), this);
    gtk_widget_show_all(_box);
}

void FunctionListPanel::refreshSoon()
{
    if (!gtk_widget_get_visible(_box)) return;
    if (_timer) g_source_remove(_timer);
    _timer = g_timeout_add(700, +[](gpointer self) -> gboolean {
        auto* p = static_cast<FunctionListPanel*>(self);
        p->_timer = 0;
        p->refreshNow();
        return G_SOURCE_REMOVE;
    }, this);
}

void FunctionListPanel::refreshNow()
{
    Buffer* b = _mw->current();
    if (!b) return;
    Editor& e = _mw->editor();
    if (e.call(SCI_GETLENGTH) > 16 * 1024 * 1024) { _entries.clear(); gtk_label_set_text(GTK_LABEL(_label), "File too large"); populate(); return; }
    std::string parser;
    _entries = parse(e.text(), b->langType, &parser);
    gtk_label_set_text(GTK_LABEL(_label), parser.empty() ? "No parser for this language" : (b->displayName() + " — " + parser).c_str());
    populate();
}

void FunctionListPanel::populate()
{
    GtkTreeStore* store = GTK_TREE_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(_tree)));
    gtk_tree_store_clear(store);
    std::string filter = gtk_entry_get_text(GTK_ENTRY(_search));
    gchar* ff = g_utf8_casefold(filter.c_str(), -1);
    filter = ff; g_free(ff);
    bool sorted = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_sortBtn));
    std::vector<Entry> v = _entries;
    if (sorted) std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return g_ascii_strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
    std::map<std::string, GtkTreeIter> classes;
    // class headers first so that methods attach to them
    for (auto& e : v) {
        if (!e.name.empty() || classes.count(e.cls)) continue;
        GtkTreeIter it;
        gtk_tree_store_append(store, &it, nullptr);
        gtk_tree_store_set(store, &it, 0, e.cls.c_str(), 1, glong(e.pos), 2, glong(e.line), -1);
        classes[e.cls] = it;
    }
    for (auto& e : v) {
        if (e.name.empty()) continue;
        if (!filter.empty()) {
            gchar* f = g_utf8_casefold(e.name.c_str(), -1);
            bool keep = std::strstr(f, filter.c_str()) != nullptr;
            g_free(f);
            if (!keep) continue;
        }
        GtkTreeIter parent, it;
        GtkTreeIter* pp = nullptr;
        if (!e.cls.empty()) {
            auto c = classes.find(e.cls);
            if (c == classes.end()) {
                gtk_tree_store_append(store, &parent, nullptr);
                gtk_tree_store_set(store, &parent, 0, e.cls.c_str(), 1, glong(-1), 2, glong(-1), -1);
                classes[e.cls] = parent;
                pp = &classes[e.cls];
            } else pp = &c->second;
        }
        gtk_tree_store_append(store, &it, pp);
        gtk_tree_store_set(store, &it, 0, e.name.c_str(), 1, glong(e.pos), 2, glong(e.line), -1);
    }
    gtk_tree_view_expand_all(GTK_TREE_VIEW(_tree));
}

} // namespace cam
