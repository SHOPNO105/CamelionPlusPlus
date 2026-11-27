# Camelion++

**Camelion++** is a Notepad++-style source code editor for Linux.

It grew out of the *Camelion* study, an extraction and analysis of the
Notepad++ v8.9.9.1 installer. Notepad++ itself is a Win32 program, so Camelion++
does not try to run it. It is a native GTK 3 editor built on the same
engines Notepad++ uses, **Scintilla 5.6.9** and **Lexilla 5.5.5**, and it reads the
Notepad++ data files that the study extracted:

| Notepad++ file (from the installer) | Used by Camelion++ for |
|---|---|
| `langs.model.xml` | languages, file extensions, keywords, comment markers |
| `stylers.model.xml`, `themes/*.xml` | syntax colours, global styles, 22 themes (Monokai, Zenburn, Solarized, ...) |
| `autoCompletion/*.xml` | function/keyword completion and call tips |
| `functionList/*.xml` | the Function List panel (same regex parsers) |
| `nppPluginList` (JSON format, signed container) | Plugins Admin |

![Camelion++ with the Monokai theme and the Function List](docs/screenshots/editor-monokai-functionlist.png)

## Features

- Tabs with drag-reorder, middle-click close, dirty/read-only markers, tab context menu
- 95 languages with the Notepad++ lexer configuration (keyword sets, sub-styles, fold properties); Markdown added
- Themes and a Style Configurator (theme + font override); dark GTK chrome follows the editor theme
- Encodings: UTF-8, UTF-8-BOM, UTF-16 LE/BE BOM, 21 ANSI character sets; "Encode in" (reinterpret) and "Convert to"
- EOL detection and conversion (CRLF / LF / CR)
- Find / Replace / Find in Files / Mark with Normal, Extended (`\n \t \x..`) and Regular-expression modes, Count,
  Find All (current / all open documents), Replace All in all opened documents, Replace in Files, highlight all matches,
  Search Results panel (F7, F4/Shift+F4)
- Smart highlighting, brace matching, bookmarks (Ctrl+F2/F2), 5 token styles, change-history margin, code folding
- Line operations (duplicate, move, join, split, sort ×8, remove duplicates/empty lines), case conversion ×6,
  blank operations, comment/uncomment (line & block), column editor, column mode (Alt+drag), date/time insertion
- Auto-completion (functions + words + paths), parameter hints, auto-indent, optional auto-close
- Function List panel, Macro record/playback/save/run-N-times, Run menu with `$(FULL_CURRENT_PATH)`-style variables
- Session restore with **session snapshot** (unsaved and untitled documents survive restarts, like Notepad++)
- External change detection, Monitoring (tail -f), printing, Summary, file rename/trash, open in terminal/file manager
- Single instance (`camelion++ file.c:120` jumps to line 120 in the running window), Notepad++-compatible command line
  (`-n<line> -c<col> -l<lang> -ro -nosession -noPlugin -multiInst`)
- **Plugins**: Linux `.so` plugins with a Notepad++-compatible API, and **Plugins Admin** driven by the
  **nppPluginList** format (see below)

## Installing

- **Arch Linux:** `packaging/arch/PKGBUILD` builds from the `v1.0.0` release: download it into an empty folder
  whose path has no spaces and run `makepkg -si`.
