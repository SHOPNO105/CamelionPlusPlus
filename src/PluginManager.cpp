// Camelion++ - loads Linux plugins (.so) and serves the Notepad++-style plugin API
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
#include "PluginManager.h"
#include "MainWindow.h"
#include "Parameters.h"
#include "Settings.h"
#include "Paths.h"
#include "LangDefs.h"
#include "ElfReader.h"
#include "Version.h"
#include <dlfcn.h>
#include <dirent.h>
#include <algorithm>
#include <cstring>

namespace cam {

PluginManager* PluginManager::s_instance = nullptr;

// Opaque handles given to plugins
static char g_nppToken, g_sciMainToken, g_sciSecondToken;

PluginManager::PluginManager(MainWindow* mw) : _mw(mw), _nextCmd(cmd::PLUGINS_CMD_FIRST), _nextDynamicCmd(cmd::PLUGINS_CMD_DYNAMIC_FIRST)
{
    s_instance = this;
}

PluginManager::~PluginManager()
{
    // Plugins are intentionally not dlclose()d: their code may still be referenced
    // by GTK signal handlers until the process exits.
    s_instance = nullptr;
}

HWND PluginManager::nppHandle() const { return &g_nppToken; }
HWND PluginManager::scintillaHandle() const { return &g_sciMainToken; }

NppData PluginManager::nppData() const
{
    NppData d{};
    d._nppHandle = &g_nppToken;
    d._scintillaMainHandle = &g_sciMainToken;
    d._scintillaSecondHandle = &g_sciSecondToken;
    d._sendMessage = &PluginManager::sendMessageThunk;
    d._apiVersion = CAMELION_PLUGIN_API_VERSION;
    return d;
}

std::vector<std::string> PluginManager::names() const
{
    std::vector<std::string> v;
    for (auto& p : _plugins) v.push_back(p->name + (p->version.empty() ? "" : " " + p->version) + "  (" + p->path + ")");
    return v;
}

LoadedPlugin* PluginManager::byFolder(const std::string& folder) const
{
    for (auto& p : _plugins) if (g_ascii_strcasecmp(p->folder.c_str(), folder.c_str()) == 0) return p.get();
    return nullptr;
}

void PluginManager::loadAll()
{
    if (!Settings::get().getBool("plugins.enabled", true) || g_getenv("CAMELION_NO_PLUGINS")) return;
    std::set<std::string> seen;
    auto dirs = paths::pluginDirs();
    for (size_t di = 0; di < dirs.size(); ++di) {
        const std::string& dir = dirs[di];
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        std::vector<std::string> folders;
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n == "." || n == ".." || n == "Config" || n == "disabled" || n == "APIs" || n[0] == '.') continue;
            if (paths::dirExists(paths::join(dir, n))) folders.push_back(n);
        }
        closedir(d);
        std::sort(folders.begin(), folders.end(), [](auto& a, auto& b) { return g_ascii_strcasecmp(a.c_str(), b.c_str()) < 0; });
        for (auto& f : folders) {
            std::string lower = f; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (seen.count(lower)) continue;   // a user copy overrides the system copy
            std::string so = paths::join(paths::join(dir, f), f + ".so");
            if (!paths::fileExists(so)) continue;
            seen.insert(lower);
            std::string err;
            if (!loadPlugin(so, f, di > 0, &err)) _failed.push_back({f, so, err});
        }
    }
}

