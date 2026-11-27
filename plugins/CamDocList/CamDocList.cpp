// CamDocList - a "Document List" side panel for Camelion++.
// Demonstrates a GTK-based plugin: CAMM_ADDPANEL docks a GtkWidget into the editor,
// NPPN_* notifications keep it in sync, NPPM_ACTIVATEDOC switches documents.
#define CAMELION_SENDMESSAGE_SHIM
#include <gtk/gtk.h>
#include <CamelionPluginInterface.h>
#include <string>
#define CAM_CB(...) (reinterpret_cast<GCallback>(__VA_ARGS__))

CAMELION_PLUGIN_VERSION("1.0.0")

static NppData nppData;
static GtkWidget* g_view = nullptr;
static GtkListStore* g_store = nullptr;
static int g_panel = 0;
static bool g_visible = false;
static bool g_updating = false;
static FuncItem funcItems[2];

static std::string pathOf(int bufferId)
{
    LRESULT len = SendMessage(nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, WPARAM(bufferId), 0);
    if (len < 0) return {};
    std::string s(size_t(len) + 1, '\0');
    SendMessage(nppData._nppHandle, NPPM_GETFULLPATHFROMBUFFERID, WPARAM(bufferId), reinterpret_cast<LPARAM>(s.data()));
    s.resize(size_t(len));
    return s;
}

static void refresh()
{
    if (!g_store) return;
    g_updating = true;
    gtk_list_store_clear(g_store);
    int n = int(SendMessage(nppData._nppHandle, NPPM_GETNBOPENFILES, 0, PRIMARY_VIEW));
    int cur = int(SendMessage(nppData._nppHandle, NPPM_GETCURRENTDOCINDEX, 0, MAIN_VIEW));
    for (int i = 0; i < n; ++i) {
        int id = int(SendMessage(nppData._nppHandle, NPPM_GETBUFFERIDFROMPOS, WPARAM(i), MAIN_VIEW));
        std::string p = pathOf(id);
        std::string name = p.substr(p.find_last_of('/') == std::string::npos ? 0 : p.find_last_of('/') + 1);
        std::string dir = p.find('/') == std::string::npos ? "" : p.substr(0, p.find_last_of('/'));
        GtkTreeIter it;
        gtk_list_store_append(g_store, &it);
        gtk_list_store_set(g_store, &it, 0, name.c_str(), 1, dir.c_str(), 2, i, -1);
        if (i == cur && g_view) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(g_view)), &it);
    }
    g_updating = false;
}

static void createPanel()
{
    g_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    g_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g_store));
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(g_view), -1, "Name", gtk_cell_renderer_text_new(), "text", 0, nullptr);
    GtkCellRenderer* r = gtk_cell_renderer_text_new();
    g_object_set(r, "foreground", "gray", "ellipsize", PANGO_ELLIPSIZE_START, nullptr);
    GtkTreeViewColumn* c = gtk_tree_view_column_new_with_attributes("Folder", r, "text", 1, nullptr);
    gtk_tree_view_column_set_expand(c, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(g_view), c);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(g_view)), "changed", CAM_CB(+[](GtkTreeSelection* s, gpointer) {
        if (g_updating) return;
        GtkTreeModel* m; GtkTreeIter it;
        if (!gtk_tree_selection_get_selected(s, &m, &it)) return;
        int idx = 0;
        gtk_tree_model_get(m, &it, 2, &idx, -1);
        SendMessage(nppData._nppHandle, NPPM_ACTIVATEDOC, MAIN_VIEW, idx);
    }), nullptr);
    GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(sw), g_view);
    g_panel = int(SendMessage(nppData._nppHandle, CAMM_ADDPANEL, reinterpret_cast<WPARAM>("Documents"), reinterpret_cast<LPARAM>(sw)));
}

static void toggle()
{
    if (!g_view) createPanel();
    g_visible = !g_visible;
    SendMessage(nppData._nppHandle, CAMM_SHOWPANEL, WPARAM(g_panel), g_visible);
    SendMessage(nppData._nppHandle, NPPM_SETMENUITEMCHECK, WPARAM(funcItems[0]._cmdID), g_visible);
    if (g_visible) refresh();
}

static void about()
{
    SendMessage(nppData._nppHandle, NPPM_SETSTATUSBAR, STATUSBAR_DOC_TYPE,
                reinterpret_cast<LPARAM>("Document List 1.0.0 - a GTK panel plugin for Camelion++"));
}

CAMELION_PLUGIN_EXPORT void setInfo(NppData d)
{
    nppData = d;
    camelionInitSendMessage(d);
    funcItems[0] = {"Document List", toggle, 0, false, nullptr};
    funcItems[1] = {"About", about, 0, false, nullptr};
}
CAMELION_PLUGIN_EXPORT const char* getName() { return "Document List"; }
CAMELION_PLUGIN_EXPORT FuncItem* getFuncsArray(int* n) { *n = 2; return funcItems; }
CAMELION_PLUGIN_EXPORT void beNotified(SCNotification* n)
{
    switch (n->nmhdr.code) {
    case NPPN_FILEOPENED: case NPPN_FILECLOSED: case NPPN_BUFFERACTIVATED: case NPPN_FILESAVED:
        if (g_visible) g_idle_add(+[](gpointer) -> gboolean { refresh(); return G_SOURCE_REMOVE; }, nullptr);
        break;
    default: break;
    }
}
CAMELION_PLUGIN_EXPORT LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
