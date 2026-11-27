// Camelion++ - text transformations used by the Edit menu
#pragma once
#include <string>
#include <vector>
#include <functional>

namespace cam {
class Editor;
struct LangInfo;

enum class CaseMode { Upper, Lower, Proper, Sentence, Invert, Random };
enum class SortMode { LexAsc, LexDesc, LexAscCI, LexDescCI, IntAsc, IntDesc, Reverse, Random };
enum class BlankOp { TrimTrailing, TrimLeading, TrimBoth, EolToSpace, TrimAll, TabToSpace, SpaceToTabLeading, SpaceToTabAll };

// Runs fn over the selected lines (or the whole document) as one undo step.
void transformLines(Editor& e, const std::function<void(std::vector<std::string>&)>& fn, bool wholeDocIfNoSelection = true);

void changeCase(Editor& e, CaseMode m);
void sortLines(Editor& e, SortMode m);
void removeDuplicateLines(Editor& e, bool consecutiveOnly);
void removeEmptyLines(Editor& e, bool includingBlank);
void blankOperation(Editor& e, BlankOp op);
void trimTrailingInEditor(Editor& e);
void joinLines(Editor& e);
void splitLines(Editor& e);
void insertBlankLine(Editor& e, bool above);

bool toggleLineComment(Editor& e, const LangInfo* li);
bool lineComment(Editor& e, const LangInfo* li, bool add);
bool streamComment(Editor& e, const LangInfo* li, bool add);

// "Extended" search mode escapes: \n \r \t \0 \\ \xNN \uNNNN \dNNN \oNNN \bNNNNNNNN
std::string expandEscapes(const std::string& s);

} // namespace cam
