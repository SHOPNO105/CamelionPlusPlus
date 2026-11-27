// Camelion++ - well-known directories
#pragma once
#include <string>
#include <vector>

namespace cam::paths {

std::string exePath();                 // /usr/bin/camelion++
std::string dataDir();                 // langs.model.xml, themes/, autoCompletion/ ...
std::string configDir();               // ~/.config/camelion++
std::string userThemesDir();           // ~/.config/camelion++/themes
std::string pluginsConfigDir();        // ~/.config/camelion++/plugins/Config
std::string userPluginsDir();          // ~/.local/share/camelion++/plugins
std::string systemPluginsDir();        // <prefix>/lib/camelion++/plugins
std::vector<std::string> pluginDirs(); // all, user first
std::string backupDir();               // ~/.local/share/camelion++/backup

std::string join(const std::string& a, const std::string& b);
bool fileExists(const std::string& p);
bool dirExists(const std::string& p);
bool mkdirs(const std::string& p);
std::string baseName(const std::string& p);
std::string dirName(const std::string& p);
std::string extension(const std::string& p);   // without the dot, lower-case
std::string readFile(const std::string& p, bool* ok = nullptr);
bool writeFileAtomic(const std::string& p, const std::string& data, std::string* err = nullptr);
bool removeTree(const std::string& p);

} // namespace cam::paths
