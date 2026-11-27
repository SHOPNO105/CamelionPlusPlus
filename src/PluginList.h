// Camelion++ - nppPluginList support: loads and verifies the plugin list
#pragma once
#include <string>
#include <vector>
#include <array>
#include <utility>

namespace cam {

struct Version {
    std::array<int, 4> v{{0, 0, 0, 0}};
    bool set = false;
    static Version parse(const std::string& s);   // "1.2", "1.2.3.4"
    bool empty() const { return !set; }
    std::string toString() const;
    int compare(const Version& o) const;
    bool operator<(const Version& o) const { return compare(o) < 0; }
    bool operator>(const Version& o) const { return compare(o) > 0; }
    bool operator==(const Version& o) const { return compare(o) == 0; }
    // Notepad++ semantics of "[from,to]" (either bound may be empty)
    bool isCompatibleTo(const Version& from, const Version& to) const;
};

// "[8.3,]" or "[,8.2.1]" or "[6.9,6.9]" -> (from, to). Throws std::runtime_error on bad syntax.
std::pair<Version, Version> parseVersionInterval(const std::string& s);

struct PluginInfo {
    std::string folderName, displayName, id, repository, description, author, homepage;
    Version version;
    std::pair<Version, Version> compatible;    // camelion-/npp-compatible-versions
    bool hasCompat = false;
};

struct PluginListData {
    bool ok = false;
    std::string error;
    std::string name, version, arch;
    std::string source;          // file it was read from
    bool signatureOk = false;
    bool testMode = false;       // unsigned JSON from the plugins Config dir
    std::vector<PluginInfo> plugins;
};

// Search order:
//   1. <config>/plugins/Config/nppPluginList.json      (test mode, unsigned - like Notepad++ debug mode)
//   2. <config>/plugins/Config/nppPluginList.so        (must be signed)
//   3. <prefix>/lib/camelion++/plugins/Config/nppPluginList.so
PluginListData loadPluginList();
PluginListData parsePluginListJson(const std::string& json, const std::string& expectedArch);
bool verifyPluginListSignature(const std::string& data, const std::string& signature, std::string* err);
std::string sha256Hex(const std::string& data);

} // namespace cam
