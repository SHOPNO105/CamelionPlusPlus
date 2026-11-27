# Camelion++ technical notes

Details moved out of the main README: file locations, the plugin system, how the
signed plugin list works, and how Camelion++ differs from Notepad++.

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

See **[docs/PLUGIN_SDK.md](PLUGIN_SDK.md)** for writing, building, packaging and publishing plugins.

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

The companion repository **[CamPlugin/](../CamPlugin/)** folder (the nppPluginList port) contains `src/pl.linux-x64.json`,
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
