// Camelion++ - command registry, menus, toolbar
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
#include "MainWindow.h"
#include "Parameters.h"
#include "Settings.h"
#include "Paths.h"
#include "LangDefs.h"
#include "TextOps.h"
#include "Dialogs.h"
#include "FindReplaceDlg.h"
#include "PluginManager.h"
#include "PluginsAdmin.h"
#include "FunctionList.h"
#include "AutoComplete.h"
#include "Version.h"
#include <CamelionPluginInterface.h>
#include <ctime>
#include <cstring>
#include <sstream>
#include <algorithm>

namespace cam {

using namespace cmd;

void buildThemeMenu(MainWindow* mw);

// ------------------------------------------------------------------ registry
void MainWindow::registerCommand(int id, const std::string& label, std::function<void()> fn, bool checkable)
{
    Command c;
    c.label = label;
    c.fn = std::move(fn);
    c.checkable = checkable;
    _commands[id] = std::move(c);
}

void MainWindow::runCommand(int id)
{
    if (id == -1) {   // resync chrome with the settings
        Settings& S = Settings::get();
        gtk_widget_set_visible(_toolbar, S.getBool("view.toolbar", true));
        gtk_widget_set_visible(_statusBar, S.getBool("view.statusBar", true));
        gtk_notebook_set_show_tabs(GTK_NOTEBOOK(_notebook), S.getBool("view.tabBar", true));
        syncCheckItems();
        return;
    }
    if (id >= PLUGINS_CMD_FIRST && id <= PLUGINS_CMD_DYNAMIC_LAST) { _plugins->runCommand(id); return; }
    auto it = _commands.find(id);
    if (it != _commands.end() && it->second.fn) {
        it->second.fn();
        if (it->second.checkable) syncCheckItems();
    }
}

void MainWindow::setChecked(int id, bool on)
{
    auto it = _commands.find(id);
    if (it == _commands.end() || !it->second.item || !GTK_IS_CHECK_MENU_ITEM(it->second.item)) return;
    _syncingChecks = true;
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it->second.item), on);
    _syncingChecks = false;
}

GtkWidget* MainWindow::addMenuItem(GtkWidget* menu, int id, const char* accel)
{
    auto it = _commands.find(id);
    if (it == _commands.end()) return nullptr;
    GtkWidget* item = it->second.checkable ? gtk_check_menu_item_new_with_mnemonic(it->second.label.c_str())
                                           : gtk_menu_item_new_with_mnemonic(it->second.label.c_str());
    g_object_set_data(G_OBJECT(item), "cam-cmd", GINT_TO_POINTER(id));
    g_signal_connect(item, "activate", CAM_CB(+[](GtkWidget* w, gpointer self) {
        auto* mw = static_cast<MainWindow*>(self);
        if (mw->_syncingChecks) return;
        mw->runCommand(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cam-cmd")));
    }), this);
    if (accel) {
        // several accelerators can be given, separated by '|'
        std::string all = accel;
        size_t start = 0;
        while (start <= all.size()) {
            size_t bar = all.find('|', start);
            std::string one = all.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
            guint key = 0; GdkModifierType mods = GdkModifierType(0);
            gtk_accelerator_parse(one.c_str(), &key, &mods);
            if (key) gtk_widget_add_accelerator(item, "activate", _accel, key, mods, GTK_ACCEL_VISIBLE);
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    it->second.item = item;
    return item;
}

static GtkWidget* submenu(GtkWidget* parent, const char* label)
{
    GtkWidget* item = gtk_menu_item_new_with_mnemonic(label);
    GtkWidget* m = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), m);
    gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
    return m;
}
static void separator(GtkWidget* m) { gtk_menu_shell_append(GTK_MENU_SHELL(m), gtk_separator_menu_item_new()); }

// If a text entry (find box of a panel, ...) has the focus, clipboard commands go to it.
static GtkEditable* focusedEditable(GtkWidget* window)
{
    GtkWidget* f = gtk_window_get_focus(GTK_WINDOW(window));
    return (f && GTK_IS_EDITABLE(f)) ? GTK_EDITABLE(f) : nullptr;
}

static void copyToClipboard(const std::string& s)
{
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), s.c_str(), -1);
}

static std::string serializeMacro(const std::vector<MacroStep>& m)
{
    std::ostringstream ss;
    for (auto& s : m) {
        ss << s.msg << ',' << s.wParam << ',' << (s.hasText ? 0 : s.lParam) << ',' << (s.hasText ? 1 : 0) << ',';
        if (s.hasText) { gchar* b = g_base64_encode(reinterpret_cast<const guchar*>(s.text.data()), s.text.size()); ss << b; g_free(b); }
        ss << ';';
    }
    return ss.str();
}

static std::vector<MacroStep> parseMacro(const std::string& data)
{
    std::vector<MacroStep> v;
    std::istringstream ss(data);
    std::string item;
    while (std::getline(ss, item, ';')) {
        if (item.empty()) continue;
        MacroStep s{};
        std::istringstream is(item);
        std::string f[5];
        for (int i = 0; i < 5; ++i) std::getline(is, f[i], ',');
        try {
            s.msg = std::stoi(f[0]); s.wParam = uptr_t(std::stoull(f[1])); s.lParam = sptr_t(std::stoll(f[2])); s.hasText = f[3] == "1";
        } catch (...) { continue; }
        if (s.hasText) { gsize n = 0; guchar* d = g_base64_decode(f[4].c_str(), &n); s.text.assign(reinterpret_cast<char*>(d), n); g_free(d); }
        v.push_back(s);
    }
    return v;
}

static void playMacro(Editor& e, const std::vector<MacroStep>& m)
{
    for (auto& s : m) {
        if (s.hasText) e.call(unsigned(s.msg), s.msg == SCI_ADDTEXT || s.msg == SCI_APPENDTEXT ? s.text.size() : s.wParam, reinterpret_cast<sptr_t>(s.text.c_str()));
        else e.call(unsigned(s.msg), s.wParam, s.lParam);
    }
}