LoadedPlugin* PluginManager::loadPlugin(const std::string& soPath, const std::string& folder, bool system, std::string* err)
{
    if (byFolder(folder)) { if (err) *err = "already loaded"; return nullptr; }
    // Check the architecture before letting the dynamic loader near the file
    std::string bytes = paths::readFile(soPath);
    ElfInfo info = elfInfo(bytes);
    if (!info.ok) { if (err) *err = "not a valid ELF shared object: " + info.error; return nullptr; }
    if (info.machine != hostElfMachine()) { if (err) *err = "built for another CPU architecture (" + elfMachineName(info.machine) + ")"; return nullptr; }
    std::vector<std::string> syms;
    elfDynamicSymbols(bytes, syms);
    for (const char* req : {"setInfo", "getName", "getFuncsArray", "beNotified", "messageProc", "camelionPluginApiVersion"})
        if (std::find(syms.begin(), syms.end(), req) == syms.end()) {
            if (err) *err = std::string("missing exported function ") + req + "() - is this a Notepad++ (Windows) plugin?";
            return nullptr;
        }

    void* h = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) { if (err) *err = dlerror(); return nullptr; }
    auto p = std::make_unique<LoadedPlugin>();
    p->folder = folder;
    p->path = soPath;
    p->system = system;
    p->handle = h;
    p->setInfo = reinterpret_cast<PFUNCSETINFO>(dlsym(h, "setInfo"));
    p->getName = reinterpret_cast<PFUNCGETNAME>(dlsym(h, "getName"));
    p->getFuncsArray = reinterpret_cast<PFUNCGETFUNCSARRAY>(dlsym(h, "getFuncsArray"));
    p->beNotified = reinterpret_cast<PBENOTIFIED>(dlsym(h, "beNotified"));
    p->messageProc = reinterpret_cast<PMESSAGEPROC>(dlsym(h, "messageProc"));
    auto apiVer = reinterpret_cast<PFUNCAPIVERSION>(dlsym(h, "camelionPluginApiVersion"));
    if (const char* v = static_cast<const char*>(dlsym(h, "camelionPluginVersion"))) p->version = v;
    if (!apiVer || apiVer() > CAMELION_PLUGIN_API_VERSION || apiVer() < 1) {
        if (err) *err = "requires a newer plugin API (" + std::to_string(apiVer ? apiVer() : 0) + ")";
        dlclose(h);
        return nullptr;
    }
    try {
        p->setInfo(nppData());
        const char* n = p->getName();
        p->name = n ? n : folder;
        p->funcs = p->getFuncsArray(&p->nbFuncs);
    } catch (...) {
        if (err) *err = "the plugin threw an exception while initialising";
        return nullptr;   // leave it loaded: unloading code that may be referenced is unsafe
    }
    if (p->nbFuncs < 0 || p->nbFuncs > 500) p->nbFuncs = 0;
    p->firstCmd = _nextCmd;
    for (int i = 0; i < p->nbFuncs; ++i) p->funcs[i]._cmdID = _nextCmd++;
    LoadedPlugin* raw = p.get();
    _plugins.push_back(std::move(p));
    return raw;
}

// Windows virtual-key code -> GDK keyval
static guint vkToKeyval(unsigned char vk)
{
    if (vk >= 'A' && vk <= 'Z') return GDK_KEY_a + (vk - 'A');
    if (vk >= '0' && vk <= '9') return GDK_KEY_0 + (vk - '0');
    if (vk >= 0x70 && vk <= 0x7B) return GDK_KEY_F1 + (vk - 0x70);
    if (vk >= 0x60 && vk <= 0x69) return GDK_KEY_KP_0 + (vk - 0x60);
    switch (vk) {
    case 0x08: return GDK_KEY_BackSpace; case 0x09: return GDK_KEY_Tab; case 0x0D: return GDK_KEY_Return;
    case 0x1B: return GDK_KEY_Escape; case 0x20: return GDK_KEY_space; case 0x21: return GDK_KEY_Page_Up;
    case 0x22: return GDK_KEY_Page_Down; case 0x23: return GDK_KEY_End; case 0x24: return GDK_KEY_Home;
    case 0x25: return GDK_KEY_Left; case 0x26: return GDK_KEY_Up; case 0x27: return GDK_KEY_Right; case 0x28: return GDK_KEY_Down;
    case 0x2D: return GDK_KEY_Insert; case 0x2E: return GDK_KEY_Delete;
    case 0x6A: return GDK_KEY_KP_Multiply; case 0x6B: return GDK_KEY_KP_Add; case 0x6D: return GDK_KEY_KP_Subtract;
    case 0x6E: return GDK_KEY_KP_Decimal; case 0x6F: return GDK_KEY_KP_Divide;
    case 0xBA: return GDK_KEY_semicolon; case 0xBB: return GDK_KEY_equal; case 0xBC: return GDK_KEY_comma;
    case 0xBD: return GDK_KEY_minus; case 0xBE: return GDK_KEY_period; case 0xBF: return GDK_KEY_slash;
    case 0xC0: return GDK_KEY_grave; case 0xDB: return GDK_KEY_bracketleft; case 0xDC: return GDK_KEY_backslash;
    case 0xDD: return GDK_KEY_bracketright; case 0xDE: return GDK_KEY_apostrophe;
    default: return 0;
    }
}

