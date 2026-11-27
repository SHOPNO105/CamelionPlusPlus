// Camelion++ - Find / Replace / Find in Files / Mark dialog
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
#include "FindReplaceDlg.h"
#include "MainWindow.h"
#include "Editor.h"
#include "Settings.h"
#include "Paths.h"
#include "TextOps.h"
#include "Encoding.h"
#include <CamelionPluginInterface.h>
#include <dirent.h>
#include <sys/stat.h>
#include <regex>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace cam {

enum : unsigned { M_FIND = 1, M_REPLACE = 2, M_FILES = 4, M_MARK = 8 };

static GtkWidget* historyCombo()
{
    GtkWidget* c = gtk_combo_box_text_new_with_entry();
    gtk_widget_set_hexpand(c, TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(c))), TRUE);
    return c;
}
static std::string comboText(GtkWidget* c) { return gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(c)))); }
static void setComboText(GtkWidget* c, const std::string& s) { gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(c))), s.c_str()); }
static bool active(GtkWidget* w) { return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)); }

FindReplaceDlg::FindReplaceDlg(MainWindow* mw) : _mw(mw)
{
    _win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(_win), "Find");
    gtk_window_set_transient_for(GTK_WINDOW(_win), mw->window());
    gtk_window_set_type_hint(GTK_WINDOW(_win), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_resizable(GTK_WINDOW(_win), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(_win), 620, -1);
    g_signal_connect(_win, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), nullptr);
    g_signal_connect(_win, "key-press-event", CAM_CB(+[](GtkWidget* w, GdkEventKey* ev, gpointer) -> gboolean {
        if (ev->keyval == GDK_KEY_Escape) { gtk_widget_hide(w); return TRUE; }
        return FALSE;
    }), nullptr);

    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(outer), 10);
    gtk_container_add(GTK_CONTAINER(_win), outer);

    _tabs = gtk_stack_switcher_new();
    GtkWidget* stack = gtk_stack_new();   // empty pages: we only use the switcher as a tab bar
    for (auto [name, title] : {std::pair{"find", "Find"}, {"replace", "Replace"}, {"files", "Find in Files"}, {"mark", "Mark"}})
        gtk_stack_add_titled(GTK_STACK(stack), gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), name, title);
    gtk_widget_show_all(stack);   // never packed, but its pages must be "visible" to be selectable
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(_tabs), GTK_STACK(stack));
    gtk_box_pack_start(GTK_BOX(outer), _tabs, FALSE, FALSE, 0);
    g_signal_connect(stack, "notify::visible-child-name", CAM_CB(+[](GtkStack* s, GParamSpec*, gpointer self) {
        const char* n = gtk_stack_get_visible_child_name(s);
        int t = !std::strcmp(n, "find") ? 0 : !std::strcmp(n, "replace") ? 1 : !std::strcmp(n, "files") ? 2 : 3;
        static_cast<FindReplaceDlg*>(self)->switchTab(t);
    }), this);
    g_object_set_data(G_OBJECT(_win), "cam-stack", stack);

    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(outer), body, TRUE, TRUE, 0);
    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(body), left, TRUE, TRUE, 0);

    GtkWidget* g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 6);
    gtk_grid_set_column_spacing(GTK_GRID(g), 8);
    gtk_box_pack_start(GTK_BOX(left), g, FALSE, FALSE, 0);
    auto lbl = [](const char* t) { GtkWidget* l = gtk_label_new_with_mnemonic(t); gtk_label_set_xalign(GTK_LABEL(l), 1); return l; };
    _what = historyCombo();
    gtk_grid_attach(GTK_GRID(g), lbl("Find _what:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), _what, 1, 0, 2, 1);
    _with = historyCombo();
    _withLabel = lbl("Rep_lace with:");
    gtk_grid_attach(GTK_GRID(g), _withLabel, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), _with, 1, 1, 2, 1);
    _filters = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(_filters), Settings::get().getString("find.filters", "*.*").c_str());
    _dir = gtk_entry_new();
    GtkWidget* browse = gtk_button_new_with_label("...");
    _filtersRow = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(_filtersRow), 6);
    gtk_grid_set_column_spacing(GTK_GRID(_filtersRow), 8);
    gtk_grid_attach(GTK_GRID(_filtersRow), lbl("Filte_rs:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(_filtersRow), _filters, 1, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(_filtersRow), lbl("Director_y:"), 0, 1, 1, 1);
    gtk_widget_set_hexpand(_dir, TRUE);
    gtk_grid_attach(GTK_GRID(_filtersRow), _dir, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(_filtersRow), browse, 2, 1, 1, 1);
    _followDoc = gtk_check_button_new_with_label("Follow current doc.");
    _subfolders = gtk_check_button_new_with_label("In all sub-folders");
    _hidden = gtk_check_button_new_with_label("In hidden folders");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_subfolders), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_followDoc), TRUE);
    GtkWidget* fopts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(fopts), _followDoc, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fopts), _subfolders, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(fopts), _hidden, FALSE, FALSE, 0);
    gtk_grid_attach(GTK_GRID(_filtersRow), fopts, 1, 2, 2, 1);
    gtk_grid_attach(GTK_GRID(g), _filtersRow, 0, 2, 3, 1);
    g_signal_connect(browse, "clicked", CAM_CB(+[](GtkWidget*, gpointer self) {
        auto* d = static_cast<FindReplaceDlg*>(self);
        GtkWidget* fc = gtk_file_chooser_dialog_new("Select a folder", GTK_WINDOW(d->_win), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, nullptr);
        if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
            gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
            gtk_entry_set_text(GTK_ENTRY(d->_dir), f);
            g_free(f);
        }
        gtk_widget_destroy(fc);
    }), this);

    // options
    GtkWidget* opts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_pack_start(GTK_BOX(left), opts, FALSE, FALSE, 0);
    GtkWidget* col1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(opts), col1, FALSE, FALSE, 0);
    _bookmarkLine = gtk_check_button_new_with_mnemonic("Bookmark line");
    _purge = gtk_check_button_new_with_mnemonic("Purge for each search");
    _backward = gtk_check_button_new_with_mnemonic("Backward direction");
    _wholeWord = gtk_check_button_new_with_mnemonic("Match _whole word only");
    _matchCase = gtk_check_button_new_with_mnemonic("Match _case");
    _wrap = gtk_check_button_new_with_mnemonic("Wra_p around");
    _inSelection = gtk_check_button_new_with_mnemonic("In select_ion");
    _highlightAll = gtk_check_button_new_with_mnemonic("Highlight all matches");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_wrap), TRUE);
    for (GtkWidget* w : {_bookmarkLine, _purge, _backward, _wholeWord, _matchCase, _wrap, _inSelection, _highlightAll})
        gtk_box_pack_start(GTK_BOX(col1), w, FALSE, FALSE, 0);
    GtkWidget* modeFrame = gtk_frame_new("Search Mode");
    GtkWidget* col2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(col2), 6);
    gtk_container_add(GTK_CONTAINER(modeFrame), col2);
    _modeNormal = gtk_radio_button_new_with_mnemonic(nullptr, "_Normal");
    _modeExt = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(_modeNormal), "E_xtended (\\n, \\r, \\t, \\0, \\x...)");
    _modeRegex = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(_modeNormal), "Re_gular expression");
    _dotAll = gtk_check_button_new_with_mnemonic(". matches _newline");
    for (GtkWidget* w : {_modeNormal, _modeExt, _modeRegex, _dotAll}) gtk_box_pack_start(GTK_BOX(col2), w, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(opts), modeFrame, FALSE, FALSE, 0);
    g_signal_connect_swapped(_highlightAll, "toggled", CAM_CB(+[](FindReplaceDlg* d) { d->highlightAllMatches(); }), this);
    g_signal_connect_swapped(gtk_bin_get_child(GTK_BIN(_what)), "changed", CAM_CB(+[](FindReplaceDlg* d) { d->highlightAllMatches(); }), this);

    _status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(_status), 0);
    gtk_label_set_ellipsize(GTK_LABEL(_status), PANGO_ELLIPSIZE_END);
    gtk_box_pack_end(GTK_BOX(left), _status, FALSE, FALSE, 0);

    // buttons
    _buttonBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(body), _buttonBox, FALSE, FALSE, 0);
    auto button = [&](const char* label, unsigned mask, void (*cb)(FindReplaceDlg*)) {
        GtkWidget* b = gtk_button_new_with_mnemonic(label);
        g_signal_connect_swapped(b, "clicked", G_CALLBACK(cb), this);
        gtk_box_pack_start(GTK_BOX(_buttonBox), b, FALSE, FALSE, 0);
        _buttons.emplace_back(b, mask);
        return b;
    };
    GtkWidget* findNextBtn = button("Find _Next", M_FIND | M_REPLACE, [](FindReplaceDlg* d) { d->findNext(!active(d->_backward)); });
    gtk_widget_set_can_default(findNextBtn, TRUE);
    g_object_set_data(G_OBJECT(_win), "cam-default", findNextBtn);
    button("C_ount", M_FIND, [](FindReplaceDlg* d) { d->doCount(); });
    button("Find All in Current _Document", M_FIND, [](FindReplaceDlg* d) { d->doFindAll(false); });
    button("Find All in All _Opened Documents", M_FIND, [](FindReplaceDlg* d) { d->doFindAll(true); });
    button("_Replace", M_REPLACE, [](FindReplaceDlg* d) { d->doReplace(); });
    button("Replace _All", M_REPLACE, [](FindReplaceDlg* d) { d->doReplaceAll(false); });
    button("Replace All in All Opened Doc_uments", M_REPLACE, [](FindReplaceDlg* d) { d->doReplaceAll(true); });
    GtkWidget* findAllFiles = button("_Find All", M_FILES, [](FindReplaceDlg* d) { d->doFindInFiles(false); });
    g_object_set_data(G_OBJECT(_win), "cam-default-files", findAllFiles);
    gtk_widget_set_can_default(findAllFiles, TRUE);
    button("Replace in _Files", M_FILES, [](FindReplaceDlg* d) { d->doFindInFiles(true); });
    button("_Stop", M_FILES, [](FindReplaceDlg* d) { d->_stop = true; });
    GtkWidget* markAll = button("Mark _All", M_MARK, [](FindReplaceDlg* d) { d->doMarkAll(); });
    g_object_set_data(G_OBJECT(_win), "cam-default-mark", markAll);
    gtk_widget_set_can_default(markAll, TRUE);
    button("Clear all _marks", M_MARK, [](FindReplaceDlg* d) { d->doClearMarks(); });
    button("Copy Marked _Text", M_MARK, [](FindReplaceDlg* d) { d->doCopyMarked(); });
    button("Close", M_FIND | M_REPLACE | M_FILES | M_MARK, [](FindReplaceDlg* d) { d->hide(); });

    gtk_widget_show_all(outer);
}