// ------------------------------------------------------------------ commands
void MainWindow::registerCommands()
{
    Settings& S = Settings::get();
    auto ed = [this]() -> Editor& { return *_editor; };
    auto langInfo = [this]() { return Parameters::get().lang(langByType(_current->langType)->name == std::string("javascript") ? "javascript.js" : langByType(_current->langType)->name); };
    auto toggleSetting = [this](const char* key, bool def) { Settings& s = Settings::get(); s.set(key, !s.getBool(key, def)); _editor->applyViewOptions(); };

    // ---------- File
    registerCommand(FILE_NEW, "_New", [this] { newDocument(); });
    registerCommand(FILE_OPEN, "_Open...", [this] {
        GtkWidget* dlg = gtk_file_chooser_dialog_new("Open", GTK_WINDOW(_window), GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg), TRUE);
        std::string dir = _current && !_current->path.empty() ? paths::dirName(_current->path) : Settings::get().getString("dialog.lastDir", "");
        if (!dir.empty()) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), dir.c_str());
        GtkFileFilter* all = gtk_file_filter_new(); gtk_file_filter_set_name(all, "All types (*)"); gtk_file_filter_add_pattern(all, "*");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), all);
        for (auto& l : allLangs()) {
            const LangInfo* li = Parameters::get().lang(l.name);
            if (!li || li->exts.empty()) continue;
            GtkFileFilter* f = gtk_file_filter_new();
            std::string name = std::string(l.longName) + " (";
            for (size_t i = 0; i < li->exts.size(); ++i) { gtk_file_filter_add_pattern(f, ("*." + li->exts[i]).c_str()); name += (i ? ";*." : "*.") + li->exts[i]; }
            gtk_file_filter_set_name(f, (name + ")").c_str());
            gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);
        }
        if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
            GSList* files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dlg));
            gtk_widget_destroy(dlg); dlg = nullptr;
            for (GSList* l = files; l; l = l->next) {
                std::string p = static_cast<char*>(l->data);
                Settings::get().set("dialog.lastDir", paths::dirName(p));
                openFile(p);
            }
            g_slist_free_full(files, g_free);
        }
        if (dlg) gtk_widget_destroy(dlg);
    });
    registerCommand(FILE_OPEN_FOLDER, "Open Containing _Folder", [this] {
        if (!_current || _current->isUntitled()) return;
        GFile* f = g_file_new_for_path(paths::dirName(_current->path).c_str());
        gchar* uri = g_file_get_uri(f);
        gtk_show_uri_on_window(GTK_WINDOW(_window), uri, GDK_CURRENT_TIME, nullptr);
        g_free(uri); g_object_unref(f);
    });
    registerCommand(FILE_OPEN_CMD, "Open in _Terminal", [this] {
        std::string dir = _current && !_current->path.empty() ? paths::dirName(_current->path) : g_get_home_dir();
        const char* term = g_getenv("TERMINAL");
        std::vector<std::string> cands;
        if (term) cands.push_back(term);
        for (auto t : {"x-terminal-emulator", "gnome-terminal", "konsole", "xfce4-terminal", "kitty", "alacritty", "xterm"}) cands.push_back(t);
        for (auto& t : cands) {
            gchar* exe = g_find_program_in_path(t.c_str());
            if (!exe) continue;
            gchar* argv[] = {exe, nullptr};
            g_spawn_async(dir.c_str(), argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr);
            g_free(exe);
            return;
        }
        showMessage(GTK_MESSAGE_WARNING, "No terminal emulator found.", "Set the TERMINAL environment variable.");
    });
    registerCommand(FILE_OPEN_DEFAULT_VIEWER, "Open in Default _Viewer", [this] {
        if (!_current || _current->isUntitled()) return;
        GFile* f = g_file_new_for_path(_current->path.c_str());
        gchar* uri = g_file_get_uri(f);
        gtk_show_uri_on_window(GTK_WINDOW(_window), uri, GDK_CURRENT_TIME, nullptr);
        g_free(uri); g_object_unref(f);
    });
    registerCommand(FILE_RELOAD, "Re_load from Disk", [this] { reloadBuffer(_current, true); });
    registerCommand(FILE_SAVE, "_Save", [this] { saveBuffer(_current); });
    registerCommand(FILE_SAVEAS, "Save _As...", [this] { std::string p = promptSaveAsPath(_current, "Save As"); if (!p.empty()) saveBufferAs(_current, p, false); });
    registerCommand(FILE_SAVECOPYAS, "Save a Cop_y As...", [this] { std::string p = promptSaveAsPath(_current, "Save a Copy As"); if (!p.empty()) saveBufferAs(_current, p, true); });
    registerCommand(FILE_SAVEALL, "Sav_e All", [this] { saveAll(); });
    registerCommand(FILE_RENAME, "Rena_me...", [this] {
        Buffer* b = _current;
        if (!b) return;
        bool ok = false;
        std::string n = inputBox(GTK_WINDOW(_window), "Rename", "New name:", b->displayName(), &ok);
        if (!ok || n.empty() || n.find('/') != std::string::npos) return;
        if (b->isUntitled()) { b->untitledName = n; updateTab(b); updateTitle(); return; }
        std::string np = paths::join(paths::dirName(b->path), n);
        if (paths::fileExists(np)) { showMessage(GTK_MESSAGE_ERROR, "A file named \"" + n + "\" already exists."); return; }
        if (::rename(b->path.c_str(), np.c_str()) != 0) { showMessage(GTK_MESSAGE_ERROR, "Rename failed", g_strerror(errno)); return; }
        b->path = np;
        if (!b->langLocked) setLanguage(b, Parameters::get().langTypeFromFileName(np));
        updateTab(b); updateTitle(); rebuildWindowMenu();
    });
    registerCommand(FILE_DELETE, "Move to Tras_h", [this] {
        Buffer* b = _current;
        if (!b || b->isUntitled()) return;
        if (askQuestion("Move \"" + b->path + "\" to the trash?", "", {{"_Cancel", 0}, {"_Move to Trash", 1}}) != 1) return;
        GFile* f = g_file_new_for_path(b->path.c_str());
        GError* err = nullptr;
        if (g_file_trash(f, nullptr, &err)) closeBuffer(b, false);
        else { showMessage(GTK_MESSAGE_ERROR, "Could not move the file to the trash.", err ? err->message : ""); if (err) g_error_free(err); }
        g_object_unref(f);
    });
    registerCommand(FILE_CLOSE, "_Close", [this] { closeBuffer(_current); });
    registerCommand(FILE_CLOSEALL, "Clo_se All", [this] { closeAll(); });
    registerCommand(FILE_CLOSEALL_BUT_CURRENT, "Close All BUT Current Document", [this] { closeAll(_current); });
    registerCommand(FILE_CLOSEALL_TOLEFT, "Close All to the Left", [this] {
        std::vector<Buffer*> v; for (auto& b : _buffers) { if (b.get() == _current) break; v.push_back(b.get()); }
        for (Buffer* b : v) if (!closeBuffer(b)) break;
    });
    registerCommand(FILE_CLOSEALL_TORIGHT, "Close All to the Right", [this] {
        std::vector<Buffer*> v; bool after = false;
        for (auto& b : _buffers) { if (after) v.push_back(b.get()); if (b.get() == _current) after = true; }
        for (Buffer* b : v) if (!closeBuffer(b)) break;
    });
    registerCommand(FILE_CLOSEALL_UNCHANGED, "Close All Unchanged", [this] {
        std::vector<Buffer*> v; for (auto& b : _buffers) if (!b->dirty) v.push_back(b.get());
        for (Buffer* b : v) closeBuffer(b);
    });
    registerCommand(FILE_LOADSESSION, "Load Session...", [this] {
        GtkWidget* dlg = gtk_file_chooser_dialog_new("Load Session", GTK_WINDOW(_window), GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
        if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
            gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
            std::string content = paths::readFile(f);
            g_free(f);
            // session files are lists of <File filename="..."/> (Camelion++ and Notepad++ formats)
            size_t p = 0;
            while ((p = content.find("filename=\"", p)) != std::string::npos) {
                p += 10; size_t q = content.find('"', p);
                if (q == std::string::npos) break;
                std::string path = content.substr(p, q - p);
                size_t amp;
                while ((amp = path.find("&amp;")) != std::string::npos) path.replace(amp, 5, "&");
                openFile(path, true, true);
                p = q;
            }
        }
        gtk_widget_destroy(dlg);
    });
    registerCommand(FILE_SAVESESSION, "Save Session...", [this] {
        GtkWidget* dlg = gtk_file_chooser_dialog_new("Save Session", GTK_WINDOW(_window), GTK_FILE_CHOOSER_ACTION_SAVE,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "session.xml");
        if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
            gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
            std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<NotepadPlus>\n    <Session activeView=\"0\">\n        <mainView activeIndex=\"" +
                              std::to_string(std::max(0, indexOf(_current))) + "\">\n";
            for (auto& b : _buffers) if (!b->path.empty()) {
                gchar* esc = g_markup_escape_text(b->path.c_str(), -1);
                xml += std::string("            <File lang=\"") + langByType(b->langType)->shortName + "\" filename=\"" + esc + "\" />\n";
                g_free(esc);
            }
            xml += "        </mainView>\n        <subView activeIndex=\"0\" />\n    </Session>\n</NotepadPlus>\n";
            paths::writeFileAtomic(f, xml);
            g_free(f);
        }
        gtk_widget_destroy(dlg);
    });
    registerCommand(FILE_PRINT, "_Print...", [this] { printDocument(this); });
    registerCommand(FILE_RESTORELASTCLOSEDFILE, "Restore Recent Closed File", [this] {
        while (!_closedFiles.empty()) { std::string p = _closedFiles.back(); _closedFiles.pop_back(); if (openFile(p, true, true)) break; }
    });
    registerCommand(FILE_OPEN_ALL_RECENT, "Open All Recent Files", [this] {
        auto r = Settings::get().recentFiles(); for (auto& p : r) openFile(p, true, true);
    });
    registerCommand(FILE_EMPTY_RECENT, "Empty Recent Files List", [this] { Settings::get().recentFiles().clear(); rebuildRecentMenu(); });
    registerCommand(FILE_EXIT, "E_xit", [this] { quit(); });

    // ---------- Edit
    registerCommand(EDIT_UNDO, "_Undo", [this, ed] { ed().call(SCI_UNDO); });
    registerCommand(EDIT_REDO, "_Redo", [this, ed] { ed().call(SCI_REDO); });
    registerCommand(EDIT_CUT, "Cu_t", [this, ed] { if (auto f = focusedEditable(_window)) gtk_editable_cut_clipboard(f); else ed().call(SCI_CUT); });
    registerCommand(EDIT_COPY, "_Copy", [this, ed] { if (auto f = focusedEditable(_window)) gtk_editable_copy_clipboard(f); else ed().call(SCI_COPY); });
    registerCommand(EDIT_PASTE, "_Paste", [this, ed] { if (auto f = focusedEditable(_window)) gtk_editable_paste_clipboard(f); else ed().call(SCI_PASTE); });
    registerCommand(EDIT_DELETE, "_Delete", [this, ed] { if (auto f = focusedEditable(_window)) gtk_editable_delete_selection(f); else ed().call(SCI_CLEAR); });
    registerCommand(EDIT_SELECTALL, "Select A_ll", [this, ed] { if (auto f = focusedEditable(_window)) gtk_editable_select_region(f, 0, -1); else ed().call(SCI_SELECTALL); });
    registerCommand(EDIT_FULLPATHTOCLIP, "Copy Current Full File _Path", [this] { if (_current) copyToClipboard(_current->path.empty() ? _current->untitledName : _current->path); });
    registerCommand(EDIT_FILENAMETOCLIP, "Copy Current _Filename", [this] { if (_current) copyToClipboard(_current->displayName()); });
    registerCommand(EDIT_CURRENTDIRTOCLIP, "Copy Current _Dir. Path", [this] { if (_current && !_current->path.empty()) copyToClipboard(paths::dirName(_current->path)); });
    registerCommand(EDIT_INS_TAB, "Increase Line Indent", [this, ed] { ed().call(SCI_TAB); });
    registerCommand(EDIT_RMV_TAB, "Decrease Line Indent", [this, ed] { ed().call(SCI_BACKTAB); });
    registerCommand(EDIT_UPPERCASE, "_UPPERCASE", [this, ed] { changeCase(ed(), CaseMode::Upper); });
    registerCommand(EDIT_LOWERCASE, "_lowercase", [this, ed] { changeCase(ed(), CaseMode::Lower); });
    registerCommand(EDIT_PROPERCASE_FORCE, "_Proper Case", [this, ed] { changeCase(ed(), CaseMode::Proper); });
    registerCommand(EDIT_SENTENCECASE_FORCE, "_Sentence case", [this, ed] { changeCase(ed(), CaseMode::Sentence); });
    registerCommand(EDIT_INVERTCASE, "_iNVERT cASE", [this, ed] { changeCase(ed(), CaseMode::Invert); });
    registerCommand(EDIT_RANDOMCASE, "ran_Dom CasE", [this, ed] { changeCase(ed(), CaseMode::Random); });
    registerCommand(EDIT_DUP_LINE, "_Duplicate Current Line", [this, ed] { ed().call(SCI_SELECTIONDUPLICATE); });
    registerCommand(EDIT_REMOVE_ANY_DUP_LINES, "Remove Duplicate Lines", [this, ed] { removeDuplicateLines(ed(), false); });
    registerCommand(EDIT_REMOVE_CONSECUTIVE_DUP_LINES, "Remove Consecutive Duplicate Lines", [this, ed] { removeDuplicateLines(ed(), true); });
    registerCommand(EDIT_SPLIT_LINES, "Split Lines", [this, ed] { splitLines(ed()); });
    registerCommand(EDIT_JOIN_LINES, "_Join Lines", [this, ed] { joinLines(ed()); });
    registerCommand(EDIT_LINE_UP, "Move Up Current Line", [this, ed] { ed().call(SCI_MOVESELECTEDLINESUP); });
    registerCommand(EDIT_LINE_DOWN, "Move Down Current Line", [this, ed] { ed().call(SCI_MOVESELECTEDLINESDOWN); });
    registerCommand(EDIT_TRANSPOSE_LINE, "Transpose Line", [this, ed] { ed().call(SCI_LINETRANSPOSE); });
    registerCommand(EDIT_REMOVEEMPTYLINES, "Remove Empty Lines", [this, ed] { removeEmptyLines(ed(), false); });
    registerCommand(EDIT_REMOVEEMPTYLINESWITHBLANK, "Remove Empty Lines (Containing Blank characters)", [this, ed] { removeEmptyLines(ed(), true); });
    registerCommand(EDIT_BLANKLINEABOVECURRENT, "Insert Blank Line Above Current", [this, ed] { insertBlankLine(ed(), true); });
    registerCommand(EDIT_BLANKLINEBELOWCURRENT, "Insert Blank Line Below Current", [this, ed] { insertBlankLine(ed(), false); });
    registerCommand(EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING, "Sort Lines Lexicographically Ascending", [this, ed] { sortLines(ed(), SortMode::LexAsc); });
    registerCommand(EDIT_SORTLINES_LEXICOGRAPHIC_DESCENDING, "Sort Lines Lexicographically Descending", [this, ed] { sortLines(ed(), SortMode::LexDesc); });
    registerCommand(EDIT_SORTLINES_LEXICO_CASE_INSENS_ASCENDING, "Sort Lines Lex. Ascending Ignoring Case", [this, ed] { sortLines(ed(), SortMode::LexAscCI); });
    registerCommand(EDIT_SORTLINES_LEXICO_CASE_INSENS_DESCENDING, "Sort Lines Lex. Descending Ignoring Case", [this, ed] { sortLines(ed(), SortMode::LexDescCI); });
    registerCommand(EDIT_SORTLINES_INTEGER_ASCENDING, "Sort Lines As Integers Ascending", [this, ed] { sortLines(ed(), SortMode::IntAsc); });
    registerCommand(EDIT_SORTLINES_INTEGER_DESCENDING, "Sort Lines As Integers Descending", [this, ed] { sortLines(ed(), SortMode::IntDesc); });
    registerCommand(EDIT_SORTLINES_REVERSE_ORDER, "Reverse Line Order", [this, ed] { sortLines(ed(), SortMode::Reverse); });
    registerCommand(EDIT_SORTLINES_RANDOMLY, "Randomize Line Order", [this, ed] { sortLines(ed(), SortMode::Random); });
    registerCommand(EDIT_TOGGLE_COMMENT, "Toggle Single Line Comment", [this, ed, langInfo] { toggleLineComment(ed(), langInfo()); });
    registerCommand(EDIT_BLOCK_COMMENT_SET, "Single Line Comment", [this, ed, langInfo] { lineComment(ed(), langInfo(), true); });
    registerCommand(EDIT_BLOCK_UNCOMMENT, "Single Line Uncomment", [this, ed, langInfo] { lineComment(ed(), langInfo(), false); });
    registerCommand(EDIT_STREAM_COMMENT, "Block Comment", [this, ed, langInfo] { streamComment(ed(), langInfo(), true); });
    registerCommand(EDIT_STREAM_UNCOMMENT, "Block Uncomment", [this, ed, langInfo] { streamComment(ed(), langInfo(), false); });
    registerCommand(EDIT_AUTOCOMPLETE, "Function Completion", [this, ed] { _autoComplete->showFunctionCompletion(ed()); });
    registerCommand(EDIT_AUTOCOMPLETE_CURRENTFILE, "Word Completion", [this, ed] { _autoComplete->showWordCompletion(ed()); });
    registerCommand(EDIT_FUNCCALLTIP, "Function Parameters Hint", [this, ed] { _autoComplete->showCallTip(ed()); });
    registerCommand(EDIT_AUTOCOMPLETE_PATH, "Path Completion", [this, ed] { _autoComplete->showPathCompletion(ed()); });
    registerCommand(FORMAT_TODOS, "_Windows (CR LF)", [this] { setEol(_current, EolType::CRLF); }, true);
    registerCommand(FORMAT_TOUNIX, "_Unix (LF)", [this] { setEol(_current, EolType::LF); }, true);
    registerCommand(FORMAT_TOMAC, "_Macintosh (CR)", [this] { setEol(_current, EolType::CR); }, true);
    registerCommand(EDIT_TRIMTRAILING, "Trim Trailing Space", [this, ed] { blankOperation(ed(), BlankOp::TrimTrailing); });
    registerCommand(EDIT_TRIMLINEHEAD, "Trim Leading Space", [this, ed] { blankOperation(ed(), BlankOp::TrimLeading); });
    registerCommand(EDIT_TRIM_BOTH, "Trim Leading and Trailing Space", [this, ed] { blankOperation(ed(), BlankOp::TrimBoth); });
    registerCommand(EDIT_EOL2WS, "EOL to Space", [this, ed] { blankOperation(ed(), BlankOp::EolToSpace); });
    registerCommand(EDIT_TRIMALL, "Trim both and EOL to Space", [this, ed] { blankOperation(ed(), BlankOp::TrimAll); });
    registerCommand(EDIT_TAB2SW, "TAB to Space", [this, ed] { blankOperation(ed(), BlankOp::TabToSpace); });
    registerCommand(EDIT_SW2TAB_LEADING, "Space to TAB (Leading)", [this, ed] { blankOperation(ed(), BlankOp::SpaceToTabLeading); });
    registerCommand(EDIT_SW2TAB_ALL, "Space to TAB (All)", [this, ed] { blankOperation(ed(), BlankOp::SpaceToTabAll); });
    auto insertDate = [this](const char* fmtKey, const char* def) {
        char buf[256]; time_t t = time(nullptr);
        strftime(buf, sizeof buf, Settings::get().getString(fmtKey, def).c_str(), localtime(&t));
        _editor->callStr(SCI_REPLACESEL, 0, buf);
    };
    registerCommand(EDIT_INSERT_DATETIME_SHORT, "Date Time (short)", [insertDate] { insertDate("datetime.short", "%H:%M %d/%m/%Y"); });
    registerCommand(EDIT_INSERT_DATETIME_LONG, "Date Time (long)", [insertDate] { insertDate("datetime.long", "%H:%M %A, %d %B %Y"); });
    registerCommand(EDIT_INSERT_DATETIME_CUSTOMIZED, "Date Time (customized)...", [this, insertDate] {
        bool ok = false;
        std::string f = inputBox(GTK_WINDOW(_window), "Date Time (customized)", "strftime format:", Settings::get().getString("datetime.custom", "%Y-%m-%d %H:%M:%S"), &ok);
        if (ok && !f.empty()) { Settings::get().set("datetime.custom", f); insertDate("datetime.custom", f.c_str()); }
    });
    registerCommand(EDIT_COLUMN_EDITOR, "Colu_mn Editor...", [this] { columnEditorDialog(this); });
    registerCommand(EDIT_COLUMNMODETIP, "Column Mode...", [this] {
        showMessage(GTK_MESSAGE_INFO, "Column Mode", "There are 2 ways to switch to column-select mode:\n\n"
                    "1. (Keyboard and Mouse) Hold Alt while left-click dragging\n\n"
                    "2. (Keyboard only) Hold Alt+Shift while using arrow keys");
    });
    registerCommand(EDIT_SETREADONLY, "Read-_Only", [this] {
        if (!_current) return;
        _current->userReadOnly = !_current->userReadOnly;
        _editor->call(SCI_SETREADONLY, _current->userReadOnly || _current->readOnly);
        updateTab(_current);
        notifyPlugins(NPPN_READONLYCHANGED, uptr_t(_current->id));
    }, true);

    // ---------- Search
    registerCommand(SEARCH_FIND, "_Find...", [this] { findDlg().show(FindReplaceDlg::TabFind); });
    registerCommand(SEARCH_REPLACE, "_Replace...", [this] { findDlg().show(FindReplaceDlg::TabReplace); });
    registerCommand(SEARCH_FINDINFILES, "Find in Fi_les...", [this] { findDlg().show(FindReplaceDlg::TabFiles); });
    registerCommand(SEARCH_MARK, "_Mark...", [this] { findDlg().show(FindReplaceDlg::TabMark); });
    registerCommand(SEARCH_FINDNEXT, "Find _Next", [this] { findDlg().findNext(true); });
    registerCommand(SEARCH_FINDPREV, "Find _Previous", [this] { findDlg().findNext(false); });
    registerCommand(SEARCH_SETANDFINDNEXT, "Select and Find Next", [this] { findDlg().selectAndFind(true, false); });
    registerCommand(SEARCH_SETANDFINDPREV, "Select and Find Previous", [this] { findDlg().selectAndFind(false, false); });
    registerCommand(SEARCH_VOLATILE_FINDNEXT, "Find (Volatile) Next", [this] { findDlg().selectAndFind(true, true); });
    registerCommand(SEARCH_VOLATILE_FINDPREV, "Find (Volatile) Previous", [this] { findDlg().selectAndFind(false, true); });
    registerCommand(VIEW_SEARCHRESULTS, "Search Results Window", [this] { toggleSearchResults(!gtk_widget_get_visible(_resultsBox)); });
    registerCommand(SEARCH_GOTONEXTFOUND, "Next Search Result", [this] { findDlg().gotoResult(true); });
    registerCommand(SEARCH_GOTOPREVFOUND, "Previous Search Result", [this] { findDlg().gotoResult(false); });
    registerCommand(SEARCH_GOTOLINE, "_Go to...", [this] { goToDialog(this); });
    registerCommand(SEARCH_GOTOMATCHINGBRACE, "Go to Matching _Brace", [this, ed] {
        Editor& e = ed();
        sptr_t pos = e.call(SCI_GETCURRENTPOS);
        sptr_t m = e.call(SCI_BRACEMATCH, uptr_t(pos), 0);
        if (m < 0 && pos > 0) m = e.call(SCI_BRACEMATCH, uptr_t(pos - 1), 0);
        if (m >= 0) e.call(SCI_GOTOPOS, uptr_t(m));
    });
    registerCommand(SEARCH_SELECTMATCHINGBRACES, "Select All Between Matching Braces", [this, ed] {
        Editor& e = ed();
        sptr_t pos = e.call(SCI_GETCURRENTPOS);
        sptr_t m = e.call(SCI_BRACEMATCH, uptr_t(pos), 0);
        sptr_t p = pos;
        if (m < 0 && pos > 0) { p = pos - 1; m = e.call(SCI_BRACEMATCH, uptr_t(p), 0); }
        if (m >= 0) e.call(SCI_SETSEL, uptr_t(std::min(p, m)), std::max(p, m) + 1);
    });
    for (int i = 0; i < 5; ++i) {
        int indic = 25 - i;
        registerCommand(SEARCH_MARKALLEXT1 + 2 * i, std::string("Using ") + "12345"[i] + (i == 0 ? "st" : i == 1 ? "nd" : i == 2 ? "rd" : "th") + " Style",
            [this, indic] { findDlg().styleToken(indic); });
        registerCommand(SEARCH_UNMARKALLEXT1 + 2 * i, std::string("Clear ") + "12345"[i] + (i == 0 ? "st" : i == 1 ? "nd" : i == 2 ? "rd" : "th") + " Style",
            [this, ed, indic] { ed().call(SCI_SETINDICATORCURRENT, uptr_t(indic)); ed().call(SCI_INDICATORCLEARRANGE, 0, ed().call(SCI_GETLENGTH)); });
    }
    registerCommand(SEARCH_CLEARALLMARKS, "Clear all Styles", [this, ed] {
        for (int i : {21, 22, 23, 24, 25, INDIC_FOUND}) { ed().call(SCI_SETINDICATORCURRENT, uptr_t(i)); ed().call(SCI_INDICATORCLEARRANGE, 0, ed().call(SCI_GETLENGTH)); }
    });
    registerCommand(SEARCH_TOGGLE_BOOKMARK, "Toggle Bookmark", [this, ed] {
        sptr_t line = ed().call(SCI_LINEFROMPOSITION, uptr_t(ed().call(SCI_GETCURRENTPOS)));
        if (ed().call(SCI_MARKERGET, uptr_t(line)) & (1 << MARK_BOOKMARK)) ed().call(SCI_MARKERDELETE, uptr_t(line), MARK_BOOKMARK);
        else ed().call(SCI_MARKERADD, uptr_t(line), MARK_BOOKMARK);
    });
    auto gotoBookmark = [this](bool next) {
        Editor& e = *_editor;
        sptr_t line = e.call(SCI_LINEFROMPOSITION, uptr_t(e.call(SCI_GETCURRENTPOS)));
        sptr_t found = next ? e.call(SCI_MARKERNEXT, uptr_t(line + 1), 1 << MARK_BOOKMARK) : e.call(SCI_MARKERPREVIOUS, uptr_t(line - 1), 1 << MARK_BOOKMARK);
        if (found < 0) found = next ? e.call(SCI_MARKERNEXT, 0, 1 << MARK_BOOKMARK) : e.call(SCI_MARKERPREVIOUS, uptr_t(e.call(SCI_GETLINECOUNT)), 1 << MARK_BOOKMARK);
        if (found >= 0) { e.call(SCI_ENSUREVISIBLE, uptr_t(found)); e.call(SCI_GOTOLINE, uptr_t(found)); }
    };
    registerCommand(SEARCH_NEXT_BOOKMARK, "Next Bookmark", [gotoBookmark] { gotoBookmark(true); });
    registerCommand(SEARCH_PREV_BOOKMARK, "Previous Bookmark", [gotoBookmark] { gotoBookmark(false); });
    registerCommand(SEARCH_CLEAR_BOOKMARKS, "Clear All Bookmarks", [this, ed] { ed().call(SCI_MARKERDELETEALL, MARK_BOOKMARK); });
    auto bookmarkedLines = [this](bool marked, auto fn) {
        Editor& e = *_editor;
        e.call(SCI_BEGINUNDOACTION);
        for (sptr_t l = e.call(SCI_GETLINECOUNT) - 1; l >= 0; --l) {
            bool m = (e.call(SCI_MARKERGET, uptr_t(l)) & (1 << MARK_BOOKMARK)) != 0;
            if (m == marked) fn(e, l);
        }
        e.call(SCI_ENDUNDOACTION);
    };
    auto copyMarked = [this]() {
        Editor& e = *_editor; std::string out;
        for (sptr_t l = 0; l < e.call(SCI_GETLINECOUNT); ++l)
            if (e.call(SCI_MARKERGET, uptr_t(l)) & (1 << MARK_BOOKMARK))
                out += e.textRange(e.call(SCI_POSITIONFROMLINE, uptr_t(l)), e.call(SCI_POSITIONFROMLINE, uptr_t(l + 1)) > 0 ? e.call(SCI_POSITIONFROMLINE, uptr_t(l + 1)) : e.call(SCI_GETLENGTH));
        copyToClipboard(out);
    };
    auto deleteLine = [](Editor& e, sptr_t l) {
        sptr_t a = e.call(SCI_POSITIONFROMLINE, uptr_t(l));
        sptr_t b = l + 1 < e.call(SCI_GETLINECOUNT) ? e.call(SCI_POSITIONFROMLINE, uptr_t(l + 1)) : e.call(SCI_GETLENGTH);
        e.call(SCI_DELETERANGE, uptr_t(a), b - a);
    };
    registerCommand(SEARCH_COPYMARKEDLINES, "Copy Bookmarked Lines", copyMarked);
    registerCommand(SEARCH_CUTMARKEDLINES, "Cut Bookmarked Lines", [copyMarked, bookmarkedLines, deleteLine] { copyMarked(); bookmarkedLines(true, deleteLine); });
    registerCommand(SEARCH_DELETEMARKEDLINES, "Remove Bookmarked Lines", [bookmarkedLines, deleteLine] { bookmarkedLines(true, deleteLine); });
    registerCommand(SEARCH_DELETEUNMARKEDLINES, "Remove Non-Bookmarked Lines", [bookmarkedLines, deleteLine] { bookmarkedLines(false, deleteLine); });
    registerCommand(SEARCH_PASTEMARKEDLINES, "Paste to (Replace) Bookmarked Lines", [this] {
        gchar* t = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
        if (!t) return;
        std::vector<std::string> lines; std::istringstream ss(t); std::string l;
        while (std::getline(ss, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); lines.push_back(l); }
        g_free(t);
        Editor& e = *_editor; size_t k = 0;
        e.call(SCI_BEGINUNDOACTION);
        for (sptr_t ln = 0; ln < e.call(SCI_GETLINECOUNT) && k < lines.size(); ++ln)
            if (e.call(SCI_MARKERGET, uptr_t(ln)) & (1 << MARK_BOOKMARK)) {
                e.call(SCI_SETTARGETRANGE, uptr_t(e.call(SCI_POSITIONFROMLINE, uptr_t(ln))), e.call(SCI_GETLINEENDPOSITION, uptr_t(ln)));
                e.call(SCI_REPLACETARGET, lines[k].size(), reinterpret_cast<sptr_t>(lines[k].c_str())); ++k;
            }
        e.call(SCI_ENDUNDOACTION);
    });
    registerCommand(SEARCH_INVERSEMARKS, "Inverse Bookmark", [this] {
        Editor& e = *_editor;
        for (sptr_t l = 0; l < e.call(SCI_GETLINECOUNT); ++l) {
            if (e.call(SCI_MARKERGET, uptr_t(l)) & (1 << MARK_BOOKMARK)) e.call(SCI_MARKERDELETE, uptr_t(l), MARK_BOOKMARK);
            else e.call(SCI_MARKERADD, uptr_t(l), MARK_BOOKMARK);
        }
    });

    // ---------- View
    registerCommand(VIEW_ALWAYSONTOP, "Always on Top", [this] {
        bool on = !Settings::get().getBool("view.alwaysOnTop", false);
        Settings::get().set("view.alwaysOnTop", on);
        gtk_window_set_keep_above(GTK_WINDOW(_window), on);
    }, true);
    registerCommand(VIEW_FULLSCREENTOGGLE, "Toggle Full Screen Mode", [this] {
        GdkWindow* w = gtk_widget_get_window(_window);
        if (w && (gdk_window_get_state(w) & GDK_WINDOW_STATE_FULLSCREEN)) gtk_window_unfullscreen(GTK_WINDOW(_window));
        else gtk_window_fullscreen(GTK_WINDOW(_window));
    });
    registerCommand(VIEW_TAB_SPACE, "Show Space and TAB", [toggleSetting] { toggleSetting("view.whitespace", false); }, true);
    registerCommand(VIEW_EOL, "Show End of Line", [toggleSetting] { toggleSetting("view.eol", false); }, true);
    registerCommand(VIEW_ALL_CHARACTERS, "Show All Characters", [toggleSetting] { toggleSetting("view.allChars", false); }, true);
    registerCommand(VIEW_INDENT_GUIDE, "Show Indent Guide", [toggleSetting] { toggleSetting("view.indentGuides", true); }, true);
    registerCommand(VIEW_WRAP_SYMBOL, "Show Wrap Symbol", [this] {
        bool on = !Settings::get().getBool("view.wrapSymbol", false);
        Settings::get().set("view.wrapSymbol", on);
        _editor->call(SCI_SETWRAPVISUALFLAGS, on ? SC_WRAPVISUALFLAG_END | SC_WRAPVISUALFLAG_START : SC_WRAPVISUALFLAG_NONE);
    }, true);
    registerCommand(VIEW_WRAP, "_Word wrap", [toggleSetting] { toggleSetting("view.wrap", false); }, true);
    registerCommand(VIEW_ZOOMIN, "Zoom _In", [this, ed] { ed().call(SCI_ZOOMIN); });
    registerCommand(VIEW_ZOOMOUT, "Zoom _Out", [this, ed] { ed().call(SCI_ZOOMOUT); });
    registerCommand(VIEW_ZOOMRESTORE, "Restore Default Zoom", [this, ed] { ed().call(SCI_SETZOOM, 0); });
    registerCommand(VIEW_FOLDALL, "Fold All", [this, ed] { ed().call(SCI_FOLDALL, SC_FOLDACTION_CONTRACT); });
    registerCommand(VIEW_UNFOLDALL, "Unfold All", [this, ed] { ed().call(SCI_FOLDALL, SC_FOLDACTION_EXPAND); });
    auto foldCurrent = [this](bool fold) {
        Editor& e = *_editor;
        sptr_t line = e.call(SCI_LINEFROMPOSITION, uptr_t(e.call(SCI_GETCURRENTPOS)));
        if (!(e.call(SCI_GETFOLDLEVEL, uptr_t(line)) & SC_FOLDLEVELHEADERFLAG)) line = e.call(SCI_GETFOLDPARENT, uptr_t(line));
        if (line >= 0) e.call(SCI_FOLDLINE, uptr_t(line), fold ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND);
    };
    registerCommand(VIEW_FOLD_CURRENT, "Collapse Current Level", [foldCurrent] { foldCurrent(true); });
    registerCommand(VIEW_UNFOLD_CURRENT, "Uncollapse Current Level", [foldCurrent] { foldCurrent(false); });
    for (int lvl = 0; lvl < 8; ++lvl) {
        auto foldLevel = [this, lvl](bool fold) {
            Editor& e = *_editor;
            for (sptr_t l = 0; l < e.call(SCI_GETLINECOUNT); ++l) {
                int fl = int(e.call(SCI_GETFOLDLEVEL, uptr_t(l)));
                if ((fl & SC_FOLDLEVELHEADERFLAG) && (fl & SC_FOLDLEVELNUMBERMASK) - SC_FOLDLEVELBASE == lvl)
                    e.call(SCI_FOLDLINE, uptr_t(l), fold ? SC_FOLDACTION_CONTRACT : SC_FOLDACTION_EXPAND);
            }
        };
        registerCommand(VIEW_FOLD_1 + lvl, std::to_string(lvl + 1), [foldLevel] { foldLevel(true); });
        registerCommand(VIEW_UNFOLD_1 + lvl, std::to_string(lvl + 1), [foldLevel] { foldLevel(false); });
    }
    registerCommand(VIEW_SUMMARY, "Summary...", [this] { summaryDialog(this); });
    registerCommand(VIEW_MONITORING, "Monitoring (tail -f)", [this] {
        if (!_current || _current->isUntitled()) return;
        bool on = !_monitors.count(_current->id);
        startMonitoring(_current, on);
        if (!on) { _current->userReadOnly = false; _editor->call(SCI_SETREADONLY, _current->readOnly); updateTab(_current); }
    }, true);
    registerCommand(VIEW_FUNC_LIST, "Function _List", [this] {
        if (!_funcList) {
            _funcList = std::make_unique<FunctionListPanel>(this);
            _functionListPanel = addSidePanel("Function List", _funcList->widget());
        }
        bool vis = gtk_widget_get_visible(_funcList->widget()) && gtk_widget_get_visible(_sideNotebook);
        showSidePanel(_functionListPanel, !vis);
        if (!vis) _funcList->refreshNow();
        Settings::get().set("view.functionList", !vis);
    }, true);
    registerCommand(VIEW_TOOLBAR_HIDE, "Toolbar", [this] { Settings::get().set("view.toolbar", !Settings::get().getBool("view.toolbar", true)); runCommand(-1); }, true);
    registerCommand(VIEW_STATUSBAR, "Status Bar", [this] { Settings::get().set("view.statusBar", !Settings::get().getBool("view.statusBar", true)); runCommand(-1); }, true);
    registerCommand(VIEW_TABBAR, "Tab Bar", [this] { Settings::get().set("view.tabBar", !Settings::get().getBool("view.tabBar", true)); runCommand(-1); }, true);
    registerCommand(VIEW_LINENUMBER, "Line Number Margin", [toggleSetting] { toggleSetting("view.lineNumbers", true); }, true);
    registerCommand(VIEW_SYMBOLMARGIN, "Bookmark Margin", [toggleSetting] { toggleSetting("view.bookmarkMargin", true); }, true);
    registerCommand(VIEW_FOLDERMAGIN, "Folding Margin", [toggleSetting] { toggleSetting("view.foldMargin", true); }, true);
    registerCommand(VIEW_CHANGEHISTORY, "Change History Margin", [toggleSetting] { toggleSetting("view.changeHistory", true); }, true);
    registerCommand(VIEW_CURLINE_HILITING, "Highlight Current Line", [this, toggleSetting] { toggleSetting("view.currentLine", true); if (_current) _editor->applyLanguage(*_current); }, true);
    registerCommand(WINDOW_NEXT, "Next Document", [this] {
        if (_buffers.size() < 2) return;
        int i = indexOf(_current); activate(_buffers[size_t(i + 1) % _buffers.size()].get());
    });
    registerCommand(WINDOW_PREV, "Previous Document", [this] {
        if (_buffers.size() < 2) return;
        int i = indexOf(_current); activate(_buffers[size_t(i + int(_buffers.size()) - 1) % _buffers.size()].get());
    });

    // ---------- Encoding
    registerCommand(FORMAT_ANSI, "_ANSI", [this] { reinterpretEncoding(_current, UniMode::Ansi, Settings::get().getString("encoding.ansiCharset", "WINDOWS-1252")); }, true);
    registerCommand(FORMAT_AS_UTF_8, "UTF-_8", [this] { reinterpretEncoding(_current, UniMode::Utf8, "UTF-8"); }, true);
    registerCommand(FORMAT_UTF_8, "UTF-8-_BOM", [this] { reinterpretEncoding(_current, UniMode::Utf8Bom, "UTF-8"); }, true);
    registerCommand(FORMAT_UTF_16BE, "UTF-16 BE BOM", [this] { reinterpretEncoding(_current, UniMode::Utf16BE, "UTF-16BE"); }, true);
    registerCommand(FORMAT_UTF_16LE, "UTF-16 LE BOM", [this] { reinterpretEncoding(_current, UniMode::Utf16LE, "UTF-16LE"); }, true);
    registerCommand(FORMAT_CONV2_ANSI, "Convert to ANSI", [this] { convertEncoding(_current, UniMode::Ansi, Settings::get().getString("encoding.ansiCharset", "WINDOWS-1252")); });
    registerCommand(FORMAT_CONV2_AS_UTF_8, "Convert to UTF-8", [this] { convertEncoding(_current, UniMode::Utf8, "UTF-8"); });
    registerCommand(FORMAT_CONV2_UTF_8, "Convert to UTF-8-BOM", [this] { convertEncoding(_current, UniMode::Utf8Bom, "UTF-8"); });
    registerCommand(FORMAT_CONV2_UTF_16BE, "Convert to UTF-16 BE BOM", [this] { convertEncoding(_current, UniMode::Utf16BE, "UTF-16BE"); });
    registerCommand(FORMAT_CONV2_UTF_16LE, "Convert to UTF-16 LE BOM", [this] { convertEncoding(_current, UniMode::Utf16LE, "UTF-16LE"); });
    const auto& cs = ansiCharsets();
    for (size_t i = 0; i < cs.size(); ++i) {
        std::string name = cs[i].iconvName;
        registerCommand(FORMAT_CHARSET_FIRST + int(i), cs[i].label, [this, name] { reinterpretEncoding(_current, UniMode::Ansi, name); }, true);
    }

    // ---------- Language
    const auto& langs = allLangs();
    for (size_t i = 0; i < langs.size(); ++i) {
        int lt = langs[i].langType;
        registerCommand(LANG_FIRST + int(i), langs[i].shortName, [this, lt] { if (_current) { _current->langLocked = true; setLanguage(_current, lt); } }, true);
    }

    // ---------- Settings
    registerCommand(SETTING_PREFERENCE, "_Preferences...", [this] { preferencesDialog(this); });
    registerCommand(SETTING_STYLER, "_Style Configurator...", [this] { styleConfiguratorDialog(this); });
    auto editUserCopy = [this](const char* userName, const char* modelName) {
        std::string user = paths::join(paths::configDir(), userName);
        if (!paths::fileExists(user)) {
            paths::mkdirs(paths::configDir());
            std::string model = paths::readFile(paths::join(paths::dataDir(), modelName));
            paths::writeFileAtomic(user, model);
        }
        openFile(user);
    };
    registerCommand(SETTING_EDITLANGS, "Edit langs.xml", [editUserCopy] { editUserCopy("langs.xml", "langs.model.xml"); });
    registerCommand(SETTING_EDITSTYLERS, "Edit stylers.xml", [editUserCopy] { editUserCopy("stylers.xml", "stylers.model.xml"); });
    registerCommand(SETTING_EDITCONFIG, "Open Settings Folder", [this] {
        paths::mkdirs(paths::configDir());
        GFile* f = g_file_new_for_path(paths::configDir().c_str());
        gchar* uri = g_file_get_uri(f);
        gtk_show_uri_on_window(GTK_WINDOW(_window), uri, GDK_CURRENT_TIME, nullptr);
        g_free(uri); g_object_unref(f);
    });
    registerCommand(SETTING_IMPORTTHEME, "Import Theme(s)...", [this] {
        GtkWidget* dlg = gtk_file_chooser_dialog_new("Import Theme(s)", GTK_WINDOW(_window), GTK_FILE_CHOOSER_ACTION_OPEN,
            "_Cancel", GTK_RESPONSE_CANCEL, "_Import", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dlg), TRUE);
        GtkFileFilter* f = gtk_file_filter_new(); gtk_file_filter_set_name(f, "Theme files (*.xml)"); gtk_file_filter_add_pattern(f, "*.xml");
        gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), f);
        if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
            paths::mkdirs(paths::userThemesDir());
            GSList* files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dlg));
            for (GSList* l = files; l; l = l->next) {
                std::string src = static_cast<char*>(l->data);
                paths::writeFileAtomic(paths::join(paths::userThemesDir(), paths::baseName(src)), paths::readFile(src));
            }
            g_slist_free_full(files, g_free);
            buildThemeMenu(this);
        }
        gtk_widget_destroy(dlg);
    });

    // ---------- Macro
    registerCommand(MACRO_STARTRECORDING, "Start _Recording", [this, ed] {
        _macro.clear(); _recording = true; ed().call(SCI_STARTRECORD); updateTitle(); syncCheckItems();
    });
    registerCommand(MACRO_STOPRECORDING, "_Stop Recording", [this, ed] {
        if (!_recording) return;
        ed().call(SCI_STOPRECORD); _recording = false; updateTitle(); syncCheckItems();
    });
    registerCommand(MACRO_PLAYBACK, "_Playback", [this, ed] {
        if (_recording || _macro.empty()) return;
        ed().call(SCI_BEGINUNDOACTION); playMacro(ed(), _macro); ed().call(SCI_ENDUNDOACTION);
    });
    registerCommand(MACRO_RUNMULTI, "Run a Macro _Multiple Times...", [this, ed] {
        if (_recording || _macro.empty()) return;
        int times = 1; bool toEnd = false;
        if (!runMacroMultipleDialog(this, times, toEnd)) return;
        Editor& e = ed();
        e.call(SCI_BEGINUNDOACTION);
        if (toEnd) {
            for (int guard = 0; guard < 1000000; ++guard) {
                sptr_t before = e.call(SCI_GETCURRENTPOS), lenBefore = e.call(SCI_GETLENGTH);
                playMacro(e, _macro);
                sptr_t after = e.call(SCI_GETCURRENTPOS);
                if (after >= e.call(SCI_GETLENGTH) || (after == before && e.call(SCI_GETLENGTH) == lenBefore)) break;
            }
        } else for (int i = 0; i < times; ++i) playMacro(e, _macro);
        e.call(SCI_ENDUNDOACTION);
    });
    registerCommand(MACRO_SAVE, "Save Current Recorded Macro...", [this] {
        if (_macro.empty()) return;
        bool ok = false;
        std::string name = inputBox(GTK_WINDOW(_window), "Save Macro", "Name:", "", &ok);
        if (!ok || name.empty()) return;
        Settings& s = Settings::get();
        int n = s.getInt("macro.count", 0);
        s.set("macro." + std::to_string(n) + ".name", name);
        s.set("macro." + std::to_string(n) + ".data", serializeMacro(_macro));
        s.set("macro.count", n + 1);
        s.save();
        rebuildMacroMenu();
    });

    // ---------- Run
    registerCommand(RUN_RUN, "_Run...", [this] {
        std::string command, saveName;
        if (!runDialog(this, command, saveName)) return;
        if (!saveName.empty()) {
            Settings& s = Settings::get();
            int n = s.getInt("run.count", 0);
            s.set("run." + std::to_string(n) + ".name", saveName);
            s.set("run." + std::to_string(n) + ".cmd", command);
            s.set("run.count", n + 1);
            s.save();
            rebuildRunMenu();
            return;
        }
        std::string full = expandRunVariables(this, command);
        gchar* argv[] = {const_cast<gchar*>("/bin/sh"), const_cast<gchar*>("-c"), const_cast<gchar*>(full.c_str()), nullptr};
        GError* err = nullptr;
        std::string dir = _current && !_current->path.empty() ? paths::dirName(_current->path) : g_get_home_dir();
        if (!g_spawn_async(dir.c_str(), argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, &err)) {
            showMessage(GTK_MESSAGE_ERROR, "Cannot run the command", err ? err->message : "");
            if (err) g_error_free(err);
        }
    });

    // ---------- Plugins
    registerCommand(PLUGINS_ADMIN, "Plugins _Admin...", [this] { PluginsAdminDlg::run(this); });
    registerCommand(PLUGINS_OPENFOLDER, "Open Plugins _Folder...", [this] {
        paths::mkdirs(paths::userPluginsDir());
        GFile* f = g_file_new_for_path(paths::userPluginsDir().c_str());
        gchar* uri = g_file_get_uri(f);
        gtk_show_uri_on_window(GTK_WINDOW(_window), uri, GDK_CURRENT_TIME, nullptr);
        g_free(uri); g_object_unref(f);
    });

    // ---------- ?
    registerCommand(ABOUT, "_About Camelion++", [this] { aboutDialog(this); });
    registerCommand(HELP_CMDLINE, "Command Line Arguments...", [this] { cmdLineHelpDialog(this); });
    registerCommand(HELP_DEBUGINFO, "Debug Info...", [this] { debugInfoDialog(this); });
    registerCommand(HELP_HOME, "Notepad++ User Manual (online)", [this] {
        gtk_show_uri_on_window(GTK_WINDOW(_window), "https://npp-user-manual.org/", GDK_CURRENT_TIME, nullptr);
    });
    (void)S;
}

