// Camelion++ - assorted dialogs
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
#include "Dialogs.h"
#include "MainWindow.h"
#include "Parameters.h"
#include "Settings.h"
#include "Paths.h"
#include "LangDefs.h"
#include "PluginManager.h"
#include "Version.h"
#include <cstring>
#include <sstream>
#include <glib/gstdio.h>

namespace cam {

static GtkWidget* makeDialog(GtkWindow* parent, const char* title)
{
    GtkWidget* d = gtk_dialog_new_with_buttons(title, parent, GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), nullptr, nullptr);
    gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), 10);
    gtk_box_set_spacing(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(d))), 8);
    return d;
}

static GtkWidget* grid()
{
    GtkWidget* g = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(g), 6);
    gtk_grid_set_column_spacing(GTK_GRID(g), 10);
    return g;
}

static GtkWidget* leftLabel(const char* t)
{
    GtkWidget* l = gtk_label_new_with_mnemonic(t);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    return l;
}

std::string inputBox(GtkWindow* parent, const std::string& title, const std::string& prompt, const std::string& initial, bool* ok)
{
    GtkWidget* d = makeDialog(parent, title.c_str());
    gtk_dialog_add_buttons(GTK_DIALOG(d), "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_box_pack_start(GTK_BOX(area), leftLabel(prompt.c_str()), FALSE, FALSE, 0);
    GtkWidget* e = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e), initial.c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(e), 40);
    gtk_box_pack_start(GTK_BOX(area), e, FALSE, FALSE, 0);
    gtk_widget_show_all(d);
    bool accepted = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK;
    std::string r = gtk_entry_get_text(GTK_ENTRY(e));
    gtk_widget_destroy(d);
    if (ok) *ok = accepted;
    return accepted ? r : std::string();
}

// ------------------------------------------------------------------ Go To
void goToDialog(MainWindow* mw)
{
    Editor& ed = mw->editor();
    GtkWidget* d = makeDialog(mw->window(), "Go To...");
    gtk_dialog_add_buttons(GTK_DIALOG(d), "_Cancel", GTK_RESPONSE_CANCEL, "_Go", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(d));
    GtkWidget* g = grid();
    GtkWidget* rLine = gtk_radio_button_new_with_mnemonic(nullptr, "_Line");
    GtkWidget* rOff = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(rLine), "_Offset");
    sptr_t pos = ed.call(SCI_GETCURRENTPOS);
    sptr_t curLine = ed.call(SCI_LINEFROMPOSITION, uptr_t(pos)) + 1;
    sptr_t lastLine = ed.call(SCI_GETLINECOUNT);
    GtkWidget* info = gtk_label_new("");
    auto updInfo = [](GtkWidget* info, bool line, sptr_t cur, sptr_t last) {
        char buf[128];
        std::snprintf(buf, sizeof buf, line ? "You are here: %ld     You want to go to (1 - %ld):" : "You are here: %ld     You want to go to (0 - %ld):", long(cur), long(last));
        gtk_label_set_text(GTK_LABEL(info), buf);
    };
    updInfo(info, true, curLine, lastLine);
    GtkWidget* e = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    gtk_entry_set_input_purpose(GTK_ENTRY(e), GTK_INPUT_PURPOSE_DIGITS);
    gtk_grid_attach(GTK_GRID(g), rLine, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), rOff, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), info, 0, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(g), e, 0, 2, 2, 1);
    gtk_box_pack_start(GTK_BOX(area), g, FALSE, FALSE, 0);
    struct Ctx { GtkWidget* info; sptr_t curLine, lastLine, pos, len; } ctx{info, curLine, lastLine, pos, ed.call(SCI_GETLENGTH)};
    g_object_set_data(G_OBJECT(rOff), "ctx", &ctx);
    g_signal_connect(rOff, "toggled", CAM_CB(+[](GtkToggleButton* t, gpointer) {
        auto* c = static_cast<Ctx*>(g_object_get_data(G_OBJECT(t), "ctx"));
        char buf[128];
        if (gtk_toggle_button_get_active(t)) std::snprintf(buf, sizeof buf, "You are here: %ld     You want to go to (0 - %ld):", long(c->pos), long(c->len));
        else std::snprintf(buf, sizeof buf, "You are here: %ld     You want to go to (1 - %ld):", long(c->curLine), long(c->lastLine));
        gtk_label_set_text(GTK_LABEL(c->info), buf);
    }), nullptr);
    gtk_widget_show_all(d);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        long v = std::atol(gtk_entry_get_text(GTK_ENTRY(e)));
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rOff))) {
            ed.call(SCI_GOTOPOS, uptr_t(std::max(0L, std::min(v, long(ed.call(SCI_GETLENGTH))))));
        } else if (v > 0) {
            sptr_t line = std::min<sptr_t>(v, lastLine) - 1;
            ed.call(SCI_ENSUREVISIBLE, uptr_t(line));
            ed.call(SCI_GOTOLINE, uptr_t(line));
        }
        ed.call(SCI_VERTICALCENTRECARET);
    }
    gtk_widget_destroy(d);
}

