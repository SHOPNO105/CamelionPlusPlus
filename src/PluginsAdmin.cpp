// Camelion++ - Plugins Admin (install / update / remove plugins from the nppPluginList)
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
#include "PluginsAdmin.h"
#include "PluginList.h"
#include "PluginManager.h"
#include "MainWindow.h"
#include "ElfReader.h"
#include "Paths.h"
#include "Settings.h"
#include "Version.h"
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <ctime>
#include <cstring>

namespace cam {

bool g_restartRequested = false;

// ------------------------------------------------------------------ download
struct DlCtx { std::string* out; size_t max; bool (*progress)(void*, double); void* ctx; };

static size_t onData(char* p, size_t sz, size_t n, void* ud)
{
    auto* c = static_cast<DlCtx*>(ud);
    if (c->out->size() + sz * n > c->max) return 0;
    c->out->append(p, sz * n);
    return sz * n;
}

static int onProgress(void* ud, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t)
{
    auto* c = static_cast<DlCtx*>(ud);
    if (!c->progress) return 0;
    return c->progress(c->ctx, total > 0 ? double(now) / double(total) : -1.0) ? 0 : 1;
}

bool PluginsAdminDlg::download(const std::string& url, std::string& out, std::string& err, bool allowFile,
                               bool (*progress)(void*, double), void* ctx)
{
    static bool init = (curl_global_init(CURL_GLOBAL_DEFAULT), true);
    (void)init;
    out.clear();
    CURL* c = curl_easy_init();
    if (!c) { err = "curl initialisation failed"; return false; }
    DlCtx dc{&out, size_t(256) * 1024 * 1024, progress, ctx};
    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, allowFile ? "https,file" : "https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Camelion++/" CAMELION_VERSION_STRING " (Plugins Admin)");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &dc);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, progress ? 0L : 1L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &dc);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    if (const char* ca = g_getenv("CAMELION_CA_BUNDLE")) curl_easy_setopt(c, CURLOPT_CAINFO, ca);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK) { err = errbuf[0] ? errbuf : curl_easy_strerror(rc); return false; }
    return true;
}