void FindReplaceDlg::switchTab(int t)
{
    _tab = t;
    unsigned m = 1u << t;
    for (auto& [w, mask] : _buttons) gtk_widget_set_visible(w, (mask & m) != 0);
    bool rep = t == TabReplace || t == TabFiles;
    gtk_widget_set_visible(_withLabel, rep);
    gtk_widget_set_visible(_with, rep);
    gtk_widget_set_visible(_filtersRow, t == TabFiles);
    gtk_widget_set_visible(_bookmarkLine, t == TabMark);
    gtk_widget_set_visible(_purge, t == TabMark);
    gtk_widget_set_visible(_backward, t == TabFind || t == TabReplace);
    gtk_widget_set_visible(_wrap, t == TabFind || t == TabReplace);
    gtk_widget_set_visible(_inSelection, t != TabFiles);
    gtk_widget_set_visible(_highlightAll, t == TabFind);
    static const char* titles[] = {"Find", "Replace", "Find in Files", "Mark"};
    gtk_window_set_title(GTK_WINDOW(_win), titles[t]);
    const char* key = t == TabFiles ? "cam-default-files" : t == TabMark ? "cam-default-mark" : "cam-default";
    gtk_window_set_default(GTK_WINDOW(_win), GTK_WIDGET(g_object_get_data(G_OBJECT(_win), key)));
    if (t == TabFiles && active(_followDoc)) {
        Buffer* b = _mw->current();
        if (b && !b->path.empty()) gtk_entry_set_text(GTK_ENTRY(_dir), paths::dirName(b->path).c_str());
        else if (!*gtk_entry_get_text(GTK_ENTRY(_dir))) { gchar* cwd = g_get_current_dir(); gtk_entry_set_text(GTK_ENTRY(_dir), cwd); g_free(cwd); }
    }
}