// ------------------------------------------------------------------ Column editor
void columnEditorDialog(MainWindow* mw)
{
    Editor& ed = mw->editor();
    GtkWidget* d = makeDialog(mw->window(), "Column / Multi-Selection Editor");
    gtk_dialog_add_buttons(GTK_DIALOG(d), "_Cancel", GTK_RESPONSE_CANCEL, "_OK", GTK_RESPONSE_OK, nullptr);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(d));
    GtkWidget* g = grid();
    GtkWidget* rText = gtk_radio_button_new_with_mnemonic(nullptr, "_Text to Insert");
    GtkWidget* eText = gtk_entry_new();
    GtkWidget* rNum = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(rText), "_Number to Insert");
    GtkWidget* eInit = gtk_spin_button_new_with_range(-1e9, 1e9, 1);
    GtkWidget* eInc = gtk_spin_button_new_with_range(-1e9, 1e9, 1);
    GtkWidget* eRep = gtk_spin_button_new_with_range(1, 1e6, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(eInit), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(eInc), 1);
    GtkWidget* lead = gtk_combo_box_text_new();
    for (auto s : {"None", "Zeros", "Spaces"}) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(lead), s);
    gtk_combo_box_set_active(GTK_COMBO_BOX(lead), 0);
    GtkWidget* fmt = gtk_combo_box_text_new();
    for (auto s : {"Dec", "Hex", "Oct", "Bin"}) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fmt), s);
    gtk_combo_box_set_active(GTK_COMBO_BOX(fmt), 0);
    gtk_grid_attach(GTK_GRID(g), rText, 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(g), eText, 0, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(g), rNum, 0, 2, 2, 1);
    gtk_grid_attach(GTK_GRID(g), leftLabel("Initial number:"), 0, 3, 1, 1); gtk_grid_attach(GTK_GRID(g), eInit, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(g), leftLabel("Increase by:"), 0, 4, 1, 1);    gtk_grid_attach(GTK_GRID(g), eInc, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(g), leftLabel("Repeat:"), 0, 5, 1, 1);         gtk_grid_attach(GTK_GRID(g), eRep, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(g), leftLabel("Leading:"), 0, 6, 1, 1);        gtk_grid_attach(GTK_GRID(g), lead, 1, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(g), leftLabel("Format:"), 0, 7, 1, 1);         gtk_grid_attach(GTK_GRID(g), fmt, 1, 7, 1, 1);
    gtk_box_pack_start(GTK_BOX(area), g, FALSE, FALSE, 0);
    gtk_widget_show_all(d);
    if (gtk_dialog_run(GTK_DIALOG(d)) != GTK_RESPONSE_OK) { gtk_widget_destroy(d); return; }

    bool text = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rText));
    std::string str = gtk_entry_get_text(GTK_ENTRY(eText));
    long long init = (long long)gtk_spin_button_get_value(GTK_SPIN_BUTTON(eInit));
    long long inc = (long long)gtk_spin_button_get_value(GTK_SPIN_BUTTON(eInc));
    int rep = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(eRep));
    int leading = gtk_combo_box_get_active(GTK_COMBO_BOX(lead));
    static const int bases[] = {10, 16, 8, 2};
    int base = bases[std::max(0, gtk_combo_box_get_active(GTK_COMBO_BOX(fmt)))];
    gtk_widget_destroy(d);

    // target lines: rectangular/multi selection or from caret line to the end
    std::vector<std::pair<sptr_t, sptr_t>> targets;   // (line, column)
    int nsel = int(ed.call(SCI_GETSELECTIONS));
    bool rect = ed.call(SCI_SELECTIONISRECTANGLE) || nsel > 1;
    if (rect) {
        for (int i = 0; i < nsel; ++i) {
            sptr_t p = std::min(ed.call(SCI_GETSELECTIONNSTART, uptr_t(i)), ed.call(SCI_GETSELECTIONNCARET, uptr_t(i)));
            sptr_t vs = ed.call(SCI_GETSELECTIONNSTARTVIRTUALSPACE, uptr_t(i));
            targets.emplace_back(ed.call(SCI_LINEFROMPOSITION, uptr_t(p)), ed.call(SCI_GETCOLUMN, uptr_t(p)) + vs);
        }
        std::sort(targets.begin(), targets.end());
    } else {
        sptr_t p = ed.call(SCI_GETCURRENTPOS);
        sptr_t col = ed.call(SCI_GETCOLUMN, uptr_t(p));
        for (sptr_t l = ed.call(SCI_LINEFROMPOSITION, uptr_t(p)); l < ed.call(SCI_GETLINECOUNT); ++l) targets.emplace_back(l, col);
    }
    auto fmtNum = [&](long long v) {
        std::string s; bool neg = v < 0; unsigned long long u = (unsigned long long)(neg ? -v : v);
        if (u == 0) s = "0";
        while (u) { int dgt = int(u % (unsigned)base); s.insert(s.begin(), char(dgt < 10 ? '0' + dgt : 'A' + dgt - 10)); u /= (unsigned)base; }
        if (neg) s.insert(s.begin(), '-');
        return s;
    };
    size_t width = 0;
    if (!text && leading) {
        long long last = init + inc * (long long)((targets.size() + size_t(rep) - 1) / size_t(rep));
        width = std::max(fmtNum(init).size(), fmtNum(last).size());
    }
    ed.call(SCI_BEGINUNDOACTION);
    for (size_t i = targets.size(); i-- > 0;) {
        auto [line, col] = targets[i];
        std::string ins = text ? str : fmtNum(init + inc * (long long)(i / size_t(rep)));
        if (!text && ins.size() < width) ins.insert(0, width - ins.size(), leading == 1 ? '0' : ' ');
        sptr_t lineEnd = ed.call(SCI_GETLINEENDPOSITION, uptr_t(line));
        sptr_t endCol = ed.call(SCI_GETCOLUMN, uptr_t(lineEnd));
        sptr_t pos;
        if (endCol < col) { std::string pad(size_t(col - endCol), ' '); ed.callStr(SCI_INSERTTEXT, uptr_t(lineEnd), pad.c_str()); pos = lineEnd + sptr_t(pad.size()); }
        else pos = ed.call(SCI_FINDCOLUMN, uptr_t(line), col);
        ed.callStr(SCI_INSERTTEXT, uptr_t(pos), ins.c_str());
    }
    ed.call(SCI_ENDUNDOACTION);
}

