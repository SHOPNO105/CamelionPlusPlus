// Camelion++ - main window
#pragma once
#include <gtk/gtk.h>
#include <memory>
#include <vector>
#include <map>
#include <functional>
#include <string>
#include "Editor.h"
#include "CommandIds.h"

namespace cam {

extern bool g_noSession;   // -nosession on the command line

class FindReplaceDlg;
class PluginManager;
class FunctionListPanel;
class AutoComplete;

struct MacroStep { int msg; uptr_t wParam; sptr_t lParam; std::string text; bool hasText; };

struct Command {
    std::string label;
    std::function<void()> fn;
    GtkWidget* item = nullptr;     // menu item (may be a GtkCheckMenuItem)
    bool checkable = false;
};

class MainWindow {
public:
    explicit MainWindow(GtkApplication* app);
    ~MainWindow();

    GtkWindow* window() const { return GTK_WINDOW(_window); }
    Editor& editor() { return *_editor; }
    void show();
    void restoreSession();
    void openFromCommandLine(const std::vector<std::string>& files, long line, long column, int langType, bool readOnly);

    // ---- buffers
    Buffer* current() const { return _current; }
    const std::vector<std::unique_ptr<Buffer>>& buffers() const { return _buffers; }
    Buffer* bufferById(int id) const;
    Buffer* bufferByPath(const std::string& path) const;
    int indexOf(const Buffer* b) const;
    Buffer* newDocument();
    Buffer* openFile(const std::string& path, bool activateIt = true, bool quietIfMissing = false);
    bool saveBuffer(Buffer* b);
    bool saveBufferAs(Buffer* b, const std::string& path, bool asCopy);
    bool saveAll();
    bool closeBuffer(Buffer* b, bool askToSave = true);
    bool closeAll(Buffer* except = nullptr);
    void activate(Buffer* b);
    bool reloadBuffer(Buffer* b, bool askIfDirty);
    void setLanguage(Buffer* b, int langType);
    void setEol(Buffer* b, EolType e);
    void reinterpretEncoding(Buffer* b, UniMode m, const std::string& charset);
    void convertEncoding(Buffer* b, UniMode m, const std::string& charset);
    std::string promptSaveAsPath(Buffer* b, const char* title);

    // ---- commands
    void runCommand(int id);
    void registerCommand(int id, const std::string& label, std::function<void()> fn, bool checkable = false);
    void setChecked(int id, bool on);
    GtkWidget* addMenuItem(GtkWidget* menu, int id, const char* accel = nullptr);
    GtkWidget* pluginsMenu() const { return _pluginsMenu; }
    GtkWidget* menuBar() const { return _menuBar; }
    GtkAccelGroup* accelGroup() const { return _accel; }

    // ---- UI helpers
    void updateStatusBar();
    void setStatusText(int field, const std::string& text);
    void updateTitle();
    void updateTab(Buffer* b);
    void showMessage(GtkMessageType type, const std::string& primary, const std::string& secondary = "");
    int  askQuestion(const std::string& primary, const std::string& secondary, const std::vector<std::pair<std::string,int>>& buttons);
    void refreshAllStyles();
    void applyThemeToChrome();
    int  addSidePanel(const std::string& title, GtkWidget* w);
    void showSidePanel(int id, bool show);
    void toggleSearchResults(bool show);
    GtkWidget* searchResultsView() const { return _resultsView; }
    void jumpTo(Buffer* b, sptr_t line, sptr_t start, sptr_t end);
    // Runs f with an Editor showing b's document (the main view if b is current, else a hidden view)
    void withBufferEditor(Buffer* b, const std::function<void(Editor&)>& f);

    PluginManager& plugins() { return *_plugins; }
    FindReplaceDlg& findDlg();

    // macro
    bool isRecording() const { return _recording; }
    std::vector<MacroStep>& macro() { return _macro; }

    bool quit();               // returns false if cancelled
    void notifyPlugins(unsigned code, uptr_t idFrom);

private:
    friend struct CommandsImpl;
    void buildUi();
    void buildMenus();
    void buildToolbar();
    void buildStatusBar();
    void registerCommands();     // MainWindowCommands.cpp
    void rebuildRecentMenu();
    void rebuildWindowMenu();
    void rebuildMacroMenu();
    void rebuildRunMenu();
    void syncCheckItems();
    Buffer* createBuffer(sptr_t doc);
    void attachTab(Buffer* b, int position = -1);
    void detachCurrentForSwitch();
    void onScintillaNotify(SCNotification* n);
    void onCharAdded(int ch);
    void onUpdateUI(SCNotification* n);
    void braceMatch();
    void smartHighlight();
    void checkExternalChanges();
    void saveSessionNow();
    void tabContextMenu(Buffer* b, GdkEventButton* ev);
    void dropFiles(GtkSelectionData* data);
    void startMonitoring(Buffer* b, bool on);

    static void onSciNotify(GtkWidget*, gint, SCNotification* n, gpointer self);
    static void onSwitchPage(GtkNotebook*, GtkWidget* page, guint, gpointer self);
    static void onPageReordered(GtkNotebook*, GtkWidget*, guint, gpointer self);
    static gboolean onDelete(GtkWidget*, GdkEvent*, gpointer self);
    static void onActiveChanged(GObject*, GParamSpec*, gpointer self);

    GtkApplication* _app;
    GtkWidget* _window = nullptr;
    GtkWidget* _menuBar = nullptr;
    GtkWidget* _toolbar = nullptr;
    GtkWidget* _notebook = nullptr;
    GtkWidget* _statusBar = nullptr;
    GtkWidget* _statusLabels[6] = {};
    GtkWidget* _sidePaned = nullptr;
    GtkWidget* _sideNotebook = nullptr;
    GtkWidget* _bottomPaned = nullptr;
    GtkWidget* _resultsBox = nullptr;
    GtkWidget* _resultsView = nullptr;
    GtkWidget* _recentMenu = nullptr;
    GtkWidget* _windowMenu = nullptr;
    GtkWidget* _macroMenu = nullptr;
    GtkWidget* _runMenu = nullptr;
    GtkWidget* _pluginsMenu = nullptr;
    GtkWidget* _langMenu = nullptr;
    GtkWidget* _themeMenu = nullptr;
    GtkAccelGroup* _accel = nullptr;
    std::unique_ptr<Editor> _editor;
    std::vector<std::unique_ptr<Buffer>> _buffers;
    Buffer* _current = nullptr;
    int _nextBufferId = 1;
    int _untitledCounter = 0;
    bool _switching = false;
    bool _syncingChecks = false;
    bool _closing = false;
    std::map<int, Command> _commands;
    std::map<int, GtkWidget*> _langItems;
    std::vector<std::string> _closedFiles;       // for "Restore Recent Closed File"
    std::unique_ptr<FindReplaceDlg> _findDlg;
    std::unique_ptr<PluginManager> _plugins;
    std::unique_ptr<FunctionListPanel> _funcList;
    std::unique_ptr<AutoComplete> _autoComplete;
    std::map<int, GtkWidget*> _sidePanels;
    int _functionListPanel = -1;
    bool _recording = false;
    std::vector<MacroStep> _macro;
    std::map<int, GFileMonitor*> _monitors;      // buffer id -> monitor (View > Monitoring)
    guint _smartTimer = 0;
    std::string _lastFindForVolatile;
    friend class FindReplaceDlg;
};

} // namespace cam
