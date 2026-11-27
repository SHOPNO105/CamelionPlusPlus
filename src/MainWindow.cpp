// Camelion++ - main window: UI construction, buffers, file I/O, notifications, session
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
#include "FindReplaceDlg.h"
#include "PluginManager.h"
#include "FunctionList.h"
#include "AutoComplete.h"
#include "Dialogs.h"
#include "Version.h"
#include <CamelionPluginInterface.h>
#include <ILoader.h>
#include "TextOps.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <climits>
#include <cstring>
#include <algorithm>
#include <ctime>

namespace cam {

static const char* kCss =
    ".cam-tab-dirty label.cam-tab-name { color: #d03030; }"
    ".cam-tab-ro label.cam-tab-name { color: #808080; font-style: italic; }"
    ".cam-tab-close { padding: 0; min-height: 0; min-width: 0; border: none; background: none; box-shadow: none; }"
    ".cam-tab-close:hover { background: alpha(currentColor, 0.15); }"
    ".cam-status label { padding: 0 8px; }"
    "notebook.cam-tabs > stack { min-height: 0; }"
    "notebook.cam-tabs header.top { border-bottom-width: 0; }";

// ------------------------------------------------------------------ helpers
static std::string absolutePath(const std::string& p)
{
    if (p.empty()) return p;
    char buf[PATH_MAX];
    if (realpath(p.c_str(), buf)) return buf;
    if (p[0] == '/') return p;
    gchar* cwd = g_get_current_dir();
    std::string r = paths::join(cwd, p);
    g_free(cwd);
    return r;
}

static bool statFile(const std::string& p, time_t& mtime, long long& size)
{
    struct stat st;
    if (::stat(p.c_str(), &st) != 0) return false;
    mtime = st.st_mtime; size = st.st_size;
    return true;
}

// The hidden view lets us operate on documents that are not currently displayed
// (Save All, reload of background tabs, ...). Notepad++ uses a similar "fake" view.
static Editor& hiddenEditor()
{
    static Editor* e = nullptr;
    if (!e) { e = new Editor(); e->call(SCI_SETCODEPAGE, SC_CP_UTF8); e->call(SCI_SETUNDOCOLLECTION, 1); }
    return *e;
}

template <typename F>
static auto withDoc(MainWindow* mw, Buffer* b, F f)
{
    if (b == mw->current()) return f(mw->editor());
    Editor& h = hiddenEditor();
    sptr_t keep = h.call(SCI_GETDOCPOINTER);
    h.call(SCI_ADDREFDOCUMENT, 0, keep);
    h.call(SCI_SETDOCPOINTER, 0, b->doc);
    h.call(SCI_SETCODEPAGE, SC_CP_UTF8);
    struct Restore { Editor& h; sptr_t keep; ~Restore() { h.call(SCI_SETDOCPOINTER, 0, keep); h.call(SCI_RELEASEDOCUMENT, 0, keep); } } r{h, keep};
    return f(h);
}

void MainWindow::withBufferEditor(Buffer* b, const std::function<void(Editor&)>& f)
{
    if (!b) return;
    withDoc(this, b, [&](Editor& e) { f(e); return 0; });
}

// ------------------------------------------------------------------ construction
MainWindow::MainWindow(GtkApplication* app) : _app(app)
{
    _editor = std::make_unique<Editor>();
    _editor->initDefaults();
    _autoComplete = std::make_unique<AutoComplete>();
    buildUi();
    _plugins = std::make_unique<PluginManager>(this);
    registerCommands();
    buildMenus();
    buildToolbar();
    buildStatusBar();
    _plugins->loadAll();
    _plugins->buildMenu();
    applyThemeToChrome();
    _editor->applyViewOptions();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, kCss, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    Settings& S = Settings::get();
    _window = gtk_application_window_new(_app);
    gtk_window_set_title(GTK_WINDOW(_window), "Camelion++");
    gtk_window_set_icon_name(GTK_WINDOW(_window), "camelion++");
    gtk_window_set_default_size(GTK_WINDOW(_window), S.getInt("window.width", 1100), S.getInt("window.height", 720));
    if (S.getBool("window.maximized", false)) gtk_window_maximize(GTK_WINDOW(_window));
    _accel = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(_window), _accel);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(_window), vbox);

    _menuBar = gtk_menu_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), _menuBar, FALSE, FALSE, 0);
    _toolbar = gtk_toolbar_new();
    gtk_toolbar_set_style(GTK_TOOLBAR(_toolbar), GTK_TOOLBAR_ICONS);
    gtk_toolbar_set_icon_size(GTK_TOOLBAR(_toolbar), GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_box_pack_start(GTK_BOX(vbox), _toolbar, FALSE, FALSE, 0);

    _sidePaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), _sidePaned, TRUE, TRUE, 0);
    _sideNotebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(_sideNotebook), TRUE);
    gtk_widget_set_size_request(_sideNotebook, 220, -1);
    gtk_paned_pack1(GTK_PANED(_sidePaned), _sideNotebook, FALSE, FALSE);
    gtk_widget_set_no_show_all(_sideNotebook, TRUE);

    _bottomPaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_paned_pack2(GTK_PANED(_sidePaned), _bottomPaned, TRUE, FALSE);

    GtkWidget* editorBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    _notebook = gtk_notebook_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(_notebook), "cam-tabs");
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(_notebook), TRUE);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(_notebook), FALSE);
    gtk_widget_set_vexpand(_notebook, FALSE);
    gtk_box_pack_start(GTK_BOX(editorBox), _notebook, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(editorBox), _editor->widget(), TRUE, TRUE, 0);
    gtk_paned_pack1(GTK_PANED(_bottomPaned), editorBox, TRUE, FALSE);

    // search results panel
    _resultsBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget* hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget* title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title), "<b>Search results</b>");
    gtk_box_pack_start(GTK_BOX(hdr), title, FALSE, FALSE, 6);
    GtkWidget* closeBtn = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(closeBtn), GTK_RELIEF_NONE);
    g_signal_connect_swapped(closeBtn, "clicked", CAM_CB(+[](MainWindow* mw) { mw->toggleSearchResults(false); }), this);
    gtk_box_pack_end(GTK_BOX(hdr), closeBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(_resultsBox), hdr, FALSE, FALSE, 0);
    GtkTreeStore* store = gtk_tree_store_new(5, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT64, G_TYPE_INT64, G_TYPE_INT64);
    _resultsView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(_resultsView), FALSE);
    GtkCellRenderer* r = gtk_cell_renderer_text_new();
    g_object_set(r, "family", "Monospace", nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(_resultsView), -1, "", r, "markup", 0, nullptr);
    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(sw), _resultsView);
    gtk_box_pack_start(GTK_BOX(_resultsBox), sw, TRUE, TRUE, 0);
    gtk_paned_pack2(GTK_PANED(_bottomPaned), _resultsBox, FALSE, TRUE);
    gtk_widget_set_size_request(_resultsBox, -1, 120);
    gtk_widget_set_no_show_all(_resultsBox, TRUE);
    g_signal_connect(_resultsView, "row-activated", CAM_CB(+[](GtkTreeView* tv, GtkTreePath* p, GtkTreeViewColumn*, gpointer self) {
        auto* mw = static_cast<MainWindow*>(self);
        GtkTreeModel* m = gtk_tree_view_get_model(tv);
        GtkTreeIter it;
        if (!gtk_tree_model_get_iter(m, &it, p)) return;
        gint bufId = 0; gint64 line = -1, a = 0, b = 0;
        gtk_tree_model_get(m, &it, 1, &bufId, 2, &line, 3, &a, 4, &b, -1);
        if (line < 0) { // a file / header row: expand or collapse
            if (gtk_tree_view_row_expanded(tv, p)) gtk_tree_view_collapse_row(tv, p); else gtk_tree_view_expand_row(tv, p, FALSE);
            return;
        }
        // bufId > 0: an open buffer; bufId < 0: a Find-in-Files hit whose file path
        // lives in the results table kept by the Find dialog.
        Buffer* buf = nullptr;
        if (bufId > 0) buf = mw->bufferById(bufId);
        else if (bufId < 0) {
            std::string path = mw->findDlg().resultFilePath(bufId);
            if (!path.empty()) buf = mw->openFile(path);
            if (buf) { a = -1; b = -1; }   // byte offsets of a file on disk don't apply: go to the line
        }
        if (buf) mw->jumpTo(buf, line, a, b);
    }), this);

    gtk_widget_show_all(vbox);

    g_signal_connect(_editor->widget(), "sci-notify", G_CALLBACK(onSciNotify), this);
    g_signal_connect(_notebook, "switch-page", G_CALLBACK(onSwitchPage), this);
    g_signal_connect(_notebook, "page-reordered", G_CALLBACK(onPageReordered), this);
    g_signal_connect(_window, "delete-event", G_CALLBACK(onDelete), this);
    g_signal_connect(_window, "notify::is-active", G_CALLBACK(onActiveChanged), this);

    // double click on the empty tab bar area => new document (like Notepad++)
    gtk_widget_add_events(_notebook, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(_notebook, "button-press-event", CAM_CB(+[](GtkWidget*, GdkEventButton* ev, gpointer self) -> gboolean {
        if (ev->type == GDK_2BUTTON_PRESS && ev->button == 1) { static_cast<MainWindow*>(self)->newDocument(); return TRUE; }
        return FALSE;
    }), this);

    // file drops on the tab bar
    gtk_drag_dest_set(_notebook, GTK_DEST_DEFAULT_ALL, nullptr, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(_notebook);
    g_signal_connect(_notebook, "drag-data-received", CAM_CB(+[](GtkWidget*, GdkDragContext*, gint, gint, GtkSelectionData* d, guint, guint, gpointer self) {
        static_cast<MainWindow*>(self)->dropFiles(d);
    }), this);

    // the initial Scintilla document becomes "new 1"
    sptr_t doc = _editor->call(SCI_GETDOCPOINTER);
    _editor->call(SCI_ADDREFDOCUMENT, 0, doc);
    Buffer* b = createBuffer(doc);
    b->untitledName = "new " + std::to_string(++_untitledCounter);
    b->eol = EolType(S.getInt("newdoc.eol", int(EolType::LF)));
    attachTab(b);
    _current = b;
    _editor->call(SCI_SETEOLMODE, b->eol == EolType::CRLF ? SC_EOL_CRLF : b->eol == EolType::CR ? SC_EOL_CR : SC_EOL_LF);
    _editor->applyLanguage(*b);
    _editor->call(SCI_SETCHANGEHISTORY, SC_CHANGE_HISTORY_ENABLED | SC_CHANGE_HISTORY_MARKERS);
}

void MainWindow::show()
{
    gtk_widget_show(_window);
    gtk_widget_grab_focus(_editor->widget());
    updateStatusBar();
    updateTitle();
    notifyPlugins(NPPN_READY, 0);
}

// ------------------------------------------------------------------ tabs & buffers
Buffer* MainWindow::createBuffer(sptr_t doc)
{
    auto b = std::make_unique<Buffer>();
    b->id = _nextBufferId++;
    b->doc = doc;
    Buffer* raw = b.get();
    _buffers.push_back(std::move(b));
    return raw;
}

void MainWindow::attachTab(Buffer* b, int position)
{
    b->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(b->page, -1, 0);
    gtk_widget_show(b->page);

    GtkWidget* ebox = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), FALSE);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    b->tabLabel = gtk_label_new(b->displayName().c_str());
    gtk_style_context_add_class(gtk_widget_get_style_context(b->tabLabel), "cam-tab-name");
    GtkWidget* close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_style_context_add_class(gtk_widget_get_style_context(close), "cam-tab-close");
    gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
    gtk_widget_set_focus_on_click(close, FALSE);
    gtk_box_pack_start(GTK_BOX(box), b->tabLabel, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), close, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(ebox), box);
    gtk_widget_show_all(ebox);
    b->tabBox = ebox;
    g_object_set_data(G_OBJECT(ebox), "cam-buffer-id", GINT_TO_POINTER(b->id));
    g_object_set_data(G_OBJECT(close), "cam-buffer-id", GINT_TO_POINTER(b->id));
    g_object_set_data(G_OBJECT(b->page), "cam-buffer-id", GINT_TO_POINTER(b->id));

    g_signal_connect(close, "clicked", CAM_CB(+[](GtkWidget* w, gpointer self) {
        auto* mw = static_cast<MainWindow*>(self);
        int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cam-buffer-id"));
        if (Buffer* b = mw->bufferById(id)) mw->closeBuffer(b);
    }), this);
    g_signal_connect(ebox, "button-press-event", CAM_CB(+[](GtkWidget* w, GdkEventButton* ev, gpointer self) -> gboolean {
        auto* mw = static_cast<MainWindow*>(self);
        int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cam-buffer-id"));
        Buffer* b = mw->bufferById(id);
        if (!b) return FALSE;
        if (ev->type == GDK_BUTTON_PRESS && ev->button == 2) { mw->closeBuffer(b); return TRUE; }
        if (ev->type == GDK_2BUTTON_PRESS && ev->button == 1 && Settings::get().getBool("tab.dblClickClose", false)) { mw->closeBuffer(b); return TRUE; }
        if (ev->type == GDK_BUTTON_PRESS && ev->button == 3) { mw->tabContextMenu(b, ev); return TRUE; }
        return FALSE;
    }), this);

    _switching = true;
    int idx = gtk_notebook_insert_page(GTK_NOTEBOOK(_notebook), b->page, ebox, position);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(_notebook), b->page, TRUE);
    _switching = false;
    // keep _buffers in notebook order
    auto it = std::find_if(_buffers.begin(), _buffers.end(), [&](auto& p) { return p.get() == b; });
    if (it != _buffers.end()) {
        auto up = std::move(*it);
        _buffers.erase(it);
        _buffers.insert(_buffers.begin() + std::min<size_t>(size_t(idx), _buffers.size()), std::move(up));
    }
    updateTab(b);
}

