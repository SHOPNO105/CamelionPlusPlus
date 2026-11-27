// Camelion++ - Plugins Admin (install / update / remove plugins from the nppPluginList)
#pragma once
#include <string>

namespace cam {
class MainWindow;
struct PluginInfo;

extern bool g_restartRequested;

class PluginsAdminDlg {
public:
    static void run(MainWindow* mw);

    // Building blocks, also used by the command-line installer (camelion++ --install-plugin)
    static bool download(const std::string& url, std::string& out, std::string& err, bool allowFileUrls,
                         bool (*progress)(void* ctx, double fraction) = nullptr, void* ctx = nullptr);
    static bool installFromArchive(const std::string& zip, const PluginInfo& info, std::string& installedSo, std::string& err);
};

} // namespace cam