// ------------------------------------------------------------------ Preferences
struct PrefBinding { GtkWidget* w; std::string key; int kind; };   // kind 0 bool, 1 int, 2 string-combo-id

void preferencesDialog(MainWindow* mw)
{
    Settings& S = Settings::get();
    GtkWidget* d = makeDialog(mw->window(), "Preferences");
    gtk_window_set_default_size(GTK_WINDOW(d), 620, 460);
    gtk_dialog_add_buttons(GTK_DIALOG(d), "_Close", GTK_RESPONSE_CLOSE, nullptr);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(d));
    GtkWidget* stack = gtk_stack_new();
    GtkWidget* side = gtk_stack_sidebar_new();
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(side), GTK_STACK(stack));
    GtkWidget* hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(hb), side, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), stack, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(area), hb, TRUE, TRUE, 0);
    auto* bindings = new std::vector<PrefBinding>();

    auto page = [&](const char* name, const char* title) {
        GtkWidget* g = grid();
        gtk_container_set_border_width(GTK_CONTAINER(g), 8);
        gtk_stack_add_titled(GTK_STACK(stack), g, name, title);
        return g;
    };
    int row = 0;
    auto check = [&](GtkWidget* g, const char* label, const char* key, bool def) {
        GtkWidget* c = gtk_check_button_new_with_mnemonic(label);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c), S.getBool(key, def));
        gtk_grid_attach(GTK_GRID(g), c, 0, row++, 2, 1);
        bindings->push_back({c, key, 0});
    };
    auto spin = [&](GtkWidget* g, const char* label, const char* key, int def, int lo, int hi) {
        GtkWidget* s = gtk_spin_button_new_with_range(lo, hi, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(s), S.getInt(key, def));
        gtk_grid_attach(GTK_GRID(g), leftLabel(label), 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(g), s, 1, row++, 1, 1);
        bindings->push_back({s, key, 1});
    };
    auto combo = [&](GtkWidget* g, const char* label, const char* key, const char* def,
                     std::vector<std::pair<const char*, const char*>> opts) {
        GtkWidget* c = gtk_combo_box_text_new();
        for (auto& [id, text] : opts) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c), id, text);
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(c), S.getString(key, def).c_str());
        gtk_grid_attach(GTK_GRID(g), leftLabel(label), 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(g), c, 1, row++, 1, 1);
        bindings->push_back({c, key, 2});
    };

    GtkWidget* g = page("general", "General");
    row = 0;
    check(g, "Show _toolbar", "view.toolbar", true);
    check(g, "Show _status bar", "view.statusBar", true);
    combo(g, "Dark mode:", "ui.darkMode", "follow-theme", {{"follow-theme", "Follow editor theme"}, {"light", "Light"}, {"dark", "Dark"}});
    check(g, "Double click to close document", "tab.dblClickClose", false);

    g = page("editing", "Editing");
    row = 0;
    spin(g, "Tab size:", "editor.tabSize", 4, 1, 16);
    check(g, "Replace tabs by _spaces", "editor.useSpaces", false);
    check(g, "Auto-_indent", "editor.autoIndent", true);
    check(g, "Auto-close brackets and quotes", "editor.autoClose", false);
    check(g, "Backspace unindents", "editor.backspaceUnindents", false);
    check(g, "Caret blinks", "editor.caretBlink", true);
    spin(g, "Caret width:", "editor.caretWidth", 1, 1, 3);
    spin(g, "Vertical edge column (0 = off):", "view.edgeColumn", 0, 0, 500);
    check(g, "Smart highlighting", "smartHighlight", true);
    check(g, "Smart highlighting: match case", "smartHighlight.matchCase", false);
    check(g, "Smart highlighting: whole word only", "smartHighlight.wholeWord", true);

    g = page("newdoc", "New Document");
    row = 0;
    combo(g, "Format (line ending):", "newdoc.eol", "2", {{"0", "Windows (CR LF)"}, {"2", "Unix (LF)"}, {"1", "Macintosh (CR)"}});
    combo(g, "Encoding:", "newdoc.encoding", "utf8", {{"utf8", "UTF-8"}, {"utf8bom", "UTF-8 with BOM"}});
    {
        std::vector<std::pair<const char*, const char*>> langs;
        for (auto& l : allLangs()) if (l.langType != 15 && l.langType != 47) langs.emplace_back(l.name, l.shortName);
        combo(g, "Default language:", "newdoc.lang", "normal", langs);
    }
    {
        std::vector<std::pair<const char*, const char*>> cs;
        for (auto& c : ansiCharsets()) cs.emplace_back(c.iconvName, c.label);
        combo(g, "\"ANSI\" character set:", "encoding.ansiCharset", "WINDOWS-1252", cs);
    }

    g = page("autocomp", "Auto-Completion");
    row = 0;
    check(g, "Enable auto-completion on each input", "autocomplete.enabled", true);
    combo(g, "Source:", "autocomplete.source", "both", {{"func", "Function completion"}, {"word", "Word completion"}, {"both", "Function and word completion"}});
    spin(g, "From Nth character:", "autocomplete.threshold", 1, 1, 9);
    check(g, "Function parameters hint on input", "autocomplete.paramHint", true);

    g = page("backup", "Backup / Session");
    row = 0;
    check(g, "Remember current session for next launch", "session.remember", true);
    check(g, "Enable session snapshot (unsaved changes survive restarts)", "session.snapshot", true);
    check(g, "Update silently when a file changes on disk", "fileChange.silentUpdate", false);
    check(g, "Trim trailing whitespace on save", "save.trimTrailing", false);
    spin(g, "Max. number of recent files:", "recent.max", 15, 0, 50);

    g = page("plugins", "Plugins");
    row = 0;
    check(g, "Load plugins at startup", "plugins.enabled", true);
    GtkWidget* note = gtk_label_new(("Plugin folders:\n  " + paths::userPluginsDir() + "\n  " + paths::systemPluginsDir()).c_str());
    gtk_label_set_xalign(GTK_LABEL(note), 0);
    gtk_label_set_selectable(GTK_LABEL(note), TRUE);
    gtk_grid_attach(GTK_GRID(g), note, 0, row++, 2, 1);

    gtk_widget_show_all(d);
    gtk_dialog_run(GTK_DIALOG(d));
    for (auto& b : *bindings) {
        switch (b.kind) {
        case 0: S.set(b.key, bool(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b.w)))); break;
        case 1: S.set(b.key, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(b.w))); break;
        case 2: if (const char* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(b.w))) S.set(b.key, std::string(id)); break;
        }
    }
    delete bindings;
    gtk_widget_destroy(d);
    S.save();
    mw->editor().applyViewOptions();
    mw->refreshAllStyles();
    mw->runCommand(-1);   // -1 = resync menus/toolbar/status bar with the settings
}

