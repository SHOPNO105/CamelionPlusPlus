// CamMimeTools - Camelion++ port of the idea behind Notepad++'s bundled "MIME Tools" plugin.
// Base64, URL (percent) and Quoted-Printable encoding/decoding of the selection.
#define CAMELION_SENDMESSAGE_SHIM
#include <CamelionPluginInterface.h>
#include <string>
#include <vector>
#include <cctype>
#include <cstdio>

CAMELION_PLUGIN_VERSION("1.0.0")

static NppData nppData;

static HWND sci()
{
    int which = 0;
    SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
    return which == 0 ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
}

static void status(const char* s) { SendMessage(nppData._nppHandle, NPPM_SETSTATUSBAR, STATUSBAR_DOC_TYPE, reinterpret_cast<LPARAM>(s)); }

static bool selection(std::string& text, Sci_Position& a, Sci_Position& b)
{
    HWND h = sci();
    a = SendMessage(h, SCI_GETSELECTIONSTART);
    b = SendMessage(h, SCI_GETSELECTIONEND);
    if (a == b) { status("MIME Tools: select some text first"); return false; }
    std::string buf(size_t(b - a) + 1, '\0');
    Sci_TextRangeFull tr;
    tr.chrg.cpMin = a; tr.chrg.cpMax = b; tr.lpstrText = buf.data();
    SendMessage(h, SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&tr));
    text = buf.substr(0, size_t(b - a));
    return true;
}

static void put(Sci_Position a, Sci_Position b, const std::string& out)
{
    HWND h = sci();
    SendMessage(h, SCI_BEGINUNDOACTION);
    SendMessage(h, SCI_SETTARGETRANGE, WPARAM(a), b);
    SendMessage(h, SCI_REPLACETARGET, out.size(), reinterpret_cast<LPARAM>(out.data()));
    SendMessage(h, SCI_ENDUNDOACTION);
    SendMessage(h, SCI_SETSEL, WPARAM(a), a + Sci_Position(out.size()));
}

// ---------------------------------------------------------------- Base64
static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64enc(const std::string& in, bool pad = true, size_t wrap = 0)
{
    std::string out;
    size_t col = 0;
    auto emit = [&](char c) { out += c; if (wrap && ++col == wrap) { out += '\n'; col = 0; } };
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        emit(B64[v >> 18]); emit(B64[(v >> 12) & 63]); emit(B64[(v >> 6) & 63]); emit(B64[v & 63]);
    }
    if (i < in.size()) {
        unsigned v = (unsigned char)in[i] << 16 | (i + 1 < in.size() ? (unsigned char)in[i + 1] << 8 : 0);
        emit(B64[v >> 18]); emit(B64[(v >> 12) & 63]);
        if (i + 1 < in.size()) emit(B64[(v >> 6) & 63]); else if (pad) emit('=');
        if (pad) emit('=');
    }
    if (wrap && !out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

static bool b64dec(const std::string& in, std::string& out, bool strict)
{
    int val = 0, bits = -8;
    out.clear();
    for (char c : in) {
        if (std::isspace((unsigned char)c)) continue;
        if (c == '=') break;
        const char* p = nullptr;
        for (const char* q = B64; *q; ++q) if (*q == c) { p = q; break; }
        if (c == '-') p = B64 + 62;   // base64url
        if (c == '_') p = B64 + 63;
        if (!p) { if (strict) return false; continue; }
        val = (val << 6) | int(p - B64);
        bits += 6;
        if (bits >= 0) { out += char((val >> bits) & 0xFF); bits -= 8; }
    }
    return true;
}

static void base64Encode() { std::string t; Sci_Position a, b; if (selection(t, a, b)) put(a, b, b64enc(t)); }
static void base64EncodeWrapped() { std::string t; Sci_Position a, b; if (selection(t, a, b)) put(a, b, b64enc(t, true, 76)); }
static void base64EncodeNoPad() { std::string t; Sci_Position a, b; if (selection(t, a, b)) put(a, b, b64enc(t, false)); }
static void base64Decode()
{
    std::string t, out; Sci_Position a, b;
    if (!selection(t, a, b)) return;
    if (!b64dec(t, out, true)) { status("MIME Tools: the selection is not valid Base64"); return; }
    put(a, b, out);
}
static void base64DecodeByLine()
{
    std::string t; Sci_Position a, b;
    if (!selection(t, a, b)) return;
    std::string out, line, dec;
    size_t s = 0;
    while (s <= t.size()) {
        size_t nl = t.find('\n', s);
        line = t.substr(s, nl == std::string::npos ? std::string::npos : nl - s);
        bool cr = !line.empty() && line.back() == '\r';
        if (cr) line.pop_back();
        b64dec(line, dec, false);
        out += dec;
        if (nl == std::string::npos) break;
        out += cr ? "\r\n" : "\n";
        s = nl + 1;
    }
    put(a, b, out);
}

// ---------------------------------------------------------------- URL
static void urlEncode()
{
    std::string t; Sci_Position a, b;
    if (!selection(t, a, b)) return;
    std::string out; char buf[4];
    for (unsigned char c : t) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += char(c);
        else { std::snprintf(buf, sizeof buf, "%%%02X", c); out += buf; }
    }
    put(a, b, out);
}