// ------------------------------------------------------------------ install
static bool safeRelative(std::string& p)
{
    std::replace(p.begin(), p.end(), '\\', '/');
    while (p.compare(0, 2, "./") == 0) p = p.substr(2);
    if (p.empty() || p[0] == '/') return false;
    size_t start = 0;
    while (start <= p.size()) {
        size_t slash = p.find('/', start);
        std::string comp = p.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (comp == "..") return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

bool PluginsAdminDlg::installFromArchive(const std::string& zip, const PluginInfo& info, std::string& installedSo, std::string& err)
{
    const std::string root = paths::userPluginsDir();
    paths::mkdirs(root);
    std::string tmp = paths::join(root, ".install-" + info.folderName + "-" + std::to_string(::getpid()));
    paths::removeTree(tmp);
    if (!paths::mkdirs(tmp)) { err = "cannot create " + tmp; return false; }

    archive* a = archive_read_new();
    archive_read_support_format_zip(a);
    if (archive_read_open_memory(a, zip.data(), zip.size()) != ARCHIVE_OK) {
        err = std::string("invalid zip file: ") + archive_error_string(a);
        archive_read_free(a); paths::removeTree(tmp); return false;
    }
    std::string wantedSo = info.folderName + ".so", foundSo;
    archive_entry* entry = nullptr;
    size_t totalBytes = 0;
    int r;
    while ((r = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        std::string name = archive_entry_pathname(entry) ? archive_entry_pathname(entry) : "";
        if (!safeRelative(name)) { err = "unsafe path in archive: " + name; break; }
        mode_t type = archive_entry_filetype(entry);
        std::string dest = paths::join(tmp, name);
        if (type == AE_IFDIR) { paths::mkdirs(dest); continue; }
        if (type != AE_IFREG) continue;   // links, devices ... are ignored
        paths::mkdirs(paths::dirName(dest));
        std::ofstream f(dest, std::ios::binary | std::ios::trunc);
        if (!f) { err = "cannot write " + dest; break; }
        const void* buf; size_t size; la_int64_t off;
        while (archive_read_data_block(a, &buf, &size, &off) == ARCHIVE_OK) {
            totalBytes += size;
            if (totalBytes > size_t(512) * 1024 * 1024) { err = "archive is too large once extracted"; break; }
            f.write(static_cast<const char*>(buf), std::streamsize(size));
        }
        f.close();
        if (!err.empty()) break;
        ::chmod(dest.c_str(), 0644);
        if (name.find('/') == std::string::npos && g_ascii_strcasecmp(name.c_str(), wantedSo.c_str()) == 0) foundSo = dest;
    }
    if (r != ARCHIVE_EOF && r != ARCHIVE_OK && err.empty()) err = std::string("corrupt zip file: ") + (archive_error_string(a) ? archive_error_string(a) : "");
    archive_read_free(a);
    if (!err.empty()) { paths::removeTree(tmp); return false; }
    if (foundSo.empty()) { err = "the zip file does not contain " + wantedSo + " at its root"; paths::removeTree(tmp); return false; }
    std::string exactSo = paths::join(tmp, wantedSo);
    if (foundSo != exactSo) ::rename(foundSo.c_str(), exactSo.c_str());

    std::string bytes = paths::readFile(exactSo);
    ElfInfo ei = elfInfo(bytes);
    if (!ei.ok) { err = wantedSo + ": " + ei.error; paths::removeTree(tmp); return false; }
    if (ei.machine != hostElfMachine()) { err = wantedSo + " is built for " + elfMachineName(ei.machine) + ", not " + elfMachineName(hostElfMachine()); paths::removeTree(tmp); return false; }
    std::vector<std::string> syms;
    elfDynamicSymbols(bytes, syms);
    for (const char* req : {"setInfo", "getName", "getFuncsArray", "beNotified", "messageProc", "camelionPluginApiVersion"})
        if (std::find(syms.begin(), syms.end(), req) == syms.end()) { err = wantedSo + " does not export " + req + "()"; paths::removeTree(tmp); return false; }

    std::string final = paths::join(root, info.folderName);
    std::string old;
    if (paths::dirExists(final)) {
        old = paths::join(root, ".old-" + info.folderName + "-" + std::to_string(time(nullptr)));
        if (::rename(final.c_str(), old.c_str()) != 0) { err = "cannot replace the installed version"; paths::removeTree(tmp); return false; }
    }
    if (::rename(tmp.c_str(), final.c_str()) != 0) {
        err = std::string("cannot install into ") + final + ": " + g_strerror(errno);
        if (!old.empty()) ::rename(old.c_str(), final.c_str());
        paths::removeTree(tmp);
        return false;
    }
    if (!old.empty()) paths::removeTree(old);
    installedSo = paths::join(final, wantedSo);
    return true;
}

// ------------------------------------------------------------------ dialog
namespace {

enum Page { P_AVAILABLE = 0, P_UPDATES, P_INSTALLED, P_INCOMPATIBLE };
enum Col { C_CHECK = 0, C_NAME, C_VERSION, C_INSTALLED, C_INDEX, C_N };

struct Installed {
    std::string folder, name, version, path;
    bool system = false, loaded = false;
    std::string failReason;
};

struct Admin {
    MainWindow* mw = nullptr;
    PluginListData list;
    std::vector<Installed> installed;
    GtkWidget* dlg = nullptr;
    GtkWidget* notebook = nullptr;
    GtkWidget* views[4] = {};
    GtkWidget* desc = nullptr;
    GtkWidget* action = nullptr;
    GtkWidget* search = nullptr;
    GtkWidget* progress = nullptr;
    GtkWidget* status = nullptr;
    bool busy = false;
    bool cancel = false;
    Version hostVersion;
};

std::set<std::string>& pendingRestart() { static std::set<std::string> s; return s; }

const Installed* findInstalled(Admin& A, const std::string& folder)
{
    for (auto& i : A.installed) if (g_ascii_strcasecmp(i.folder.c_str(), folder.c_str()) == 0) return &i;
    return nullptr;
}

bool compatible(Admin& A, const PluginInfo& p)
{
    return !p.hasCompat || A.hostVersion.isCompatibleTo(p.compatible.first, p.compatible.second);
}

void scanInstalled(Admin& A)
{
    A.installed.clear();
    PluginManager& pm = A.mw->plugins();
    for (auto& p : pm.plugins()) A.installed.push_back({p->folder, p->name, p->version, p->path, p->system, true, ""});
    for (auto& f : pm.failed()) A.installed.push_back({f.folder, f.folder, "", f.path, paths::dirName(paths::dirName(f.path)) != paths::userPluginsDir(), false, f.reason});
    // installed during this session, not loaded yet
    for (auto& folder : pendingRestart()) {
        if (findInstalled(A, folder)) continue;
        std::string so = paths::join(paths::join(paths::userPluginsDir(), folder), folder + ".so");
        if (!paths::fileExists(so)) continue;
        std::string ver;
        elfSymbolData(paths::readFile(so), "camelionPluginVersion", ver);
        A.installed.push_back({folder, folder, ver, so, false, false, "installed - restart Camelion++ to load it"});
    }
}

void fill(Admin& A)
{
    std::string filter = gtk_entry_get_text(GTK_ENTRY(A.search));
    auto matches = [&](const std::string& name, const std::string& descr) {
        if (filter.empty()) return true;
        gchar* f = g_utf8_casefold(filter.c_str(), -1);
        gchar* n = g_utf8_casefold((name + "\n" + descr).c_str(), -1);
        bool ok = std::strstr(n, f) != nullptr;
        g_free(f); g_free(n);
        return ok;
    };
    for (int pg = 0; pg < 4; ++pg) {
        GtkListStore* s = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(A.views[pg])));
        gtk_list_store_clear(s);
    }
    auto add = [&](int pg, const std::string& name, const std::string& ver, const std::string& inst, int idx) {
        GtkListStore* s = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(A.views[pg])));
        GtkTreeIter it;
        gtk_list_store_append(s, &it);
        gtk_list_store_set(s, &it, C_CHECK, FALSE, C_NAME, name.c_str(), C_VERSION, ver.c_str(), C_INSTALLED, inst.c_str(), C_INDEX, idx, -1);
    };
    for (size_t i = 0; i < A.list.plugins.size(); ++i) {
        const PluginInfo& p = A.list.plugins[i];
        if (!matches(p.displayName, p.description)) continue;
        const Installed* ins = findInstalled(A, p.folderName);
        if (!compatible(A, p)) { add(P_INCOMPATIBLE, p.displayName, p.version.toString(), ins ? ins->version : "", int(i)); continue; }
        if (!ins) add(P_AVAILABLE, p.displayName, p.version.toString(), "", int(i));
        else {
            Version iv;
            try { iv = Version::parse(ins->version); } catch (...) {}
            if (!iv.empty() && p.version > iv) add(P_UPDATES, p.displayName, p.version.toString(), ins->version, int(i));
        }
    }
    for (size_t i = 0; i < A.installed.size(); ++i) {
        const Installed& ins = A.installed[i];
        if (!matches(ins.name, ins.folder)) continue;
        add(P_INSTALLED, ins.name, ins.version, ins.loaded ? "" : "not loaded", int(i));
    }
    for (int pg = 0; pg < 4; ++pg) {
        static const char* titles[] = {"Available", "Updates", "Installed", "Incompatible"};
        int n = gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(A.views[pg])), nullptr);
        std::string t = std::string(titles[pg]) + " (" + std::to_string(n) + ")";
        gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(A.notebook), gtk_notebook_get_nth_page(GTK_NOTEBOOK(A.notebook), pg), t.c_str());
    }
}

