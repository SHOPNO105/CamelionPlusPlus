// Camelion++ - Scintilla view wrapper
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
#include "Editor.h"
#include "Parameters.h"
#include "Settings.h"
#include "LangDefs.h"
#include "Paths.h"
#include <ILexer.h>
#include <Lexilla.h>
#include <SciLexer.h>
#include <sstream>
#include <vector>
#include <cstring>

namespace cam {

std::string Buffer::displayName() const
{
    return path.empty() ? untitledName : paths::baseName(path);
}

Editor::Editor()
{
    _w = scintilla_new();
    g_object_ref_sink(_w);
}

void Editor::initDefaults()
{
    call(SCI_SETCODEPAGE, SC_CP_UTF8);
    call(SCI_SETTECHNOLOGY, SC_TECHNOLOGY_DEFAULT);
    call(SCI_USEPOPUP, SC_POPUP_ALL);
    call(SCI_SETMULTIPLESELECTION, 1);
    call(SCI_SETADDITIONALSELECTIONTYPING, 1);
    call(SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
    call(SCI_SETVIRTUALSPACEOPTIONS, SCVS_RECTANGULARSELECTION);
    call(SCI_SETMOUSESELECTIONRECTANGULARSWITCH, 1);
    call(SCI_SETSCROLLWIDTHTRACKING, 1);
    call(SCI_SETSCROLLWIDTH, 1);
    call(SCI_SETENDATLASTLINE, 1);
    call(SCI_SETCARETLINEVISIBLEALWAYS, 1);
    call(SCI_SETMOUSEDWELLTIME, 500);
    call(SCI_SETCARETSTYLE, CARETSTYLE_LINE);
    call(SCI_SETCARETWIDTH, 1);
    call(SCI_SETLAYOUTCACHE, SC_CACHE_PAGE);
    call(SCI_SETCHARACTERCATEGORYOPTIMIZATION, 0x10000);
    call(SCI_SETBUFFEREDDRAW, 1);
    call(SCI_SETWRAPVISUALFLAGS, SC_WRAPVISUALFLAG_END);
    call(SCI_SETWRAPINDENTMODE, SC_WRAPINDENT_SAME);
    call(SCI_AUTOCSETSEPARATOR, ' ');
    call(SCI_AUTOCSETMAXHEIGHT, 10);
    call(SCI_AUTOCSETORDER, SC_ORDER_PERFORMSORT);
    call(SCI_AUTOCSETCANCELATSTART, 0);
    call(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT | SC_PERFORMED_UNDO | SC_PERFORMED_REDO |
                              SC_MOD_CHANGEFOLD | SC_PERFORMED_USER);

    // margins: 0 line numbers, 1 bookmarks, 2 folding, 3 change history
    call(SCI_SETMARGINS, 4);
    call(SCI_SETMARGINTYPEN, MARGIN_LINENUM, SC_MARGIN_NUMBER);
    call(SCI_SETMARGINTYPEN, MARGIN_SYMBOL, SC_MARGIN_SYMBOL);
    call(SCI_SETMARGINMASKN, MARGIN_SYMBOL, 1 << MARK_BOOKMARK);
    call(SCI_SETMARGINWIDTHN, MARGIN_SYMBOL, 16);
    call(SCI_SETMARGINSENSITIVEN, MARGIN_SYMBOL, 1);
    call(SCI_SETMARGINTYPEN, MARGIN_FOLD, SC_MARGIN_SYMBOL);
    call(SCI_SETMARGINMASKN, MARGIN_FOLD, SC_MASK_FOLDERS);
    call(SCI_SETMARGINWIDTHN, MARGIN_FOLD, 14);
    call(SCI_SETMARGINSENSITIVEN, MARGIN_FOLD, 1);
    call(SCI_SETMARGINTYPEN, MARGIN_HISTORY, SC_MARGIN_SYMBOL);
    call(SCI_SETMARGINMASKN, MARGIN_HISTORY, (1 << SC_MARKNUM_HISTORY_REVERTED_TO_ORIGIN) |
         (1 << SC_MARKNUM_HISTORY_SAVED) | (1 << SC_MARKNUM_HISTORY_MODIFIED) | (1 << SC_MARKNUM_HISTORY_REVERTED_TO_MODIFIED));
    call(SCI_SETMARGINWIDTHN, MARGIN_HISTORY, 4);
    call(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
    call(SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED);

    call(SCI_MARKERDEFINE, MARK_BOOKMARK, SC_MARK_BOOKMARK);
    call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS);
    call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS);
    call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE);
    call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER);
    call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED);
    call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED);
    call(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER);
    call(SCI_MARKERENABLEHIGHLIGHT, 1);
    for (int m : {SC_MARKNUM_HISTORY_REVERTED_TO_ORIGIN, SC_MARKNUM_HISTORY_SAVED, SC_MARKNUM_HISTORY_MODIFIED, SC_MARKNUM_HISTORY_REVERTED_TO_MODIFIED})
        call(SCI_MARKERDEFINE, m, SC_MARK_FULLRECT);

    // indicators
    for (int i : {INDIC_SMART, INDIC_FOUND, INDIC_FINDALL, INDIC_TAGMATCH, 21, 22, 23, 24, 25}) {
        call(SCI_INDICSETSTYLE, i, INDIC_ROUNDBOX);
        call(SCI_INDICSETALPHA, i, 100);
        call(SCI_INDICSETOUTLINEALPHA, i, 150);
        call(SCI_INDICSETUNDER, i, 1);
    }
    call(SCI_INDICSETALPHA, INDIC_SMART, 60);
    call(SCI_BRACEHIGHLIGHTINDICATOR, 0, 0);
}