void PluginManager::buildMenu()
{
    GtkWidget* menu = _mw->pluginsMenu();
    if (!menu) return;
    GList* ch = gtk_container_get_children(GTK_CONTAINER(menu));
    for (GList* l = ch; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(ch);
    auto sorted = std::vector<LoadedPlugin*>();
    for (auto& p : _plugins) sorted.push_back(p.get());
    std::sort(sorted.begin(), sorted.end(), [](auto* a, auto* b) { return g_ascii_strcasecmp(a->name.c_str(), b->name.c_str()) < 0; });
    for (LoadedPlugin* p : sorted) {
        GtkWidget* top = gtk_menu_item_new_with_label(p->name.c_str());
        GtkWidget* sub = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(top), sub);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), top);
        p->menu = sub;
        for (int i = 0; i < p->nbFuncs; ++i) {
            FuncItem& f = p->funcs[i];
            std::string label(f._itemName, strnlen(f._itemName, menuItemSize));
            if (label.empty() || label == "---" || !f._pFunc) { gtk_menu_shell_append(GTK_MENU_SHELL(sub), gtk_separator_menu_item_new()); continue; }
            GtkWidget* it = f._init2Check ? gtk_check_menu_item_new_with_label(label.c_str()) : gtk_menu_item_new_with_label(label.c_str());
            if (f._init2Check) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(it), TRUE);
            g_object_set_data(G_OBJECT(it), "cam-cmd", GINT_TO_POINTER(f._cmdID));
            g_signal_connect(it, "activate", CAM_CB(+[](GtkWidget* w, gpointer self) {
                auto* pm = static_cast<PluginManager*>(self);
                if (pm->_guard) return;
                pm->runCommand(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cam-cmd")));
            }), this);
            if (f._pShKey && f._pShKey->_key) {
                guint key = vkToKeyval(f._pShKey->_key);
                guint mods = (f._pShKey->_isCtrl ? GDK_CONTROL_MASK : 0) | (f._pShKey->_isAlt ? GDK_MOD1_MASK : 0) | (f._pShKey->_isShift ? GDK_SHIFT_MASK : 0);
                if (key) gtk_widget_add_accelerator(it, "activate", _mw->accelGroup(), key, GdkModifierType(mods), GTK_ACCEL_VISIBLE);
            }
            gtk_menu_shell_append(GTK_MENU_SHELL(sub), it);
        }
    }
    if (!_plugins.empty()) gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    _mw->addMenuItem(menu, cmd::PLUGINS_ADMIN);
    _mw->addMenuItem(menu, cmd::PLUGINS_OPENFOLDER);
    gtk_widget_show_all(menu);
}

void PluginManager::runCommand(int id)
{
    for (auto& p : _plugins) {
        if (id < p->firstCmd || id >= p->firstCmd + p->nbFuncs) continue;
        FuncItem& f = p->funcs[id - p->firstCmd];
        if (f._pFunc) { try { f._pFunc(); } catch (...) { _mw->showMessage(GTK_MESSAGE_ERROR, p->name + " crashed while running \"" + f._itemName + "\"."); } }
        return;
    }
}

void PluginManager::notify(SCNotification* n)
{
    for (auto& p : _plugins) {
        if (!p->beNotified) continue;
        try { p->beNotified(n); } catch (...) {}
    }
}

