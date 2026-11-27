# Camelion++ plugin SDK

A Camelion++ plugin is a Linux shared object, `<Name>/<Name>.so`, placed in a plugin
directory:

- `~/.local/share/camelion++/plugins/<Name>/<Name>.so` (per user; Plugins Admin installs here)
- `<prefix>/lib/camelion++/plugins/<Name>/<Name>.so` (system wide)

The header is `sdk/include/CamelionPluginInterface.h`, installed as
`<prefix>/include/camelion++/CamelionPluginInterface.h` together with `Scintilla.h`.

## Minimal plugin

```cpp
#define CAMELION_SENDMESSAGE_SHIM          // gives you a ::SendMessage() look-alike
#include <CamelionPluginInterface.h>

CAMELION_PLUGIN_VERSION("1.0.0")           // exported version + API version

static NppData npp;

static void hello()
{
    HWND sci = npp._scintillaMainHandle;
    SendMessage(sci, SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("Hello from a plugin!"));
}

static ShortcutKey key = {true, true, false, 'H'};    // Ctrl+Alt+H (Windows VK codes)
static FuncItem items[] = {
    {"Say hello", hello, 0, false, &key},
};

CAMELION_PLUGIN_EXPORT void setInfo(NppData d)            { npp = d; camelionInitSendMessage(d); }
CAMELION_PLUGIN_EXPORT const char* getName()              { return "Hello"; }
CAMELION_PLUGIN_EXPORT FuncItem* getFuncsArray(int* n)    { *n = 1; return items; }
CAMELION_PLUGIN_EXPORT void beNotified(SCNotification* n) { (void)n; }
CAMELION_PLUGIN_EXPORT LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
```

Build:

```bash
g++ -std=c++17 -O2 -fPIC -shared -fvisibility=hidden \
    -I/usr/include/camelion++ Hello.cpp -o Hello.so
mkdir -p ~/.local/share/camelion++/plugins/Hello && cp Hello.so ~/.local/share/camelion++/plugins/Hello/
```

The plugin appears under **Plugins > Hello** at the next start, or immediately when it is
installed through Plugins Admin.

## Porting a Notepad++ plugin

| Notepad++ (Windows) | Camelion++ (Linux) |
|---|---|
| `extern "C" __declspec(dllexport)` | `CAMELION_PLUGIN_EXPORT` |
| `const TCHAR* getName()`, `wchar_t _itemName[64]` | `const char*` / `char _itemName[64]`, **UTF-8** |
| `BOOL isUnicode()` | not needed; export `camelionPluginApiVersion` (via `CAMELION_PLUGIN_VERSION`) |
| `::SendMessage(nppData._nppHandle, NPPM_X, w, l)` | unchanged with `CAMELION_SENDMESSAGE_SHIM`, or `nppData._sendMessage(...)` |
| `::SendMessage(scintillaHandle, SCI_X, w, l)` | unchanged |
| DLL version resource | `CAMELION_PLUGIN_VERSION("x.y.z")` |
| `NPPM_GETPLUGINSCONFIGDIR` (wchar_t buffer) | same message, `char` buffer |
| Win32 dialogs / docking (`NPPM_DMMREGASDCKDLG`) | GTK 3 widgets; `CAMM_ADDPANEL` docks a `GtkWidget*` in the side panel |
| `HWND` parent for dialogs | `CAMM_GETMAINWINDOW` returns the `GtkWindow*` |
| `ShortcutKey::_key` VK code | same VK codes (A–Z, 0–9, F1–F12, arrows, ...) |

`NPPN_*` notification codes and `SCN_*` notifications reach `beNotified()` as in Notepad++.
All Scintilla notifications are forwarded. Call the API from the GTK main thread only.

## Messages implemented

Window and documents: `NPPM_GETCURRENTSCINTILLA`, `NPPM_GETCURRENTLANGTYPE`, `NPPM_SETCURRENTLANGTYPE`,
`NPPM_GETNBOPENFILES`, `NPPM_GETCURRENTDOCINDEX`, `NPPM_ACTIVATEDOC`, `NPPM_GETCURRENTVIEW`,
`NPPM_GETCURRENTBUFFERID`, `NPPM_GETBUFFERIDFROMPOS`, `NPPM_GETPOSFROMBUFFERID`,
`NPPM_GETFULLPATHFROMBUFFERID`, `NPPM_GETBUFFERLANGTYPE`, `NPPM_SETBUFFERLANGTYPE`,
`NPPM_GETBUFFERENCODING`, `NPPM_SETBUFFERENCODING`, `NPPM_GETBUFFERFORMAT`, `NPPM_SETBUFFERFORMAT`,
`NPPM_RELOADBUFFERID`, `NPPM_RELOADFILE`, `NPPM_SWITCHTOFILE`, `NPPM_DOOPEN`, `NPPM_SAVECURRENTFILE`,
`NPPM_SAVECURRENTFILEAS`, `NPPM_SAVEFILE`, `NPPM_SAVEALLFILES`, `NPPM_MAKECURRENTBUFFERDIRTY`.