Buffer* MainWindow::bufferById(int id) const
{
    for (auto& b : _buffers) if (b->id == id) return b.get();
    return nullptr;
}

Buffer* MainWindow::bufferByPath(const std::string& path) const
{
    for (auto& b : _buffers) if (!b->path.empty() && b->path == path) return b.get();
    return nullptr;
}

int MainWindow::indexOf(const Buffer* b) const
{
    for (size_t i = 0; i < _buffers.size(); ++i) if (_buffers[i].get() == b) return int(i);
    return -1;
}

void MainWindow::updateTab(Buffer* b)
{
    if (!b || !b->tabLabel) return;
    gtk_label_set_text(GTK_LABEL(b->tabLabel), b->displayName().c_str());
    GtkStyleContext* sc = gtk_widget_get_style_context(b->tabBox);
    if (b->dirty) gtk_style_context_add_class(sc, "cam-tab-dirty"); else gtk_style_context_remove_class(sc, "cam-tab-dirty");
    if (b->readOnly || b->userReadOnly) gtk_style_context_add_class(sc, "cam-tab-ro"); else gtk_style_context_remove_class(sc, "cam-tab-ro");
    gtk_widget_set_tooltip_text(b->tabBox, b->path.empty() ? b->untitledName.c_str() : b->path.c_str());
}

void MainWindow::updateTitle()
{
    if (!_current) return;
    std::string t = (_current->dirty ? "*" : "") + (_current->path.empty() ? _current->untitledName : _current->path) + " - Camelion++";
    if (_recording) t += "  [recording macro]";
    gtk_window_set_title(GTK_WINDOW(_window), t.c_str());
}

void MainWindow::onSwitchPage(GtkNotebook*, GtkWidget* page, guint, gpointer self)
{
    auto* mw = static_cast<MainWindow*>(self);
    if (mw->_switching || mw->_closing) return;
    int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(page), "cam-buffer-id"));
    if (Buffer* b = mw->bufferById(id)) if (b != mw->_current) mw->activate(b);
}

void MainWindow::onPageReordered(GtkNotebook* nb, GtkWidget*, guint, gpointer self)
{
    auto* mw = static_cast<MainWindow*>(self);
    std::vector<std::unique_ptr<Buffer>> ordered;
    int n = gtk_notebook_get_n_pages(nb);
    for (int i = 0; i < n; ++i) {
        int id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(nb, i)), "cam-buffer-id"));
        auto it = std::find_if(mw->_buffers.begin(), mw->_buffers.end(), [&](auto& p) { return p && p->id == id; });
        if (it != mw->_buffers.end()) ordered.push_back(std::move(*it));
    }
    for (auto& p : mw->_buffers) if (p) ordered.push_back(std::move(p));
    mw->_buffers = std::move(ordered);
    mw->rebuildWindowMenu();
}