void setDescription(Admin& A, const std::string& text)
{
    GtkTextBuffer* b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(A.desc));
    gtk_text_buffer_set_text(b, text.c_str(), -1);
}

void showSelected(Admin& A, int pg = -1)
{
    if (pg < 0) pg = gtk_notebook_get_current_page(GTK_NOTEBOOK(A.notebook));
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(A.views[pg]));
    GtkTreeModel* m; GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, &m, &it)) { setDescription(A, ""); return; }
    gint idx = 0;
    gtk_tree_model_get(m, &it, C_INDEX, &idx, -1);
    std::string t;
    if (pg == P_INSTALLED) {
        const Installed& ins = A.installed[size_t(idx)];
        t = ins.name + (ins.version.empty() ? "" : "  " + ins.version) + "\n\nFolder: " + ins.folder + "\nFile: " + ins.path + "\n";
        if (ins.system) t += "Installed system-wide (read-only).\n";
        if (!ins.failReason.empty()) t += "\nNot loaded: " + ins.failReason + "\n";
        for (auto& p : A.list.plugins) if (g_ascii_strcasecmp(p.folderName.c_str(), ins.folder.c_str()) == 0) {
            t += "\n" + p.description + "\n\nAuthor: " + p.author + "\nHomepage: " + p.homepage + "\n";
            break;
        }
    } else {
        const PluginInfo& p = A.list.plugins[size_t(idx)];
        std::string d = p.description;
        d.erase(std::remove(d.begin(), d.end(), '\r'), d.end());
        t = d + "\n\nAuthor: " + p.author + "\nVersion: " + p.version.toString() + "\nHomepage: " + p.homepage + "\nDownload: " + p.repository + "\nSHA-256: " + p.id + "\n";
        if (p.hasCompat) t += "Requires Camelion++ " + p.compatible.first.toString() + " - " + (p.compatible.second.empty() ? std::string("latest") : p.compatible.second.toString()) + "\n";
    }
    setDescription(A, t);
}