void FindReplaceDlg::show(Tab t)
{
    Editor& e = _mw->editor();
    std::string sel = e.selectedText();
    if (!sel.empty() && sel.size() < 1024 && sel.find('\n') == std::string::npos) setComboText(_what, sel);
    else if (sel.empty() && comboText(_what).empty()) setComboText(_what, e.currentWord());
    GtkStack* stack = GTK_STACK(g_object_get_data(G_OBJECT(_win), "cam-stack"));
    static const char* names[] = {"find", "replace", "files", "mark"};
    gtk_stack_set_visible_child_name(stack, names[t]);
    switchTab(t);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_inSelection), !sel.empty() && sel.find('\n') != std::string::npos);
    setStatus("");
    gtk_window_present(GTK_WINDOW(_win));
    GtkWidget* entry = gtk_bin_get_child(GTK_BIN(_what));
    gtk_widget_grab_focus(entry);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
}

void FindReplaceDlg::hide()
{
    if (_win) gtk_widget_hide(_win);
}

FindReplaceDlg::Options FindReplaceDlg::read() const
{
    Options o;
    o.what = comboText(_what);
    o.with = comboText(_with);
    o.matchCase = active(_matchCase);
    o.wholeWord = active(_wholeWord);
    o.wrap = active(_wrap);
    o.backward = active(_backward);
    o.mode = active(_modeRegex) ? 2 : active(_modeExt) ? 1 : 0;
    o.dotAll = active(_dotAll);
    o.inSelection = active(_inSelection) && gtk_widget_get_visible(_inSelection);
    o.bookmarkLine = active(_bookmarkLine);
    o.purge = active(_purge);
    o.filters = gtk_entry_get_text(GTK_ENTRY(_filters));
    o.dir = gtk_entry_get_text(GTK_ENTRY(_dir));
    o.subfolders = active(_subfolders);
    o.hidden = active(_hidden);
    return o;
}

int FindReplaceDlg::searchFlags(const Options& o) const
{
    int f = 0;
    if (o.matchCase) f |= SCFIND_MATCHCASE;
    if (o.wholeWord && o.mode != 2) f |= SCFIND_WHOLEWORD;
    if (o.mode == 2) f |= SCFIND_REGEXP | SCFIND_CXX11REGEX;
    return f;
}

static std::string dotAllize(const std::string& re)
{
    std::string o; bool inClass = false;
    for (size_t i = 0; i < re.size(); ++i) {
        char c = re[i];
        if (c == '\\' && i + 1 < re.size()) { o += c; o += re[++i]; continue; }
        if (c == '[') inClass = true; else if (c == ']') inClass = false;
        if (c == '.' && !inClass) o += "[\\s\\S]"; else o += c;
    }
    return o;
}

std::string FindReplaceDlg::pattern(const Options& o) const
{
    if (o.mode == 1) return expandEscapes(o.what);
    if (o.mode == 2 && o.dotAll) return dotAllize(o.what);
    return o.what;
}

std::string FindReplaceDlg::replacement(const Options& o) const
{
    if (o.mode == 1) return expandEscapes(o.with);
    if (o.mode == 2) {   // also accept $1 style back-references: Scintilla understands \1
        std::string s; for (size_t i = 0; i < o.with.size(); ++i) {
            if (o.with[i] == '$' && i + 1 < o.with.size() && isdigit((unsigned char)o.with[i + 1])) { s += '\\'; continue; }
            s += o.with[i];
        }
        return s;
    }
    return o.with;
}

void FindReplaceDlg::addHistory(GtkWidget* combo, const std::string& s)
{
    if (s.empty()) return;
    GtkTreeModel* m = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
    GtkTreeIter it;
    int idx = 0;
    if (gtk_tree_model_get_iter_first(m, &it)) do {
        gchar* t = nullptr; gtk_tree_model_get(m, &it, 0, &t, -1);
        bool same = t && s == t; g_free(t);
        if (same) { gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(combo), idx); break; }
        ++idx;
    } while (gtk_tree_model_iter_next(m, &it));
    gtk_combo_box_text_prepend_text(GTK_COMBO_BOX_TEXT(combo), s.c_str());
    if (gtk_tree_model_iter_n_children(m, nullptr) > 20) gtk_combo_box_text_remove(GTK_COMBO_BOX_TEXT(combo), 20);
    setComboText(combo, s);
}

