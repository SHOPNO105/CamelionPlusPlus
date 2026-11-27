// Camelion++ - language definitions (LangType order identical to Notepad++)
#pragma once
#include <string>
#include <vector>

namespace cam {

// Indexes of keyword lists inside langs.xml / stylers.xml (Notepad++ LANG_INDEX_*)
enum LangIndex {
    LANG_INDEX_INSTR = 0, LANG_INDEX_INSTR2, LANG_INDEX_TYPE, LANG_INDEX_TYPE2,
    LANG_INDEX_TYPE3, LANG_INDEX_TYPE4, LANG_INDEX_TYPE5, LANG_INDEX_TYPE6,
    LANG_INDEX_TYPE7, LANG_INDEX_SUBSTYLE1, LANG_INDEX_SUBSTYLE2, LANG_INDEX_SUBSTYLE3,
    LANG_INDEX_SUBSTYLE4, LANG_INDEX_SUBSTYLE5, LANG_INDEX_SUBSTYLE6, LANG_INDEX_SUBSTYLE7,
    LANG_INDEX_SUBSTYLE8, NB_LANG_INDEX
};

int keywordClassFromName(const char* s);   // "instre1" -> 0 ... "substyle8" -> 16, "0".."8"

struct SubStyleSpec {
    int baseStyle;      // e.g. SCE_C_IDENTIFIER
    int count;          // how many substyles
    int firstLangIndex; // LANG_INDEX_SUBSTYLE1 or 5
};

struct LangDef {
    const char* name;       // name used in langs.xml / stylers.xml (e.g. "cpp")
    const char* shortName;  // menu label
    const char* longName;   // status bar description
    int         langType;   // Notepad++ LangType value
    const char* lexer;      // Lexilla lexer name
    // keyword set mapping: "set:langIndex,..."  'D' as langIndex = C++ doxygen list (cpp type2)
    const char* kwMap;
    // extra lexer properties "key=value;key=value"
    const char* props;
    std::vector<SubStyleSpec> subStyles;
};

const std::vector<LangDef>& allLangs();
const LangDef* langByType(int langType);
const LangDef* langByName(const std::string& name);

} // namespace cam
