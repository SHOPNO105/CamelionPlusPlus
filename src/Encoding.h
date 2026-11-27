// Camelion++ - text encoding detection and conversion
#pragma once
#include <string>
#include <vector>

namespace cam {

// Values match Notepad++'s UniMode as reported by NPPM_GETBUFFERENCODING
enum class UniMode { Ansi = 0, Utf8Bom = 1, Utf16BE = 2, Utf16LE = 3, Utf8 = 4 };

// Values match Notepad++'s EolType (NPPM_GETBUFFERFORMAT)
enum class EolType { CRLF = 0, CR = 1, LF = 2 };

struct DecodedText {
    std::string utf8;
    UniMode mode = UniMode::Utf8;
    std::string charset = "UTF-8";   // for Ansi: the 8-bit charset used
    bool lossy = false;
};

DecodedText decodeText(const std::string& raw, const std::string& ansiCharset);
// Force a specific interpretation ("reinterpret as")
DecodedText decodeAs(const std::string& raw, UniMode mode, const std::string& charset);
bool encodeText(const std::string& utf8, UniMode mode, const std::string& charset,
                std::string& out, std::string* err);

EolType detectEol(const std::string& utf8, EolType fallback);
const char* uniModeLabel(UniMode m, const std::string& charset);
const char* eolLabel(EolType e);

struct CharsetInfo { const char* iconvName; const char* label; const char* group; };
const std::vector<CharsetInfo>& ansiCharsets();

} // namespace cam
