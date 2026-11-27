// Camelion++ - well-known directories
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
#include "Paths.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <climits>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ftw.h>

namespace cam::paths {

std::string join(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    if (a.back() == '/') return a + b;
    return a + "/" + b;
}

bool fileExists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }
bool dirExists(const std::string& p)  { struct stat st; return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
bool mkdirs(const std::string& p)     { return g_mkdir_with_parents(p.c_str(), 0755) == 0; }

std::string baseName(const std::string& p) { auto i = p.find_last_of('/'); return i == std::string::npos ? p : p.substr(i + 1); }
std::string dirName(const std::string& p)  { auto i = p.find_last_of('/'); return i == std::string::npos ? "." : (i == 0 ? "/" : p.substr(0, i)); }
std::string extension(const std::string& p)
{
    std::string b = baseName(p);
    auto i = b.find_last_of('.');
    if (i == std::string::npos || i == 0) return "";
    std::string e = b.substr(i + 1);
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    return e;
}

std::string exePath()
{
    char buf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "camelion++";
    buf[n] = 0;
    return buf;
}

std::string dataDir()
{
    static std::string cached;
    if (!cached.empty()) return cached;
    std::vector<std::string> candidates;
    if (const char* env = g_getenv("CAMELION_DATA_DIR")) candidates.push_back(env);
    std::string exeDir = dirName(exePath());
    candidates.push_back(join(exeDir, "../share/camelion++"));  // installed layout
    candidates.push_back(join(exeDir, "data"));                 // portable layout
    candidates.push_back(CAMELION_INSTALL_DATADIR);
    candidates.push_back(CAMELION_SOURCE_DATADIR);              // running from the build tree
    for (auto& c : candidates)
        if (fileExists(join(c, "langs.model.xml"))) {
            char* r = realpath(c.c_str(), nullptr);
            cached = r ? r : c;
            free(r);
            return cached;
        }
    cached = CAMELION_INSTALL_DATADIR;
    return cached;
}

std::string configDir()
{
    if (const char* env = g_getenv("CAMELION_CONFIG_DIR")) return env;
    return join(g_get_user_config_dir(), "camelion++");
}
std::string userThemesDir()    { return join(configDir(), "themes"); }
std::string pluginsConfigDir() { return join(configDir(), "plugins/Config"); }
std::string userPluginsDir()
{
    if (const char* env = g_getenv("CAMELION_PLUGINS_DIR")) return env;
    return join(g_get_user_data_dir(), "camelion++/plugins");
}
std::string systemPluginsDir()
{
    std::string exeDir = dirName(exePath());
    for (const char* lib : {CAMELION_RELATIVE_LIBDIR, "lib/camelion++"}) {   // relocatable install
        std::string rel = join(join(exeDir, ".."), std::string(lib) + "/plugins");
        if (dirExists(rel)) {
            char* r = realpath(rel.c_str(), nullptr);
            std::string canon = r ? r : rel;
            free(r);
            return canon;
        }
    }
    std::string portable = join(exeDir, "plugins");
    if (dirExists(portable)) return portable;
    return join(CAMELION_INSTALL_LIBDIR, "plugins");
}
std::vector<std::string> pluginDirs()
{
    std::vector<std::string> v{userPluginsDir()};
    std::string s = systemPluginsDir();
    if (s != v[0]) v.push_back(s);
    return v;
}
std::string backupDir() { return join(g_get_user_data_dir(), "camelion++/backup"); }

std::string readFile(const std::string& p, bool* ok)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) { if (ok) *ok = false; return {}; }
    std::ostringstream ss; ss << f.rdbuf();
    if (ok) *ok = true;
    return ss.str();
}

bool writeFileAtomic(const std::string& p, const std::string& data, std::string* err)
{
    // Write to a temp file in the same directory, fsync, then rename over the target.
    std::string dir = dirName(p);
    std::string tmpl = join(dir, "." + baseName(p) + ".camXXXXXX");
    std::vector<char> t(tmpl.begin(), tmpl.end()); t.push_back(0);
    int fd = ::mkstemp(t.data());
    if (fd < 0) { if (err) *err = std::strerror(errno); return false; }
    struct stat st;
    bool hadMode = ::stat(p.c_str(), &st) == 0;
    size_t off = 0;
    while (off < data.size()) {
        ssize_t w = ::write(fd, data.data() + off, data.size() - off);
        if (w < 0) { if (errno == EINTR) continue; if (err) *err = std::strerror(errno); ::close(fd); ::unlink(t.data()); return false; }
        off += size_t(w);
    }
    ::fsync(fd);
    if (hadMode) ::fchmod(fd, st.st_mode & 07777); else ::fchmod(fd, 0644);
    ::close(fd);
    if (::rename(t.data(), p.c_str()) != 0) {
        // e.g. no permission to create files in the directory: fall back to in-place write
        ::unlink(t.data());
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        if (!f) { if (err) *err = std::strerror(errno); return false; }
        f.write(data.data(), std::streamsize(data.size()));
        if (!f) { if (err) *err = "write failed"; return false; }
    }
    return true;
}

static int rmCb(const char* p, const struct stat*, int, struct FTW*) { return ::remove(p); }
bool removeTree(const std::string& p) { return ::nftw(p.c_str(), rmCb, 16, FTW_DEPTH | FTW_PHYS) == 0; }

} // namespace cam::paths