void updateAction(Admin& A, int pg = -1)
{
    if (pg < 0) pg = gtk_notebook_get_current_page(GTK_NOTEBOOK(A.notebook));
    static const char* labels[] = {"_Install", "_Update", "_Remove", ""};
    gtk_button_set_label(GTK_BUTTON(A.action), labels[pg]);
    gtk_widget_set_visible(A.action, pg != P_INCOMPATIBLE);
    showSelected(A, pg);
}

std::vector<int> checkedIndexes(Admin& A, int pg)
{
    std::vector<int> v;
    GtkTreeModel* m = gtk_tree_view_get_model(GTK_TREE_VIEW(A.views[pg]));
    GtkTreeIter it;
    if (gtk_tree_model_get_iter_first(m, &it)) do {
        gboolean on = FALSE; gint idx = 0;
        gtk_tree_model_get(m, &it, C_CHECK, &on, C_INDEX, &idx, -1);
        if (on) v.push_back(idx);
    } while (gtk_tree_model_iter_next(m, &it));
    return v;
}

bool pumpProgress(void* ctx, double f)
{
    auto* A = static_cast<Admin*>(ctx);
    if (f >= 0) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(A->progress), f);
    else gtk_progress_bar_pulse(GTK_PROGRESS_BAR(A->progress));
    while (gtk_events_pending()) gtk_main_iteration();
    return !A->cancel;
}

void askRestart(Admin& A, const std::string& why)
{
    int r = A.mw->askQuestion("Camelion++ needs to be restarted", why + "\n\nRestart now? Your session (including unsaved changes) is kept.",
                              {{"_Later", 0}, {"_Restart Now", 1}});
    if (r == 1) {
        gtk_dialog_response(GTK_DIALOG(A.dlg), GTK_RESPONSE_CLOSE);
        g_restartRequested = true;
        g_idle_add(+[](gpointer mw) -> gboolean {
            if (!static_cast<MainWindow*>(mw)->quit()) g_restartRequested = false;
            return G_SOURCE_REMOVE;
        }, A.mw);
    }
}