// ------------------------------------------------------------------ Style configurator (theme + font)
void styleConfiguratorDialog(MainWindow* mw)
{
    Settings& S = Settings::get();
    Parameters& P = Parameters::get();
    GtkWidget* d = makeDialog(mw->window(), "Style Configurator");
    gtk_dialog_add_buttons(GTK_DIALOG(d), "_Cancel", GTK_RESPONSE_CANCEL, "_Save & Close", GTK_RESPONSE_OK, nullptr);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(d));
    GtkWidget* g = grid();
    GtkWidget* themeCombo = gtk_combo_box_text_new();
    auto themes = P.themes();
    int active = 0;
    for (size_t i = 0; i < themes.size(); ++i) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(themeCombo), themes[i].path.c_str(), themes[i].name.c_str());
        if (themes[i].path == P.currentThemePath()) active = int(i);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(themeCombo), active);
    GtkWidget* useFont = gtk_check_button_new_with_label("Override the theme's font:");
    GtkWidget* fontBtn = gtk_font_button_new();
    gtk_font_chooser_set_filter_func(GTK_FONT_CHOOSER(fontBtn), +[](const PangoFontFamily* f, const PangoFontFace*, gpointer) -> gboolean {
        return pango_font_family_is_monospace(const_cast<PangoFontFamily*>(f));
    }, nullptr, nullptr);
    std::string font = S.getString("editor.font", "");
    int size = S.getInt("editor.fontSize", 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(useFont), !font.empty());
    std::string desc = (font.empty() ? P.defaultFont() : font) + " " + std::to_string(size > 0 ? size : P.defaultFontSize());
    gtk_font_chooser_set_font(GTK_FONT_CHOOSER(fontBtn), desc.c_str());
    gtk_grid_attach(GTK_GRID(g), leftLabel("Select theme:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), themeCombo, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(g), useFont, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(g), fontBtn, 1, 1, 1, 1);
    GtkWidget* hint = gtk_label_new(nullptr);
    gtk_label_set_text(GTK_LABEL(hint), ("Themes are Notepad++-format stylers XML files.\nDrop your own into: " + paths::userThemesDir()).c_str());
    gtk_label_set_xalign(GTK_LABEL(hint), 0);
    gtk_label_set_selectable(GTK_LABEL(hint), TRUE);
    gtk_grid_attach(GTK_GRID(g), hint, 0, 2, 2, 1);
    gtk_box_pack_start(GTK_BOX(area), g, FALSE, FALSE, 0);
    // live preview of the theme
    g_signal_connect(themeCombo, "changed", CAM_CB(+[](GtkComboBox* c, gpointer self) {
        const char* id = gtk_combo_box_get_active_id(c);
        if (id && Parameters::get().loadTheme(id)) static_cast<MainWindow*>(self)->refreshAllStyles();
    }), mw);
    std::string oldTheme = P.currentThemePath();
    gtk_widget_show_all(d);
    if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
        if (const char* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(themeCombo))) S.set("theme", std::string(id));
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(useFont))) {
            PangoFontDescription* fd = gtk_font_chooser_get_font_desc(GTK_FONT_CHOOSER(fontBtn));
            if (fd) {
                S.set("editor.font", std::string(pango_font_description_get_family(fd)));
                S.set("editor.fontSize", int(pango_font_description_get_size(fd) / PANGO_SCALE));
                pango_font_description_free(fd);
            }
        } else { S.set("editor.font", std::string()); S.set("editor.fontSize", 0); }
        S.save();
    } else {
        P.loadTheme(oldTheme);
    }
    gtk_widget_destroy(d);
    mw->refreshAllStyles();
    mw->runCommand(-1);
}