void MainWindow::activate(Buffer* b)
{
    if (!b) return;
    if (_current && _current != b) {
        _editor->saveViewState(*_current);
        _editor->call(SCI_AUTOCCANCEL);
        _editor->call(SCI_CALLTIPCANCEL);
    }
    bool changed = _current != b;
    _current = b;
    if (changed) {
        _editor->call(SCI_SETDOCPOINTER, 0, b->doc);
    }
    _editor->call(SCI_SETCODEPAGE, SC_CP_UTF8);
    _editor->call(SCI_SETEOLMODE, b->eol == EolType::CRLF ? SC_EOL_CRLF : b->eol == EolType::CR ? SC_EOL_CR : SC_EOL_LF);
    _editor->call(SCI_SETREADONLY, b->readOnly || b->userReadOnly);
    _editor->applyLanguage(*b);
    if (_editor->call(SCI_GETCHANGEHISTORY) == 0 && !_editor->call(SCI_CANUNDO))
        _editor->call(SCI_SETCHANGEHISTORY, SC_CHANGE_HISTORY_ENABLED | SC_CHANGE_HISTORY_MARKERS);
    if (changed) _editor->restoreViewState(*b);
    _editor->updateLineNumberWidth();
    int idx = gtk_notebook_page_num(GTK_NOTEBOOK(_notebook), b->page);
    if (idx >= 0 && gtk_notebook_get_current_page(GTK_NOTEBOOK(_notebook)) != idx) {
        _switching = true;
        gtk_notebook_set_current_page(GTK_NOTEBOOK(_notebook), idx);
        _switching = false;
    }
    _autoComplete->setLanguage(b->langType);
    updateStatusBar();
    updateTitle();
    syncCheckItems();
    if (_funcList) _funcList->refreshSoon();
    gtk_widget_grab_focus(_editor->widget());
    if (changed) notifyPlugins(NPPN_BUFFERACTIVATED, uptr_t(b->id));
}

Buffer* MainWindow::newDocument()
{
    sptr_t doc = hiddenEditor().call(SCI_CREATEDOCUMENT, 0, SC_DOCUMENTOPTION_DEFAULT);
    Buffer* b = createBuffer(doc);
    b->untitledName = "new " + std::to_string(++_untitledCounter);
    b->eol = EolType(Settings::get().getInt("newdoc.eol", int(EolType::LF)));
    std::string enc = Settings::get().getString("newdoc.encoding", "utf8");
    b->mode = enc == "utf8bom" ? UniMode::Utf8Bom : UniMode::Utf8;
    if (auto l = langByName(Settings::get().getString("newdoc.lang", "normal"))) b->langType = l->langType;
    int pos = _current ? indexOf(_current) + 1 : -1;
    attachTab(b, pos);
    activate(b);
    rebuildWindowMenu();
    return b;
}

// A document built by an ILoader has undo collection switched off: turn it on, mark
// the save point and start change history, as for a document typed from scratch.
static void prepareLoadedDocument(sptr_t doc)
{
    Editor& h = hiddenEditor();
    sptr_t keep = h.call(SCI_GETDOCPOINTER);
    h.call(SCI_ADDREFDOCUMENT, 0, keep);
    h.call(SCI_SETDOCPOINTER, 0, doc);
    h.call(SCI_SETUNDOCOLLECTION, 1);
    h.call(SCI_EMPTYUNDOBUFFER);
    h.call(SCI_SETSAVEPOINT);
    h.call(SCI_SETCHANGEHISTORY, SC_CHANGE_HISTORY_ENABLED | SC_CHANGE_HISTORY_MARKERS);
    h.call(SCI_SETDOCPOINTER, 0, keep);
    h.call(SCI_RELEASEDOCUMENT, 0, keep);
}

static sptr_t loadDocument(Editor& ed, const std::string& utf8)
{
    auto* loader = reinterpret_cast<Scintilla::ILoader*>(ed.call(SCI_CREATELOADER, utf8.size() + 1, SC_DOCUMENTOPTION_DEFAULT));
    if (!loader) return 0;
    const size_t chunk = 1 << 20;
    for (size_t off = 0; off < utf8.size(); off += chunk) {
        size_t n = std::min(chunk, utf8.size() - off);
        if (loader->AddData(utf8.data() + off, Sci_Position(n)) != SC_STATUS_OK) { loader->Release(); return 0; }
    }
    return reinterpret_cast<sptr_t>(loader->ConvertToDocument());
}

Buffer* MainWindow::openFile(const std::string& rawPath, bool activateIt, bool quietIfMissing)
{
    if (rawPath.empty()) return nullptr;
    std::string path = rawPath;
    if (path.compare(0, 7, "file://") == 0) {
        gchar* p = g_filename_from_uri(path.c_str(), nullptr, nullptr);
        if (p) { path = p; g_free(p); }
    }
    path = absolutePath(path);
    if (Buffer* existing = bufferByPath(path)) { if (activateIt) activate(existing); return existing; }
    if (paths::dirExists(path)) {
        showMessage(GTK_MESSAGE_INFO, "\"" + path + "\" is a folder.", "Use File > Open to choose files inside it.");
        return nullptr;
    }
    time_t mtime = 0; long long size = 0;
    if (!statFile(path, mtime, size)) {
        if (quietIfMissing) return nullptr;
        int r = askQuestion("\"" + path + "\" doesn't exist.", "Create it?", {{"_Cancel", 0}, {"C_reate", 1}});
        if (r != 1) return nullptr;
        if (!paths::writeFileAtomic(path, "")) { showMessage(GTK_MESSAGE_ERROR, "Cannot create \"" + path + "\"."); return nullptr; }
        statFile(path, mtime, size);
    }
    if (size > (long long)(1536) * 1024 * 1024) {
        showMessage(GTK_MESSAGE_ERROR, "File is too big to be opened by Camelion++", path);
        return nullptr;
    }
    notifyPlugins(NPPN_FILEBEFORELOAD, 0);
    notifyPlugins(NPPN_FILEBEFOREOPEN, 0);
    bool ok = false;
    std::string raw = paths::readFile(path, &ok);
    if (!ok) { showMessage(GTK_MESSAGE_ERROR, "Cannot open file \"" + path + "\".", g_strerror(errno)); return nullptr; }
    DecodedText dt = decodeText(raw, Settings::get().getString("encoding.ansiCharset", "WINDOWS-1252"));
    // SCI_CREATELOADER resets the fold state of the view it is sent to: use the hidden view
    sptr_t doc = loadDocument(hiddenEditor(), dt.utf8);
    if (!doc) { showMessage(GTK_MESSAGE_ERROR, "Not enough memory to open \"" + path + "\"."); return nullptr; }
    prepareLoadedDocument(doc);

    // replace the pristine "new 1" document, like Notepad++ does
    Buffer* replaceMe = nullptr;
    if (_buffers.size() == 1 && _current && _current->isUntitled() && !_current->dirty &&
        _editor->call(SCI_GETLENGTH) == 0 && !_editor->call(SCI_CANUNDO))
        replaceMe = _current;

    Buffer* b = createBuffer(doc);
    b->path = path;
    b->mode = dt.mode;
    b->charset = dt.charset;
    b->eol = detectEol(dt.utf8, EolType(Settings::get().getInt("newdoc.eol", int(EolType::LF))));
    b->mtime = mtime; b->fileSize = size;
    b->readOnly = access(path.c_str(), W_OK) != 0;
    int lt = Parameters::get().langTypeFromFileName(path);
    if (lt == 0) lt = Parameters::get().langTypeFromContent(dt.utf8.substr(0, 512));
    b->langType = lt;
    int pos = _current ? indexOf(_current) + 1 : -1;
    attachTab(b, pos);
    Settings::get().addRecent(path);
    rebuildRecentMenu();
    if (activateIt || replaceMe) activate(b);
    if (replaceMe) closeBuffer(replaceMe, false);
    rebuildWindowMenu();
    if (dt.lossy) setStatusText(STATUSBAR_DOC_TYPE, "Warning: invalid byte sequences were replaced");
    notifyPlugins(NPPN_FILEOPENED, uptr_t(b->id));
    return b;
}

std::string MainWindow::promptSaveAsPath(Buffer* b, const char* title)
{
    GtkWidget* dlg = gtk_file_chooser_dialog_new(title, GTK_WINDOW(_window), GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    if (!b->path.empty()) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dlg), b->path.c_str());
    else {
        std::string dir = Settings::get().getString("dialog.lastDir", "");
        if (!dir.empty()) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), dir.c_str());
        std::string name = b->untitledName;
        const LangInfo* li = Parameters::get().lang(langByType(b->langType)->name);
        if (li && !li->exts.empty()) name += "." + li->exts[0]; else name += ".txt";
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), name.c_str());
    }
    std::string result;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (f) { result = f; g_free(f); Settings::get().set("dialog.lastDir", paths::dirName(result)); }
    }
    gtk_widget_destroy(dlg);
    return result;
}

static bool writeBufferTo(MainWindow* mw, Buffer* b, const std::string& path, std::string& err)
{
    std::string utf8 = withDoc(mw, b, [](Editor& e) { return e.text(); });
    std::string out;
    if (!encodeText(utf8, b->mode, b->charset, out, &err)) return false;
    return paths::writeFileAtomic(path, out, &err);
}

