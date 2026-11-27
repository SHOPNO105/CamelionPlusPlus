// Camelion++ - text encoding detection and conversion
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
#include "Encoding.h"
#include <glib.h>
#include <cstring>

namespace cam {

const std::vector<CharsetInfo>& ansiCharsets()
{
    static const std::vector<CharsetInfo> v = {
        {"WINDOWS-1252", "Windows-1252 (Western European)", "Western European"},
        {"ISO-8859-1",   "ISO 8859-1 (Latin-1)",            "Western European"},
        {"ISO-8859-15",  "ISO 8859-15 (Latin-9)",           "Western European"},
        {"CP437",        "OEM 437 (DOS)",                    "Western European"},
        {"WINDOWS-1250", "Windows-1250 (Central European)",  "Central European"},
        {"ISO-8859-2",   "ISO 8859-2",                       "Central European"},
        {"WINDOWS-1251", "Windows-1251 (Cyrillic)",          "Cyrillic"},
        {"KOI8-R",       "KOI8-R",                           "Cyrillic"},
        {"KOI8-U",       "KOI8-U",                           "Cyrillic"},
        {"WINDOWS-1253", "Windows-1253 (Greek)",             "Greek"},
        {"WINDOWS-1254", "Windows-1254 (Turkish)",           "Turkish"},
        {"WINDOWS-1255", "Windows-1255 (Hebrew)",            "Hebrew"},
        {"WINDOWS-1256", "Windows-1256 (Arabic)",            "Arabic"},
        {"WINDOWS-1257", "Windows-1257 (Baltic)",            "Baltic"},
        {"WINDOWS-1258", "Windows-1258 (Vietnamese)",        "Vietnamese"},
        {"TIS-620",      "TIS-620 (Thai)",                   "Thai"},
        {"SHIFT_JIS",    "Shift-JIS",                        "Japanese"},
        {"EUC-JP",       "EUC-JP",                           "Japanese"},
        {"GB18030",      "GB18030 (Simplified Chinese)",     "Chinese"},
        {"BIG5",         "Big5 (Traditional Chinese)",       "Chinese"},
        {"EUC-KR",       "EUC-KR (Korean)",                  "Korean"},
    };
    return v;
}

static std::string convert(const std::string& in, const char* to, const char* from, bool* ok, bool* lossy)
{
    GError* e = nullptr;
    gsize written = 0;
    gchar* r = g_convert_with_fallback(in.data(), gssize(in.size()), to, from, "?", nullptr, &written, &e);
    if (!r) {
        if (e) g_error_free(e);
        *ok = false;
        return {};
    }
    *ok = true;
    std::string s(r, written);
    g_free(r);
    if (lossy) {
        // Round-trip check is too expensive for big files; g_convert_with_fallback
        // only substitutes when it must, so detect '?' growth as a cheap heuristic.
        *lossy = false;
    }
    return s;
}

static bool looksUtf16(const std::string& raw, bool& littleEndian)
{
    if (raw.size() < 4) return false;
    size_t n = std::min<size_t>(raw.size(), 4096) & ~size_t(1);
    size_t zEven = 0, zOdd = 0;
    for (size_t i = 0; i < n; i += 2) {
        if (raw[i] == 0) ++zEven;
        if (raw[i + 1] == 0) ++zOdd;
    }
    size_t pairs = n / 2;
    if (zOdd > pairs * 0.4 && zEven < pairs * 0.05) { littleEndian = true; return true; }
    if (zEven > pairs * 0.4 && zOdd < pairs * 0.05) { littleEndian = false; return true; }
    return false;
}

DecodedText decodeAs(const std::string& raw, UniMode mode, const std::string& charset)
{
    DecodedText d;
    d.mode = mode;
    bool ok = true;
    switch (mode) {
    case UniMode::Utf8Bom:
    case UniMode::Utf8: {
        size_t skip = (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) ? 3 : 0;
        d.utf8 = raw.substr(skip);
        if (!g_utf8_validate(d.utf8.data(), gssize(d.utf8.size()), nullptr)) {
            gchar* v = g_utf8_make_valid(d.utf8.data(), gssize(d.utf8.size()));
            d.utf8 = v; g_free(v); d.lossy = true;
        }
        d.charset = "UTF-8";
        break;
    }
    case UniMode::Utf16LE:
    case UniMode::Utf16BE: {
        bool le = mode == UniMode::Utf16LE;
        size_t skip = 0;
        if (raw.size() >= 2) {
            unsigned char a = raw[0], b = raw[1];
            if ((le && a == 0xFF && b == 0xFE) || (!le && a == 0xFE && b == 0xFF)) skip = 2;
        }
        std::string body = raw.substr(skip);
        if (body.size() & 1) body.push_back('\0');
        d.utf8 = convert(body, "UTF-8", le ? "UTF-16LE" : "UTF-16BE", &ok, &d.lossy);
        d.charset = le ? "UTF-16LE" : "UTF-16BE";
        if (!ok) { d.utf8 = raw; d.lossy = true; }
        break;
    }
    case UniMode::Ansi: {
        std::string cs = charset.empty() ? "WINDOWS-1252" : charset;
        d.utf8 = convert(raw, "UTF-8", cs.c_str(), &ok, &d.lossy);
        d.charset = cs;
        if (!ok) { d.utf8 = convert(raw, "UTF-8", "ISO-8859-1", &ok, &d.lossy); d.charset = "ISO-8859-1"; }
        break;
    }
    }
    return d;
}

DecodedText decodeText(const std::string& raw, const std::string& ansiCharset)
{
    const unsigned char* u = reinterpret_cast<const unsigned char*>(raw.data());
    if (raw.size() >= 3 && u[0] == 0xEF && u[1] == 0xBB && u[2] == 0xBF) return decodeAs(raw, UniMode::Utf8Bom, "");
    if (raw.size() >= 2 && u[0] == 0xFF && u[1] == 0xFE) return decodeAs(raw, UniMode::Utf16LE, "");
    if (raw.size() >= 2 && u[0] == 0xFE && u[1] == 0xFF) return decodeAs(raw, UniMode::Utf16BE, "");
    bool le = true;
    if (looksUtf16(raw, le)) return decodeAs(raw, le ? UniMode::Utf16LE : UniMode::Utf16BE, "");
    if (g_utf8_validate(raw.data(), gssize(raw.size()), nullptr)) {
        DecodedText d; d.utf8 = raw; d.mode = UniMode::Utf8; return d;
    }
    return decodeAs(raw, UniMode::Ansi, ansiCharset);
}

bool encodeText(const std::string& utf8, UniMode mode, const std::string& charset, std::string& out, std::string* err)
{
    bool ok = true;
    switch (mode) {
    case UniMode::Utf8:    out = utf8; return true;
    case UniMode::Utf8Bom: out = "\xEF\xBB\xBF" + utf8; return true;
    case UniMode::Utf16LE: out = "\xFF\xFE"; out += convert(utf8, "UTF-16LE", "UTF-8", &ok, nullptr); break;
    case UniMode::Utf16BE: out = "\xFE\xFF"; out += convert(utf8, "UTF-16BE", "UTF-8", &ok, nullptr); break;
    case UniMode::Ansi:    out = convert(utf8, charset.empty() ? "WINDOWS-1252" : charset.c_str(), "UTF-8", &ok, nullptr); break;
    }
    if (!ok && err) *err = "Could not convert the text to " + (charset.empty() ? std::string("the target encoding") : charset);
    return ok;
}

EolType detectEol(const std::string& s, EolType fallback)
{
    size_t n = std::min<size_t>(s.size(), 1 << 20);
    for (size_t i = 0; i < n; ++i) {
        if (s[i] == '\r') return (i + 1 < s.size() && s[i + 1] == '\n') ? EolType::CRLF : EolType::CR;
        if (s[i] == '\n') return EolType::LF;
    }
    return fallback;
}

const char* uniModeLabel(UniMode m, const std::string& charset)
{
    switch (m) {
    case UniMode::Utf8: return "UTF-8";
    case UniMode::Utf8Bom: return "UTF-8-BOM";
    case UniMode::Utf16LE: return "UTF-16 LE BOM";
    case UniMode::Utf16BE: return "UTF-16 BE BOM";
    case UniMode::Ansi:
        for (auto& c : ansiCharsets()) if (charset == c.iconvName) return c.iconvName;
        return "ANSI";
    }
    return "";
}

const char* eolLabel(EolType e)
{
    switch (e) {
    case EolType::CRLF: return "Windows (CR LF)";
    case EolType::CR: return "Macintosh (CR)";
    case EolType::LF: return "Unix (LF)";
    }
    return "";
}

} // namespace cam