// ------------------------------------------------------------------ menus

void MainWindow::buildMenus()
{
    GtkWidget* mb = _menuBar;
    // File
    GtkWidget* m = submenu(mb, "_File");
    addMenuItem(m, FILE_NEW, "<Control>n");
    addMenuItem(m, FILE_OPEN, "<Control>o");
    GtkWidget* sub = submenu(m, "Open Containing Folder");
    addMenuItem(sub, FILE_OPEN_FOLDER);
    addMenuItem(sub, FILE_OPEN_CMD);
    addMenuItem(m, FILE_OPEN_DEFAULT_VIEWER);
    addMenuItem(m, FILE_RELOAD);
    addMenuItem(m, FILE_SAVE, "<Control>s");
    addMenuItem(m, FILE_SAVEAS, "<Control><Alt>s");
    addMenuItem(m, FILE_SAVECOPYAS);
    addMenuItem(m, FILE_SAVEALL, "<Control><Shift>s");
    addMenuItem(m, FILE_RENAME);
    addMenuItem(m, FILE_CLOSE, "<Control>w|<Control>F4");
    addMenuItem(m, FILE_CLOSEALL, "<Control><Shift>w");
    sub = submenu(m, "Close Multiple Documents");
    addMenuItem(sub, FILE_CLOSEALL_BUT_CURRENT);
    addMenuItem(sub, FILE_CLOSEALL_TOLEFT);
    addMenuItem(sub, FILE_CLOSEALL_TORIGHT);
    addMenuItem(sub, FILE_CLOSEALL_UNCHANGED);
    addMenuItem(m, FILE_DELETE);
    separator(m);
    addMenuItem(m, FILE_LOADSESSION);
    addMenuItem(m, FILE_SAVESESSION);
    separator(m);
    addMenuItem(m, FILE_PRINT, "<Control>p");
    separator(m);
    _recentMenu = submenu(m, "Recent Files");
    addMenuItem(m, FILE_RESTORELASTCLOSEDFILE, "<Control><Shift>t");
    separator(m);
    addMenuItem(m, FILE_EXIT, "<Alt>F4");
    rebuildRecentMenu();

    // Edit
    m = submenu(mb, "_Edit");
    addMenuItem(m, EDIT_UNDO, "<Control>z");
    addMenuItem(m, EDIT_REDO, "<Control>y|<Control><Shift>z");
    separator(m);
    addMenuItem(m, EDIT_CUT, "<Control>x");
    addMenuItem(m, EDIT_COPY, "<Control>c");
    addMenuItem(m, EDIT_PASTE, "<Control>v");
    addMenuItem(m, EDIT_DELETE, "Delete");
    addMenuItem(m, EDIT_SELECTALL, "<Control>a");
    separator(m);
    sub = submenu(m, "Insert");
    addMenuItem(sub, EDIT_INSERT_DATETIME_SHORT);
    addMenuItem(sub, EDIT_INSERT_DATETIME_LONG);
    addMenuItem(sub, EDIT_INSERT_DATETIME_CUSTOMIZED);
    sub = submenu(m, "Copy to Clipboard");
    addMenuItem(sub, EDIT_FULLPATHTOCLIP);
    addMenuItem(sub, EDIT_FILENAMETOCLIP);
    addMenuItem(sub, EDIT_CURRENTDIRTOCLIP);
    sub = submenu(m, "Indent");
    addMenuItem(sub, EDIT_INS_TAB);
    addMenuItem(sub, EDIT_RMV_TAB);
    sub = submenu(m, "Convert Case to");
    addMenuItem(sub, EDIT_UPPERCASE, "<Control><Shift>u");
    addMenuItem(sub, EDIT_LOWERCASE, "<Control>u");
    addMenuItem(sub, EDIT_PROPERCASE_FORCE, "<Alt>u");
    addMenuItem(sub, EDIT_SENTENCECASE_FORCE, "<Control><Alt>u");
    addMenuItem(sub, EDIT_INVERTCASE, "<Alt><Shift>u");
    addMenuItem(sub, EDIT_RANDOMCASE, "<Control><Alt><Shift>u");
    sub = submenu(m, "Line Operations");
    addMenuItem(sub, EDIT_DUP_LINE, "<Control>d");
    addMenuItem(sub, EDIT_REMOVE_ANY_DUP_LINES);
    addMenuItem(sub, EDIT_REMOVE_CONSECUTIVE_DUP_LINES);
    addMenuItem(sub, EDIT_SPLIT_LINES, "<Control>i");
    addMenuItem(sub, EDIT_JOIN_LINES, "<Control>j");
    addMenuItem(sub, EDIT_LINE_UP, "<Control><Shift>Up");
    addMenuItem(sub, EDIT_LINE_DOWN, "<Control><Shift>Down");
    addMenuItem(sub, EDIT_TRANSPOSE_LINE, "<Control>t");
    addMenuItem(sub, EDIT_REMOVEEMPTYLINES);
    addMenuItem(sub, EDIT_REMOVEEMPTYLINESWITHBLANK);
    addMenuItem(sub, EDIT_BLANKLINEABOVECURRENT, "<Control><Alt>Return");
    addMenuItem(sub, EDIT_BLANKLINEBELOWCURRENT, "<Control><Alt><Shift>Return");
    separator(sub);
    addMenuItem(sub, EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING);
    addMenuItem(sub, EDIT_SORTLINES_LEXICO_CASE_INSENS_ASCENDING);
    addMenuItem(sub, EDIT_SORTLINES_INTEGER_ASCENDING);
    addMenuItem(sub, EDIT_SORTLINES_LEXICOGRAPHIC_DESCENDING);
    addMenuItem(sub, EDIT_SORTLINES_LEXICO_CASE_INSENS_DESCENDING);
    addMenuItem(sub, EDIT_SORTLINES_INTEGER_DESCENDING);
    addMenuItem(sub, EDIT_SORTLINES_REVERSE_ORDER);
    addMenuItem(sub, EDIT_SORTLINES_RANDOMLY);
    sub = submenu(m, "Comment/Uncomment");
    addMenuItem(sub, EDIT_TOGGLE_COMMENT, "<Control>q|<Control>slash");
    addMenuItem(sub, EDIT_BLOCK_COMMENT_SET, "<Control>k");
    addMenuItem(sub, EDIT_BLOCK_UNCOMMENT, "<Control><Shift>k");
    addMenuItem(sub, EDIT_STREAM_COMMENT, "<Control><Shift>q");
    addMenuItem(sub, EDIT_STREAM_UNCOMMENT);
    sub = submenu(m, "Auto-Completion");
    addMenuItem(sub, EDIT_AUTOCOMPLETE, "<Control>space");
    addMenuItem(sub, EDIT_AUTOCOMPLETE_CURRENTFILE, "<Control>Return");
    addMenuItem(sub, EDIT_FUNCCALLTIP, "<Control><Shift>space");
    addMenuItem(sub, EDIT_AUTOCOMPLETE_PATH, "<Control><Alt>space");
    sub = submenu(m, "EOL Conversion");
    addMenuItem(sub, FORMAT_TODOS);
    addMenuItem(sub, FORMAT_TOUNIX);
    addMenuItem(sub, FORMAT_TOMAC);
    sub = submenu(m, "Blank Operations");
    addMenuItem(sub, EDIT_TRIMTRAILING);
    addMenuItem(sub, EDIT_TRIMLINEHEAD);
    addMenuItem(sub, EDIT_TRIM_BOTH);
    addMenuItem(sub, EDIT_EOL2WS);
    addMenuItem(sub, EDIT_TRIMALL);
    separator(sub);
    addMenuItem(sub, EDIT_TAB2SW);
    addMenuItem(sub, EDIT_SW2TAB_ALL);
    addMenuItem(sub, EDIT_SW2TAB_LEADING);
    separator(m);
    addMenuItem(m, EDIT_COLUMNMODETIP);
    addMenuItem(m, EDIT_COLUMN_EDITOR, "<Alt>c");
    separator(m);
    addMenuItem(m, EDIT_SETREADONLY);

    // Search
    m = submenu(mb, "_Search");
    addMenuItem(m, SEARCH_FIND, "<Control>f");
    addMenuItem(m, SEARCH_FINDINFILES, "<Control><Shift>f");
    addMenuItem(m, SEARCH_FINDNEXT, "F3");
    addMenuItem(m, SEARCH_FINDPREV, "<Shift>F3");
    addMenuItem(m, SEARCH_SETANDFINDNEXT, "<Control>F3");
    addMenuItem(m, SEARCH_SETANDFINDPREV, "<Control><Shift>F3");
    addMenuItem(m, SEARCH_VOLATILE_FINDNEXT, "<Control><Alt>F3");
    addMenuItem(m, SEARCH_VOLATILE_FINDPREV, "<Control><Alt><Shift>F3");
    addMenuItem(m, SEARCH_REPLACE, "<Control>h");
    separator(m);
    addMenuItem(m, VIEW_SEARCHRESULTS, "F7");
    addMenuItem(m, SEARCH_GOTONEXTFOUND, "F4");
    addMenuItem(m, SEARCH_GOTOPREVFOUND, "<Shift>F4");
    addMenuItem(m, SEARCH_GOTOLINE, "<Control>g");
    addMenuItem(m, SEARCH_GOTOMATCHINGBRACE, "<Control>b");
    addMenuItem(m, SEARCH_SELECTMATCHINGBRACES, "<Control><Alt>b");
    addMenuItem(m, SEARCH_MARK);
    separator(m);
    sub = submenu(m, "Style All Occurrences of Token");
    for (int i = 0; i < 5; ++i) addMenuItem(sub, SEARCH_MARKALLEXT1 + 2 * i);
    sub = submenu(m, "Clear Style");
    for (int i = 0; i < 5; ++i) addMenuItem(sub, SEARCH_UNMARKALLEXT1 + 2 * i);
    addMenuItem(sub, SEARCH_CLEARALLMARKS);
    separator(m);
    sub = submenu(m, "Bookmark");
    addMenuItem(sub, SEARCH_TOGGLE_BOOKMARK, "<Control>F2");
    addMenuItem(sub, SEARCH_NEXT_BOOKMARK, "F2");
    addMenuItem(sub, SEARCH_PREV_BOOKMARK, "<Shift>F2");
    addMenuItem(sub, SEARCH_CLEAR_BOOKMARKS);
    addMenuItem(sub, SEARCH_CUTMARKEDLINES);
    addMenuItem(sub, SEARCH_COPYMARKEDLINES);
    addMenuItem(sub, SEARCH_PASTEMARKEDLINES);
    addMenuItem(sub, SEARCH_DELETEMARKEDLINES);
    addMenuItem(sub, SEARCH_DELETEUNMARKEDLINES);
    addMenuItem(sub, SEARCH_INVERSEMARKS);

    // View
    m = submenu(mb, "_View");
    addMenuItem(m, VIEW_ALWAYSONTOP);
    addMenuItem(m, VIEW_FULLSCREENTOGGLE, "F11");
    separator(m);
    sub = submenu(m, "Show Symbol");
    addMenuItem(sub, VIEW_TAB_SPACE);
    addMenuItem(sub, VIEW_EOL);
    addMenuItem(sub, VIEW_ALL_CHARACTERS);
    separator(sub);
    addMenuItem(sub, VIEW_INDENT_GUIDE);
    addMenuItem(sub, VIEW_WRAP_SYMBOL);
    sub = submenu(m, "Zoom");
    addMenuItem(sub, VIEW_ZOOMIN, "<Control>plus|<Control>equal|<Control>KP_Add");
    addMenuItem(sub, VIEW_ZOOMOUT, "<Control>minus|<Control>KP_Subtract");
    addMenuItem(sub, VIEW_ZOOMRESTORE, "<Control>0|<Control>KP_Divide");
    sub = submenu(m, "Margins & Bars");
    addMenuItem(sub, VIEW_TOOLBAR_HIDE);
    addMenuItem(sub, VIEW_TABBAR);
    addMenuItem(sub, VIEW_STATUSBAR);
    separator(sub);
    addMenuItem(sub, VIEW_LINENUMBER);
    addMenuItem(sub, VIEW_SYMBOLMARGIN);
    addMenuItem(sub, VIEW_FOLDERMAGIN);
    addMenuItem(sub, VIEW_CHANGEHISTORY);
    addMenuItem(sub, VIEW_CURLINE_HILITING);
    separator(m);
    addMenuItem(m, VIEW_WRAP);
    separator(m);
    addMenuItem(m, VIEW_FOLDALL, "<Alt>0");
    addMenuItem(m, VIEW_UNFOLDALL, "<Alt><Shift>0");
    addMenuItem(m, VIEW_FOLD_CURRENT, "<Control><Alt>f");
    addMenuItem(m, VIEW_UNFOLD_CURRENT, "<Control><Alt><Shift>f");
    sub = submenu(m, "Collapse Level");
    for (int i = 0; i < 8; ++i) addMenuItem(sub, VIEW_FOLD_1 + i, (std::string("<Alt>") + char('1' + i)).c_str());
    sub = submenu(m, "Uncollapse Level");
    for (int i = 0; i < 8; ++i) addMenuItem(sub, VIEW_UNFOLD_1 + i, (std::string("<Alt><Shift>") + char('1' + i)).c_str());
    separator(m);
    addMenuItem(m, VIEW_SUMMARY);
    addMenuItem(m, VIEW_FUNC_LIST);
    addMenuItem(m, VIEW_MONITORING);

    // Encoding
    m = submenu(mb, "E_ncoding");
    addMenuItem(m, FORMAT_ANSI);
    addMenuItem(m, FORMAT_AS_UTF_8);
    addMenuItem(m, FORMAT_UTF_8);
    addMenuItem(m, FORMAT_UTF_16BE);
    addMenuItem(m, FORMAT_UTF_16LE);
    sub = submenu(m, "Character sets");
    {
        std::map<std::string, GtkWidget*> groups;
        const auto& cs = ansiCharsets();
        for (size_t i = 0; i < cs.size(); ++i) {
            GtkWidget*& g = groups[cs[i].group];
            if (!g) g = submenu(sub, cs[i].group);
            addMenuItem(g, FORMAT_CHARSET_FIRST + int(i));
        }
    }
    separator(m);
    addMenuItem(m, FORMAT_CONV2_ANSI);
    addMenuItem(m, FORMAT_CONV2_AS_UTF_8);
    addMenuItem(m, FORMAT_CONV2_UTF_8);
    addMenuItem(m, FORMAT_CONV2_UTF_16BE);
    addMenuItem(m, FORMAT_CONV2_UTF_16LE);

    // Language: grouped by initial letter, like Notepad++
    _langMenu = submenu(mb, "_Language");
    {
        const auto& langs = allLangs();
        addMenuItem(_langMenu, LANG_FIRST + 0);   // Normal text
        separator(_langMenu);
        std::vector<size_t> order;
        for (size_t i = 1; i < langs.size(); ++i) {
            int lt = langs[i].langType;
            if (lt == 15 || lt == 47 || lt == 19) continue;   // udf, searchResult, embedded JS
            order.push_back(i);
        }
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return g_ascii_strcasecmp(langs[a].shortName, langs[b].shortName) < 0; });
        std::map<char, GtkWidget*> letters;
        for (size_t i : order) {
            char c = char(g_ascii_toupper(langs[i].shortName[0]));
            GtkWidget*& lm = letters[c];
            if (!lm) lm = submenu(_langMenu, std::string(1, c).c_str());
            _langItems[langs[i].langType] = addMenuItem(lm, LANG_FIRST + int(i));
        }
    }

    // Settings
    m = submenu(mb, "Se_ttings");
    addMenuItem(m, SETTING_PREFERENCE);
    addMenuItem(m, SETTING_STYLER);
    _themeMenu = submenu(m, "Theme");
    g_object_set_data(G_OBJECT(_window), "cam-theme-menu", _themeMenu);
    buildThemeMenu(this);
    addMenuItem(m, SETTING_IMPORTTHEME);
    separator(m);
    addMenuItem(m, SETTING_EDITLANGS);
    addMenuItem(m, SETTING_EDITSTYLERS);
    addMenuItem(m, SETTING_EDITCONFIG);

    // Macro
    _macroMenu = submenu(mb, "_Macro");
    rebuildMacroMenu();

    // Run
    _runMenu = submenu(mb, "_Run");
    rebuildRunMenu();

    // Plugins (filled by PluginManager::buildMenu)
    _pluginsMenu = submenu(mb, "_Plugins");

    // Window
    _windowMenu = submenu(mb, "_Window");
    rebuildWindowMenu();

    // ?
    m = submenu(mb, "_?");
    addMenuItem(m, HELP_CMDLINE);
    addMenuItem(m, HELP_HOME);
    addMenuItem(m, HELP_DEBUGINFO);
    separator(m);
    addMenuItem(m, ABOUT, "F1");

    gtk_widget_show_all(mb);
    syncCheckItems();
}