bool MainWindow::saveBuffer(Buffer* b)
{
    if (!b) return false;
    if (b->isUntitled()) {
        std::string p = promptSaveAsPath(b, "Save As");
        if (p.empty()) return false;
        return saveBufferAs(b, p, false);
    }
    notifyPlugins(NPPN_FILEBEFORESAVE, uptr_t(b->id));
    if (Settings::get().getBool("save.trimTrailing", false))
        withDoc(this, b, [](Editor& e) { trimTrailingInEditor(e); return 0; });
    std::string err;
    if (!writeBufferTo(this, b, b->path, err)) {
        showMessage(GTK_MESSAGE_ERROR, "Please check if this file is opened in another program, or you have the permission to write here.",
                    "Save failed for \"" + b->path + "\": " + err);
        return false;
    }
    withDoc(this, b, [](Editor& e) { e.call(SCI_SETSAVEPOINT); return 0; });
    b->dirty = false;
    b->missing = false;
    statFile(b->path, b->mtime, b->fileSize);
    b->readOnly = false;
    updateTab(b);
    if (b == _current) updateTitle();
    notifyPlugins(NPPN_FILESAVED, uptr_t(b->id));
    return true;
}

bool MainWindow::saveBufferAs(Buffer* b, const std::string& rawPath, bool asCopy)
{
    if (!b || rawPath.empty()) return false;
    std::string path = absolutePath(rawPath);
    if (!asCopy) {
        Buffer* other = bufferByPath(path);
        if (other && other != b) {
            showMessage(GTK_MESSAGE_WARNING, "The file is already opened in Camelion++.", "Close it first, then save again.");
            return false;
        }
    }
    std::string err;
    if (!writeBufferTo(this, b, path, err)) {
        showMessage(GTK_MESSAGE_ERROR, "Save failed for \"" + path + "\"", err);
        return false;
    }
    if (asCopy) return true;
    bool wasUntitled = b->isUntitled();
    b->path = path;
    withDoc(this, b, [](Editor& e) { e.call(SCI_SETSAVEPOINT); return 0; });
    b->dirty = false;
    statFile(path, b->mtime, b->fileSize);
    if (!b->langLocked && (wasUntitled || b->langType == 0)) {
        int lt = Parameters::get().langTypeFromFileName(path);
        if (lt != 0 || wasUntitled) setLanguage(b, lt);
    }
    Settings::get().addRecent(path);
    rebuildRecentMenu();
    updateTab(b);
    if (b == _current) { updateTitle(); updateStatusBar(); }
    rebuildWindowMenu();
    notifyPlugins(NPPN_FILESAVED, uptr_t(b->id));
    return true;
}

bool MainWindow::saveAll()
{
    bool ok = true;
    for (auto& b : _buffers) if (b->dirty) ok = saveBuffer(b.get()) && ok;
    return ok;
}

bool MainWindow::closeBuffer(Buffer* b, bool askToSave)
{
    if (!b) return false;
    if (askToSave && b->dirty) {
        if (b != _current) activate(b);
        int r = askQuestion("Save file \"" + (b->path.empty() ? b->untitledName : b->path) + "\" ?", "",
                            {{"_Cancel", 0}, {"_No", 1}, {"_Yes", 2}});
        if (r == 0) return false;
        if (r == 2 && !saveBuffer(b)) return false;
    }
    notifyPlugins(NPPN_FILEBEFORECLOSE, uptr_t(b->id));
    startMonitoring(b, false);
    if (_buffers.size() == 1) {
        // never leave the editor without a document
        Buffer* fresh = newDocument();
        (void)fresh;
    }
    if (b == _current) {
        int i = indexOf(b);
        Buffer* next = (i + 1 < int(_buffers.size())) ? _buffers[size_t(i + 1)].get() : (i > 0 ? _buffers[size_t(i - 1)].get() : nullptr);
        if (next) activate(next);
    }
    if (!b->path.empty()) {
        _closedFiles.erase(std::remove(_closedFiles.begin(), _closedFiles.end(), b->path), _closedFiles.end());
        _closedFiles.push_back(b->path);
    }
    int id = b->id;
    _closing = true;
    int page = gtk_notebook_page_num(GTK_NOTEBOOK(_notebook), b->page);
    if (page >= 0) gtk_notebook_remove_page(GTK_NOTEBOOK(_notebook), page);
    _closing = false;
    _editor->call(SCI_RELEASEDOCUMENT, 0, b->doc);
    _buffers.erase(std::remove_if(_buffers.begin(), _buffers.end(), [&](auto& p) { return p.get() == b; }), _buffers.end());
    rebuildWindowMenu();
    notifyPlugins(NPPN_FILECLOSED, uptr_t(id));
    return true;
}

bool MainWindow::closeAll(Buffer* except)
{
    std::vector<Buffer*> list;
    for (auto& b : _buffers) if (b.get() != except) list.push_back(b.get());
    for (Buffer* b : list) if (!closeBuffer(b)) return false;
    return true;
}

bool MainWindow::reloadBuffer(Buffer* b, bool askIfDirty)
{
    if (!b || b->isUntitled()) return false;
    if (askIfDirty && b->dirty) {
        int r = askQuestion("Reload \"" + b->path + "\"?", "You will lose the changes you've made.", {{"_Cancel", 0}, {"_Reload", 1}});
        if (r != 1) return false;
    }
    bool ok = false;
    std::string raw = paths::readFile(b->path, &ok);
    if (!ok) { showMessage(GTK_MESSAGE_ERROR, "Cannot read \"" + b->path + "\"."); return false; }
    DecodedText dt = (b->mode == UniMode::Ansi) ? decodeAs(raw, UniMode::Ansi, b->charset)
                                                : decodeText(raw, Settings::get().getString("encoding.ansiCharset", "WINDOWS-1252"));
    b->mode = dt.mode; b->charset = dt.charset;
    withDoc(this, b, [&](Editor& e) {
        sptr_t caret = e.call(SCI_GETCURRENTPOS);
        sptr_t first = e.call(SCI_GETFIRSTVISIBLELINE);
        bool ro = e.call(SCI_GETREADONLY);
        e.call(SCI_SETREADONLY, 0);
        e.callStr(SCI_SETTEXT, 0, dt.utf8.c_str());
        e.call(SCI_EMPTYUNDOBUFFER);
        e.call(SCI_SETSAVEPOINT);
        e.call(SCI_SETREADONLY, ro);
        e.call(SCI_GOTOPOS, uptr_t(std::min(caret, e.call(SCI_GETLENGTH))));
        e.call(SCI_SETFIRSTVISIBLELINE, uptr_t(first));
        return 0;
    });
    b->dirty = false; b->missing = false;
    statFile(b->path, b->mtime, b->fileSize);
    updateTab(b);
    if (b == _current) { updateTitle(); updateStatusBar(); }
    return true;
}

void MainWindow::setLanguage(Buffer* b, int langType)
{
    if (!b) return;
    b->langType = langType;
    if (b == _current) {
        _editor->applyLanguage(*b);
        _autoComplete->setLanguage(langType);
        updateStatusBar();
        syncCheckItems();
        if (_funcList) _funcList->refreshSoon();
    }
    notifyPlugins(NPPN_LANGCHANGED, uptr_t(b->id));
}

void MainWindow::setEol(Buffer* b, EolType e)
{
    if (!b) return;
    b->eol = e;
    int mode = e == EolType::CRLF ? SC_EOL_CRLF : e == EolType::CR ? SC_EOL_CR : SC_EOL_LF;
    withDoc(this, b, [&](Editor& ed) { ed.call(SCI_SETEOLMODE, uptr_t(mode)); ed.call(SCI_CONVERTEOLS, uptr_t(mode)); return 0; });
    if (b == _current) updateStatusBar();
}

void MainWindow::reinterpretEncoding(Buffer* b, UniMode m, const std::string& charset)
{
    if (!b) return;
    if (b->isUntitled() || !paths::fileExists(b->path)) { b->mode = m; b->charset = charset; updateStatusBar(); return; }
    if (b->dirty) {
        int r = askQuestion("You should save the current modification.", "All the saved modifications can not be undone.\n\nContinue?",
                            {{"_Cancel", 0}, {"_Discard changes", 1}});
        if (r != 1) return;
    }
    bool ok = false;
    std::string raw = paths::readFile(b->path, &ok);
    if (!ok) return;
    DecodedText dt = decodeAs(raw, m, charset);
    b->mode = m; b->charset = dt.charset;
    withDoc(this, b, [&](Editor& e) {
        e.callStr(SCI_SETTEXT, 0, dt.utf8.c_str());
        e.call(SCI_EMPTYUNDOBUFFER);
        e.call(SCI_SETSAVEPOINT);
        return 0;
    });
    b->dirty = false;
    updateTab(b);
    updateStatusBar();
    syncCheckItems();
}

void MainWindow::convertEncoding(Buffer* b, UniMode m, const std::string& charset)
{
    if (!b) return;
    if (b->mode == m && (m != UniMode::Ansi || b->charset == charset)) return;
    b->mode = m; b->charset = (m == UniMode::Ansi) ? charset : (m == UniMode::Utf16LE ? "UTF-16LE" : m == UniMode::Utf16BE ? "UTF-16BE" : "UTF-8");
    // the text itself doesn't change, only the bytes on disk: mark dirty
    b->dirty = true;
    updateTab(b); updateTitle(); updateStatusBar(); syncCheckItems();
}