// ------------------------------------------------------------------ Summary
void summaryDialog(MainWindow* mw)
{
    Editor& e = mw->editor();
    Buffer* b = mw->current();
    std::string text = e.text();
    long chars = long(g_utf8_strlen(text.c_str(), gssize(text.size())));
    long words = 0, nonBlank = 0;
    bool inWord = false;
    for (const char* p = text.c_str(); *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        bool w = g_unichar_isalnum(c) || c == '_';
        if (w && !inWord) ++words;
        inWord = w;
        if (!g_unichar_isspace(c)) ++nonBlank;
    }
    std::ostringstream ss;
    if (b && !b->path.empty()) {
        ss << "Full file path: " << b->path << "\n";
        GStatBuf st;
        if (g_stat(b->path.c_str(), &st) == 0) {
            char tbuf[64]; strftime(tbuf, sizeof tbuf, "%Y-%m-%d %H:%M:%S", localtime(&st.st_mtime));
            ss << "Modified: " << tbuf << "\n";
        }
    }
    ss << "\nCharacters (without line endings): " << chars << "\n"
       << "Non-blank characters: " << nonBlank << "\n"
       << "Words: " << words << "\n"
       << "Lines: " << e.call(SCI_GETLINECOUNT) << "\n"
       << "Document length (UTF-8 bytes): " << text.size() << "\n";
    sptr_t a = e.call(SCI_GETSELECTIONSTART), z = e.call(SCI_GETSELECTIONEND);
    if (z > a) ss << "Selected: " << e.call(SCI_COUNTCHARACTERS, uptr_t(a), z) << " characters\n";
    mw->showMessage(GTK_MESSAGE_INFO, "Summary", ss.str());
}