void FindReplaceDlg::setStatus(const std::string& s, int kind)
{
    const char* colour = kind == 1 ? "#d02020" : kind == 2 ? "#008000" : "#2060c0";
    gchar* esc = g_markup_escape_text(s.c_str(), -1);
    std::string m = std::string("<span foreground=\"") + colour + "\">" + esc + "</span>";
    g_free(esc);
    gtk_label_set_markup(GTK_LABEL(_status), m.c_str());
    if (kind == 1 && !s.empty()) gtk_widget_error_bell(_win);
    if (!s.empty()) _mw->setStatusText(0, s);
}

// ------------------------------------------------------------------ core search
static sptr_t searchIn(Editor& e, const std::string& pat, int flags, sptr_t from, sptr_t to)
{
    e.call(SCI_SETSEARCHFLAGS, uptr_t(flags));
    e.call(SCI_SETTARGETRANGE, uptr_t(from), to);
    return e.call(SCI_SEARCHINTARGET, pat.size(), reinterpret_cast<sptr_t>(pat.c_str()));
}

bool FindReplaceDlg::findNext(bool forward, bool quiet)
{
    Options o = read();
    if (o.what.empty()) { if (!quiet) show(TabFind); return false; }
    addHistory(_what, o.what);
    Editor& e = _mw->editor();
    std::string pat = pattern(o);
    int flags = searchFlags(o);
    sptr_t len = e.call(SCI_GETLENGTH);
    sptr_t a = e.call(SCI_GETSELECTIONSTART), b = e.call(SCI_GETSELECTIONEND);
    sptr_t f;
    bool wrapped = false;
    if (forward) {
        f = searchIn(e, pat, flags, b, len);
        if (f >= 0 && f == b && e.call(SCI_GETTARGETEND) == b && b < len) f = searchIn(e, pat, flags, b + 1, len);  // empty match
        if (f < 0 && o.wrap) { f = searchIn(e, pat, flags, 0, len); wrapped = true; }
    } else {
        f = searchIn(e, pat, flags, a, 0);
        if (f < 0 && o.wrap) { f = searchIn(e, pat, flags, len, 0); wrapped = true; }
    }
    if (f == -2) { setStatus("Find: Invalid regular expression", 1); return false; }
    if (f < 0) { setStatus("Find: Can't find the text \"" + o.what + "\"", 1); return false; }
    sptr_t te = e.call(SCI_GETTARGETEND);
    e.call(SCI_ENSUREVISIBLEENFORCEPOLICY, uptr_t(e.call(SCI_LINEFROMPOSITION, uptr_t(f))));
    e.call(SCI_SETSEL, uptr_t(f), te);
    e.call(SCI_SCROLLRANGE, uptr_t(te), f);
    _mw->setStatusText(0, "");
    if (wrapped) setStatus(forward ? "Find: Found the 1st occurrence from the top. The end of the document has been reached."
                                   : "Find: Found the 1st occurrence from the bottom. The beginning of the document has been reached.", 2);
    else setStatus("");
    return true;
}

void FindReplaceDlg::selectAndFind(bool forward, bool isVolatile)
{
    Editor& e = _mw->editor();
    std::string s = e.selectedText();
    if (s.empty()) {
        sptr_t pos = e.call(SCI_GETCURRENTPOS);
        sptr_t a = e.call(SCI_WORDSTARTPOSITION, uptr_t(pos), 1), b = e.call(SCI_WORDENDPOSITION, uptr_t(pos), 1);
        if (a == b) return;
        e.call(SCI_SETSEL, uptr_t(a), b);
        s = e.textRange(a, b);
    }
    std::string keep = comboText(_what);
    setComboText(_what, s);
    bool keepRegex = active(_modeRegex), keepExt = active(_modeExt);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_modeNormal), TRUE);
    findNext(forward, true);
    if (keepRegex) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_modeRegex), TRUE);
    if (keepExt) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(_modeExt), TRUE);
    if (isVolatile) setComboText(_what, keep);
}

void FindReplaceDlg::doReplace()
{
    Options o = read();
    if (o.what.empty()) return;
    addHistory(_with, o.with);
    Editor& e = _mw->editor();
    if (e.call(SCI_GETREADONLY)) { setStatus("Replace: Cannot replace text. The current document is read-only.", 1); return; }
    std::string pat = pattern(o);
    int flags = searchFlags(o);
    sptr_t a = e.call(SCI_GETSELECTIONSTART), b = e.call(SCI_GETSELECTIONEND);
    // replace only if the current selection is exactly a match
    if (a != b || o.mode == 2) {
        sptr_t f = searchIn(e, pat, flags, a, b);
        if (f == a && e.call(SCI_GETTARGETEND) == b) {
            std::string rep = replacement(o);
            if (o.mode == 2) e.call(SCI_REPLACETARGETRE, rep.size(), reinterpret_cast<sptr_t>(rep.c_str()));
            else e.call(SCI_REPLACETARGET, rep.size(), reinterpret_cast<sptr_t>(rep.c_str()));
            e.call(SCI_SETSEL, uptr_t(e.call(SCI_GETTARGETEND)), e.call(SCI_GETTARGETEND));
            setStatus("Replace: 1 occurrence was replaced.");
        }
    }
    findNext(!o.backward, true);
}

