// Camelion++ - langs.xml / stylers.xml / theme data (Notepad++ formats)
#pragma once
#include <string>
#include <vector>
#include <map>
#include <array>
#include "LangDefs.h"

namespace cam {

enum FontStyleBits { FONTSTYLE_BOLD = 1, FONTSTYLE_ITALIC = 2, FONTSTYLE_UNDERLINE = 4 };

struct Style {
    int id = 0;
    std::string name;
    int fg = -1;            // Scintilla colour (0xBBGGRR) or -1
    int bg = -1;
    std::string font;
    int fontStyle = -1;     // FontStyleBits or -1
    int fontSize = -1;
    int kwClass = -1;       // LANG_INDEX_* or -1
    std::string keywords;   // user keywords stored in the style
};

struct LexerStyler {
    std::string name, desc, ext;
    std::vector<Style> styles;
    const Style* byId(int id) const;
};

struct LangInfo {
    std::string name;
    std::vector<std::string> exts;
    std::string commentLine, commentStart, commentEnd;
    std::array<std::string, NB_LANG_INDEX> keywords;
};

struct ThemeInfo { std::string name; std::string path; };

class Parameters {
public:
    static Parameters& get();

    void load();
    bool loadTheme(const std::string& path);   // replaces lexer + global styles
    std::vector<ThemeInfo> themes() const;
    const std::string& currentThemePath() const { return _themePath; }

    const LangInfo* lang(const std::string& name) const;
    const LexerStyler* lexerStyler(const std::string& name) const;
    const Style* globalStyle(const std::string& name) const;
    const Style* globalStyleById(int id) const;

    // detection
    int langTypeFromFileName(const std::string& path) const;
    int langTypeFromContent(const std::string& firstBytes) const;

    // colours of the "Default Style" (STYLE_DEFAULT) after theme load
    int defaultFg() const;
    int defaultBg() const;
    bool isDarkTheme() const;
    std::string defaultFont() const;
    int defaultFontSize() const;

    // built-in style sets for lexers that Notepad++ does not style (markdown)
    LexerStyler builtinStyler(const std::string& name) const;

    static int parseColour(const char* hex);   // "RRGGBB" -> 0xBBGGRR
    static std::string fontOrFallback(const std::string& f);

private:
    void loadLangs(const std::string& path);
    std::map<std::string, LangInfo> _langs;
    std::map<std::string, LexerStyler> _lexers;
    std::vector<Style> _globals;
    std::string _themePath;
};

} // namespace cam
