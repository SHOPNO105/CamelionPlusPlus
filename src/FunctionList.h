// Camelion++ - Function List panel driven by Notepad++ functionList/*.xml parsers
#pragma once
#include <gtk/gtk.h>
#include <string>
#include <vector>

namespace cam {
class MainWindow;

class FunctionListPanel {
public:
    explicit FunctionListPanel(MainWindow* mw);
    GtkWidget* widget() const { return _box; }
    void refreshSoon();
    void refreshNow();

    struct Entry { std::string name; std::string cls; long pos = 0; long line = 0; };
    // Exposed for tests: parse `text` with the parser file for `langType`.
    static std::vector<Entry> parse(const std::string& text, int langType, std::string* parserName = nullptr);

private:
    void populate();
    MainWindow* _mw;
    GtkWidget* _box = nullptr;
    GtkWidget* _tree = nullptr;
    GtkWidget* _search = nullptr;
    GtkWidget* _sortBtn = nullptr;
    GtkWidget* _label = nullptr;
    guint _timer = 0;
    std::vector<Entry> _entries;
};

} // namespace cam