void doInstall(Admin& A, int pg)
{
    std::vector<int> idx = checkedIndexes(A, pg);
    if (idx.empty()) { A.mw->showMessage(GTK_MESSAGE_INFO, "Tick the plugins you want to " + std::string(pg == P_UPDATES ? "update." : "install.")); return; }
    A.busy = true; A.cancel = false;
    gtk_widget_set_sensitive(A.notebook, FALSE);
    gtk_widget_set_sensitive(A.action, FALSE);
    gtk_widget_show(A.progress);
    std::vector<std::string> ok, failed;
    bool needRestart = false;
    for (int i : idx) {
        const PluginInfo& p = A.list.plugins[size_t(i)];
        gtk_label_set_text(GTK_LABEL(A.status), ("Downloading " + p.displayName + "...").c_str());
        std::string zip, err;
        if (!PluginsAdminDlg::download(p.repository, zip, err, A.list.testMode, pumpProgress, &A)) { failed.push_back(p.displayName + ": download failed (" + err + ")"); continue; }
        std::string hash = sha256Hex(zip);
        if (g_ascii_strcasecmp(hash.c_str(), p.id.c_str()) != 0) {
            failed.push_back(p.displayName + ": the downloaded file's SHA-256 (" + hash + ") does not match the plugin list - not installed");
            continue;
        }
        gtk_label_set_text(GTK_LABEL(A.status), ("Installing " + p.displayName + "...").c_str());
        while (gtk_events_pending()) gtk_main_iteration();
        std::string so;
        if (!PluginsAdminDlg::installFromArchive(zip, p, so, err)) { failed.push_back(p.displayName + ": " + err); continue; }
        ok.push_back(p.displayName);
        PluginManager& pm = A.mw->plugins();
        if (pm.byFolder(p.folderName)) { needRestart = true; pendingRestart().insert(p.folderName); }
        else {
            std::string lerr;
            if (pm.loadPlugin(so, p.folderName, false, &lerr)) pm.buildMenu();
            else { failed.push_back(p.displayName + ": installed but could not be loaded: " + lerr); pendingRestart().insert(p.folderName); }
        }
    }
    gtk_widget_hide(A.progress);
    gtk_widget_set_sensitive(A.notebook, TRUE);
    gtk_widget_set_sensitive(A.action, TRUE);
    A.busy = false;
    scanInstalled(A);
    fill(A);
    updateAction(A);
    std::string msg;
    if (!ok.empty()) { msg += "Installed: "; for (size_t i = 0; i < ok.size(); ++i) msg += (i ? ", " : "") + ok[i]; msg += "\n"; }
    for (auto& f : failed) msg += "\n" + f;
    gtk_label_set_text(GTK_LABEL(A.status), ok.empty() ? "Nothing was installed." : "Done.");
    if (!failed.empty()) A.mw->showMessage(GTK_MESSAGE_WARNING, "Some plugins could not be installed", msg);
    if (needRestart) askRestart(A, "The updated plugins will be used after a restart.");
}

void doRemove(Admin& A)
{
    std::vector<int> idx = checkedIndexes(A, P_INSTALLED);
    if (idx.empty()) { A.mw->showMessage(GTK_MESSAGE_INFO, "Tick the plugins you want to remove."); return; }
    std::string names;
    for (int i : idx) names += "\n  " + A.installed[size_t(i)].name;
    if (A.mw->askQuestion("Remove these plugins?" + names, "", {{"_Cancel", 0}, {"_Remove", 1}}) != 1) return;
    std::vector<std::string> failed;
    bool removedLoaded = false;
    for (int i : idx) {
        const Installed& ins = A.installed[size_t(i)];
        if (ins.system) { failed.push_back(ins.name + " is installed system-wide: remove it with your package manager."); continue; }
        std::string dir = paths::dirName(ins.path);
        if (paths::dirName(dir) != paths::userPluginsDir()) { failed.push_back(ins.name + ": unexpected location " + dir); continue; }
        if (!paths::removeTree(dir)) { failed.push_back(ins.name + ": " + g_strerror(errno)); continue; }
        if (ins.loaded) removedLoaded = true;
        pendingRestart().erase(ins.folder);
    }
    // a removed plugin stays in memory until restart: keep showing it, flagged
    scanInstalled(A);
    for (auto& ins : A.installed)
        if (ins.loaded && !paths::fileExists(ins.path)) { ins.failReason = "removed - restart Camelion++ to unload it"; ins.loaded = false; }
    fill(A);
    updateAction(A);
    if (!failed.empty()) { std::string m; for (auto& f : failed) m += f + "\n"; A.mw->showMessage(GTK_MESSAGE_WARNING, "Some plugins were not removed", m); }
    if (removedLoaded) askRestart(A, "Removed plugins are unloaded at the next start.");
}

