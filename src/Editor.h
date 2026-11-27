// Camelion++ - Scintilla view wrapper and per-document buffer state
#pragma once
#include <gtk/gtk.h>
#include <string>
#include <ctime>
#include <Scintilla.h>
#include <ScintillaWidget.h>
#include "Encoding.h"

// G_CALLBACK() for lambdas: a variadic macro so commas inside the lambda are fine
#define CAM_CB(...) (reinterpret_cast<GCallback>(__VA_ARGS__))

namespace cam {

// Marker / indicator numbers (same values as Notepad++)
constexpr int MARK_BOOKMARK = 20;
constexpr int INDIC_SMART   = 29;   // smart highlighting
constexpr int INDIC_FOUND   = 31;   // "Mark All" (Find Mark Style)
constexpr int INDIC_FINDALL = 28;   // highlight all matches
constexpr int INDIC_MARK1   = 25;   // Mark Style 1..5 = 25..21
constexpr int INDIC_TAGMATCH = 27;

constexpr int MARGIN_LINENUM = 0;
constexpr int MARGIN_SYMBOL  = 1;
constexpr int MARGIN_FOLD    = 2;
constexpr int MARGIN_HISTORY = 3;

struct Buffer {
    int id = 0;                       // BufferID handed to plugins
    sptr_t doc = 0;                   // Scintilla document
    std::string path;                 // empty => untitled
    std::string untitledName;         // "new 1"
    int langType = 0;
    UniMode mode = UniMode::Utf8;
    std::string charset = "UTF-8";
    EolType eol = EolType::LF;
    bool dirty = false;
    bool readOnly = false;
    bool userReadOnly = false;
    bool missing = false;             // file deleted outside the editor
    time_t mtime = 0;
    long long fileSize = 0;
    bool langLocked = false;          // user picked a language explicitly
    // saved view state
    sptr_t anchor = 0, caret = 0, firstLine = 0, xOffset = 0;
    int selMode = 0;
    bool viewSaved = false;
    // tab widgets
    GtkWidget* page = nullptr;
    GtkWidget* tabLabel = nullptr;
    GtkWidget* tabBox = nullptr;

    std::string displayName() const;
    bool isUntitled() const { return path.empty(); }
};

class Editor {
public:
    Editor();
    GtkWidget* widget() const { return _w; }
    ScintillaObject* sci() const { return SCINTILLA(_w); }
    sptr_t call(unsigned int msg, uptr_t w = 0, sptr_t l = 0) const { return scintilla_send_message(SCINTILLA(_w), msg, w, l); }
    sptr_t callStr(unsigned int msg, uptr_t w, const char* s) const { return call(msg, w, reinterpret_cast<sptr_t>(s)); }

    void initDefaults();
    void applyLanguage(const Buffer& b);   // lexer, keywords, styles, global styles
    void applyViewOptions();               // wrap, whitespace, margins ... from Settings
    void updateLineNumberWidth();

    std::string text() const;
    std::string textRange(sptr_t a, sptr_t b) const;
    std::string selectedText() const;
    std::string currentWord() const;
    std::string lineText(sptr_t line) const;

    void saveViewState(Buffer& b) const;
    void restoreViewState(const Buffer& b);

    void setFoldMarginVisible(bool v);

private:
    void applyStyle(int id, int fg, int bg, const std::string& font, int fontStyle, int fontSize);
    void applyGlobalStyles();
    GtkWidget* _w;
};

} // namespace cam