void Editor::applyStyle(int id, int fg, int bg, const std::string& font, int fontStyle, int fontSize)
{
    if (fg >= 0) call(SCI_STYLESETFORE, id, fg);
    if (bg >= 0) call(SCI_STYLESETBACK, id, bg);
    if (!font.empty()) callStr(SCI_STYLESETFONT, id, Parameters::fontOrFallback(font).c_str());
    if (fontStyle >= 0) {
        call(SCI_STYLESETBOLD, id, (fontStyle & FONTSTYLE_BOLD) != 0);
        call(SCI_STYLESETITALIC, id, (fontStyle & FONTSTYLE_ITALIC) != 0);
        call(SCI_STYLESETUNDERLINE, id, (fontStyle & FONTSTYLE_UNDERLINE) != 0);
    }
    if (fontSize > 0) call(SCI_STYLESETSIZE, id, fontSize);
}

static std::string joinWords(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + " " + b;
}

static void parseProps(const char* props, std::vector<std::pair<std::string, std::string>>& out)
{
    std::istringstream ss(props ? props : "");
    std::string kv;
    while (std::getline(ss, kv, ';')) {
        auto eq = kv.find('=');
        if (eq != std::string::npos) out.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
    }
}

void Editor::applyLanguage(const Buffer& b)
{
    Parameters& P = Parameters::get();
    const LangDef* def = langByType(b.langType);
    std::string styleName = def->name;
    std::string kwLangName = def->name;
    if (styleName == "javascript") { styleName = "javascript.js"; kwLangName = "javascript.js"; }

    // ---- STYLE_DEFAULT then clear all
    const Style* defStyle = P.globalStyleById(STYLE_DEFAULT);
    applyStyle(STYLE_DEFAULT, P.defaultFg(), P.defaultBg(), P.defaultFont(),
               defStyle ? std::max(0, defStyle->fontStyle) : 0, P.defaultFontSize());
    call(SCI_STYLECLEARALL);

    // ---- lexer
    std::string lexerName = def->lexer;
    std::string kwMap = def->kwMap;
    if (kwMap == "HTML") lexerName = "hypertext";
    Scintilla::ILexer5* lex = nullptr;
    if (lexerName != "null") lex = CreateLexer(lexerName.c_str());
    call(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(lex));

    auto styleKeywords = [&](const LexerStyler* st, std::array<std::string, NB_LANG_INDEX>& kw) {
        if (!st) return;
        for (auto& s : st->styles)
            if (s.kwClass >= 0 && s.kwClass < NB_LANG_INDEX && !s.keywords.empty())
                kw[s.kwClass] = s.keywords;
    };
    auto applyStyler = [&](const LexerStyler* st) {
        if (!st) return;
        for (auto& s : st->styles) applyStyle(s.id, s.fg, s.bg, s.font, s.fontStyle, s.fontSize);
    };
    auto langKw = [&](const std::string& lang, int idx) -> std::string {
        const LangInfo* li = P.lang(lang);
        return (li && idx >= 0 && idx < NB_LANG_INDEX) ? li->keywords[idx] : std::string();
    };

    std::vector<std::pair<std::string, std::string>> props;
    parseProps(def->props, props);

    if (kwMap == "HTML") {
        // hypertext lexer: HTML + embedded JS + PHP + ASP(VB)
        struct Part { const char* styler; const char* lang; int kwIndex; int set; };
        const Part parts[] = {
            {"html", "html", LANG_INDEX_INSTR, 0}, {"html", "html", LANG_INDEX_INSTR2, 5},
            {"javascript", "javascript", LANG_INDEX_INSTR, 1},
            {"php", "php", LANG_INDEX_INSTR, 4},
            {"asp", "vb", LANG_INDEX_INSTR, 2},
        };
        for (auto* n : {"html", "javascript", "php", "asp"}) applyStyler(P.lexerStyler(n));
        for (auto& p : parts) {
            std::array<std::string, NB_LANG_INDEX> skw;
            styleKeywords(P.lexerStyler(p.styler), skw);
            std::string words = joinWords(skw[p.kwIndex], langKw(p.lang, p.kwIndex));
            callStr(SCI_SETKEYWORDS, p.set, words.c_str());
        }
        props.emplace_back("fold", "1"); props.emplace_back("fold.compact", "0");
        props.emplace_back("fold.html", "1"); props.emplace_back("fold.hypertext.comment", "1");
        props.emplace_back("asp.default.language", "2");
        for (int s : {SCE_HJ_DEFAULT, SCE_HJ_COMMENT, SCE_HJ_COMMENTDOC, SCE_HPHP_DEFAULT, SCE_HPHP_COMMENT, SCE_HBA_DEFAULT})
            call(SCI_STYLESETEOLFILLED, s, 1);
    } else if (kwMap == "XML") {
        const LexerStyler* st = P.lexerStyler("xml");
        applyStyler(st);
        std::array<std::string, NB_LANG_INDEX> skw; styleKeywords(st, skw);
        callStr(SCI_SETKEYWORDS, 5, joinWords(skw[LANG_INDEX_INSTR], langKw("xml", LANG_INDEX_INSTR)).c_str());
        props.emplace_back("lexer.xml.allow.scripts", "0");
        props.emplace_back("fold", "1"); props.emplace_back("fold.compact", "0"); props.emplace_back("fold.html", "1");
    } else {
        const LexerStyler* st = P.lexerStyler(styleName);
        LexerStyler builtin;
        if (!st) { builtin = P.builtinStyler(styleName); if (!builtin.styles.empty()) st = &builtin; }
        applyStyler(st);
        std::array<std::string, NB_LANG_INDEX> skw; styleKeywords(st, skw);
        // keyword sets
        std::istringstream ss(kwMap);
        std::string item;
        while (std::getline(ss, item, ',')) {
            auto colon = item.find(':');
            if (colon == std::string::npos) continue;
            int set = std::atoi(item.substr(0, colon).c_str());
            std::string src = item.substr(colon + 1);
            std::string words;
            if (src == "D") words = langKw("cpp", LANG_INDEX_TYPE2);   // doxygen keywords
            else {
                int idx = std::atoi(src.c_str());
                words = joinWords(skw[idx], langKw(kwLangName, idx));
            }
            callStr(SCI_SETKEYWORDS, set, words.c_str());
        }
        // substyles (user keyword groups 1..8)
        for (auto& ss2 : def->subStyles) {
            int first = int(call(SCI_ALLOCATESUBSTYLES, ss2.baseStyle, ss2.count) & 0xFF);
            if (first < 0) continue;
            for (int i = 0; i < ss2.count; ++i) {
                int idx = ss2.firstLangIndex + i;
                std::string words = joinWords(skw[idx], langKw(kwLangName, idx));
                callStr(SCI_SETIDENTIFIERS, first + i, words.c_str());
                // the styles for substyles live at 128+ in stylers.xml; map them onto the allocated ids
                if (st) if (const Style* sub = st->byId(128 + (idx - LANG_INDEX_SUBSTYLE1)))
                    applyStyle(first + i, sub->fg, sub->bg, sub->font, sub->fontStyle, sub->fontSize);
            }
        }
    }
    for (auto& [k, v] : props) call(SCI_SETPROPERTY, reinterpret_cast<uptr_t>(k.c_str()), reinterpret_cast<sptr_t>(v.c_str()));

    applyGlobalStyles();

    // tab settings (per-language override "lang.<name>.tabSize")
    Settings& S = Settings::get();
    int tab = S.getInt(std::string("lang.") + def->name + ".tabSize", S.getInt("editor.tabSize", 4));
    bool spaces = S.getBool(std::string("lang.") + def->name + ".useSpaces",
                            S.getBool("editor.useSpaces", def->langType == 22 /*python*/ || def->langType == 49 /*yaml*/));
    if (def->langType == 10 /*makefile*/) spaces = false;
    call(SCI_SETTABWIDTH, tab);
    call(SCI_SETINDENT, 0);
    call(SCI_SETUSETABS, !spaces);
    bool pyIndent = def->langType == 22 || def->langType == 49 || def->langType == 76 || def->langType == 18;
    if (S.getBool("view.indentGuides", true))
        call(SCI_SETINDENTATIONGUIDES, pyIndent ? SC_IV_LOOKFORWARD : SC_IV_LOOKBOTH);

    call(SCI_COLOURISE, 0, -1);
}