int FindReplaceDlg::replaceAllIn(Editor& e, const Options& o, bool inSel)
{
    if (e.call(SCI_GETREADONLY)) return -1;
    std::string pat = pattern(o), rep = replacement(o);
    int flags = searchFlags(o);
    sptr_t start = inSel ? e.call(SCI_GETSELECTIONSTART) : 0;
    sptr_t end = inSel ? e.call(SCI_GETSELECTIONEND) : e.call(SCI_GETLENGTH);
    int count = 0;
    e.call(SCI_BEGINUNDOACTION);
    while (start <= end) {
        sptr_t f = searchIn(e, pat, flags, start, end);
        if (f < 0) break;
        sptr_t te = e.call(SCI_GETTARGETEND);
        sptr_t matchLen = te - f;
        sptr_t newLen = (o.mode == 2) ? e.call(SCI_REPLACETARGETRE, rep.size(), reinterpret_cast<sptr_t>(rep.c_str()))
                                      : e.call(SCI_REPLACETARGET, rep.size(), reinterpret_cast<sptr_t>(rep.c_str()));
        ++count;
        end += newLen - matchLen;
        start = f + newLen;
        if (matchLen == 0) ++start;   // avoid looping on empty matches
        if (count > 10000000) break;
    }
    e.call(SCI_ENDUNDOACTION);
    return count;
}

void FindReplaceDlg::doReplaceAll(bool allDocs)
{
    Options o = read();
    if (o.what.empty()) return;
    addHistory(_what, o.what);
    addHistory(_with, o.with);
    int total = 0, docs = 0;
    if (!allDocs) {
        total = replaceAllIn(_mw->editor(), o, o.inSelection);
        if (total < 0) { setStatus("Replace All: Cannot replace text. The current document is read-only.", 1); return; }
    } else {
        if (_mw->askQuestion("Are you sure you want to replace all occurrences in all opened documents?", "", {{"_Cancel", 0}, {"_Replace All", 1}}) != 1) return;
        for (auto& b : _mw->buffers()) {
            int n = 0;
            _mw->withBufferEditor(b.get(), [&](Editor& e) { n = replaceAllIn(e, o, false); });
            if (n > 0) { total += n; ++docs; b->dirty = b->dirty || n > 0; _mw->updateTab(b.get()); }
        }
    }
    if (total > 0) _mw->notifyPlugins(NPPN_GLOBALMODIFIED, uptr_t(_mw->current()->id));
    std::string msg = "Replace All: " + std::to_string(std::max(total, 0)) + " occurrence" + (total == 1 ? " was" : "s were") + " replaced";
    if (allDocs) msg += " in " + std::to_string(docs) + " document" + (docs == 1 ? "" : "s");
    setStatus(msg + ".", total > 0 ? 0 : 1);
}

std::vector<FindReplaceDlg::Hit> FindReplaceDlg::collectHits(Editor& e, const Options& o)
{
    std::vector<Hit> hits;
    std::string pat = pattern(o);
    int flags = searchFlags(o);
    sptr_t start = o.inSelection ? e.call(SCI_GETSELECTIONSTART) : 0;
    sptr_t end = o.inSelection ? e.call(SCI_GETSELECTIONEND) : e.call(SCI_GETLENGTH);
    while (start <= end) {
        sptr_t f = searchIn(e, pat, flags, start, end);
        if (f < 0) break;
        sptr_t te = e.call(SCI_GETTARGETEND);
        long line = long(e.call(SCI_LINEFROMPOSITION, uptr_t(f)));
        hits.push_back({line, long(f), long(te), {}});
        start = te > f ? te : f + 1;
        if (hits.size() > 200000) break;
    }
    return hits;
}

void FindReplaceDlg::doCount()
{
    Options o = read();
    if (o.what.empty()) return;
    addHistory(_what, o.what);
    size_t n = collectHits(_mw->editor(), o).size();
    setStatus("Count: " + std::to_string(n) + " match" + (n == 1 ? "" : "es") + (o.inSelection ? " in selection." : " in entire file."), n ? 0 : 1);
}

void FindReplaceDlg::doMarkAll()
{
    Options o = read();
    if (o.what.empty()) return;
    addHistory(_what, o.what);
    Editor& e = _mw->editor();
    if (o.purge) doClearMarks();
    auto hits = collectHits(e, o);
    e.call(SCI_SETINDICATORCURRENT, INDIC_FOUND);
    for (auto& h : hits) {
        e.call(SCI_INDICATORFILLRANGE, uptr_t(h.start), h.end - h.start);
        if (o.bookmarkLine) e.call(SCI_MARKERADD, uptr_t(h.line), MARK_BOOKMARK);
    }
    setStatus("Mark: " + std::to_string(hits.size()) + " match" + (hits.size() == 1 ? "" : "es") + (o.inSelection ? " in selection." : " in entire file."), hits.empty() ? 1 : 0);
}

void FindReplaceDlg::doClearMarks()
{
    Editor& e = _mw->editor();
    e.call(SCI_SETINDICATORCURRENT, INDIC_FOUND);
    e.call(SCI_INDICATORCLEARRANGE, 0, e.call(SCI_GETLENGTH));
    if (active(_bookmarkLine)) e.call(SCI_MARKERDELETEALL, MARK_BOOKMARK);
}