void buildThemeMenu(MainWindow* mw)
{
    // theme radio-ish items: rebuilt on demand
    GList* children = nullptr;
    GtkWidget* menu = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(mw->window()), "cam-theme-menu"));
    if (!menu) return;
    children = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList* l = children; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);
    auto themes = Parameters::get().themes();
    std::string cur = Parameters::get().currentThemePath();
    for (auto& t : themes) {
        GtkWidget* it = gtk_check_menu_item_new_with_label(t.name.c_str());
        gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(it), TRUE);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), t.path == cur);
        g_object_set_data_full(G_OBJECT(it), "cam-theme", g_strdup(t.path.c_str()), g_free);
        g_signal_connect(it, "activate", CAM_CB(+[](GtkWidget* w, gpointer self) {
            auto* mw = static_cast<MainWindow*>(self);
            if (g_object_get_data(G_OBJECT(w), "cam-guard")) return;
            const char* path = static_cast<const char*>(g_object_get_data(G_OBJECT(w), "cam-theme"));
            if (Parameters::get().loadTheme(path)) {
                Settings::get().set("theme", std::string(path));
                Settings::get().save();
                mw->refreshAllStyles();
            }
            g_idle_add(+[](gpointer s) -> gboolean { buildThemeMenu(static_cast<MainWindow*>(s)); return G_SOURCE_REMOVE; }, mw);
        }), mw);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    }
    gtk_widget_show_all(menu);
}

