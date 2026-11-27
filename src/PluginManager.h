// Camelion++ - loads Linux plugins (.so) and serves the Notepad++-style plugin API
#pragma once
#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <memory>
#include <set>
#include <CamelionPluginInterface.h>

namespace cam {
class MainWindow;

struct LoadedPlugin {
    std::string folder;        // folder name == plugin id in the plugin list
    std::string path;          // full path of the .so
    std::string name;          // getName()
    std::string version;       // camelionPluginVersion, may be empty
    bool system = false;       // lives in the read-only system plugins dir
    void* handle = nullptr;
    PFUNCSETINFO setInfo = nullptr;
    PFUNCGETNAME getName = nullptr;
    PFUNCGETFUNCSARRAY getFuncsArray = nullptr;
    PBENOTIFIED beNotified = nullptr;
    PMESSAGEPROC messageProc = nullptr;
    FuncItem* funcs = nullptr;
    int nbFuncs = 0;
    int firstCmd = 0;
    GtkWidget* menu = nullptr;
};

struct FailedPlugin { std::string folder, path, reason; };

class PluginManager {
public:
    explicit PluginManager(MainWindow* mw);
    ~PluginManager();

    void loadAll();
    LoadedPlugin* loadPlugin(const std::string& soPath, const std::string& folder, bool system, std::string* err);
    void buildMenu();
    void notify(SCNotification* n);
    void runCommand(int id);

    HWND nppHandle() const;
    HWND scintillaHandle() const;
    size_t count() const { return _plugins.size(); }
    std::vector<std::string> names() const;
    const std::vector<std::unique_ptr<LoadedPlugin>>& plugins() const { return _plugins; }
    const std::vector<FailedPlugin>& failed() const { return _failed; }
    LoadedPlugin* byFolder(const std::string& folder) const;

    static LRESULT sendMessageThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    LRESULT handleNppMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void setItemChecked(int cmdId, bool on);
    NppData nppData() const;

    MainWindow* _mw;
    std::vector<std::unique_ptr<LoadedPlugin>> _plugins;
    std::vector<FailedPlugin> _failed;
    std::set<GtkWidget*> _extraScintillas;
    int _nextCmd;
    int _nextDynamicCmd;
    int _nextMarker = 10;
    int _nextIndicator = 9;
    bool _guard = false;
    static PluginManager* s_instance;
};

} // namespace cam