void FindReplaceDlg::doCopyMarked()
{
    Editor& e = _mw->editor();
    std::string out;
    sptr_t len = e.call(SCI_GETLENGTH), pos = 0;
    while (pos < len) {
        sptr_t s = e.call(SCI_INDICATORSTART, INDIC_FOUND, pos);
        sptr_t t = e.call(SCI_INDICATOREND, INDIC_FOUND, s);
        if (e.call(SCI_INDICATORVALUEAT, INDIC_FOUND, s)) { if (!out.empty()) out += "\n"; out += e.textRange(s, t); }
        if (t <= pos) break;
        pos = t;
    }
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), out.c_str(), -1);
    setStatus(out.empty() ? "Nothing is marked." : "Marked text copied to the clipboard.");
}

void FindReplaceDlg::styleToken(int indicator)
{
    Editor& e = _mw->editor();
    std::string s = e.selectedText();
    bool word = false;
    if (s.empty()) { s = e.currentWord(); word = true; }
    if (s.empty()) return;
    e.call(SCI_SETINDICATORCURRENT, uptr_t(indicator));
    int flags = SCFIND_MATCHCASE | (word ? SCFIND_WHOLEWORD : 0);
    sptr_t pos = 0, len = e.call(SCI_GETLENGTH);
    while (pos < len) {
        sptr_t f = searchIn(e, s, flags, pos, len);
        if (f < 0) break;
        sptr_t te = e.call(SCI_GETTARGETEND);
        e.call(SCI_INDICATORFILLRANGE, uptr_t(f), te - f);
        pos = te > f ? te : f + 1;
    }
}

void FindReplaceDlg::highlightAllMatches()
{
    Editor& e = _mw->editor();
    e.call(SCI_SETINDICATORCURRENT, INDIC_FINDALL);
    e.call(SCI_INDICATORCLEARRANGE, 0, e.call(SCI_GETLENGTH));
    if (!active(_highlightAll)) return;
    Options o = read();
    if (o.what.empty() || o.what.size() > 500) return;
    auto hits = collectHits(e, o);
    for (auto& h : hits) e.call(SCI_INDICATORFILLRANGE, uptr_t(h.start), h.end - h.start);
}

// ------------------------------------------------------------------ results panel
static std::string markupLine(const std::string& line, long a, long b)
{
    // a/b are byte offsets of the match inside the line
    std::string pre = line.substr(0, size_t(std::max(0L, a)));
    std::string mid = line.substr(size_t(std::max(0L, a)), size_t(std::max(0L, b - a)));
    std::string post = size_t(b) < line.size() ? line.substr(size_t(b)) : "";
    auto esc = [](const std::string& s) {
        std::string v = s;
        if (!g_utf8_validate(v.c_str(), gssize(v.size()), nullptr)) { gchar* m = g_utf8_make_valid(v.c_str(), gssize(v.size())); v = m; g_free(m); }
        gchar* e = g_markup_escape_text(v.c_str(), -1); std::string r = e; g_free(e); return r;
    };
    return esc(pre) + "<span background=\"#ffe680\" foreground=\"#000000\"><b>" + esc(mid) + "</b></span>" + esc(post);
}

void FindReplaceDlg::showResults(const std::string& header, const std::vector<std::pair<std::string, std::vector<Hit>>>& files,
                                 const std::vector<int>& bufferIds, size_t totalHits)
{
    GtkTreeView* tv = GTK_TREE_VIEW(_mw->searchResultsView());
    GtkTreeStore* store = GTK_TREE_STORE(gtk_tree_view_get_model(tv));
    // collapse previous searches, add this one on top (like Notepad++)
    gtk_tree_view_collapse_all(tv);
    GtkTreeIter root;
    gtk_tree_store_prepend(store, &root, nullptr);
    gchar* eh = g_markup_escape_text(header.c_str(), -1);
    std::string rootText = std::string("<b>") + eh + "</b>";
    g_free(eh);
    gtk_tree_store_set(store, &root, 0, rootText.c_str(), 1, 0, 2, gint64(-1), 3, gint64(-1), 4, gint64(-1), -1);
    for (size_t i = 0; i < files.size(); ++i) {
        auto& [path, hits] = files[i];
        GtkTreeIter fi;
        gtk_tree_store_append(store, &fi, &root);
        gchar* ep = g_markup_escape_text(path.c_str(), -1);
        std::string ft = std::string("<span foreground=\"#2e7d32\">") + ep + "</span> (" + std::to_string(hits.size()) + " hit" + (hits.size() == 1 ? "" : "s") + ")";
        g_free(ep);
        gtk_tree_store_set(store, &fi, 0, ft.c_str(), 1, 0, 2, gint64(-1), 3, gint64(-1), 4, gint64(-1), -1);
        for (auto& h : hits) {
            GtkTreeIter hi;
            gtk_tree_store_append(store, &hi, &fi);
            std::string t = "\tLine " + std::to_string(h.line + 1) + ": " + h.text;
            gtk_tree_store_set(store, &hi, 0, t.c_str(), 1, bufferIds[i], 2, gint64(h.line), 3, gint64(h.start), 4, gint64(h.end), -1);
        }
    }
    GtkTreePath* p = gtk_tree_model_get_path(GTK_TREE_MODEL(store), &root);
    gtk_tree_view_expand_row(tv, p, TRUE);
    gtk_tree_view_set_cursor(tv, p, nullptr, FALSE);
    gtk_tree_path_free(p);
    _mw->toggleSearchResults(true);
    (void)totalHits;
}