UI: `NPPM_SETSTATUSBAR`, `NPPM_GETMENUHANDLE` (returns a `GtkMenuShell*`), `NPPM_SETMENUITEMCHECK`,
`NPPM_MENUCOMMAND` (Notepad++ `IDM_*` values for the common commands), `NPPM_HIDETOOLBAR` /
`ISTOOLBARHIDDEN`, `NPPM_HIDETABBAR` / `ISTABBARHIDDEN`, `NPPM_HIDEMENU` / `ISMENUHIDDEN`,
`NPPM_HIDESTATUSBAR` / `ISSTATUSBARHIDDEN`, `NPPM_ISDARKMODEENABLED`,
`NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR` / `BACKGROUNDCOLOR`.

Resources: `NPPM_ALLOCATECMDID`, `NPPM_ALLOCATEMARKER`, `NPPM_ALLOCATEINDICATOR`, `NPPM_GETBOOKMARKID`,
`NPPM_CREATESCINTILLAHANDLE` / `NPPM_DESTROYSCINTILLAHANDLE`.

Information: `NPPM_GETNPPVERSION`, `NPPM_GETPLUGINSCONFIGDIR`, `NPPM_GETPLUGINHOMEPATH`,
`NPPM_GETNPPSETTINGSDIRPATH`, `NPPM_GETLANGUAGENAME`, `NPPM_GETLANGUAGEDESC`, `NPPM_ISAUTOINDENTON`,
`NPPM_GETCURRENTMACROSTATUS`, and the `RUNCOMMAND_USER` family: `NPPM_GETFULLCURRENTPATH`,
`NPPM_GETCURRENTDIRECTORY`, `NPPM_GETFILENAME`, `NPPM_GETNAMEPART`, `NPPM_GETEXTPART`,
`NPPM_GETCURRENTWORD`, `NPPM_GETNPPDIRECTORY`, `NPPM_GETNPPFULLFILEPATH`, `NPPM_GETFILENAMEATCURSOR`,
`NPPM_GETCURRENTLINESTR`, `NPPM_GETCURRENTLINE`, `NPPM_GETCURRENTCOLUMN`.

Inter-plugin: `NPPM_MSGTOPLUGIN` (destination = folder name, with or without `.so`).

Camelion++ only: `CAMM_GETMAINWINDOW`, `CAMM_ADDPANEL`, `CAMM_SHOWPANEL`, `CAMM_GETSCINTILLAWIDGET`,
`CAMM_GETVERSIONSTRING`.

Unknown messages return 0.

## Publishing through Plugins Admin (nppPluginList)

1. Build release binaries for each architecture (`linux-x64`, `linux-arm64`). Plugins that only
   use the SDK cross-compile with `aarch64-linux-gnu-g++`; see `tools/package-plugins.sh`.
2. Zip each so that `<Name>.so` sits at the **root** of the archive, and upload the zips somewhere
   reachable over HTTPS (for example GitHub release assets).
3. Add an entry to `src/pl.linux-<arch>.json` in the nppPluginList repository:

```json
{
    "folder-name": "Hello",
    "display-name": "Hello",
    "version": "1.0.0",
    "camelion-compatible-versions": "[1.0,]",
    "id": "<sha256 of the zip>",
    "repository": "https://github.com/you/Hello/releases/download/v1.0.0/Hello-1.0.0-linux-x64.zip",
    "description": "Says hello.",
    "author": "You",
    "homepage": "https://github.com/you/Hello"
}
```

4. Run `python3 validator.py linux-x64`. It downloads the zip and checks the hash, the ELF
   architecture, the required exports and that `camelionPluginVersion` equals `version`.
5. To try the list before it is signed, copy it to
   `~/.config/camelion++/plugins/Config/nppPluginList.json` (test mode; `file://` URLs allowed).