- **Debian / Ubuntu:** download `camelion-plus-plus_1.0.0_amd64.deb` from the
  [releases](https://github.com/SHOPNO105/CamelionPlusPlus/releases) and run `sudo apt install ./camelion-plus-plus_1.0.0_amd64.deb`.
- **Anything else:** build from source as below.

## Building

Ubuntu/Debian:

```bash
sudo apt install build-essential cmake ninja-build pkg-config libgtk-3-dev \
     libcurl4-openssl-dev libarchive-dev libssl-dev libtinyxml2-dev nlohmann-json3-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/camelion++                      # runs from the build tree, bundled plugins included
sudo cmake --install build              # or: cd build && cpack  =>  .deb and .tar.gz
```

Fedora: `gtk3-devel libcurl-devel libarchive-devel openssl-devel tinyxml2-devel json-devel`.
Arch: `gtk3 curl libarchive openssl tinyxml2 nlohmann-json`.

Scintilla and Lexilla are vendored in `third_party/` (taken from the Notepad++ source tree,
GTK backend included). Lexilla's `LexUser.cxx` was patched to build without `windows.h`.

## Where things live

| Path | Content |
|---|---|
| `~/.config/camelion++/config.xml` | settings (GUIConfig entries), recent files, saved macros and run commands |
| `~/.config/camelion++/session.xml` | session; unsaved content is snapshotted in `~/.local/share/camelion++/backup/` |
| `~/.config/camelion++/langs.xml`, `stylers.xml` | optional user copies (Settings > Edit langs.xml / stylers.xml) |
| `~/.config/camelion++/themes/` | your own themes (any Notepad++ theme XML works) |
| `~/.config/camelion++/plugins/Config/` | plugin settings; also where a test plugin list may be put |
| `~/.local/share/camelion++/plugins/<Name>/<Name>.so` | plugins installed by you / by Plugins Admin |
| `<prefix>/lib/camelion++/plugins/` | system plugins, and `Config/nppPluginList.so` (the signed list) |
| `<prefix>/share/camelion++/` | the Notepad++ data files |

## Plugins

The plugin ABI mirrors Notepad++'s `PluginInterface.h`: the same entry points
(`setInfo`, `getName`, `getFuncsArray`, `beNotified`, `messageProc`), the same `NppData`,
`FuncItem` and `ShortcutKey` structures, the same `NPPM_*` / `NPPN_*` numbers and
`LangType` values, and Scintilla messages sent to the Scintilla handle. Strings are UTF-8 and
there is no Win32 `SendMessage`; `NppData` carries a function pointer instead, and a
`::SendMessage` look-alike is available with `#define CAMELION_SENDMESSAGE_SHIM`. Plugins that
only talk to Scintilla and Notepad++ through messages usually port with small changes.
GTK plugins can dock panels (`CAMM_ADDPANEL`).

Bundled plugins:

| Plugin | What it is |
|---|---|
| **Converter** (`CamConverter`) | ASCII ↔ HEX, the idea behind Notepad++'s bundled NppConverter |
| **MIME Tools** (`CamMimeTools`) | Base64 / URL / Quoted-printable, the idea behind Notepad++'s bundled mimeTools |
| **Document List** (`CamDocList`) | a GTK side panel listing open documents (panel API demo) |

See **[docs/PLUGIN_SDK.md](docs/PLUGIN_SDK.md)** for writing, building, packaging and publishing plugins.

## nppPluginList support

Plugins Admin works exactly like the Notepad++ one, from the same JSON format:

1. It reads `nppPluginList.so`, the Linux counterpart of `nppPluginList.dll`, from
   `~/.config/camelion++/plugins/Config/` or `<prefix>/lib/camelion++/plugins/Config/`.
   The JSON lives in the `.npp_pluginlist` ELF section and an **Ed25519 signature** in
   `.npp_pluginlist_sig`. Camelion++ reads them as data and never loads the library. A list whose
   signature doesn't verify against the public key compiled into Camelion++
   (`packaging/keys/pluginlist-ed25519-pub.pem`) is refused. This is the analogue of Notepad++
   checking the DLL's Authenticode signature.
2. It only accepts lists for its own architecture (`linux-x64`, `linux-arm64`). A Windows list
   (`"arch": "64"`) is rejected with an explanation, because Windows DLLs cannot run on Linux.
3. **Install / Update**: it downloads the zip over HTTPS (proxy variables honoured), checks the
   **SHA-256 against the list's `id`**, extracts it safely (no absolute paths, no `..`, no links),
   checks that `<folder-name>.so` is an ELF object for this CPU with the required exports,
   installs it atomically, and **loads new plugins immediately**. Updates and removals offer a
   restart that keeps the session.
4. `camelion-compatible-versions` (or `npp-compatible-versions`) hides plugins that don't
   support the running version ("Incompatible" tab).
5. **Test mode**: an unsigned `~/.config/camelion++/plugins/Config/nppPluginList.json` takes
   precedence, as in Notepad++'s debug mode. A banner shows, and `file://` URLs are allowed.
   `camelion++ --check-pluginlist` prints which list is used and whether it verified.

The companion repository **[CamPlugin](https://github.com/SHOPNO105/CamPlugin)** (the nppPluginList port) contains `src/pl.linux-x64.json`,
`src/pl.linux-arm64.json`, the schema update, the ELF-aware `validator.py`, the CMake build
and signing of `nppPluginList.so`, and a CI workflow.

### Signing key

`packaging/keys/pluginlist-ed25519-pub.pem` is the public half of the key that signed the
included `packaging/nppPluginList/nppPluginList.so`. Whoever holds the private half controls
which plugins your users are offered, so keep it out of the repository (for example in a CI
secret). To use your own key:

```bash
openssl genpkey -algorithm ed25519 -out pluginlist-private.pem
openssl pkey -in pluginlist-private.pem -pubout -out pluginlist-pub.pem
cmake -S . -B build -DCAMELION_PLUGINLIST_PUBKEY=$PWD/pluginlist-pub.pem
```

and build `nppPluginList.so` with `-DPLUGINLIST_SIGNING_KEY=pluginlist-private.pem`.

## Differences from Notepad++

- Linux/GTK 3 only; no Windows plugins (DLLs) can be loaded.
- One editing view: there is no split "other view" yet (`_scintillaSecondHandle` aliases the main view).
- User Defined Languages are not supported yet (the UDL lexer is built; the UDL dialog and
  `userDefineLangs` loading are not). Markdown uses Lexilla's Markdown lexer instead of the bundled UDL.
- No Shortcut Mapper, Document Map, Folder as Workspace, Project panels, Compare or auto-updater
  (Linux distributions update software through their package managers).
- Regular expressions are ECMAScript (C++11) in documents and PCRE in Function List parsers;
  Notepad++ uses Boost for both. Most everyday patterns behave identically.

## Licence

GPL-3.0-or-later, see [LICENSE](LICENSE). The Notepad++ data files are © Don HO and the
Notepad++ contributors (GPL-3); Scintilla and Lexilla are © Neil Hodgson (their own licence).
Camelion++ is not affiliated with or endorsed by the Notepad++ project.