void PluginManager::setItemChecked(int cmdId, bool on)
{
    for (auto& p : _plugins) {
        if (!p->menu || cmdId < p->firstCmd || cmdId >= p->firstCmd + p->nbFuncs) continue;
        GList* ch = gtk_container_get_children(GTK_CONTAINER(p->menu));
        int pos = 0;
        for (GList* l = ch; l; l = l->next, ++pos) {
            GtkWidget* w = GTK_WIDGET(l->data);
            if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "cam-cmd")) != cmdId) continue;
            if (!GTK_IS_CHECK_MENU_ITEM(w)) {
                // turn a plain item into a check item at the same position
                const char* label = gtk_menu_item_get_label(GTK_MENU_ITEM(w));
                GtkWidget* c = gtk_check_menu_item_new_with_label(label);
                g_object_set_data(G_OBJECT(c), "cam-cmd", GINT_TO_POINTER(cmdId));
                g_signal_connect(c, "activate", CAM_CB(+[](GtkWidget* x, gpointer self) {
                    auto* pm = static_cast<PluginManager*>(self);
                    if (pm->_guard) return;
                    pm->runCommand(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(x), "cam-cmd")));
                }), this);
                FuncItem& f = p->funcs[cmdId - p->firstCmd];
                if (f._pShKey && f._pShKey->_key) {
                    guint key = vkToKeyval(f._pShKey->_key);
                    guint mods = (f._pShKey->_isCtrl ? GDK_CONTROL_MASK : 0) | (f._pShKey->_isAlt ? GDK_MOD1_MASK : 0) | (f._pShKey->_isShift ? GDK_SHIFT_MASK : 0);
                    if (key) gtk_widget_add_accelerator(c, "activate", _mw->accelGroup(), key, GdkModifierType(mods), GTK_ACCEL_VISIBLE);
                }
                gtk_widget_destroy(w);
                gtk_menu_shell_insert(GTK_MENU_SHELL(p->menu), c, pos);
                gtk_widget_show(c);
                w = c;
            }
            _guard = true;
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), on);
            _guard = false;
            break;
        }
        g_list_free(ch);
        return;
    }
}

LRESULT PluginManager::sendMessageThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PluginManager* pm = s_instance;
    if (!pm) return 0;
    if (hwnd == &g_nppToken) return pm->handleNppMessage(msg, wParam, lParam);
    if (hwnd == &g_sciMainToken || hwnd == &g_sciSecondToken || hwnd == static_cast<HWND>(pm->_mw->editor().widget()))
        return pm->_mw->editor().call(msg, wParam, lParam);
    GtkWidget* w = static_cast<GtkWidget*>(hwnd);
    if (pm->_extraScintillas.count(w)) return scintilla_send_message(SCINTILLA(w), msg, wParam, lParam);
    return 0;
}

static LRESULT copyOut(const std::string& s, WPARAM size, LPARAM buf)
{
    char* out = reinterpret_cast<char*>(buf);
    if (!out || size == 0) return FALSE;
    if (s.size() + 1 > size) { out[0] = 0; return FALSE; }
    std::memcpy(out, s.c_str(), s.size() + 1);
    return TRUE;
}

static LRESULT lengthOrCopy(const std::string& s, LPARAM buf)
{
    if (buf) std::memcpy(reinterpret_cast<char*>(buf), s.c_str(), s.size() + 1);
    return LRESULT(s.size());
}