// ------------------------------------------------------------------ About / help
void aboutDialog(MainWindow* mw)
{
    GtkWidget* d = gtk_about_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(d), mw->window());
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(d), "Camelion++");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(d), CAMELION_VERSION_STRING);
    gtk_about_dialog_set_logo_icon_name(GTK_ABOUT_DIALOG(d), "camelion++");
    std::string comments = "A Notepad++-style source code editor for Linux.\n"
                           "Built on Scintilla " SCINTILLA_VERSION_TEXT " and Lexilla " LEXILLA_VERSION_TEXT " (GTK backend).\n"
                           "Reads Notepad++ langs.xml, stylers.xml, themes, auto-completion and function list files.";
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(d), comments.c_str());
    gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(d), GTK_LICENSE_GPL_3_0);
    const char* authors[] = {"Camelion++ contributors", "Based on the design of Notepad++ by Don Ho and contributors", nullptr};
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(d), authors);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

void cmdLineHelpDialog(MainWindow* mw)
{
    mw->showMessage(GTK_MESSAGE_INFO, "Command Line Arguments",
        "camelion++ [--help] [-multiInst] [-noPlugin] [-l<Language>] [-n<line>] [-c<column>] [-ro] "
        "[-nosession] [-notabbar] [filePath ...]\n\n"
        "-multiInst : start another instance instead of reusing the running one\n"
        "-noPlugin  : launch without loading any plugin\n"
        "-l         : open the files with the given language (e.g. -lpython)\n"
        "-n         : scroll to the given line\n"
        "-c         : scroll to the given column\n"
        "-ro        : open the files read-only\n"
        "-nosession : don't restore or save the session\n"
        "+N         : same as -nN (vim style)\n"
        "file:line  : also accepted, e.g. main.cpp:120");
}

void debugInfoDialog(MainWindow* mw)
{
    std::ostringstream ss;
    ss << "Camelion++ " << CAMELION_VERSION_STRING << " (" << CAMELION_ARCH << ")\n"
       << "Scintilla " << SCINTILLA_VERSION_TEXT << " / Lexilla " << LEXILLA_VERSION_TEXT << "\n"
       << "GTK " << gtk_get_major_version() << "." << gtk_get_minor_version() << "." << gtk_get_micro_version() << "\n"
       << "Data dir: " << paths::dataDir() << "\n"
       << "Config dir: " << paths::configDir() << "\n"
       << "Plugin dirs: " << paths::userPluginsDir() << " ; " << paths::systemPluginsDir() << "\n"
       << "Theme: " << Parameters::get().currentThemePath() << "\n"
       << "Plugins loaded: " << mw->plugins().count() << "\n";
    for (auto& n : mw->plugins().names()) ss << "    " << n << "\n";
    std::string s = ss.str();
    GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(cb, s.c_str(), -1);
    mw->showMessage(GTK_MESSAGE_INFO, "Debug Info (copied to clipboard)", s);
}

