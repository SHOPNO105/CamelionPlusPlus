// Camelion++ - persistent settings (config.xml) and session (session.xml)
#pragma once
#include <string>
#include <vector>
#include <map>

namespace cam {

struct SessionFile {
    std::string path;
    int langType = -1;
    long anchor = 0, caret = 0, firstLine = 0;
    std::string encoding;   // "" = auto
    bool current = false;
    std::string backup;     // snapshot of unsaved content
    std::string untitled;   // "new 3"
    bool dirty = false;
};

class Settings {
public:
    static Settings& get();
    void load();
    void save() const;

    std::string getString(const std::string& k, const std::string& def = "") const;
    int  getInt(const std::string& k, int def = 0) const;
    bool getBool(const std::string& k, bool def = false) const;
    void set(const std::string& k, const std::string& v);
    void set(const std::string& k, int v);
    void set(const std::string& k, bool v);
    void setDefault(const std::string& k, const std::string& v);

    std::vector<std::string>& recentFiles() { return _recent; }
    void addRecent(const std::string& path);

    std::vector<SessionFile> loadSession() const;
    void saveSession(const std::vector<SessionFile>& files) const;

private:
    std::map<std::string, std::string> _kv;
    std::vector<std::string> _recent;
};

} // namespace cam