void FindReplaceDlg::doFindAll(bool allDocs)
{
    Options o = read();
    if (o.what.empty()) return;
    addHistory(_what, o.what);
    std::vector<std::pair<std::string, std::vector<Hit>>> files;
    std::vector<int> ids;
    size_t total = 0;
    std::vector<Buffer*> list;
    if (allDocs) for (auto& b : _mw->buffers()) list.push_back(b.get());
    else list.push_back(_mw->current());
    for (Buffer* b : list) {
        std::vector<Hit> hits;
        _mw->withBufferEditor(b, [&](Editor& e) {
            Options oo = o;
            if (b != _mw->current()) oo.inSelection = false;
            hits = collectHits(e, oo);
            for (auto& h : hits) {
                sptr_t ls = e.call(SCI_POSITIONFROMLINE, uptr_t(h.line));
                std::string line = e.lineText(h.line);
                long a = h.start - long(ls), z = std::min<long>(h.end - long(ls), long(line.size()));
                h.text = markupLine(line, a, z);
            }
        });
        if (hits.empty()) continue;
        total += hits.size();
        files.emplace_back(b->path.empty() ? b->untitledName : b->path, std::move(hits));
        ids.push_back(b->id);
    }
    std::string header = "Search \"" + o.what + "\" (" + std::to_string(total) + " hit" + (total == 1 ? "" : "s") + " in " +
                         std::to_string(files.size()) + " file" + (files.size() == 1 ? "" : "s") + (allDocs ? " of " + std::to_string(list.size()) + " searched)" : ")");
    showResults(header, files, ids, total);
    setStatus(std::to_string(total) + " hit(s)", total ? 0 : 1);
}

// ------------------------------------------------------------------ Find in Files
static bool matchesFilters(const std::string& name, const std::vector<std::string>& include, const std::vector<std::string>& exclude)
{
    for (auto& x : exclude) if (g_pattern_match_simple(x.c_str(), name.c_str())) return false;
    if (include.empty()) return true;
    for (auto& p : include) {
        if (p == "*.*" || p == "*") return true;
        if (g_pattern_match_simple(p.c_str(), name.c_str())) return true;
    }
    return false;
}