void Editor::applyGlobalStyles()
{
    Parameters& P = Parameters::get();
    auto g = [&](const char* n) { return P.globalStyle(n); };
    auto styleById = [&](int id) {
        if (const Style* s = P.globalStyleById(id)) applyStyle(id, s->fg, s->bg, s->font, s->fontStyle, s->fontSize);
    };
    styleById(STYLE_INDENTGUIDE);
    styleById(STYLE_BRACELIGHT);
    styleById(STYLE_BRACEBAD);
    if (const Style* s = P.globalStyleById(STYLE_LINENUMBER)) {
        applyStyle(STYLE_LINENUMBER, s->fg, s->bg, "", s->fontStyle, -1);
    }
    int bg = P.defaultBg(), fg = P.defaultFg();
    auto opaque = [](int c) { return sptr_t(c) | (sptr_t(0xFF) << 24); };

    if (auto s = g("Current line background colour"); s && s->bg >= 0) {
        call(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, opaque(s->bg));
    }
    call(SCI_SETCARETLINEFRAME, 0);
    if (!Settings::get().getBool("view.currentLine", true)) call(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK);

    if (auto s = g("Selected text colour"); s && s->bg >= 0) {
        call(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_BACK, opaque(s->bg));
        call(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_BACK, opaque(s->bg));
        call(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_ADDITIONAL_BACK, opaque(s->bg));
        call(SCI_SETSELECTIONLAYER, SC_LAYER_UNDER_TEXT);
    }
    if (auto s = g("Multi-selected text color"); s && s->bg >= 0)
        call(SCI_SETELEMENTCOLOUR, SC_ELEMENT_SELECTION_ADDITIONAL_BACK, opaque(s->bg));
    if (auto s = g("Caret colour"); s && s->fg >= 0) call(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET, opaque(s->fg));
    if (auto s = g("Multi-edit carets color"); s && s->fg >= 0) call(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_ADDITIONAL, opaque(s->fg));
    if (auto s = g("Edge colour"); s && s->fg >= 0) call(SCI_SETEDGECOLOUR, s->fg);
    if (auto s = g("White space symbol"); s && s->fg >= 0) {
        call(SCI_SETELEMENTCOLOUR, SC_ELEMENT_WHITE_SPACE, opaque(s->fg));
    }

    // fold margin + markers
    int foldFg = 0x808080, foldBg = 0xF3F3F3, foldActive = 0x0000FF;
    if (auto s = g("Fold"); s) { if (s->fg >= 0) foldFg = s->fg; if (s->bg >= 0) foldBg = s->bg; }
    if (auto s = g("Fold active"); s && s->fg >= 0) foldActive = s->fg;
    for (int m = SC_MARKNUM_FOLDEREND; m <= SC_MARKNUM_FOLDEROPEN; ++m) {
        call(SCI_MARKERSETFORE, m, foldBg);
        call(SCI_MARKERSETBACK, m, foldFg);
        call(SCI_MARKERSETBACKSELECTED, m, foldActive);
    }
    if (auto s = g("Fold margin"); s) {
        call(SCI_SETFOLDMARGINCOLOUR, 1, s->bg >= 0 ? s->bg : bg);
        call(SCI_SETFOLDMARGINHICOLOUR, 1, s->fg >= 0 ? s->fg : bg);
    }
    int bookmarkBg = 0xE0E0E0;
    if (auto s = g("Bookmark margin"); s && s->bg >= 0) bookmarkBg = s->bg;
    call(SCI_SETMARGINBACKN, MARGIN_SYMBOL, bookmarkBg);
    call(SCI_SETMARGINTYPEN, MARGIN_SYMBOL, SC_MARGIN_COLOUR);
    call(SCI_SETMARGINBACKN, MARGIN_HISTORY, bookmarkBg);
    call(SCI_SETMARGINTYPEN, MARGIN_HISTORY, SC_MARGIN_COLOUR);
    call(SCI_MARKERSETFORE, MARK_BOOKMARK, 0x101010);
    call(SCI_MARKERSETBACK, MARK_BOOKMARK, 0xE07800);   // blue-ish bookmark (BGR)

    struct H { const char* name; int marker; int def; };
    const H hist[] = {
        {"Change History modified", SC_MARKNUM_HISTORY_MODIFIED, 0x0080FF},
        {"Change History revert modified", SC_MARKNUM_HISTORY_REVERTED_TO_MODIFIED, 0x00C0A0},
        {"Change History revert origin", SC_MARKNUM_HISTORY_REVERTED_TO_ORIGIN, 0xBFA040},
        {"Change History saved", SC_MARKNUM_HISTORY_SAVED, 0x00A000},
    };
    for (auto& h : hist) {
        int c = h.def;
        if (auto s = g(h.name); s && s->bg >= 0) c = s->bg;
        call(SCI_MARKERSETFORE, h.marker, c);
        call(SCI_MARKERSETBACK, h.marker, c);
    }

    // indicators
    auto indic = [&](const char* n, int id, int def) {
        int c = def;
        if (auto s = g(n); s && s->bg >= 0) c = s->bg;
        call(SCI_INDICSETFORE, id, c);
    };
    indic("Smart Highlighting", INDIC_SMART, 0x00FF00);
    indic("Find Mark Style", INDIC_FOUND, 0x0000FF);
    indic("Find matches highlight all", INDIC_FINDALL, 0xFF8000);
    indic("Tags match highlighting", INDIC_TAGMATCH, 0xFF0080);
    indic("Mark Style 1", 25, 0xFFFF00); indic("Mark Style 2", 24, 0x0080FF);
    indic("Mark Style 3", 23, 0x00FFFF); indic("Mark Style 4", 22, 0xFF0080);
    indic("Mark Style 5", 21, 0x008000);

    // call tips / autocomplete lists follow the theme
    call(SCI_CALLTIPSETBACK, bg);
    call(SCI_CALLTIPSETFORE, fg);
    call(SCI_STYLESETBACK, STYLE_CALLTIP, bg);
    call(SCI_STYLESETFORE, STYLE_CALLTIP, fg);
    call(SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST, opaque(fg));
    call(SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_BACK, opaque(bg));
    if (auto s = g("Selected text colour"); s && s->bg >= 0)
        call(SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_SELECTED_BACK, opaque(s->bg));
    call(SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_SELECTED, opaque(fg));
    (void)fg;
}