GtkWidget* makeView(Admin& A, bool showInstalled)
{
    GtkListStore* s = gtk_list_store_new(C_N, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    GtkWidget* v = gtk_tree_view_new_with_model(GTK_TREE_MODEL(s));
    g_object_unref(s);
    GtkCellRenderer* tog = gtk_cell_renderer_toggle_new();
    g_object_set_data(G_OBJECT(tog), "cam-view", v);
    g_signal_connect(tog, "toggled", CAM_CB(+[](GtkCellRendererToggle* r, gchar* path, gpointer) {
        GtkWidget* view = GTK_WIDGET(g_object_get_data(G_OBJECT(r), "cam-view"));
        GtkTreeModel* m = gtk_tree_view_get_model(GTK_TREE_VIEW(view));
        GtkTreeIter it;
        if (!gtk_tree_model_get_iter_from_string(m, &it, path)) return;
        gboolean on = FALSE;
        gtk_tree_model_get(m, &it, C_CHECK, &on, -1);
        gtk_list_store_set(GTK_LIST_STORE(m), &it, C_CHECK, !on, -1);
    }), nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(v), -1, "", tog, "active", C_CHECK, nullptr);
    GtkCellRenderer* txt = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* c = gtk_tree_view_column_new_with_attributes("Plugin", txt, "text", C_NAME, nullptr);
    gtk_tree_view_column_set_expand(c, TRUE);
    gtk_tree_view_column_set_sort_column_id(c, C_NAME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(v), c);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(v), -1, "Version", gtk_cell_renderer_text_new(), "text", C_VERSION, nullptr);
    if (showInstalled) gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(v), -1, "Installed", gtk_cell_renderer_text_new(), "text", C_INSTALLED, nullptr);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(s), C_NAME, GTK_SORT_ASCENDING);
    g_signal_connect_swapped(gtk_tree_view_get_selection(GTK_TREE_VIEW(v)), "changed", CAM_CB(+[](Admin* a) { showSelected(*a); }), &A);
    return v;
}

} // namespace