// ------------------------------------------------------------------ status bar
void MainWindow::buildStatusBar()
{
    _statusBar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(_statusBar), "cam-status");
    const int widths[6] = {28, 30, 40, 16, 14, 4};
    for (int i = 0; i < 6; ++i) {
        _statusLabels[i] = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(_statusLabels[i]), 0);
        gtk_label_set_width_chars(GTK_LABEL(_statusLabels[i]), widths[i]);
        gtk_label_set_ellipsize(GTK_LABEL(_statusLabels[i]), PANGO_ELLIPSIZE_END);
        GtkWidget* ebox = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(ebox), _statusLabels[i]);
        gtk_box_pack_start(GTK_BOX(_statusBar), ebox, i == 0, TRUE, 0);
        if (i > 0) gtk_box_pack_start(GTK_BOX(_statusBar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
        if (i == 3 || i == 4) {
            g_object_set_data(G_OBJECT(ebox), "cam-field", GINT_TO_POINTER(i));
            g_signal_connect(ebox, "button-press-event", CAM_CB(+[](GtkWidget* w, GdkEventButton* ev, gpointer self) -> gboolean {
                auto* mw = static_cast<MainWindow*>(self);
                int field = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cam-field"));
                GtkWidget* menu = gtk_menu_new();
                auto add = [&](const char* label, int cmdId) {
                    GtkWidget* it = gtk_menu_item_new_with_label(label);
                    g_object_set_data(G_OBJECT(it), "cam-cmd", GINT_TO_POINTER(cmdId));
                    g_signal_connect(it, "activate", CAM_CB(+[](GtkWidget* i2, gpointer s) {
                        static_cast<MainWindow*>(s)->runCommand(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(i2), "cam-cmd")));
                    }), mw);
                    gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
                };
                if (field == 3) {
                    add("Windows (CR LF)", cmd::FORMAT_TODOS); add("Unix (LF)", cmd::FORMAT_TOUNIX); add("Macintosh (CR)", cmd::FORMAT_TOMAC);
                } else {
                    add("Convert to UTF-8", cmd::FORMAT_CONV2_AS_UTF_8); add("Convert to UTF-8-BOM", cmd::FORMAT_CONV2_UTF_8);
                    add("Convert to UTF-16 BE BOM", cmd::FORMAT_CONV2_UTF_16BE); add("Convert to UTF-16 LE BOM", cmd::FORMAT_CONV2_UTF_16LE);
                    add("Convert to ANSI", cmd::FORMAT_CONV2_ANSI);
                }
                gtk_widget_show_all(menu);
                gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(ev));
                return TRUE;
            }), this);
        }
    }
    GtkWidget* vbox = gtk_widget_get_parent(_menuBar);
    gtk_box_pack_end(GTK_BOX(vbox), _statusBar, FALSE, FALSE, 0);
    gtk_widget_show_all(_statusBar);
    if (!Settings::get().getBool("view.statusBar", true)) gtk_widget_hide(_statusBar);
    if (!Settings::get().getBool("view.toolbar", true)) gtk_widget_hide(_toolbar);
}

void MainWindow::setStatusText(int field, const std::string& text)
{
    if (field >= 0 && field < 6 && _statusLabels[field]) {
        gtk_label_set_text(GTK_LABEL(_statusLabels[field]), text.c_str());
        gtk_widget_set_tooltip_text(_statusLabels[field], text.c_str());
    }
}

void MainWindow::updateStatusBar()
{
    if (!_current || !_statusLabels[0]) return;
    Editor& e = *_editor;
    setStatusText(0, langByType(_current->langType)->longName);
    char buf[256];
    std::snprintf(buf, sizeof buf, "length : %'ld    lines : %'ld", long(e.call(SCI_GETLENGTH)), long(e.call(SCI_GETLINECOUNT)));
    setStatusText(1, buf);
    sptr_t pos = e.call(SCI_GETCURRENTPOS);
    sptr_t line = e.call(SCI_LINEFROMPOSITION, uptr_t(pos));
    sptr_t col = e.call(SCI_GETCOLUMN, uptr_t(pos));
    int nsel = int(e.call(SCI_GETSELECTIONS));
    std::string sel;
    if (nsel > 1) {
        long total = 0;
        for (int i = 0; i < nsel; ++i) total += long(e.call(SCI_GETSELECTIONNEND, uptr_t(i)) - e.call(SCI_GETSELECTIONNSTART, uptr_t(i)));
        std::snprintf(buf, sizeof buf, "Sel : %d sel. | %ld", nsel, total);
        sel = buf;
    } else {
        sptr_t a = e.call(SCI_GETSELECTIONSTART), b = e.call(SCI_GETSELECTIONEND);
        long chars = long(e.call(SCI_COUNTCHARACTERS, uptr_t(a), b));
        long lines = (b > a) ? long(e.call(SCI_LINEFROMPOSITION, uptr_t(b)) - e.call(SCI_LINEFROMPOSITION, uptr_t(a)) + 1) : 0;
        std::snprintf(buf, sizeof buf, "Sel : %ld | %ld", chars, lines);
        sel = buf;
    }
    std::snprintf(buf, sizeof buf, "Ln : %ld    Col : %ld    Pos : %ld    %s", long(line + 1), long(col + 1), long(pos + 1), sel.c_str());
    setStatusText(2, buf);
    setStatusText(3, eolLabel(_current->eol));
    setStatusText(4, uniModeLabel(_current->mode, _current->charset));
    setStatusText(5, e.call(SCI_GETOVERTYPE) ? "OVR" : "INS");
}

// ------------------------------------------------------------------ dialogs
void MainWindow::showMessage(GtkMessageType type, const std::string& primary, const std::string& secondary)
{
    GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(_window), GTK_DIALOG_MODAL, type, GTK_BUTTONS_OK, "%s", primary.c_str());
    if (!secondary.empty()) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", secondary.c_str());
    gtk_window_set_title(GTK_WINDOW(d), "Camelion++");
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