// ------------------------------------------------------------------ Macro / Run
bool runMacroMultipleDialog(MainWindow* mw, int& times, bool& untilEnd)
{
    GtkWidget* d = makeDialog(mw->window(), "Run a Macro Multiple Times");
    gtk_dialog_add_buttons(GTK_DIALOG(d), "_Cancel", GTK_RESPONSE_CANCEL, "_Run", GTK_RESPONSE_OK, nullptr);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(d));
    GtkWidget* rN = gtk_radio_button_new_with_label(nullptr, "Run");
    GtkWidget* spin = gtk_spin_button_new_with_range(1, 100000, 1);
    GtkWidget* rEnd = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(rN), "Run until the end of file");
    GtkWidget* hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(hb), rN, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hb), gtk_label_new("times"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(area), hb, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(area), rEnd, FALSE, FALSE, 0);
    gtk_widget_show_all(d);
    bool ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK;
    times = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
    untilEnd = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rEnd));
    gtk_widget_destroy(d);
    return ok;
}

bool runDialog(MainWindow* mw, std::string& command, std::string& saveName)
{
    GtkWidget* d = makeDialog(mw->window(), "Run...");
    gtk_dialog_add_buttons(GTK_DIALOG(d), "_Cancel", GTK_RESPONSE_CANCEL, "_Save...", 1, "_Run", GTK_RESPONSE_OK, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(d));
    gtk_box_pack_start(GTK_BOX(area), leftLabel("The program to run (shell command):"), FALSE, FALSE, 0);
    GtkWidget* e = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(e), 60);
    gtk_entry_set_text(GTK_ENTRY(e), Settings::get().getString("run.last", "xdg-open \"$(FULL_CURRENT_PATH)\"").c_str());
    gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
    gtk_box_pack_start(GTK_BOX(area), e, FALSE, FALSE, 0);
    GtkWidget* help = gtk_label_new("Variables: $(FULL_CURRENT_PATH) $(CURRENT_DIRECTORY) $(FILE_NAME) $(NAME_PART)\n"
                                    "$(EXT_PART) $(CURRENT_WORD) $(CURRENT_LINE) $(CURRENT_COLUMN) $(NPP_DIRECTORY)");
    gtk_label_set_xalign(GTK_LABEL(help), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(help), "dim-label");
    gtk_box_pack_start(GTK_BOX(area), help, FALSE, FALSE, 0);
    gtk_widget_show_all(d);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    command = gtk_entry_get_text(GTK_ENTRY(e));
    gtk_widget_destroy(d);
    if (r == 1) {
        bool ok = false;
        saveName = inputBox(mw->window(), "Save Run Command", "Name:", "", &ok);
        return ok && !saveName.empty();
    }
    saveName.clear();
    if (r == GTK_RESPONSE_OK) Settings::get().set("run.last", command);
    return r == GTK_RESPONSE_OK;
}

static std::string shellQuoteless(const std::string& s) { return s; }

std::string expandRunVariables(MainWindow* mw, const std::string& cmdIn)
{
    Buffer* b = mw->current();
    std::string full = b ? (b->path.empty() ? b->untitledName : b->path) : "";
    std::string dir = b && !b->path.empty() ? paths::dirName(b->path) : "";
    std::string file = b ? b->displayName() : "";
    std::string ext = paths::extension(file);
    std::string name = file.substr(0, file.rfind('.') == std::string::npos ? file.size() : file.rfind('.'));
    Editor& e = mw->editor();
    sptr_t pos = e.call(SCI_GETCURRENTPOS);
    std::string vars[][2] = {
        {"$(FULL_CURRENT_PATH)", full}, {"$(CURRENT_DIRECTORY)", dir}, {"$(FILE_NAME)", file},
        {"$(NAME_PART)", name}, {"$(EXT_PART)", ext.empty() ? "" : "." + ext}, {"$(CURRENT_WORD)", e.currentWord()},
        {"$(CURRENT_LINE)", std::to_string(e.call(SCI_LINEFROMPOSITION, uptr_t(pos)) + 1)},
        {"$(CURRENT_COLUMN)", std::to_string(e.call(SCI_GETCOLUMN, uptr_t(pos)) + 1)},
        {"$(NPP_DIRECTORY)", paths::dirName(paths::exePath())}, {"$(NPP_FULL_FILE_PATH)", paths::exePath()},
        {"$(CURRENT_LINESTR)", e.lineText(e.call(SCI_LINEFROMPOSITION, uptr_t(pos)))},
    };
    std::string s = cmdIn;
    for (auto& v : vars) {
        size_t p = 0;
        while ((p = s.find(v[0], p)) != std::string::npos) { s.replace(p, v[0].size(), shellQuoteless(v[1])); p += v[1].size(); }
    }
    return s;
}

} // namespace cam