void PluginsAdminDlg::run(MainWindow* mw)
{
    Admin A;
    A.mw = mw;
    try { A.hostVersion = Version::parse(CAMELION_VERSION_STRING); } catch (...) {}
    A.list = loadPluginList();
    scanInstalled(A);

    A.dlg = gtk_dialog_new_with_buttons("Plugins Admin", mw->window(), GTK_DIALOG_MODAL, "_Close", GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(A.dlg), 780, 560);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(A.dlg));
    gtk_container_set_border_width(GTK_CONTAINER(area), 8);
    gtk_box_set_spacing(GTK_BOX(area), 6);

    // list status
    GtkWidget* info = gtk_info_bar_new();
    GtkWidget* infoLabel = gtk_label_new(nullptr);
    gtk_label_set_line_wrap(GTK_LABEL(infoLabel), TRUE);
    gtk_label_set_xalign(GTK_LABEL(infoLabel), 0);
    gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(info))), infoLabel);
    std::string infoText;
    if (!A.list.ok) {
        gtk_info_bar_set_message_type(GTK_INFO_BAR(info), GTK_MESSAGE_WARNING);
        infoText = "The plugin list could not be loaded: " + A.list.error;
    } else if (A.list.testMode) {
        gtk_info_bar_set_message_type(GTK_INFO_BAR(info), GTK_MESSAGE_WARNING);
        infoText = "TEST MODE - unsigned plugin list " + A.list.source + " (version " + A.list.version + ", " + A.list.arch + "). "
                   "Delete that file to use the signed nppPluginList.so again.";
    } else {
        gtk_info_bar_set_message_type(GTK_INFO_BAR(info), GTK_MESSAGE_INFO);
        infoText = "Plugin list version " + A.list.version + " (" + A.list.arch + ") - signature verified - " + A.list.source;
    }
    gtk_label_set_text(GTK_LABEL(infoLabel), infoText.c_str());
    gtk_box_pack_start(GTK_BOX(area), info, FALSE, FALSE, 0);

    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(top), gtk_label_new_with_mnemonic("_Search:"), FALSE, FALSE, 0);
    A.search = gtk_search_entry_new();
    gtk_widget_set_hexpand(A.search, TRUE);
    gtk_box_pack_start(GTK_BOX(top), A.search, TRUE, TRUE, 0);
    A.action = gtk_button_new_with_mnemonic("_Install");
    gtk_style_context_add_class(gtk_widget_get_style_context(A.action), "suggested-action");
    gtk_box_pack_end(GTK_BOX(top), A.action, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(area), top, FALSE, FALSE, 0);

    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    A.notebook = gtk_notebook_new();
    static const char* titles[] = {"Available", "Updates", "Installed", "Incompatible"};
    for (int pg = 0; pg < 4; ++pg) {
        A.views[pg] = makeView(A, pg == P_UPDATES || pg == P_INSTALLED || pg == P_INCOMPATIBLE);
        GtkWidget* sw = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(sw), A.views[pg]);
        gtk_notebook_append_page(GTK_NOTEBOOK(A.notebook), sw, gtk_label_new(titles[pg]));
    }
    gtk_paned_pack1(GTK_PANED(paned), A.notebook, TRUE, FALSE);
    A.desc = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(A.desc), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(A.desc), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(A.desc), 6);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(A.desc), 4);
    GtkWidget* dsw = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(dsw), A.desc);
    gtk_widget_set_size_request(dsw, -1, 140);
    gtk_paned_pack2(GTK_PANED(paned), dsw, FALSE, FALSE);
    gtk_box_pack_start(GTK_BOX(area), paned, TRUE, TRUE, 0);

    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    A.status = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(A.status), 0);
    gtk_label_set_ellipsize(GTK_LABEL(A.status), PANGO_ELLIPSIZE_END);
    A.progress = gtk_progress_bar_new();
    gtk_widget_set_no_show_all(A.progress, TRUE);
    gtk_box_pack_start(GTK_BOX(bottom), A.status, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(bottom), A.progress, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(area), bottom, FALSE, FALSE, 0);
    std::string hint = "Plugins are installed into " + paths::userPluginsDir();
    gtk_label_set_text(GTK_LABEL(A.status), hint.c_str());

    g_signal_connect_swapped(A.search, "search-changed", CAM_CB(+[](Admin* a) { fill(*a); }), &A);
    // "switch-page" fires before the current page changes: use the page number it gives
    g_signal_connect(A.notebook, "switch-page", CAM_CB(+[](GtkNotebook*, GtkWidget*, guint page, gpointer a) {
        updateAction(*static_cast<Admin*>(a), int(page));
    }), &A);
    g_signal_connect_swapped(A.action, "clicked", CAM_CB(+[](Admin* a) {
        int pg = gtk_notebook_get_current_page(GTK_NOTEBOOK(a->notebook));
        if (pg == P_AVAILABLE || pg == P_UPDATES) doInstall(*a, pg);
        else if (pg == P_INSTALLED) doRemove(*a);
    }), &A);
    g_signal_connect(A.dlg, "response", CAM_CB(+[](GtkDialog*, gint, gpointer a) { static_cast<Admin*>(a)->cancel = true; }), &A);

    fill(A);
    gtk_widget_show_all(A.dlg);
    updateAction(A);
    while (true) {
        int r = gtk_dialog_run(GTK_DIALOG(A.dlg));
        if (A.busy && r != GTK_RESPONSE_DELETE_EVENT) continue;
        break;
    }
    gtk_widget_destroy(A.dlg);
}

} // namespace cam
