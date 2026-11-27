// Camelion++ - auto-completion using Notepad++ autoCompletion/*.xml files
#pragma once
#include <string>
#include <vector>
#include <map>
#include <Scintilla.h>

namespace cam {
class Editor;

class AutoComplete {
public:
    void setLanguage(int langType);
    void onCharAdded(Editor& e, int ch);
    void showFunctionCompletion(Editor& e);
    void showWordCompletion(Editor& e);
    void showCallTip(Editor& e);
    void showPathCompletion(Editor& e);

private:
    struct Overload { std::string retVal, descr; std::vector<std::string> params; };
    struct KeyWord { std::string name; bool func = false; std::vector<Overload> overloads; };
    struct LangData {
        bool loaded = false;
        bool ignoreCase = false;
        char startFunc = '(', stopFunc = ')', paramSep = ',', terminal = 0;
        std::string additionalWordChars;
        std::vector<KeyWord> words;            // sorted by name
    };
    LangData& data();
    void load(LangData& d, const std::string& file);
    std::string wordBeforeCaret(Editor& e, sptr_t* start = nullptr) const;
    void show(Editor& e, bool funcs, bool words);
    void updateCallTip(Editor& e);

    int _lang = -1;
    std::map<int, LangData> _cache;
    // call tip state
    const KeyWord* _ctWord = nullptr;
    size_t _ctOverload = 0;
    sptr_t _ctStart = -1;
};

} // namespace cam