void FindReplaceDlg::doFindInFiles(bool replace)
{
    Options o = read();
    if (o.what.empty() || o.dir.empty()) { setStatus("Find in Files: the directory and the search text are required.", 1); return; }
    if (!paths::dirExists(o.dir)) { setStatus("Find in Files: invalid directory.", 1); return; }
    addHistory(_what, o.what);
    Settings::get().set("find.filters", o.filters);
    if (replace) {
        addHistory(_with, o.with);
        if (_mw->askQuestion("Are you sure you want to replace all occurrences in:\n" + o.dir + "\nfor file type: " + o.filters + " ?",
                             "This operation cannot be undone.", {{"_Cancel", 0}, {"_Replace", 1}}) != 1) return;
    }
    std::vector<std::string> include, exclude;
    std::istringstream fs(o.filters);
    std::string tok;
    while (fs >> tok) {
        // filters are separated by spaces or ';', "!pattern" excludes
        std::istringstream parts(tok);
        std::string s;
        while (std::getline(parts, s, ';')) {
            if (s.empty()) continue;
            if (s[0] == '!') exclude.push_back(s.substr(1));
            else include.push_back(s);
        }
    }

    // build the matcher
    std::string pat = pattern(o);
    std::regex re;
    bool useRegex = o.mode == 2;
    bool literalAsRegex = false;
    if (!useRegex && !o.matchCase) {
        // case-insensitive literal search: go through std::regex so match offsets stay exact
        std::string esc;
        for (char c : pat) { if (std::strchr("\\^$.|?*+()[]{}", c)) esc += '\\'; esc += c; }
        if (o.wholeWord) esc = "\\b" + esc + "\\b";
        pat = esc; useRegex = true; literalAsRegex = true;
    }
    if (useRegex) {
        try { re = std::regex(pat, std::regex::ECMAScript | (o.matchCase ? std::regex::flag_type(0) : std::regex::icase)); }
        catch (const std::regex_error&) { setStatus("Find in Files: Invalid regular expression", 1); return; }
    }
    std::string needle = pat;
    auto isWordChar = [](unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; };

    std::vector<std::pair<std::string, std::vector<Hit>>> files;
    std::vector<int> ids;
    size_t total = 0, scanned = 0, replacedFiles = 0, replacedCount = 0;
    _stop = false;
    std::vector<std::string> stack{o.dir};
    _filePaths.clear();
    while (!stack.empty() && !_stop) {
        std::string dir = stack.back(); stack.pop_back();
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        std::vector<std::string> entries;
        while (dirent* de = readdir(d)) entries.push_back(de->d_name);
        closedir(d);
        std::sort(entries.begin(), entries.end());
        for (auto& n : entries) {
            if (n == "." || n == "..") continue;
            std::string full = paths::join(dir, n);
            struct stat st;
            if (lstat(full.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                if (!o.subfolders) continue;
                if (n[0] == '.' && !o.hidden) continue;
                stack.push_back(full);
                continue;
            }
            if (!S_ISREG(st.st_mode) || st.st_size > 64 * 1024 * 1024) continue;
            if (!matchesFilters(n, include, exclude)) continue;
            ++scanned;
            if (scanned % 50 == 0) {
                setStatus("Searching... " + std::to_string(scanned) + " files scanned (" + std::to_string(total) + " hits)");
                while (gtk_events_pending()) gtk_main_iteration();
                if (_stop) break;
            }
            bool ok = false;
            std::string raw = paths::readFile(full, &ok);
            if (!ok) continue;
            if (raw.find('\0', 0) != std::string::npos && raw.find('\0') < 8000 && !(raw.size() > 1 && (unsigned char)raw[0] >= 0xFE)) continue;  // binary
            DecodedText dt = decodeText(raw, Settings::get().getString("encoding.ansiCharset", "WINDOWS-1252"));
            const std::string& text = dt.utf8;
            std::vector<Hit> hits;
            size_t lineStart = 0; long lineNo = 0;
            while (lineStart <= text.size()) {
                size_t nl = text.find('\n', lineStart);
                size_t lineEnd = nl == std::string::npos ? text.size() : nl;
                std::string line = text.substr(lineStart, lineEnd - lineStart);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (useRegex) {
                    for (auto it = std::sregex_iterator(line.begin(), line.end(), re); it != std::sregex_iterator(); ++it) {
                        long a = long(it->position()), z = a + long(it->length());
                        hits.push_back({lineNo, long(lineStart) + a, long(lineStart) + z, markupLine(line, a, z)});
                        if (it->length() == 0) break;
                    }
                } else {
                    const std::string& hay = line;
                    size_t p = 0;
                    while (!needle.empty() && (p = hay.find(needle, p)) != std::string::npos) {
                        bool okWord = !o.wholeWord || ((p == 0 || !isWordChar(hay[p - 1])) &&
                                      (p + needle.size() >= hay.size() || !isWordChar(hay[p + needle.size()])));
                        if (okWord) {
                            long a = long(p), z = long(p + needle.size());
                            hits.push_back({lineNo, long(lineStart) + a, long(lineStart) + z, markupLine(line, a, z)});
                        }
                        p += std::max<size_t>(1, needle.size());
                    }
                }
                if (nl == std::string::npos) break;
                lineStart = nl + 1; ++lineNo;
                if (hits.size() > 10000) break;
            }
            if (hits.empty()) continue;
            total += hits.size();
            if (replace) {
                std::string out;
                std::string rep = o.mode == 1 ? expandEscapes(o.with) : o.with;
                if (useRegex) {
                    std::string fmt;   // \1 => $1 for std::regex_replace; literal text keeps '$' literal
                    for (size_t i = 0; i < rep.size(); ++i) {
                        if (literalAsRegex) { if (rep[i] == '$') fmt += '$'; fmt += rep[i]; continue; }
                        if (rep[i] == '\\' && i + 1 < rep.size() && isdigit((unsigned char)rep[i + 1])) { fmt += '$'; continue; }
                        fmt += rep[i];
                    }
                    out = std::regex_replace(text, re, fmt);
                } else {
                    // replace using the hit offsets (already computed per line)
                    size_t last = 0;
                    for (auto& h : hits) { out += text.substr(last, size_t(h.start) - last); out += rep; last = size_t(h.end); }
                    out += text.substr(last);
                }
                std::string bytes, err;
                if (encodeText(out, dt.mode, dt.charset, bytes, &err) && paths::writeFileAtomic(full, bytes, &err)) {
                    ++replacedFiles; replacedCount += hits.size();
                    if (Buffer* ob = _mw->bufferByPath(full)) _mw->reloadBuffer(ob, false);
                }
            }
            _filePaths.push_back(full);
            int rid = -int(_filePaths.size());
            files.emplace_back(full, std::move(hits));
            ids.push_back(rid);
        }
    }
    std::string header = (replace ? "Replace \"" : "Search \"") + o.what + "\" (" + std::to_string(total) + " hit" + (total == 1 ? "" : "s") + " in " +
                         std::to_string(files.size()) + " file" + (files.size() == 1 ? "" : "s") + " of " + std::to_string(scanned) + " searched)";
    if (_stop) header += " [stopped]";
    showResults(header, files, ids, total);
    if (replace) setStatus("Replace in Files: " + std::to_string(replacedCount) + " occurrences were replaced in " + std::to_string(replacedFiles) + " files.");
    else setStatus(std::to_string(total) + " hits in " + std::to_string(files.size()) + " files.", total ? 0 : 1);
}

std::string FindReplaceDlg::resultFilePath(int id) const
{
    size_t i = size_t(-id) - 1;
    return i < _filePaths.size() ? _filePaths[i] : std::string();
}

void FindReplaceDlg::gotoResult(bool next)
{
    GtkTreeView* tv = GTK_TREE_VIEW(_mw->searchResultsView());
    GtkTreeModel* m = gtk_tree_view_get_model(tv);
    GtkTreePath* cur = nullptr;
    gtk_tree_view_get_cursor(tv, &cur, nullptr);
    // flatten the first (latest) search into a list of hit paths
    GtkTreeIter root;
    if (!gtk_tree_model_get_iter_first(m, &root)) { if (cur) gtk_tree_path_free(cur); return; }
    std::vector<GtkTreePath*> hits;
    GtkTreeIter file;
    if (gtk_tree_model_iter_children(m, &file, &root)) do {
        GtkTreeIter h;
        if (gtk_tree_model_iter_children(m, &h, &file)) do hits.push_back(gtk_tree_model_get_path(m, &h)); while (gtk_tree_model_iter_next(m, &h));
    } while (gtk_tree_model_iter_next(m, &file));
    if (hits.empty()) { if (cur) gtk_tree_path_free(cur); return; }
    int idx = -1;
    if (cur) for (size_t i = 0; i < hits.size(); ++i) if (gtk_tree_path_compare(cur, hits[i]) == 0) idx = int(i);
    int n = int(hits.size());
    int target = idx < 0 ? (next ? 0 : n - 1) : (next ? (idx + 1) % n : (idx + n - 1) % n);
    gtk_tree_view_expand_to_path(tv, hits[size_t(target)]);
    gtk_tree_view_set_cursor(tv, hits[size_t(target)], nullptr, FALSE);
    gtk_tree_view_row_activated(tv, hits[size_t(target)], nullptr);
    for (auto* p : hits) gtk_tree_path_free(p);
    if (cur) gtk_tree_path_free(cur);
}

} // namespace cam
