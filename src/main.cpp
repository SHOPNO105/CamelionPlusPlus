// Camelion++ - entry point
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
#include <gtk/gtk.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include "MainWindow.h"
#include "Parameters.h"
#include "Settings.h"
#include "Paths.h"
#include "LangDefs.h"
#include "PluginList.h"
#include "PluginsAdmin.h"
#include "ElfReader.h"
#include "Version.h"

using namespace cam;

namespace cam { bool g_noSession = false; }

static MainWindow* g_main = nullptr;

struct CmdLine {
    std::vector<std::string> files;
    long line = -1, column = -1;
    int langType = -1;
    bool readOnly = false, noSession = false, noPlugin = false, multiInst = false;
};

static CmdLine parseArgs(const std::vector<std::string>& args, const std::string& cwd)
{
    CmdLine c;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto num = [&](size_t off) { try { return std::stol(a.substr(off)); } catch (...) { return -1L; } };
        if (a.compare(0, 2, "-n") == 0 && a.size() > 2 && isdigit((unsigned char)a[2])) c.line = num(2);
        else if (a.compare(0, 2, "-c") == 0 && a.size() > 2 && isdigit((unsigned char)a[2])) c.column = num(2);
        else if (a.size() > 1 && a[0] == '+' && isdigit((unsigned char)a[1])) c.line = num(1);
        else if (a.compare(0, 2, "-l") == 0 && a.size() > 2) { if (auto l = langByName(a.substr(2))) c.langType = l->langType; }
        else if (a == "-ro") c.readOnly = true;
        else if (a == "-nosession") c.noSession = true;
        else if (a == "-noPlugin" || a == "--no-plugins") c.noPlugin = true;
        else if (a == "-multiInst" || a == "--new-instance") c.multiInst = true;
        else if (a == "--") { for (++i; i < args.size(); ++i) c.files.push_back(args[i]); }
        else if (!a.empty() && a[0] == '-' && a.size() > 1) { /* unknown option: ignore, like Notepad++ */ }
        else {
            std::string p = a;
            if (p[0] != '/' && p.compare(0, 7, "file://") != 0) p = paths::join(cwd, p);
            // "file:line[:col]" (compiler / grep output style)
            if (!paths::fileExists(p)) {
                size_t c1 = p.rfind(':');
                if (c1 != std::string::npos && c1 + 1 < p.size() && p.find_first_not_of("0123456789", c1 + 1) == std::string::npos) {
                    std::string head = p.substr(0, c1);
                    long n1 = std::stol(p.substr(c1 + 1));
                    size_t c2 = head.rfind(':');
                    if (c2 != std::string::npos && c2 + 1 < head.size() && head.find_first_not_of("0123456789", c2 + 1) == std::string::npos &&
                        paths::fileExists(head.substr(0, c2))) {
                        c.line = std::stol(head.substr(c2 + 1)); c.column = n1; p = head.substr(0, c2);
                    } else if (paths::fileExists(head)) { c.line = n1; p = head; }
                }
            }
            c.files.push_back(p);
        }
    }
    return c;
}

static int onCommandLine(GApplication* app, GApplicationCommandLine* cl, gpointer)
{
    gint argc = 0;
    gchar** argv = g_application_command_line_get_arguments(cl, &argc);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    g_strfreev(argv);
    const gchar* cwd = g_application_command_line_get_cwd(cl);
    CmdLine c = parseArgs(args, cwd ? cwd : ".");
    if (!g_main) {
        if (c.noSession) cam::g_noSession = true;
        if (c.noPlugin) g_setenv("CAMELION_NO_PLUGINS", "1", TRUE);
        g_main = new MainWindow(GTK_APPLICATION(app));
        if (!cam::g_noSession) g_main->restoreSession();
        g_main->show();
    }
    if (!c.files.empty()) g_main->openFromCommandLine(c.files, c.line, c.column, c.langType, c.readOnly);
    else gtk_window_present(g_main->window());
    return 0;
}

static int printPluginListStatus()
{
    PluginListData d = loadPluginList();
    std::printf("host arch     : %s\n", hostPluginArch().c_str());
    std::printf("source        : %s\n", d.source.empty() ? "(none)" : d.source.c_str());
    std::printf("signature     : %s\n", d.testMode ? "TEST MODE (unsigned json)" : d.signatureOk ? "verified" : "not verified");
    if (!d.ok) { std::printf("error         : %s\n", d.error.c_str()); return 2; }
    std::printf("list          : %s %s (%s)\n", d.name.c_str(), d.version.c_str(), d.arch.c_str());
    std::printf("plugins       : %zu\n", d.plugins.size());
    for (auto& p : d.plugins) std::printf("  - %-24s %-10s %s\n", p.folderName.c_str(), p.version.toString().c_str(), p.repository.c_str());
    return 0;
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    bool multi = false;
    for (auto& a : args) {
        if (a == "--version" || a == "-v") { std::printf("Camelion++ %s (Scintilla %s, Lexilla %s)\n", CAMELION_VERSION_STRING, SCINTILLA_VERSION_TEXT, LEXILLA_VERSION_TEXT); return 0; }
        if (a == "--help" || a == "-h") {
            std::printf("Usage: camelion++ [options] [file[:line[:col]] ...]\n\n"
                        "  -n<line>        go to line\n"
                        "  -c<column>      go to column\n"
                        "  +<line>         go to line\n"
                        "  -l<language>    set the language (e.g. -lpython, -lcpp)\n"
                        "  -ro             open read-only\n"
                        "  -nosession      neither restore nor save the session\n"
                        "  -noPlugin       do not load plugins\n"
                        "  -multiInst      start a separate instance\n"
                        "  --check-pluginlist  print the nppPluginList status and exit\n"
                        "  --version       print the version and exit\n");
            return 0;
        }
        if (a == "--check-pluginlist") { Settings::get().load(); return printPluginListStatus(); }
        if (a == "-multiInst" || a == "--new-instance") multi = true;
    }

    Settings::get().load();
    GApplicationFlags flags = GApplicationFlags(G_APPLICATION_HANDLES_COMMAND_LINE | (multi ? G_APPLICATION_NON_UNIQUE : 0));
    GtkApplication* app = gtk_application_new("org.camelion.CamelionPlusPlus", flags);
    g_signal_connect(app, "startup", CAM_CB(+[](GApplication*, gpointer) {
        gtk_window_set_default_icon_name("camelion++");
        Parameters::get().load();
    }), nullptr);
    g_signal_connect(app, "command-line", G_CALLBACK(onCommandLine), nullptr);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    if (cam::g_restartRequested) {
        std::string exe = paths::exePath();
        std::vector<char*> nargv{const_cast<char*>(exe.c_str())};
        if (multi) nargv.push_back(const_cast<char*>("-multiInst"));
        nargv.push_back(nullptr);
        execv(exe.c_str(), nargv.data());
        std::perror("restart");
    }
    return status;
}
