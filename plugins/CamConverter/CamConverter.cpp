// CamConverter - Camelion++ port of the idea behind Notepad++'s bundled NppConverter plugin.
// ASCII <-> HEX conversion of the selection (or the whole document).
//
// This file is also the reference example of a Camelion++ plugin: it only uses
// the SDK header and Scintilla messages, so it builds for any architecture
// without GTK development files.
#define CAMELION_SENDMESSAGE_SHIM
#include <CamelionPluginInterface.h>
#include <string>
#include <cstring>
#include <cctype>
#include <cstdio>

CAMELION_PLUGIN_VERSION("1.0.0")

static NppData nppData;

static HWND currentScintilla()
{
    int which = 0;
    SendMessage(nppData._nppHandle, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
    return which == 0 ? nppData._scintillaMainHandle : nppData._scintillaSecondHandle;
}

// Returns the selected text (or whole document if nothing is selected) and its range.
static std::string getTarget(HWND sci, Sci_Position& start, Sci_Position& end)
{
    start = SendMessage(sci, SCI_GETSELECTIONSTART);
    end = SendMessage(sci, SCI_GETSELECTIONEND);
    if (start == end) { start = 0; end = SendMessage(sci, SCI_GETLENGTH); }
    std::string s(size_t(end - start), '\0');
    Sci_TextRangeFull tr;
    tr.chrg.cpMin = start; tr.chrg.cpMax = end;
    std::string buf(size_t(end - start) + 1, '\0');
    tr.lpstrText = buf.data();
    SendMessage(sci, SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&tr));
    return buf.substr(0, size_t(end - start));
}

static void replaceTarget(HWND sci, Sci_Position start, Sci_Position end, const std::string& out)
{
    SendMessage(sci, SCI_BEGINUNDOACTION);
    SendMessage(sci, SCI_SETTARGETRANGE, WPARAM(start), end);
    SendMessage(sci, SCI_REPLACETARGET, out.size(), reinterpret_cast<LPARAM>(out.c_str()));
    SendMessage(sci, SCI_ENDUNDOACTION);
    SendMessage(sci, SCI_SETSEL, WPARAM(start), start + Sci_Position(out.size()));
}

static void asciiToHex()
{
    HWND sci = currentScintilla();
    Sci_Position a, b;
    std::string in = getTarget(sci, a, b);
    if (in.empty()) return;
    std::string out;
    char buf[4];
    for (size_t i = 0; i < in.size(); ++i) {
        std::snprintf(buf, sizeof buf, "%02X", static_cast<unsigned char>(in[i]));
        out += buf;
    }
    replaceTarget(sci, a, b, out);
}

static int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void hexToAscii()
{
    HWND sci = currentScintilla();
    Sci_Position a, b;
    std::string in = getTarget(sci, a, b);
    std::string digits;
    for (char c : in) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (hexVal(c) < 0) {
            SendMessage(nppData._nppHandle, NPPM_SETSTATUSBAR, STATUSBAR_DOC_TYPE,
                        reinterpret_cast<LPARAM>("CamConverter: the selection is not hexadecimal"));
            return;
        }
        digits += c;
    }
    if (digits.size() % 2) {
        SendMessage(nppData._nppHandle, NPPM_SETSTATUSBAR, STATUSBAR_DOC_TYPE,
                    reinterpret_cast<LPARAM>("CamConverter: odd number of hex digits"));
        return;
    }
    std::string out;
    for (size_t i = 0; i < digits.size(); i += 2) out += char(hexVal(digits[i]) * 16 + hexVal(digits[i + 1]));
    replaceTarget(sci, a, b, out);
}

static void asciiToHexSpaced()
{
    HWND sci = currentScintilla();
    Sci_Position a, b;
    std::string in = getTarget(sci, a, b);
    if (in.empty()) return;
    std::string out;
    char buf[4];
    for (size_t i = 0; i < in.size(); ++i) {
        std::snprintf(buf, sizeof buf, "%02X", static_cast<unsigned char>(in[i]));
        if (i) out += (i % 16 == 0) ? '\n' : ' ';
        out += buf;
    }
    replaceTarget(sci, a, b, out);
}

static void about()
{
    char ver[32] = "";
    SendMessage(nppData._nppHandle, CAMM_GETVERSIONSTRING, sizeof ver, reinterpret_cast<LPARAM>(ver));
    std::string msg = std::string("CamConverter 1.0.0 running in Camelion++ ") + ver;
    SendMessage(nppData._nppHandle, NPPM_SETSTATUSBAR, STATUSBAR_DOC_TYPE, reinterpret_cast<LPARAM>(msg.c_str()));
}

static ShortcutKey hexKey = {true, true, true, 'H'};   // Ctrl+Alt+Shift+H, VK code like on Windows
static FuncItem funcItems[] = {
    {"ASCII -> HEX", asciiToHex, 0, false, &hexKey},
    {"ASCII -> HEX (spaced, 16 per line)", asciiToHexSpaced, 0, false, nullptr},
    {"HEX -> ASCII", hexToAscii, 0, false, nullptr},
    {"---", nullptr, 0, false, nullptr},
    {"About CamConverter", about, 0, false, nullptr},
};

CAMELION_PLUGIN_EXPORT void setInfo(NppData d) { nppData = d; camelionInitSendMessage(d); }
CAMELION_PLUGIN_EXPORT const char* getName() { return "Converter"; }
CAMELION_PLUGIN_EXPORT FuncItem* getFuncsArray(int* n) { *n = int(sizeof(funcItems) / sizeof(funcItems[0])); return funcItems; }
CAMELION_PLUGIN_EXPORT void beNotified(SCNotification*) {}
CAMELION_PLUGIN_EXPORT LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