int MainWindow::askQuestion(const std::string& primary, const std::string& secondary, const std::vector<std::pair<std::string, int>>& buttons)
{
    GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(_window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", primary.c_str());
    if (!secondary.empty()) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", secondary.c_str());
    gtk_window_set_title(GTK_WINDOW(d), "Camelion++");
    for (auto& [label, id] : buttons) gtk_dialog_add_button(GTK_DIALOG(d), label.c_str(), id);
    if (!buttons.empty()) gtk_dialog_set_default_response(GTK_DIALOG(d), buttons.back().second);
    int r = gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
    return r < 0 ? 0 : r;
}

FindReplaceDlg& MainWindow::findDlg()
{
    if (!_findDlg) _findDlg = std::make_unique<FindReplaceDlg>(this);
    return *_findDlg;
}

// ------------------------------------------------------------------ panels
int MainWindow::addSidePanel(const std::string& title, GtkWidget* w)
{
    static int nextId = 1;
    int id = nextId++;
    GtkWidget* label = gtk_label_new(title.c_str());
    gtk_widget_show_all(w);
    gtk_notebook_append_page(GTK_NOTEBOOK(_sideNotebook), w, label);
    _sidePanels[id] = w;
    gtk_widget_hide(w);
    return id;
}

void MainWindow::showSidePanel(int id, bool show)
{
    auto it = _sidePanels.find(id);
    if (it == _sidePanels.end()) return;
    if (show) gtk_widget_show(it->second); else gtk_widget_hide(it->second);
    bool any = false;
    for (auto& [pid, w] : _sidePanels) if (gtk_widget_get_visible(w)) any = true;
    if (any) {
        gtk_widget_show(_sideNotebook);
        if (show) gtk_notebook_set_current_page(GTK_NOTEBOOK(_sideNotebook), gtk_notebook_page_num(GTK_NOTEBOOK(_sideNotebook), it->second));
        if (gtk_paned_get_position(GTK_PANED(_sidePaned)) < 80) gtk_paned_set_position(GTK_PANED(_sidePaned), 240);
    } else gtk_widget_hide(_sideNotebook);
}

void MainWindow::toggleSearchResults(bool show)
{
    if (show) {
        gtk_widget_set_no_show_all(_resultsBox, FALSE);
        gtk_widget_show_all(_resultsBox);
        gtk_widget_set_no_show_all(_resultsBox, TRUE);
        int h = gtk_widget_get_allocated_height(_bottomPaned);
        if (h > 300 && gtk_paned_get_position(GTK_PANED(_bottomPaned)) > h - 60) gtk_paned_set_position(GTK_PANED(_bottomPaned), h - 200);
    } else gtk_widget_hide(_resultsBox);
}

void MainWindow::jumpTo(Buffer* b, sptr_t line, sptr_t start, sptr_t end)
{
    if (!b) return;
    activate(b);
    Editor& e = *_editor;
    if (start >= 0 && end >= start && end <= e.call(SCI_GETLENGTH)) {
        e.call(SCI_ENSUREVISIBLE, uptr_t(e.call(SCI_LINEFROMPOSITION, uptr_t(start))));
        e.call(SCI_SETSEL, uptr_t(start), end);
    } else {
        e.call(SCI_ENSUREVISIBLE, uptr_t(line));
        e.call(SCI_GOTOLINE, uptr_t(line));
    }
    e.call(SCI_SCROLLCARET);
    e.call(SCI_VERTICALCENTRECARET);
    gtk_widget_grab_focus(e.widget());
}

// ------------------------------------------------------------------ theme
void MainWindow::refreshAllStyles()
{
    if (_current) _editor->applyLanguage(*_current);
    applyThemeToChrome();
    notifyPlugins(NPPN_WORDSTYLESUPDATED, 0);
}

// Does the GTK theme ship a dark variant? (Adwaita has one built in.)
static bool gtkThemeHasDark(const std::string& name)
{
    if (name == "Adwaita" || name.empty()) return true;
    std::vector<std::string> dirs = {paths::join(g_get_user_data_dir(), "themes"), paths::join(g_get_home_dir(), ".themes")};
    for (const gchar* const* d = g_get_system_data_dirs(); d && *d; ++d) dirs.push_back(paths::join(*d, "themes"));
    for (auto& d : dirs)
        if (paths::fileExists(paths::join(d, name + "/gtk-3.0/gtk-dark.css"))) return true;
    return false;
}

void MainWindow::applyThemeToChrome()
{
    bool dark = Parameters::get().isDarkTheme();
    std::string mode = Settings::get().getString("ui.darkMode", "follow-theme");
    bool wantDark = mode == "dark" || (mode == "follow-theme" && dark);
    GtkSettings* gs = gtk_settings_get_default();
    gboolean cur = FALSE;
    g_object_get(gs, "gtk-application-prefer-dark-theme", &cur, nullptr);
    // Remember the user's GTK theme; fall back to Adwaita when it has no dark variant.
    static std::string userTheme;
    if (userTheme.empty()) { gchar* t = nullptr; g_object_get(gs, "gtk-theme-name", &t, nullptr); userTheme = t ? t : "Adwaita"; g_free(t); }
    std::string wantTheme = (wantDark && !gtkThemeHasDark(userTheme)) ? "Adwaita" : userTheme;
    gchar* curTheme = nullptr;
    g_object_get(gs, "gtk-theme-name", &curTheme, nullptr);
    if (wantTheme != (curTheme ? curTheme : "")) g_object_set(gs, "gtk-theme-name", wantTheme.c_str(), nullptr);
    g_free(curTheme);
    if (bool(cur) != wantDark) {
        g_object_set(gs, "gtk-application-prefer-dark-theme", wantDark, nullptr);
        notifyPlugins(NPPN_DARKMODECHANGED, 0);
    }
}

// ------------------------------------------------------------------ Scintilla notifications
void MainWindow::onSciNotify(GtkWidget*, gint, SCNotification* n, gpointer self)
{
    static_cast<MainWindow*>(self)->onScintillaNotify(n);
}

void MainWindow::onScintillaNotify(SCNotification* n)
{
    switch (n->nmhdr.code) {
    case SCN_SAVEPOINTREACHED:
    case SCN_SAVEPOINTLEFT:
        if (_current) {
            _current->dirty = n->nmhdr.code == SCN_SAVEPOINTLEFT;
            updateTab(_current);
            updateTitle();
        }
        break;
    case SCN_UPDATEUI:
        onUpdateUI(n);
        break;
    case SCN_CHARADDED:
        onCharAdded(n->ch);
        break;
    case SCN_MODIFIED:
        if (n->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) {
            if (n->linesAdded != 0) _editor->updateLineNumberWidth();
            if (_funcList) _funcList->refreshSoon();
        }
        break;
    case SCN_MARGINCLICK:
        if (n->margin == MARGIN_SYMBOL || n->margin == MARGIN_LINENUM) {
            sptr_t line = _editor->call(SCI_LINEFROMPOSITION, uptr_t(n->position));
            if (_editor->call(SCI_MARKERGET, uptr_t(line)) & (1 << MARK_BOOKMARK))
                _editor->call(SCI_MARKERDELETE, uptr_t(line), MARK_BOOKMARK);
            else _editor->call(SCI_MARKERADD, uptr_t(line), MARK_BOOKMARK);
        }
        break;
    case SCN_ZOOM:
        _editor->updateLineNumberWidth();
        Settings::get().set("view.zoom", int(_editor->call(SCI_GETZOOM)));
        break;
    case SCN_MACRORECORD:
        if (_recording) {
            MacroStep s{int(n->message), n->wParam, n->lParam, {}, false};
            switch (n->message) {
            case SCI_REPLACESEL: case SCI_INSERTTEXT: case SCI_SEARCHNEXT: case SCI_SEARCHPREV:
                if (n->lParam) { s.text = reinterpret_cast<const char*>(n->lParam); s.hasText = true; }
                break;
            case SCI_ADDTEXT: case SCI_APPENDTEXT:
                if (n->lParam) { s.text.assign(reinterpret_cast<const char*>(n->lParam), size_t(n->wParam)); s.hasText = true; }
                break;
            default: break;
            }
            _macro.push_back(std::move(s));
        }
        break;
    case SCN_URIDROPPED:
        if (n->text) {
            gchar** uris = g_uri_list_extract_uris(n->text);
            for (gchar** u = uris; u && *u; ++u) openFile(*u);
            g_strfreev(uris);
        }
        break;
    case SCN_MODIFYATTEMPTRO:
        setStatusText(0, "This document is read-only");
        break;
    case SCN_AUTOCSELECTION:
        break;
    default: break;
    }
    // Forward every Scintilla notification to the plugins, as Notepad++ does.
    if (_plugins) {
        SCNotification copy = *n;
        copy.nmhdr.hwndFrom = _plugins->scintillaHandle();
        _plugins->notify(&copy);
    }
}

void MainWindow::onUpdateUI(SCNotification* n)
{
    if (n->updated & (SC_UPDATE_SELECTION | SC_UPDATE_CONTENT)) {
        updateStatusBar();
        braceMatch();
        if (Settings::get().getBool("smartHighlight", true)) smartHighlight();
    }
}

void MainWindow::braceMatch()
{
    Editor& e = *_editor;
    sptr_t pos = e.call(SCI_GETCURRENTPOS);
    auto isBrace = [](int c) { return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}'; };
    sptr_t bracePos = -1;
    if (pos > 0 && isBrace(int(e.call(SCI_GETCHARAT, uptr_t(pos - 1))))) bracePos = pos - 1;
    else if (isBrace(int(e.call(SCI_GETCHARAT, uptr_t(pos))))) bracePos = pos;
    if (bracePos < 0) { e.call(SCI_BRACEHIGHLIGHT, uptr_t(-1), -1); e.call(SCI_SETHIGHLIGHTGUIDE, 0); return; }
    sptr_t match = e.call(SCI_BRACEMATCH, uptr_t(bracePos), 0);
    if (match < 0) { e.call(SCI_BRACEBADLIGHT, uptr_t(bracePos)); e.call(SCI_SETHIGHLIGHTGUIDE, 0); return; }
    e.call(SCI_BRACEHIGHLIGHT, uptr_t(bracePos), match);
    if (e.call(SCI_GETINDENTATIONGUIDES) != SC_IV_NONE) {
        sptr_t col = std::min(e.call(SCI_GETCOLUMN, uptr_t(bracePos)), e.call(SCI_GETCOLUMN, uptr_t(match)));
        e.call(SCI_SETHIGHLIGHTGUIDE, uptr_t(col));
    }
}

void MainWindow::smartHighlight()
{
    Editor& e = *_editor;
    sptr_t docLen = e.call(SCI_GETLENGTH);
    e.call(SCI_SETINDICATORCURRENT, INDIC_SMART);
    e.call(SCI_INDICATORCLEARRANGE, 0, docLen);
    sptr_t a = e.call(SCI_GETSELECTIONSTART), b = e.call(SCI_GETSELECTIONEND);
    if (b <= a || b - a > 200 || e.call(SCI_GETSELECTIONS) > 1) return;
    sptr_t wa = e.call(SCI_WORDSTARTPOSITION, uptr_t(a), 1), wb = e.call(SCI_WORDENDPOSITION, uptr_t(a), 1);
    bool wholeWord = Settings::get().getBool("smartHighlight.wholeWord", true);
    if (wholeWord && (wa != a || wb != b)) return;
    std::string word = e.textRange(a, b);
    if (word.find_first_of(" \t\r\n") != std::string::npos) return;
    sptr_t firstVis = e.call(SCI_DOCLINEFROMVISIBLE, uptr_t(e.call(SCI_GETFIRSTVISIBLELINE)));
    sptr_t lastVis = e.call(SCI_DOCLINEFROMVISIBLE, uptr_t(e.call(SCI_GETFIRSTVISIBLELINE) + e.call(SCI_LINESONSCREEN) + 1));
    sptr_t from = e.call(SCI_POSITIONFROMLINE, uptr_t(firstVis));
    sptr_t to = e.call(SCI_GETLINEENDPOSITION, uptr_t(lastVis));
    int flags = (Settings::get().getBool("smartHighlight.matchCase", false) ? SCFIND_MATCHCASE : 0) | (wholeWord ? SCFIND_WHOLEWORD : 0);
    sptr_t keepTs = e.call(SCI_GETTARGETSTART), keepTe = e.call(SCI_GETTARGETEND);
    e.call(SCI_SETSEARCHFLAGS, uptr_t(flags));
    while (from < to) {
        e.call(SCI_SETTARGETRANGE, uptr_t(from), to);
        sptr_t f = e.call(SCI_SEARCHINTARGET, word.size(), reinterpret_cast<sptr_t>(word.c_str()));
        if (f < 0) break;
        sptr_t fe = e.call(SCI_GETTARGETEND);
        if (f != a) e.call(SCI_INDICATORFILLRANGE, uptr_t(f), fe - f);
        from = fe > f ? fe : f + 1;
    }
    e.call(SCI_SETTARGETRANGE, uptr_t(keepTs), keepTe);
}

void MainWindow::onCharAdded(int ch)
{
    Editor& e = *_editor;
    Settings& S = Settings::get();
    // auto-indent
    if ((ch == '\n' || ch == '\r') && S.getBool("editor.autoIndent", true)) {
        int eolMode = int(e.call(SCI_GETEOLMODE));
        if (ch == '\r' && eolMode == SC_EOL_CRLF) return;   // wait for the \n
        sptr_t line = e.call(SCI_LINEFROMPOSITION, uptr_t(e.call(SCI_GETCURRENTPOS)));
        if (line > 0) {
            sptr_t prevIndent = e.call(SCI_GETLINEINDENTATION, uptr_t(line - 1));
            std::string prev = e.lineText(line - 1);
            while (!prev.empty() && (prev.back() == ' ' || prev.back() == '\t')) prev.pop_back();
            int lt = _current ? _current->langType : 0;
            const LangDef* d = langByType(lt);
            bool cLike = std::string(d->lexer) == "cpp" || lt == 20 /*css*/ || lt == 81 /*rust*/ || lt == 21 || lt == 1 || lt == 57;
            sptr_t indent = prevIndent;
            if (cLike && !prev.empty() && prev.back() == '{') indent += e.call(SCI_GETINDENT) ? e.call(SCI_GETINDENT) : e.call(SCI_GETTABWIDTH);
            if (lt == 22 /*python*/ && !prev.empty() && prev.back() == ':') indent += e.call(SCI_GETTABWIDTH);
            if (indent > 0) {
                e.call(SCI_SETLINEINDENTATION, uptr_t(line), indent);
                e.call(SCI_GOTOPOS, uptr_t(e.call(SCI_GETLINEINDENTPOSITION, uptr_t(line))));
            }
            // "{|}" + Enter => put the closing brace on its own line
            if (cLike && !prev.empty() && prev.back() == '{') {
                std::string cur = e.lineText(line);
                std::string trimmed = cur; trimmed.erase(0, trimmed.find_first_not_of(" \t"));
                if (!trimmed.empty() && trimmed[0] == '}') {
                    sptr_t pos = e.call(SCI_GETCURRENTPOS);
                    e.call(SCI_NEWLINE);
                    e.call(SCI_SETLINEINDENTATION, uptr_t(line + 1), prevIndent);
                    e.call(SCI_GOTOPOS, uptr_t(pos));
                }
            }
        }
        return;
    }
    // un-indent a closing brace typed on an otherwise empty line
    if (ch == '}' && S.getBool("editor.autoIndent", true)) {
        sptr_t pos = e.call(SCI_GETCURRENTPOS);
        sptr_t line = e.call(SCI_LINEFROMPOSITION, uptr_t(pos));
        std::string t = e.lineText(line);
        std::string trimmed = t; trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        if (trimmed == "}") {
            sptr_t match = e.call(SCI_BRACEMATCH, uptr_t(pos - 1), 0);
            if (match >= 0) {
                sptr_t ind = e.call(SCI_GETLINEINDENTATION, uptr_t(e.call(SCI_LINEFROMPOSITION, uptr_t(match))));
                e.call(SCI_SETLINEINDENTATION, uptr_t(line), ind);
            }
        }
    }
    // auto-close brackets and quotes
    if (S.getBool("editor.autoClose", false)) {
        const char* closer = nullptr;
        switch (ch) { case '(': closer = ")"; break; case '[': closer = "]"; break; case '{': closer = "}"; break;
                      case '"': closer = "\""; break; case '\'': closer = "'"; break; default: break; }
        if (closer) {
            sptr_t pos = e.call(SCI_GETCURRENTPOS);
            int next = int(e.call(SCI_GETCHARAT, uptr_t(pos)));
            bool quote = ch == '"' || ch == '\'';
            if (!quote || next == 0 || next == ' ' || next == '\n' || next == '\r' || next == ')' || next == ']' || next == '}') {
                e.callStr(SCI_INSERTTEXT, uptr_t(pos), closer);
            }
        }
    }
    _autoComplete->onCharAdded(e, ch);
}

// ------------------------------------------------------------------ external changes
void MainWindow::onActiveChanged(GObject* obj, GParamSpec*, gpointer self)
{
    if (gtk_window_is_active(GTK_WINDOW(obj)))
        g_idle_add(+[](gpointer s) -> gboolean { static_cast<MainWindow*>(s)->checkExternalChanges(); return G_SOURCE_REMOVE; }, self);
}

void MainWindow::checkExternalChanges()
{
    static bool busy = false;
    if (busy) return;
    busy = true;
    bool silent = Settings::get().getBool("fileChange.silentUpdate", false);
    std::vector<int> ids;
    for (auto& b : _buffers) ids.push_back(b->id);
    for (int id : ids) {
        Buffer* b = bufferById(id);
        if (!b || b->isUntitled() || _monitors.count(id)) continue;
        time_t mt = 0; long long sz = 0;
        if (!statFile(b->path, mt, sz)) {
            if (!b->missing) {
                b->missing = true;
                int r = askQuestion("The file \"" + b->path + "\" doesn't exist anymore.", "Keep this file in editor?", {{"_Close", 0}, {"_Keep", 1}});
                if (r == 0) { closeBuffer(b, true); continue; }
                b->dirty = true; updateTab(b); if (b == _current) updateTitle();
            }
            continue;
        }
        if (mt != b->mtime || sz != b->fileSize) {
            b->mtime = mt; b->fileSize = sz;
            if (silent && !b->dirty) { reloadBuffer(b, false); continue; }
            activate(b);
            int r = askQuestion(b->path, std::string("This file has been modified by another program.\nDo you want to reload it") +
                                (b->dirty ? " and lose the changes made in Camelion++?" : "?"), {{"_No", 0}, {"_Yes", 1}});
            if (r == 1) reloadBuffer(b, false);
        }
    }
    busy = false;
}

void MainWindow::startMonitoring(Buffer* b, bool on)
{
    if (!b) return;
    auto it = _monitors.find(b->id);
    if (!on) {
        if (it != _monitors.end()) { g_file_monitor_cancel(it->second); g_object_unref(it->second); _monitors.erase(it); }
        return;
    }
    if (it != _monitors.end() || b->isUntitled()) return;
    GFile* f = g_file_new_for_path(b->path.c_str());
    GFileMonitor* mon = g_file_monitor_file(f, G_FILE_MONITOR_NONE, nullptr, nullptr);
    g_object_unref(f);
    if (!mon) return;
    g_object_set_data(G_OBJECT(mon), "cam-buffer-id", GINT_TO_POINTER(b->id));
    g_signal_connect(mon, "changed", CAM_CB(+[](GFileMonitor* m, GFile*, GFile*, GFileMonitorEvent ev, gpointer self) {
        if (ev != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT && ev != G_FILE_MONITOR_EVENT_CHANGED) return;
        auto* mw = static_cast<MainWindow*>(self);
        Buffer* b = mw->bufferById(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(m), "cam-buffer-id")));
        if (!b) return;
        mw->reloadBuffer(b, false);
        withDoc(mw, b, [](Editor& e) { e.call(SCI_DOCUMENTEND); return 0; });
    }), this);
    _monitors[b->id] = mon;
    b->userReadOnly = true;
    withDoc(this, b, [](Editor& e) { e.call(SCI_SETREADONLY, 1); e.call(SCI_DOCUMENTEND); return 0; });
    updateTab(b);
}