// ------------------------------------------------------------------ Printing (GtkPrintOperation + SCI_FORMATRANGEFULL)
namespace cam {
struct PrintCtx {
    MainWindow* mw;
    std::vector<sptr_t> pageStarts;
    sptr_t length = 0;
    std::string title;
};

static Sci_RangeToFormatFull makeRange(GtkPrintContext* pc, sptr_t start, sptr_t end, double headerH)
{
    Sci_RangeToFormatFull fr{};
    cairo_t* cr = gtk_print_context_get_cairo_context(pc);
    fr.hdc = cr; fr.hdcTarget = cr;
    int w = int(gtk_print_context_get_width(pc)), h = int(gtk_print_context_get_height(pc));
    fr.rcPage = {0, 0, w, h};
    fr.rc = {0, int(headerH), w, h};
    fr.chrg.cpMin = start; fr.chrg.cpMax = end;
    return fr;
}

void printDocument(MainWindow* mw)
{
    Buffer* b = mw->current();
    if (!b) return;
    GtkPrintOperation* op = gtk_print_operation_new();
    gtk_print_operation_set_unit(op, GTK_UNIT_POINTS);
    gtk_print_operation_set_job_name(op, b->displayName().c_str());
    auto* ctx = new PrintCtx{mw, {}, mw->editor().call(SCI_GETLENGTH), b->path.empty() ? b->untitledName : b->path};
    mw->editor().call(SCI_SETPRINTCOLOURMODE, SC_PRINT_COLOURONWHITE);
    mw->editor().call(SCI_SETPRINTWRAPMODE, SC_WRAP_WORD);
    g_signal_connect(op, "begin-print", CAM_CB(+[](GtkPrintOperation* o, GtkPrintContext* pc, gpointer d) {
        auto* c = static_cast<PrintCtx*>(d);
        sptr_t pos = 0;
        c->pageStarts.clear();
        while (pos < c->length && c->pageStarts.size() < 100000) {
            c->pageStarts.push_back(pos);
            Sci_RangeToFormatFull fr = makeRange(pc, pos, c->length, 18);
            sptr_t next = c->mw->editor().call(SCI_FORMATRANGEFULL, 0, reinterpret_cast<sptr_t>(&fr));
            if (next <= pos) break;
            pos = next;
        }
        if (c->pageStarts.empty()) c->pageStarts.push_back(0);
        gtk_print_operation_set_n_pages(o, int(c->pageStarts.size()));
    }), ctx);
    g_signal_connect(op, "draw-page", CAM_CB(+[](GtkPrintOperation*, GtkPrintContext* pc, gint page, gpointer d) {
        auto* c = static_cast<PrintCtx*>(d);
        cairo_t* cr = gtk_print_context_get_cairo_context(pc);
        PangoLayout* lay = gtk_print_context_create_pango_layout(pc);
        PangoFontDescription* fd = pango_font_description_from_string("Sans 8");
        pango_layout_set_font_description(lay, fd);
        std::string hdr = c->title + "    —    page " + std::to_string(page + 1) + "/" + std::to_string(c->pageStarts.size());
        pango_layout_set_text(lay, hdr.c_str(), -1);
        cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
        cairo_move_to(cr, 0, 0);
        pango_cairo_show_layout(cr, lay);
        pango_font_description_free(fd);
        g_object_unref(lay);
        sptr_t start = c->pageStarts[size_t(page)];
        sptr_t end = size_t(page + 1) < c->pageStarts.size() ? c->pageStarts[size_t(page + 1)] : c->length;
        Sci_RangeToFormatFull fr = makeRange(pc, start, end, 18);
        c->mw->editor().call(SCI_FORMATRANGEFULL, 1, reinterpret_cast<sptr_t>(&fr));
    }), ctx);
    GError* err = nullptr;
    gtk_print_operation_run(op, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, mw->window(), &err);
    if (err) { mw->showMessage(GTK_MESSAGE_ERROR, "Printing failed", err->message); g_error_free(err); }
    mw->editor().call(SCI_FORMATRANGEFULL, 0, 0);
    g_object_unref(op);
    delete ctx;
}
} // namespace cam
