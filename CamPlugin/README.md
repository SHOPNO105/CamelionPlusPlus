# CamPlugin

**CamPlugin** is the plugin list of [Camelion++](../README.md),
a Notepad++-style editor for Linux. It is a fork of the official
[Notepad++ Plugin List](https://github.com/notepad-plus-plus/nppPluginList) (version 2.0) and keeps
its format, so the same tools and JSON layout serve both editors.

What CamPlugin adds is described in [Camelion++ (Linux) support](#camelion-linux-support) below:
the Linux lists `src/pl.linux-x64.json` / `src/pl.linux-arm64.json`, the signed `nppPluginList.so`,
and Linux support in `validator.py`. The Windows lists are kept unchanged from upstream.

It lives in the `CamPlugin/` folder of the Camelion++ repository; its Linux CI is
`.github/workflows/camplugin-linux.yml` at the repository root (the workflows kept under
`CamPlugin/.github/` are the inherited Windows ones and do not run from here).

The file names (`nppPluginList.so`, `nppPluginList.json`) are unchanged on purpose: Camelion++
looks for exactly those names.

---

<picture>
  <source media="(prefers-color-scheme: light)" srcset="./nppPlugins.png">
  <source media="(prefers-color-scheme: dark)" srcset="./nppPluginsDark.png">
  <img alt="Notepad++ Plugin List" src="./nppPlugins.png">
</picture>

**Notepad++ Plugin List** is an official collection of [Notepad++](https://github.com/notepad-plus-plus/notepad-plus-plus) plugins. It provides a list of plugins to the built-in Plugin Admin in Notepad++ for the installation/update/deletion of plugins as users desire.

The list is in JSON format, but encapsulated in a binary (DLL), so it can be signed by a certificate to avoid being hacked. Any Notepad++ plugin is welcome to be submitted here, but please test it locally before doing your PR.

To review the current list of plugins and their features see the generated list of Plugins in either:

* [32-Bit Plugin List](doc/plugin_list_x86.md)
* [64-Bit Plugin List](doc/plugin_list_x64.md)
* [64-Bit ARM Plugin List](doc/plugin_list_arm64.md)

Here is all the information you need to perform local tests:  
https://npp-user-manual.org/docs/plugins/#plugins-admin

Please check here if you need any support:  
https://community.notepad-plus-plus.org/topic/16566/support-for-plugins-admin-npppluginlist

Build Status
------------

[![Github build status](https://github.com/notepad-plus-plus/nppPluginList/actions/workflows/CI_build.yml/badge.svg)](https://github.com/notepad-plus-plus/nppPluginList/actions/workflows/CI_build.yml)
[![GitHub release](https://img.shields.io/github/release/notepad-plus-plus/nppPluginList.svg)](https://github.com/notepad-plus-plus/nppPluginList/releases)

Camelion++ (Linux) support
--------------------------

This repository also carries the plugin lists of **Camelion++**, the Linux editor
built on the same Scintilla/Lexilla engines and the same plugin-list format.
Windows DLL plugins cannot run on Linux, so Linux plugins have their own lists:

| List | Plugins | Overview |
|---|---|---|
| `src/pl.linux-x64.json` | Linux x86-64 `.so` plugins | [doc/plugin_list_linux-x64.md](doc/plugin_list_linux-x64.md) |
| `src/pl.linux-arm64.json` | Linux AArch64 `.so` plugins | [doc/plugin_list_linux-arm64.md](doc/plugin_list_linux-arm64.md) |

Entries use exactly the same fields as the Windows lists (`folder-name`, `display-name`,
`version`, `id` = SHA-256 of the zip, `repository`, `description`, `author`, `homepage`).
`camelion-compatible-versions` plays the role of `npp-compatible-versions` for Camelion++
versions. The zip must contain `<folder-name>.so` at its root.

### What a Linux plugin must export

Built against `CamelionPluginInterface.h` (Camelion++ SDK, a port of Notepad++'s
`PluginInterface.h`): `setInfo`, `getName`, `getFuncsArray`, `beNotified`, `messageProc`,
`camelionPluginApiVersion`, plus the version string declared with
`CAMELION_PLUGIN_VERSION("x.y.z")`. `validator.py` checks all of this with
[pyelftools](https://github.com/eliben/pyelftools), the way it checks DLL version
resources on Windows.

### Validating locally

```bash
pip3 install -r requirements-linux.txt
python3 validator.py linux-x64          # downloads every plugin, checks hash, ELF arch, exports, version
python3 validator.py all_md             # regenerates every doc/plugin_list_*.md
python3 sort_plugin_lists.py            # sorts the Linux lists by display-name
```

`file://` repositories are accepted by the validator to test a list before publishing.

### nppPluginList.so

`nppPluginList.dll` is a signed resource container; its Linux counterpart is
`nppPluginList.so`, whose JSON lives in the `.npp_pluginlist` ELF section and whose
Ed25519 signature lives in `.npp_pluginlist_sig`. Camelion++ reads both sections as data
(the library is never loaded) and refuses a list whose signature does not verify
against the public key compiled into Camelion++.

```bash
cmake -S linux -B build-linux -DPLUGINLIST_ARCH=x64 \
      -DPLUGINLIST_SIGNING_KEY=/secure/place/pluginlist-ed25519-private.pem
cmake --build build-linux                                  # => build-linux/nppPluginList.so
linux/sign_pluginlist.sh verify build-linux/nppPluginList.so pluginlist-ed25519-pub.pem
```

Create a key pair with `linux/sign_pluginlist.sh genkey <dir>`. The CI workflow
`.github/workflows/CI_build_linux.yml` signs with the `PLUGINLIST_SIGNING_KEY`
repository secret. Never commit the private key.

### Testing a list in Camelion++

Like Notepad++'s debug mode, Camelion++ uses an **unsigned**
`~/.config/camelion++/plugins/Config/nppPluginList.json` when that file exists
(Plugins Admin then shows a "TEST MODE" banner and also accepts `file://` downloads).
Delete it to go back to the signed `nppPluginList.so`.
`camelion++ --check-pluginlist` prints which list is used and whether its signature verified.