void Editor::applyViewOptions()
{
    Settings& S = Settings::get();
    call(SCI_SETWRAPMODE, S.getBool("view.wrap", false) ? SC_WRAP_WORD : SC_WRAP_NONE);
    bool allChars = S.getBool("view.allChars", false);
    call(SCI_SETVIEWWS, (allChars || S.getBool("view.whitespace", false)) ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    call(SCI_SETVIEWEOL, allChars || S.getBool("view.eol", false));
    call(SCI_SETWHITESPACESIZE, 2);
    call(SCI_SETINDENTATIONGUIDES, S.getBool("view.indentGuides", true) ? SC_IV_LOOKBOTH : SC_IV_NONE);
    call(SCI_SETCARETLINEVISIBLE, S.getBool("view.currentLine", true));
    int edge = S.getInt("view.edgeColumn", 0);
    call(SCI_SETEDGEMODE, edge > 0 ? EDGE_LINE : EDGE_NONE);
    call(SCI_SETEDGECOLUMN, edge);
    call(SCI_SETMARGINWIDTHN, MARGIN_SYMBOL, S.getBool("view.bookmarkMargin", true) ? 16 : 0);
    call(SCI_SETMARGINWIDTHN, MARGIN_HISTORY, S.getBool("view.changeHistory", true) ? 4 : 0);
    setFoldMarginVisible(S.getBool("view.foldMargin", true));
    updateLineNumberWidth();
    call(SCI_SETCARETPERIOD, S.getBool("editor.caretBlink", true) ? 600 : 0);
    call(SCI_SETCARETWIDTH, S.getInt("editor.caretWidth", 1));
    call(SCI_SETSCROLLWIDTHTRACKING, 1);
    call(SCI_SETBACKSPACEUNINDENTS, S.getBool("editor.backspaceUnindents", false));
    call(SCI_SETTABINDENTS, 1);
    int z = S.getInt("view.zoom", 0);
    call(SCI_SETZOOM, z);
}

void Editor::setFoldMarginVisible(bool v)
{
    call(SCI_SETMARGINWIDTHN, MARGIN_FOLD, v ? 14 : 0);
}

void Editor::updateLineNumberWidth()
{
    if (!Settings::get().getBool("view.lineNumbers", true)) { call(SCI_SETMARGINWIDTHN, MARGIN_LINENUM, 0); return; }
    sptr_t lines = call(SCI_GETLINECOUNT);
    int digits = 3;
    for (sptr_t n = lines; n >= 1000; n /= 10) ++digits;
    std::string sample(size_t(digits + 1), '8');
    int w = int(call(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<sptr_t>(sample.c_str())));
    if (call(SCI_GETMARGINWIDTHN, MARGIN_LINENUM) != w) call(SCI_SETMARGINWIDTHN, MARGIN_LINENUM, w);
}

std::string Editor::text() const
{
    sptr_t len = call(SCI_GETLENGTH);
    std::string s(size_t(len), '\0');
    if (len > 0) call(SCI_GETTEXT, uptr_t(len + 1), reinterpret_cast<sptr_t>(s.data()));
    // SCI_GETTEXT writes a terminating NUL into the buffer; make sure the size is right
    return s;
}

std::string Editor::textRange(sptr_t a, sptr_t b) const
{
    if (b <= a) return {};
    std::string s(size_t(b - a), '\0');
    Sci_TextRangeFull tr;
    tr.chrg.cpMin = a; tr.chrg.cpMax = b;
    std::vector<char> buf(size_t(b - a) + 1);
    tr.lpstrText = buf.data();
    call(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&tr));
    return std::string(buf.data(), size_t(b - a));
}