void MainWindow::rebuildRecentMenu()
{
    if (!_recentMenu) return;
    GList* ch = gtk_container_get_children(GTK_CONTAINER(_recentMenu));
    for (GList* l = ch; l; l = l->next) {
        GtkWidget* w = GTK_WIDGET(l->data);
        int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cam-cmd"));
        auto it = _commands.find(id);
        if (it != _commands.end() && it->second.item == w) it->second.item = nullptr;
        gtk_widget_destroy(w);
    }
    g_list_free(ch);
    auto& rec = Settings::get().recentFiles();
    int i = 0;
    for (auto& p : rec) {
        std::string label = std::to_string(i + 1) + ": " + p;
        GtkWidget* it = gtk_menu_item_new_with_label(label.c_str());
        g_object_set_data_full(G_OBJECT(it), "cam-path", g_strdup(p.c_str()), g_free);
        g_signal_connect(it, "activate", CAM_CB(+[](GtkWidget* w, gpointer self) {
            static_cast<MainWindow*>(self)->openFile(static_cast<const char*>(g_object_get_data(G_OBJECT(w), "cam-path")));
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(_recentMenu), it);
        ++i;
    }
    if (!rec.empty()) separator(_recentMenu);
    addMenuItem(_recentMenu, FILE_OPEN_ALL_RECENT);
    addMenuItem(_recentMenu, FILE_EMPTY_RECENT);
    gtk_widget_show_all(_recentMenu);
}

void MainWindow::rebuildWindowMenu()
{
    if (!_windowMenu) return;
    GList* ch = gtk_container_get_children(GTK_CONTAINER(_windowMenu));
    for (GList* l = ch; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(ch);
    _commands[WINDOW_NEXT].item = nullptr; _commands[WINDOW_PREV].item = nullptr;
    addMenuItem(_windowMenu, WINDOW_NEXT, "<Control>Page_Down|<Control>Tab");
    addMenuItem(_windowMenu, WINDOW_PREV, "<Control>Page_Up|<Control><Shift>ISO_Left_Tab");
    separator(_windowMenu);
    int i = 0;
    for (auto& b : _buffers) {
        std::string label = (i < 9 ? "_" : "") + std::to_string(i + 1) + ": " + (b->path.empty() ? b->untitledName : b->path);
        GtkWidget* it = gtk_check_menu_item_new_with_mnemonic(label.c_str());
        gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(it), TRUE);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), b.get() == _current);
        g_object_set_data(G_OBJECT(it), "cam-buffer-id", GINT_TO_POINTER(b->id));
        g_signal_connect(it, "activate", CAM_CB(+[](GtkWidget* w, gpointer self) {
            auto* mw = static_cast<MainWindow*>(self);
            if (mw->_syncingChecks) return;
            if (Buffer* b = mw->bufferById(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cam-buffer-id")))) mw->activate(b);
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(_windowMenu), it);
        ++i;
    }
    gtk_widget_show_all(_windowMenu);
}

void MainWindow::rebuildMacroMenu()
{
    if (!_macroMenu) return;
    GList* ch = gtk_container_get_children(GTK_CONTAINER(_macroMenu));
    for (GList* l = ch; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(ch);
    addMenuItem(_macroMenu, MACRO_STARTRECORDING, "<Control><Shift>r");
    addMenuItem(_macroMenu, MACRO_STOPRECORDING, "<Control><Shift>e");
    addMenuItem(_macroMenu, MACRO_PLAYBACK, "<Control><Shift>p");
    addMenuItem(_macroMenu, MACRO_SAVE);
    addMenuItem(_macroMenu, MACRO_RUNMULTI);
    Settings& s = Settings::get();
    int n = s.getInt("macro.count", 0);
    if (n > 0) separator(_macroMenu);
    for (int i = 0; i < n; ++i) {
        std::string name = s.getString("macro." + std::to_string(i) + ".name");
        std::string data = s.getString("macro." + std::to_string(i) + ".data");
        int id = MACRO_SAVED_FIRST + i;
        registerCommand(id, name, [this, data] {
            auto m = parseMacro(data);
            _editor->call(SCI_BEGINUNDOACTION); playMacro(*_editor, m); _editor->call(SCI_ENDUNDOACTION);
        });
        addMenuItem(_macroMenu, id);
    }
    gtk_widget_show_all(_macroMenu);
}

void MainWindow::rebuildRunMenu()
{
    if (!_runMenu) return;
    GList* ch = gtk_container_get_children(GTK_CONTAINER(_runMenu));
    for (GList* l = ch; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(ch);
    addMenuItem(_runMenu, RUN_RUN, "F5");
    addMenuItem(_runMenu, FILE_OPEN_CMD);
    Settings& s = Settings::get();
    int n = s.getInt("run.count", 0);
    if (n > 0) separator(_runMenu);
    for (int i = 0; i < n; ++i) {
        std::string name = s.getString("run." + std::to_string(i) + ".name");
        std::string command = s.getString("run." + std::to_string(i) + ".cmd");
        int id = RUN_SAVED_FIRST + i;
        registerCommand(id, name, [this, command] {
            std::string full = expandRunVariables(this, command);
            gchar* argv[] = {const_cast<gchar*>("/bin/sh"), const_cast<gchar*>("-c"), const_cast<gchar*>(full.c_str()), nullptr};
            std::string dir = _current && !_current->path.empty() ? paths::dirName(_current->path) : g_get_home_dir();
            g_spawn_async(dir.c_str(), argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr);
        });
        addMenuItem(_runMenu, id);
    }
    gtk_widget_show_all(_runMenu);
}

void MainWindow::syncCheckItems()
{
    if (_syncingChecks) return;
    Settings& S = Settings::get();
    auto set = [this](int id, bool on) { setChecked(id, on); };
    set(VIEW_WRAP, S.getBool("view.wrap", false));
    set(VIEW_TAB_SPACE, S.getBool("view.whitespace", false));
    set(VIEW_EOL, S.getBool("view.eol", false));
    set(VIEW_ALL_CHARACTERS, S.getBool("view.allChars", false));
    set(VIEW_INDENT_GUIDE, S.getBool("view.indentGuides", true));
    set(VIEW_WRAP_SYMBOL, S.getBool("view.wrapSymbol", false));
    set(VIEW_ALWAYSONTOP, S.getBool("view.alwaysOnTop", false));
    set(VIEW_TOOLBAR_HIDE, S.getBool("view.toolbar", true));
    set(VIEW_STATUSBAR, S.getBool("view.statusBar", true));
    set(VIEW_TABBAR, S.getBool("view.tabBar", true));
    set(VIEW_LINENUMBER, S.getBool("view.lineNumbers", true));
    set(VIEW_SYMBOLMARGIN, S.getBool("view.bookmarkMargin", true));
    set(VIEW_FOLDERMAGIN, S.getBool("view.foldMargin", true));
    set(VIEW_CHANGEHISTORY, S.getBool("view.changeHistory", true));
    set(VIEW_CURLINE_HILITING, S.getBool("view.currentLine", true));
    set(VIEW_FUNC_LIST, _funcList && gtk_widget_get_visible(_funcList->widget()) && gtk_widget_get_visible(_sideNotebook));
    if (_current) {
        set(VIEW_MONITORING, _monitors.count(_current->id) > 0);
        set(EDIT_SETREADONLY, _current->userReadOnly);
        set(FORMAT_TODOS, _current->eol == EolType::CRLF);
        set(FORMAT_TOUNIX, _current->eol == EolType::LF);
        set(FORMAT_TOMAC, _current->eol == EolType::CR);
        set(FORMAT_ANSI, _current->mode == UniMode::Ansi);
        set(FORMAT_AS_UTF_8, _current->mode == UniMode::Utf8);
        set(FORMAT_UTF_8, _current->mode == UniMode::Utf8Bom);
        set(FORMAT_UTF_16BE, _current->mode == UniMode::Utf16BE);
        set(FORMAT_UTF_16LE, _current->mode == UniMode::Utf16LE);
        const auto& cs = ansiCharsets();
        for (size_t i = 0; i < cs.size(); ++i) set(FORMAT_CHARSET_FIRST + int(i), _current->mode == UniMode::Ansi && _current->charset == cs[i].iconvName);
        const auto& langs = allLangs();
        for (size_t i = 0; i < langs.size(); ++i) set(LANG_FIRST + int(i), langs[i].langType == _current->langType);
    }
    auto sens = [this](int id, bool on) { auto it = _commands.find(id); if (it != _commands.end() && it->second.item) gtk_widget_set_sensitive(it->second.item, on); };
    sens(MACRO_STARTRECORDING, !_recording);
    sens(MACRO_STOPRECORDING, _recording);
    sens(MACRO_PLAYBACK, !_recording && !_macro.empty());
    sens(MACRO_SAVE, !_recording && !_macro.empty());
    sens(MACRO_RUNMULTI, !_recording && !_macro.empty());
    // window menu radio state
    if (_windowMenu) {
        _syncingChecks = true;
        GList* ch = gtk_container_get_children(GTK_CONTAINER(_windowMenu));
        for (GList* l = ch; l; l = l->next) {
            GtkWidget* w = GTK_WIDGET(l->data);
            if (!GTK_IS_CHECK_MENU_ITEM(w)) continue;
            int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cam-buffer-id"));
            if (id) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), _current && _current->id == id);
        }
        g_list_free(ch);
        _syncingChecks = false;
    }
    // toolbar toggles
    if (_toolbar) {
        _syncingChecks = true;
        GList* ch = gtk_container_get_children(GTK_CONTAINER(_toolbar));
        for (GList* l = ch; l; l = l->next) {
            if (!GTK_IS_TOGGLE_TOOL_BUTTON(l->data)) continue;
            int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(l->data), "cam-cmd"));
            bool on = false;
            if (id == VIEW_WRAP) on = S.getBool("view.wrap", false);
            else if (id == VIEW_ALL_CHARACTERS) on = S.getBool("view.allChars", false);
            else if (id == VIEW_INDENT_GUIDE) on = S.getBool("view.indentGuides", true);
            else if (id == MACRO_STARTRECORDING) on = _recording;
            else if (id == VIEW_FUNC_LIST) on = _funcList && gtk_widget_get_visible(_funcList->widget()) && gtk_widget_get_visible(_sideNotebook);
            else if (id == VIEW_MONITORING) on = _current && _monitors.count(_current->id);
            gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(l->data), on);
        }
        g_list_free(ch);
        _syncingChecks = false;
    }
}

// ------------------------------------------------------------------ toolbar
void MainWindow::buildToolbar()
{
    struct T { int id; const char* icon; const char* tip; bool toggle; };
    const T items[] = {
        {FILE_NEW, "document-new", "New", false}, {FILE_OPEN, "document-open", "Open", false},
        {FILE_SAVE, "document-save", "Save", false}, {FILE_SAVEALL, "document-save-as", "Save All", false},
        {FILE_CLOSE, "window-close", "Close", false}, {FILE_PRINT, "document-print", "Print", false},
        {0, nullptr, nullptr, false},
        {EDIT_CUT, "edit-cut", "Cut", false}, {EDIT_COPY, "edit-copy", "Copy", false}, {EDIT_PASTE, "edit-paste", "Paste", false},
        {0, nullptr, nullptr, false},
        {EDIT_UNDO, "edit-undo", "Undo", false}, {EDIT_REDO, "edit-redo", "Redo", false},
        {0, nullptr, nullptr, false},
        {SEARCH_FIND, "edit-find", "Find", false}, {SEARCH_REPLACE, "edit-find-replace", "Replace", false},
        {0, nullptr, nullptr, false},
        {VIEW_ZOOMIN, "zoom-in", "Zoom in", false}, {VIEW_ZOOMOUT, "zoom-out", "Zoom out", false},
        {0, nullptr, nullptr, false},
        {VIEW_WRAP, "format-justify-fill", "Word wrap", true}, {VIEW_ALL_CHARACTERS, "format-text-direction-ltr", "Show all characters", true},
        {VIEW_INDENT_GUIDE, "format-indent-more", "Show indent guide", true},
        {0, nullptr, nullptr, false},
        {VIEW_FUNC_LIST, "view-list", "Function List", true}, {VIEW_MONITORING, "view-refresh", "Monitoring (tail -f)", true},
        {0, nullptr, nullptr, false},
        {MACRO_STARTRECORDING, "media-record", "Start recording", true}, {MACRO_STOPRECORDING, "media-playback-stop", "Stop recording", false},
        {MACRO_PLAYBACK, "media-playback-start", "Playback", false}, {MACRO_RUNMULTI, "media-seek-forward", "Run a macro multiple times", false},
    };
    for (auto& t : items) {
        GtkToolItem* it;
        if (!t.icon) { it = gtk_separator_tool_item_new(); gtk_toolbar_insert(GTK_TOOLBAR(_toolbar), it, -1); continue; }
        GtkWidget* img = gtk_image_new_from_icon_name(t.icon, GTK_ICON_SIZE_SMALL_TOOLBAR);
        it = t.toggle ? gtk_toggle_tool_button_new() : gtk_tool_button_new(nullptr, t.tip);
        gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(it), img);
        gtk_tool_button_set_label(GTK_TOOL_BUTTON(it), t.tip);
        gtk_tool_item_set_tooltip_text(it, t.tip);
        g_object_set_data(G_OBJECT(it), "cam-cmd", GINT_TO_POINTER(t.id));
        g_signal_connect(it, t.toggle ? "toggled" : "clicked", CAM_CB(+[](GtkWidget* w, gpointer self) {
            auto* mw = static_cast<MainWindow*>(self);
            if (mw->_syncingChecks) return;
            int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cam-cmd"));
            if (id == MACRO_STARTRECORDING && mw->isRecording()) id = MACRO_STOPRECORDING;
            mw->runCommand(id);
            mw->syncCheckItems();
        }), this);
        gtk_toolbar_insert(GTK_TOOLBAR(_toolbar), it, -1);
    }
    gtk_widget_show_all(_toolbar);
}

} // namespace cam