static int hv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static void urlDecode()
{
    std::string t; Sci_Position a, b;
    if (!selection(t, a, b)) return;
    std::string out;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '%' && i + 2 < t.size() && hv(t[i + 1]) >= 0 && hv(t[i + 2]) >= 0) { out += char(hv(t[i + 1]) * 16 + hv(t[i + 2])); i += 2; }
        else if (t[i] == '+') out += ' ';
        else out += t[i];
    }
    put(a, b, out);
}

// ---------------------------------------------------------------- Quoted-printable
static void qpEncode()
{
    std::string t; Sci_Position a, b;
    if (!selection(t, a, b)) return;
    std::string out; size_t col = 0; char buf[4];
    for (size_t i = 0; i < t.size(); ++i) {
        unsigned char c = t[i];
        std::string piece;
        if (c == '\n') { out += "\n"; col = 0; continue; }
        if (c == '\r' && i + 1 < t.size() && t[i + 1] == '\n') { out += "\r\n"; ++i; col = 0; continue; }
        bool trailingWs = (c == ' ' || c == '\t') && (i + 1 == t.size() || t[i + 1] == '\r' || t[i + 1] == '\n');
        if ((c >= 33 && c <= 126 && c != '=') || ((c == ' ' || c == '\t') && !trailingWs)) piece = char(c);
        else { std::snprintf(buf, sizeof buf, "=%02X", c); piece = buf; }
        if (col + piece.size() > 75) { out += "=\n"; col = 0; }
        out += piece; col += piece.size();
    }
    put(a, b, out);
}

static void qpDecode()
{
    std::string t; Sci_Position a, b;
    if (!selection(t, a, b)) return;
    std::string out;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '=') {
            if (i + 1 < t.size() && t[i + 1] == '\n') { ++i; continue; }
            if (i + 2 < t.size() && t[i + 1] == '\r' && t[i + 2] == '\n') { i += 2; continue; }
            if (i + 2 < t.size() && hv(t[i + 1]) >= 0 && hv(t[i + 2]) >= 0) { out += char(hv(t[i + 1]) * 16 + hv(t[i + 2])); i += 2; continue; }
        }
        out += t[i];
    }
    put(a, b, out);
}

static FuncItem funcItems[] = {
    {"Base64 Encode", base64Encode, 0, false, nullptr},
    {"Base64 Encode with line breaks", base64EncodeWrapped, 0, false, nullptr},
    {"Base64 Encode without padding", base64EncodeNoPad, 0, false, nullptr},
    {"Base64 Decode", base64Decode, 0, false, nullptr},
    {"Base64 Decode by line", base64DecodeByLine, 0, false, nullptr},
    {"---", nullptr, 0, false, nullptr},
    {"URL Encode", urlEncode, 0, false, nullptr},
    {"URL Decode", urlDecode, 0, false, nullptr},
    {"---", nullptr, 0, false, nullptr},
    {"Quoted-printable Encode", qpEncode, 0, false, nullptr},
    {"Quoted-printable Decode", qpDecode, 0, false, nullptr},
};

CAMELION_PLUGIN_EXPORT void setInfo(NppData d) { nppData = d; camelionInitSendMessage(d); }
CAMELION_PLUGIN_EXPORT const char* getName() { return "MIME Tools"; }
CAMELION_PLUGIN_EXPORT FuncItem* getFuncsArray(int* n) { *n = int(sizeof(funcItems) / sizeof(funcItems[0])); return funcItems; }
CAMELION_PLUGIN_EXPORT void beNotified(SCNotification*) {}
CAMELION_PLUGIN_EXPORT LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