std::string Editor::selectedText() const
{
    sptr_t a = call(SCI_GETSELECTIONSTART), b = call(SCI_GETSELECTIONEND);
    return textRange(a, b);
}

std::string Editor::currentWord() const
{
    sptr_t pos = call(SCI_GETCURRENTPOS);
    sptr_t a = call(SCI_WORDSTARTPOSITION, uptr_t(pos), 1);
    sptr_t b = call(SCI_WORDENDPOSITION, uptr_t(pos), 1);
    return textRange(a, b);
}

std::string Editor::lineText(sptr_t line) const
{
    sptr_t a = call(SCI_POSITIONFROMLINE, uptr_t(line));
    sptr_t b = call(SCI_GETLINEENDPOSITION, uptr_t(line));
    return textRange(a, b);
}

void Editor::saveViewState(Buffer& b) const
{
    b.anchor = call(SCI_GETANCHOR);
    b.caret = call(SCI_GETCURRENTPOS);
    sptr_t vis = call(SCI_GETFIRSTVISIBLELINE);
    b.firstLine = call(SCI_DOCLINEFROMVISIBLE, uptr_t(vis));
    b.xOffset = call(SCI_GETXOFFSET);
    b.selMode = int(call(SCI_GETSELECTIONMODE));
    b.viewSaved = true;
}

void Editor::restoreViewState(const Buffer& b)
{
    if (!b.viewSaved) { call(SCI_GOTOPOS, 0); return; }
    call(SCI_SETSELECTIONMODE, uptr_t(b.selMode));
    call(SCI_SETANCHOR, uptr_t(b.anchor));
    call(SCI_SETCURRENTPOS, uptr_t(b.caret));
    call(SCI_CANCEL);
    call(SCI_SETXOFFSET, uptr_t(b.xOffset));
    call(SCI_CHOOSECARETX);
    call(SCI_SETFIRSTVISIBLELINE, uptr_t(call(SCI_VISIBLEFROMDOCLINE, uptr_t(b.firstLine))));
}

} // namespace cam