// ------------------------------------------------------------------ drag & drop, context menus
void MainWindow::dropFiles(GtkSelectionData* data)
{
    gchar** uris = gtk_selection_data_get_uris(data);
    for (gchar** u = uris; u && *u; ++u) openFile(*u);
    g_strfreev(uris);
}

void MainWindow::tabContextMenu(Buffer* b, GdkEventButton* ev)
{
    if (b != _current) activate(b);
    GtkWidget* menu = gtk_menu_new();
    auto add = [&](int id, const char* label) {
        GtkWidget* it = gtk_menu_item_new_with_mnemonic(label);
        g_object_set_data(G_OBJECT(it), "cam-cmd", GINT_TO_POINTER(id));
        g_signal_connect(it, "activate", CAM_CB(+[](GtkWidget* w, gpointer self) {
            static_cast<MainWindow*>(self)->runCommand(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cam-cmd")));
        }), this);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    };
    auto sep = [&] { gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new()); };
    add(cmd::FILE_CLOSE, "_Close");
    add(cmd::FILE_CLOSEALL_BUT_CURRENT, "Close All BUT This");
    add(cmd::FILE_CLOSEALL_TOLEFT, "Close All to the Left");
    add(cmd::FILE_CLOSEALL_TORIGHT, "Close All to the Right");
    add(cmd::FILE_CLOSEALL_UNCHANGED, "Close All Unchanged");
    sep();
    add(cmd::FILE_SAVE, "_Save");
    add(cmd::FILE_SAVEAS, "Save As...");
    add(cmd::FILE_RENAME, "Rename...");
    add(cmd::FILE_DELETE, "Move to Trash");
    add(cmd::FILE_RELOAD, "Reload");
    sep();
    add(cmd::FILE_OPEN_FOLDER, "Open Containing Folder");
    add(cmd::FILE_OPEN_CMD, "Open in Terminal");
    sep();
    add(cmd::EDIT_SETREADONLY, "Read-Only");
    add(cmd::EDIT_FULLPATHTOCLIP, "Copy Full File Path");
    add(cmd::EDIT_FILENAMETOCLIP, "Copy Filename");
    add(cmd::EDIT_CURRENTDIRTOCLIP, "Copy Current Dir. Path");
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(ev));
}