LRESULT PluginManager::handleNppMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow& mw = *_mw;
    Buffer* cur = mw.current();
    auto bufferOf = [&](WPARAM id) { return mw.bufferById(int(id)); };
    switch (msg) {
    case NPPM_GETCURRENTSCINTILLA: if (lParam) *reinterpret_cast<int*>(lParam) = 0; return TRUE;
    case NPPM_GETCURRENTLANGTYPE: if (lParam && cur) *reinterpret_cast<int*>(lParam) = cur->langType; return TRUE;
    case NPPM_SETCURRENTLANGTYPE: if (cur) { cur->langLocked = true; mw.setLanguage(cur, int(lParam)); } return TRUE;
    case NPPM_GETNBOPENFILES: return lParam == SECOND_VIEW ? 0 : LRESULT(mw.buffers().size());
    case NPPM_GETCURRENTDOCINDEX: return lParam == SUB_VIEW ? -1 : mw.indexOf(cur);
    case NPPM_GETCURRENTVIEW: return MAIN_VIEW;
    case NPPM_SETSTATUSBAR: if (lParam) mw.setStatusText(int(wParam), reinterpret_cast<const char*>(lParam)); return TRUE;
    case NPPM_GETMENUHANDLE: return reinterpret_cast<LRESULT>(wParam == NPPMAINMENU ? mw.menuBar() : mw.pluginsMenu());
    case NPPM_ACTIVATEDOC: {
        if (wParam != MAIN_VIEW) return FALSE;
        size_t i = size_t(lParam);
        if (i < mw.buffers().size()) { mw.activate(mw.buffers()[i].get()); return TRUE; }
        return FALSE;
    }
    case NPPM_RELOADFILE: { Buffer* b = lParam ? mw.bufferByPath(reinterpret_cast<const char*>(lParam)) : nullptr; return b && mw.reloadBuffer(b, wParam != 0); }
    case NPPM_SWITCHTOFILE: { Buffer* b = lParam ? mw.bufferByPath(reinterpret_cast<const char*>(lParam)) : nullptr; if (b) mw.activate(b); return b != nullptr; }
    case NPPM_SAVECURRENTFILE: return mw.saveBuffer(cur);
    case NPPM_SAVEALLFILES: return mw.saveAll();
    case NPPM_SAVECURRENTFILEAS: return lParam && mw.saveBufferAs(cur, reinterpret_cast<const char*>(lParam), wParam != 0);
    case NPPM_SAVEFILE: { Buffer* b = lParam ? mw.bufferByPath(reinterpret_cast<const char*>(lParam)) : nullptr; return b && mw.saveBuffer(b); }
    case NPPM_DOOPEN: return lParam && mw.openFile(reinterpret_cast<const char*>(lParam)) != nullptr;
    case NPPM_SETMENUITEMCHECK: setItemChecked(int(wParam), lParam != 0); mw.setChecked(int(wParam), lParam != 0); return TRUE;
    case NPPM_MAKECURRENTBUFFERDIRTY: if (cur) { cur->dirty = true; mw.updateTab(cur); mw.updateTitle(); } return TRUE;
    case NPPM_GETPLUGINSCONFIGDIR: paths::mkdirs(paths::pluginsConfigDir()); return copyOut(paths::pluginsConfigDir(), wParam, lParam);
    case NPPM_GETPLUGINHOMEPATH: return lengthOrCopy(paths::userPluginsDir(), lParam);
    case NPPM_GETNPPSETTINGSDIRPATH: return lengthOrCopy(paths::configDir(), lParam);
    case NPPM_MENUCOMMAND: mw.runCommand(int(lParam)); return TRUE;
    case NPPM_GETNPPVERSION: {
        int major = CAMELION_VERSION_MAJOR, minor = CAMELION_VERSION_MINOR, patch = CAMELION_VERSION_PATCH;
        int low = wParam ? minor * 100 + patch * 10 : (patch ? minor * 10 + patch : minor);
        return (LRESULT(major) << 16) | LRESULT(low & 0xFFFF);
    }
    case NPPM_HIDETABBAR: { bool prev = !Settings::get().getBool("view.tabBar", true); Settings::get().set("view.tabBar", lParam == 0); mw.runCommand(-1); return prev; }
    case NPPM_ISTABBARHIDDEN: return !Settings::get().getBool("view.tabBar", true);
    case NPPM_HIDETOOLBAR: { bool prev = !Settings::get().getBool("view.toolbar", true); Settings::get().set("view.toolbar", lParam == 0); mw.runCommand(-1); return prev; }
    case NPPM_ISTOOLBARHIDDEN: return !Settings::get().getBool("view.toolbar", true);
    case NPPM_HIDESTATUSBAR: { bool prev = !Settings::get().getBool("view.statusBar", true); Settings::get().set("view.statusBar", lParam == 0); mw.runCommand(-1); return prev; }
    case NPPM_ISSTATUSBARHIDDEN: return !Settings::get().getBool("view.statusBar", true);
    case NPPM_HIDEMENU: { bool prev = !gtk_widget_get_visible(mw.menuBar()); gtk_widget_set_visible(mw.menuBar(), lParam == 0); return prev; }
    case NPPM_ISMENUHIDDEN: return !gtk_widget_get_visible(mw.menuBar());
    case NPPM_GETPOSFROMBUFFERID: { Buffer* b = bufferOf(wParam); return b ? mw.indexOf(b) : -1; }
    case NPPM_GETFULLPATHFROMBUFFERID: { Buffer* b = bufferOf(wParam); if (!b) return -1; return lengthOrCopy(b->path.empty() ? b->untitledName : b->path, lParam); }
    case NPPM_GETBUFFERIDFROMPOS: { if (lParam != MAIN_VIEW) return 0; size_t i = size_t(wParam); return i < mw.buffers().size() ? mw.buffers()[i]->id : 0; }
    case NPPM_GETCURRENTBUFFERID: return cur ? cur->id : 0;
    case NPPM_RELOADBUFFERID: { Buffer* b = bufferOf(wParam); return b && mw.reloadBuffer(b, lParam != 0); }
    case NPPM_GETBUFFERLANGTYPE: { Buffer* b = bufferOf(wParam); return b ? b->langType : -1; }
    case NPPM_SETBUFFERLANGTYPE: { Buffer* b = bufferOf(wParam); if (!b) return FALSE; b->langLocked = true; mw.setLanguage(b, int(lParam)); return TRUE; }
    case NPPM_GETBUFFERENCODING: { Buffer* b = bufferOf(wParam); return b ? int(b->mode) : -1; }
    case NPPM_SETBUFFERENCODING: { Buffer* b = bufferOf(wParam); if (!b || lParam < 0 || lParam > 4) return FALSE; mw.convertEncoding(b, UniMode(lParam), b->charset); return TRUE; }
    case NPPM_GETBUFFERFORMAT: { Buffer* b = bufferOf(wParam); return b ? int(b->eol) : -1; }
    case NPPM_SETBUFFERFORMAT: { Buffer* b = bufferOf(wParam); if (!b || lParam < 0 || lParam > 2) return FALSE; mw.setEol(b, EolType(lParam)); return TRUE; }
    case NPPM_ALLOCATECMDID: {
        int n = int(wParam);
        if (n <= 0 || !lParam || _nextDynamicCmd + n > cmd::PLUGINS_CMD_DYNAMIC_LAST) return FALSE;
        *reinterpret_cast<int*>(lParam) = _nextDynamicCmd; _nextDynamicCmd += n; return TRUE;
    }
    case NPPM_ALLOCATEMARKER: {
        int n = int(wParam);
        if (n <= 0 || !lParam || _nextMarker + n > MARK_BOOKMARK) return FALSE;
        *reinterpret_cast<int*>(lParam) = _nextMarker; _nextMarker += n; return TRUE;
    }
    case NPPM_ALLOCATEINDICATOR: {
        int n = int(wParam);
        if (n <= 0 || !lParam || _nextIndicator + n > 21) return FALSE;
        *reinterpret_cast<int*>(lParam) = _nextIndicator; _nextIndicator += n; return TRUE;
    }
    case NPPM_GETLANGUAGENAME: return lengthOrCopy(langByType(int(wParam))->shortName, lParam);
    case NPPM_GETLANGUAGEDESC: return lengthOrCopy(langByType(int(wParam))->longName, lParam);
    case NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR: return Parameters::get().defaultFg();
    case NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR: return Parameters::get().defaultBg();
    case NPPM_ISAUTOINDENTON: return Settings::get().getBool("editor.autoIndent", true);
    case NPPM_GETCURRENTMACROSTATUS: return mw.isRecording() ? 1 : (mw.macro().empty() ? 0 : 3);
    case NPPM_ISDARKMODEENABLED: return Parameters::get().isDarkTheme();
    case NPPM_GETBOOKMARKID: return MARK_BOOKMARK;
    case NPPM_CREATESCINTILLAHANDLE: {
        GtkWidget* w = scintilla_new();
        g_object_ref_sink(w);
        scintilla_send_message(SCINTILLA(w), SCI_SETCODEPAGE, SC_CP_UTF8, 0);
        _extraScintillas.insert(w);
        return reinterpret_cast<LRESULT>(w);
    }
    case NPPM_DESTROYSCINTILLAHANDLE: {
        GtkWidget* w = reinterpret_cast<GtkWidget*>(lParam);
        if (_extraScintillas.erase(w)) { g_object_unref(w); return TRUE; }
        return FALSE;
    }
    case NPPM_MSGTOPLUGIN: {
        const char* dest = reinterpret_cast<const char*>(wParam);
        if (!dest) return FALSE;
        std::string d = dest;
        if (d.size() > 3 && d.substr(d.size() - 3) == ".so") d = d.substr(0, d.size() - 3);
        LoadedPlugin* p = byFolder(d);
        if (!p || !p->messageProc) return FALSE;
        try { p->messageProc(NPPM_MSGTOPLUGIN, 0, lParam); } catch (...) {}
        return TRUE;
    }

    // RUNCOMMAND_USER family
    case NPPM_GETFULLCURRENTPATH: return copyOut(cur ? (cur->path.empty() ? cur->untitledName : cur->path) : "", wParam, lParam);
    case NPPM_GETCURRENTDIRECTORY: return copyOut(cur && !cur->path.empty() ? paths::dirName(cur->path) : "", wParam, lParam);
    case NPPM_GETFILENAME: return copyOut(cur ? cur->displayName() : "", wParam, lParam);
    case NPPM_GETNAMEPART: {
        std::string f = cur ? cur->displayName() : "";
        size_t dot = f.rfind('.');
        return copyOut(dot == std::string::npos || dot == 0 ? f : f.substr(0, dot), wParam, lParam);
    }
    case NPPM_GETEXTPART: {
        std::string f = cur ? cur->displayName() : "";
        size_t dot = f.rfind('.');
        return copyOut(dot == std::string::npos || dot == 0 ? "" : f.substr(dot), wParam, lParam);
    }
    case NPPM_GETCURRENTWORD: return copyOut(mw.editor().selectedText().empty() ? mw.editor().currentWord() : mw.editor().selectedText(), wParam, lParam);
    case NPPM_GETNPPDIRECTORY: return copyOut(paths::dirName(paths::exePath()), wParam, lParam);
    case NPPM_GETNPPFULLFILEPATH: return copyOut(paths::exePath(), wParam, lParam);
    case NPPM_GETFILENAMEATCURSOR: {
        Editor& e = mw.editor();
        sptr_t pos = e.call(SCI_GETCURRENTPOS), line = e.call(SCI_LINEFROMPOSITION, uptr_t(pos));
        std::string t = e.lineText(line);
        long col = long(pos - e.call(SCI_POSITIONFROMLINE, uptr_t(line)));
        auto isPathChar = [](char c) { return std::isalnum((unsigned char)c) || std::strchr("/._-~+:@%", c); };
        long a = col, b = col;
        while (a > 0 && isPathChar(t[size_t(a - 1)])) --a;
        while (size_t(b) < t.size() && isPathChar(t[size_t(b)])) ++b;
        return copyOut(t.substr(size_t(a), size_t(b - a)), wParam, lParam);
    }
    case NPPM_GETCURRENTLINESTR: {
        Editor& e = mw.editor();
        return copyOut(e.lineText(e.call(SCI_LINEFROMPOSITION, uptr_t(e.call(SCI_GETCURRENTPOS)))), wParam, lParam);
    }
    case NPPM_GETCURRENTLINE: { Editor& e = mw.editor(); return e.call(SCI_LINEFROMPOSITION, uptr_t(e.call(SCI_GETCURRENTPOS))); }
    case NPPM_GETCURRENTCOLUMN: { Editor& e = mw.editor(); return e.call(SCI_GETCOLUMN, uptr_t(e.call(SCI_GETCURRENTPOS))); }

    // Camelion++ specific
    case CAMM_GETMAINWINDOW: return reinterpret_cast<LRESULT>(mw.window());
    case CAMM_ADDPANEL: {
        GtkWidget* w = reinterpret_cast<GtkWidget*>(lParam);
        const char* title = reinterpret_cast<const char*>(wParam);
        if (!w || !GTK_IS_WIDGET(w)) return 0;
        return mw.addSidePanel(title ? title : "Plugin", w);
    }
    case CAMM_SHOWPANEL: mw.showSidePanel(int(wParam), lParam != 0); return TRUE;
    case CAMM_GETSCINTILLAWIDGET: return reinterpret_cast<LRESULT>(mw.editor().widget());
    case CAMM_GETVERSIONSTRING: return copyOut(CAMELION_VERSION_STRING, wParam, lParam);
    default: return 0;
    }
}

} // namespace cam
