// Camelion++ - Find / Replace / Find in Files / Mark dialog
#pragma once
#include <gtk/gtk.h>
#include <string>
#include <vector>
#include <Scintilla.h>

namespace cam {
class MainWindow;
class Editor;
struct Buffer;

class FindReplaceDlg {
public:
    enum Tab { TabFind = 0, TabReplace, TabFiles, TabMark };
    explicit FindReplaceDlg(MainWindow* mw);
    void show(Tab t);
    void hide();

    bool findNext(bool forward, bool quiet = false);
    void selectAndFind(bool forward, bool isVolatile);
    void gotoResult(bool next);
    void styleToken(int indicator);
    std::string resultFilePath(int id) const;

private:
    struct Options {
        std::string what, with;
        bool matchCase = false, wholeWord = false, wrap = true, backward = false;
        int mode = 0;           // 0 normal, 1 extended, 2 regex
        bool dotAll = false;
        bool inSelection = false;
        bool bookmarkLine = false, purge = false;
        std::string filters, dir;
        bool subfolders = true, hidden = false;
    };
    Options read() const;
    int searchFlags(const Options& o) const;
    std::string pattern(const Options& o) const;
    std::string replacement(const Options& o) const;
    void addHistory(GtkWidget* combo, const std::string& s);
    void setStatus(const std::string& s, int kind = 0);   // 0 info, 1 not found, 2 wrapped
    void switchTab(int t);

    void doReplace();
    int  replaceAllIn(Editor& e, const Options& o, bool inSel);
    void doReplaceAll(bool allDocs);
    void doCount();
    void doMarkAll();
    void doClearMarks();
    void doCopyMarked();
    void doFindAll(bool allDocs);
    void doFindInFiles(bool replace);
    void highlightAllMatches();

    struct Hit { long line; long start, end; std::string text; };
    std::vector<Hit> collectHits(Editor& e, const Options& o);
    void showResults(const std::string& header, const std::vector<std::pair<std::string, std::vector<Hit>>>& files,
                     const std::vector<int>& bufferIds, size_t totalHits);

    MainWindow* _mw;
    GtkWidget* _win = nullptr;
    GtkWidget* _tabs = nullptr;
    GtkWidget* _what = nullptr;
    GtkWidget* _with = nullptr;
    GtkWidget* _withLabel = nullptr;
    GtkWidget* _filtersRow = nullptr;
    GtkWidget* _filters = nullptr;
    GtkWidget* _dir = nullptr;
    GtkWidget* _followDoc = nullptr;
    GtkWidget* _subfolders = nullptr;
    GtkWidget* _hidden = nullptr;
    GtkWidget* _backward = nullptr, *_wholeWord = nullptr, *_matchCase = nullptr, *_wrap = nullptr;
    GtkWidget* _modeNormal = nullptr, *_modeExt = nullptr, *_modeRegex = nullptr, *_dotAll = nullptr;
    GtkWidget* _inSelection = nullptr, *_bookmarkLine = nullptr, *_purge = nullptr, *_highlightAll = nullptr;
    GtkWidget* _status = nullptr;
    GtkWidget* _buttonBox = nullptr;
    std::vector<std::pair<GtkWidget*, unsigned>> _buttons;   // widget, mask of tabs where visible
    std::vector<std::string> _filePaths;                      // Find-in-Files results
    bool _stop = false;
    int _tab = 0;
};

} // namespace cam