// ------------------------------------------------------------------ session
void MainWindow::saveSessionNow()
{
    std::vector<SessionFile> files;
    bool snapshot = Settings::get().getBool("session.snapshot", true);
    paths::mkdirs(paths::backupDir());
    for (auto& up : _buffers) {
        Buffer* b = up.get();
        if (b == _current) _editor->saveViewState(*b);
        SessionFile f;
        f.path = b->path;
        f.langType = b->langLocked ? b->langType : -1;
        f.anchor = long(b->anchor); f.caret = long(b->caret); f.firstLine = long(b->firstLine);
        f.current = b == _current;
        if (b->mode == UniMode::Ansi) f.encoding = b->charset;
        if (snapshot && (b->dirty || (b->isUntitled()))) {
            std::string text = withDoc(this, b, [](Editor& e) { return e.text(); });
            if (b->isUntitled() && text.empty() && !b->dirty) continue;
            std::string bk = paths::join(paths::backupDir(), b->displayName() + "@" + std::to_string(time(nullptr)) + "-" + std::to_string(b->id));
            if (paths::writeFileAtomic(bk, text)) { f.backup = bk; f.untitled = b->untitledName; f.dirty = b->dirty; }
        } else if (b->isUntitled()) continue;
        files.push_back(f);
    }
    Settings::get().saveSession(files);
}

void MainWindow::restoreSession()
{
    if (!Settings::get().getBool("session.remember", true)) return;
    auto files = Settings::get().loadSession();
    Buffer* cur = nullptr;
    std::vector<std::string> usedBackups;
    for (auto& f : files) {
        Buffer* b = nullptr;
        if (!f.backup.empty() && paths::fileExists(f.backup)) {
            std::string text = paths::readFile(f.backup);
            if (f.path.empty()) {
                b = newDocument();
                if (!f.untitled.empty()) { b->untitledName = f.untitled; updateTab(b); }
            } else {
                b = openFile(f.path, true, true);
                if (!b) {  // the file was deleted: restore as an untitled-like buffer with its path
                    b = newDocument();
                    b->path = f.path;
                    updateTab(b);
                }
            }
            withDoc(this, b, [&](Editor& e) { e.callStr(SCI_SETTEXT, 0, text.c_str()); e.call(SCI_EMPTYUNDOBUFFER); if (!f.dirty) e.call(SCI_SETSAVEPOINT); return 0; });
            b->dirty = f.dirty || (f.path.empty() && !text.empty());
            usedBackups.push_back(f.backup);
            updateTab(b);
        } else if (!f.path.empty()) {
            b = openFile(f.path, false, true);
            if (b && !f.encoding.empty()) { b->mode = UniMode::Ansi; reinterpretEncoding(b, UniMode::Ansi, f.encoding); }
        }
        if (!b) continue;
        if (f.langType >= 0) { b->langType = f.langType; b->langLocked = true; }
        b->anchor = f.anchor; b->caret = f.caret; b->firstLine = f.firstLine; b->viewSaved = true;
        if (f.current) cur = b;
    }
    // remove the pristine "new 1" if the session brought documents back
    if (_buffers.size() > 1) {
        Buffer* first = _buffers.front().get();
        if (first->isUntitled() && !first->dirty && withDoc(this, first, [](Editor& e) { return e.call(SCI_GETLENGTH); }) == 0)
            closeBuffer(first, false);
    }
    activate(cur ? cur : _buffers.back().get());
    if (_current) { _editor->restoreViewState(*_current); }
    // delete stale backups
    if (DIR* d = opendir(paths::backupDir().c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            std::string full = paths::join(paths::backupDir(), n);
            if (std::find(usedBackups.begin(), usedBackups.end(), full) == usedBackups.end()) ::unlink(full.c_str());
        }
        closedir(d);
    }
}

void MainWindow::openFromCommandLine(const std::vector<std::string>& files, long line, long column, int langType, bool readOnly)
{
    Buffer* last = nullptr;
    for (auto& f : files) if (Buffer* b = openFile(f)) last = b;
    if (!last) return;
    activate(last);
    if (langType >= 0) setLanguage(last, langType);
    if (readOnly) { last->userReadOnly = true; _editor->call(SCI_SETREADONLY, 1); updateTab(last); }
    if (line > 0) {
        sptr_t pos = _editor->call(SCI_POSITIONFROMLINE, uptr_t(line - 1));
        if (column > 0) pos = _editor->call(SCI_FINDCOLUMN, uptr_t(line - 1), column - 1);
        _editor->call(SCI_ENSUREVISIBLE, uptr_t(line - 1));
        _editor->call(SCI_GOTOPOS, uptr_t(pos));
        _editor->call(SCI_VERTICALCENTRECARET);
    }
    gtk_window_present(GTK_WINDOW(_window));
}

bool MainWindow::quit()
{
    notifyPlugins(NPPN_BEFORESHUTDOWN, 0);
    Settings& S = Settings::get();
    bool snapshot = S.getBool("session.snapshot", true);
    if (!snapshot) {
        for (auto& b : _buffers) {
            if (!b->dirty) continue;
            activate(b.get());
            int r = askQuestion("Save file \"" + (b->path.empty() ? b->untitledName : b->path) + "\" ?", "",
                                {{"_Cancel", 0}, {"_No", 1}, {"_Yes", 2}});
            if (r == 0) return false;
            if (r == 2 && !saveBuffer(b.get())) return false;
            if (r == 1) b->dirty = false;
        }
    }
    if (S.getBool("session.remember", true) && !g_noSession) saveSessionNow();
    int w = 0, h = 0;
    gtk_window_get_size(GTK_WINDOW(_window), &w, &h);
    bool maximized = gtk_window_is_maximized(GTK_WINDOW(_window));
    if (!maximized) { S.set("window.width", w); S.set("window.height", h); }
    S.set("window.maximized", maximized);
    S.save();
    notifyPlugins(NPPN_SHUTDOWN, 0);
    for (auto& [id, m] : _monitors) { g_file_monitor_cancel(m); g_object_unref(m); }
    _monitors.clear();
    if (_findDlg) _findDlg->hide();
    gtk_widget_destroy(_window);
    return true;
}

gboolean MainWindow::onDelete(GtkWidget*, GdkEvent*, gpointer self)
{
    static_cast<MainWindow*>(self)->quit();
    return TRUE;
}

void MainWindow::notifyPlugins(unsigned code, uptr_t idFrom)
{
    if (!_plugins) return;
    SCNotification n{};
    n.nmhdr.code = code;
    n.nmhdr.hwndFrom = _plugins->nppHandle();
    n.nmhdr.idFrom = idFrom;
    _plugins->notify(&n);
}

} // namespace cam
