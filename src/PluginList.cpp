// Camelion++ - nppPluginList support: loads and verifies the plugin list
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
#include "PluginList.h"
#include "ElfReader.h"
#include "Paths.h"
#include "PluginListKey.h"
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <regex>
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <cstring>

namespace cam {

// ------------------------------------------------------------------ Version
Version Version::parse(const std::string& s)
{
    Version r;
    if (s.empty()) return r;
    std::istringstream ss(s);
    std::string part;
    int i = 0;
    while (std::getline(ss, part, '.') && i < 4) {
        if (part.empty() || part.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("invalid version \"" + s + "\"");
        r.v[size_t(i++)] = std::stoi(part);
    }
    r.set = i > 0;
    return r;
}

std::string Version::toString() const
{
    if (!set) return "";
    int last = 3;
    while (last > 2 && v[size_t(last)] == 0) --last;   // at least major.minor.patch
    std::string s;
    for (int i = 0; i <= last; ++i) { if (i) s += '.'; s += std::to_string(v[size_t(i)]); }
    return s;
}

int Version::compare(const Version& o) const
{
    for (size_t i = 0; i < 4; ++i) if (v[i] != o.v[i]) return v[i] < o.v[i] ? -1 : 1;
    return 0;
}

bool Version::isCompatibleTo(const Version& from, const Version& to) const
{
    if (empty()) return false;
    if (from.empty() && to.empty()) return true;
    if (from.empty()) return compare(to) <= 0;
    if (to.empty()) return compare(from) >= 0;
    return compare(from) >= 0 && compare(to) <= 0;
}

std::pair<Version, Version> parseVersionInterval(const std::string& in)
{
    std::string s = in;
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']') s = s.substr(1, s.size() - 2);
    auto comma = s.find(',');
    if (comma == std::string::npos) { Version v = Version::parse(s); return {v, v}; }   // "6.9" => exactly 6.9
    return {Version::parse(s.substr(0, comma)), Version::parse(s.substr(comma + 1))};
}

// ------------------------------------------------------------------ crypto
std::string sha256Hex(const std::string& data)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int n = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, md, &n);
    EVP_MD_CTX_free(ctx);
    static const char* hex = "0123456789abcdef";
    std::string r;
    for (unsigned i = 0; i < n; ++i) { r += hex[md[i] >> 4]; r += hex[md[i] & 15]; }
    return r;
}

bool verifyPluginListSignature(const std::string& data, const std::string& sig, std::string* err)
{
    BIO* bio = BIO_new_mem_buf(CAMELION_PLUGINLIST_PUBKEY_PEM, -1);
    EVP_PKEY* key = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!key) { if (err) *err = "the embedded public key is invalid"; return false; }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1 &&
              EVP_DigestVerify(ctx, reinterpret_cast<const unsigned char*>(sig.data()), sig.size(),
                               reinterpret_cast<const unsigned char*>(data.data()), data.size()) == 1;
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    if (!ok && err) *err = "the signature does not match";
    return ok;
}

// ------------------------------------------------------------------ JSON
PluginListData parsePluginListJson(const std::string& json, const std::string& expectedArch)
{
    PluginListData d;
    nlohmann::json j;
    try { j = nlohmann::json::parse(json); }
    catch (const std::exception& e) { d.error = std::string("invalid JSON: ") + e.what(); return d; }
    try {
        d.name = j.at("name").get<std::string>();
        d.version = j.at("version").get<std::string>();
        d.arch = j.at("arch").get<std::string>();
    } catch (const std::exception& e) { d.error = std::string("missing list header: ") + e.what(); return d; }
    if (d.name != "npp-pluginList" && d.name != "camelion-pluginList") { d.error = "unknown list name \"" + d.name + "\""; return d; }
    if (d.arch != expectedArch) {
        if (d.arch == "32" || d.arch == "64" || d.arch == "arm64")
            d.error = "this is a Notepad++ for Windows plugin list (arch \"" + d.arch + "\"). Windows plugins cannot run on Linux; "
                      "a \"" + expectedArch + "\" list is needed.";
        else d.error = "plugin list architecture \"" + d.arch + "\" does not match this Camelion++ (" + expectedArch + ")";
        return d;
    }
    const auto& arr = j.contains("camelion-plugins") ? j["camelion-plugins"] : j.value("npp-plugins", nlohmann::json::array());
    for (const auto& p : arr) {
        try {
            PluginInfo pi;
            pi.folderName = p.at("folder-name").get<std::string>();
            pi.displayName = p.at("display-name").get<std::string>();
            pi.version = Version::parse(p.at("version").get<std::string>());
            pi.id = p.at("id").get<std::string>();
            pi.repository = p.at("repository").get<std::string>();
            pi.description = p.at("description").get<std::string>();
            pi.author = p.at("author").get<std::string>();
            pi.homepage = p.at("homepage").get<std::string>();
            const char* compatKey = p.contains("camelion-compatible-versions") ? "camelion-compatible-versions" : "npp-compatible-versions";
            if (p.contains(compatKey)) { pi.compatible = parseVersionInterval(p[compatKey].get<std::string>()); pi.hasCompat = true; }
            if (pi.folderName.empty() || pi.folderName.find('/') != std::string::npos || pi.folderName.find("..") != std::string::npos) continue;
            if (!std::regex_match(pi.id, std::regex("^[0-9A-Fa-f]{64}$"))) continue;
            d.plugins.push_back(std::move(pi));
        } catch (...) {
            continue;   // like Notepad++: a malformed entry is skipped
        }
    }
    d.ok = true;
    return d;
}

PluginListData loadPluginList()
{
    const std::string arch = hostPluginArch();
    std::string testJson = paths::join(paths::pluginsConfigDir(), "nppPluginList.json");
    if (paths::fileExists(testJson)) {
        PluginListData d = parsePluginListJson(paths::readFile(testJson), arch);
        d.source = testJson;
        d.testMode = true;
        return d;
    }
    std::vector<std::string> candidates = {
        paths::join(paths::pluginsConfigDir(), "nppPluginList.so"),
        paths::join(paths::systemPluginsDir(), "Config/nppPluginList.so"),
    };
    std::string lastError = "nppPluginList.so was not found (looked in " + candidates[0] + " and " + candidates[1] + ")";
    for (auto& c : candidates) {
        if (!paths::fileExists(c)) continue;
        std::string bytes = paths::readFile(c);
        // The list is read as data from ELF sections; the library is never dlopen()ed.
        ElfInfo info = elfInfo(bytes);
        if (!info.ok) { lastError = c + ": " + info.error; continue; }
        std::string json, sig;
        if (!elfSection(bytes, ".npp_pluginlist", json)) { lastError = c + ": no .npp_pluginlist section"; continue; }
        while (!json.empty() && json.back() == '\0') json.pop_back();
        if (!elfSection(bytes, ".npp_pluginlist_sig", sig) || sig.size() != 64) {
            lastError = c + ": the plugin list is not signed";
            continue;
        }
        std::string err;
        if (!verifyPluginListSignature(json, sig, &err)) { lastError = c + ": " + err + " - the plugin list may have been tampered with"; continue; }
        PluginListData d = parsePluginListJson(json, arch);
        d.source = c;
        d.signatureOk = true;
        return d;
    }
    PluginListData d;
    d.error = lastError;
    return d;
}

} // namespace cam
