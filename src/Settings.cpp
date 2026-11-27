// Camelion++ - persistent settings (config.xml) and session (session.xml)
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
#include "Settings.h"
#include "Paths.h"
#include <tinyxml2.h>
#include <algorithm>

using namespace tinyxml2;

namespace cam {

Settings& Settings::get() { static Settings s; return s; }

static std::string configPath()  { return paths::join(paths::configDir(), "config.xml"); }
static std::string sessionPath() { return paths::join(paths::configDir(), "session.xml"); }

void Settings::load()
{
    XMLDocument doc;
    if (doc.LoadFile(configPath().c_str()) != XML_SUCCESS) return;
    XMLElement* root = doc.FirstChildElement("CamelionPlusPlus");
    if (!root) return;
    if (XMLElement* gui = root->FirstChildElement("GUIConfigs"))
        for (XMLElement* e = gui->FirstChildElement("GUIConfig"); e; e = e->NextSiblingElement("GUIConfig"))
            if (const char* n = e->Attribute("name"))
                _kv[n] = e->Attribute("value") ? e->Attribute("value") : "";
    if (XMLElement* h = root->FirstChildElement("History"))
        for (XMLElement* e = h->FirstChildElement("File"); e; e = e->NextSiblingElement("File"))
            if (const char* f = e->Attribute("filename")) _recent.push_back(f);
}

void Settings::save() const
{
    paths::mkdirs(paths::configDir());
    XMLDocument doc;
    doc.InsertEndChild(doc.NewDeclaration());
    XMLElement* root = doc.NewElement("CamelionPlusPlus");
    doc.InsertEndChild(root);
    XMLElement* gui = doc.NewElement("GUIConfigs");
    root->InsertEndChild(gui);
    for (auto& [k, v] : _kv) {
        XMLElement* e = doc.NewElement("GUIConfig");
        e->SetAttribute("name", k.c_str());
        e->SetAttribute("value", v.c_str());
        gui->InsertEndChild(e);
    }
    XMLElement* h = doc.NewElement("History");
    root->InsertEndChild(h);
    for (auto& f : _recent) {
        XMLElement* e = doc.NewElement("File");
        e->SetAttribute("filename", f.c_str());
        h->InsertEndChild(e);
    }
    XMLPrinter pr;
    doc.Print(&pr);
    paths::writeFileAtomic(configPath(), pr.CStr());
}

std::string Settings::getString(const std::string& k, const std::string& def) const
{
    auto it = _kv.find(k);
    return it == _kv.end() ? def : it->second;
}
int Settings::getInt(const std::string& k, int def) const
{
    auto it = _kv.find(k);
    if (it == _kv.end() || it->second.empty()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
}
bool Settings::getBool(const std::string& k, bool def) const
{
    auto it = _kv.find(k);
    if (it == _kv.end()) return def;
    return it->second == "yes" || it->second == "1" || it->second == "true";
}
void Settings::set(const std::string& k, const std::string& v) { _kv[k] = v; }
void Settings::set(const std::string& k, int v) { _kv[k] = std::to_string(v); }
void Settings::set(const std::string& k, bool v) { _kv[k] = v ? "yes" : "no"; }
void Settings::setDefault(const std::string& k, const std::string& v) { if (!_kv.count(k)) _kv[k] = v; }

void Settings::addRecent(const std::string& path)
{
    _recent.erase(std::remove(_recent.begin(), _recent.end(), path), _recent.end());
    _recent.insert(_recent.begin(), path);
    size_t max = size_t(std::max(0, getInt("recent.max", 15)));
    if (_recent.size() > max) _recent.resize(max);
}

std::vector<SessionFile> Settings::loadSession() const
{
    std::vector<SessionFile> v;
    XMLDocument doc;
    if (doc.LoadFile(sessionPath().c_str()) != XML_SUCCESS) return v;
    XMLElement* root = doc.FirstChildElement("CamelionPlusPlus");
    XMLElement* s = root ? root->FirstChildElement("Session") : nullptr;
    if (!s) return v;
    for (XMLElement* e = s->FirstChildElement("File"); e; e = e->NextSiblingElement("File")) {
        SessionFile f;
        if (const char* p = e->Attribute("filename")) f.path = p; else continue;
        f.langType = e->IntAttribute("lang", -1);
        f.anchor = e->Int64Attribute("startPos", 0);
        f.caret = e->Int64Attribute("endPos", 0);
        f.firstLine = e->Int64Attribute("firstVisibleLine", 0);
        if (const char* enc = e->Attribute("encoding")) f.encoding = enc;
        f.current = e->BoolAttribute("current", false);
        if (const char* bk = e->Attribute("backupFilePath")) f.backup = bk;
        if (const char* un = e->Attribute("untitledName")) f.untitled = un;
        f.dirty = e->BoolAttribute("dirty", false);
        if (f.path.empty() && f.backup.empty()) continue;
        v.push_back(f);
    }
    return v;
}

void Settings::saveSession(const std::vector<SessionFile>& files) const
{
    paths::mkdirs(paths::configDir());
    XMLDocument doc;
    doc.InsertEndChild(doc.NewDeclaration());
    XMLElement* root = doc.NewElement("CamelionPlusPlus");
    doc.InsertEndChild(root);
    XMLElement* s = doc.NewElement("Session");
    root->InsertEndChild(s);
    for (auto& f : files) {
        XMLElement* e = doc.NewElement("File");
        e->SetAttribute("filename", f.path.c_str());
        e->SetAttribute("lang", f.langType);
        e->SetAttribute("startPos", int64_t(f.anchor));
        e->SetAttribute("endPos", int64_t(f.caret));
        e->SetAttribute("firstVisibleLine", int64_t(f.firstLine));
        if (!f.encoding.empty()) e->SetAttribute("encoding", f.encoding.c_str());
        if (f.current) e->SetAttribute("current", true);
        if (!f.backup.empty()) e->SetAttribute("backupFilePath", f.backup.c_str());
        if (!f.untitled.empty()) e->SetAttribute("untitledName", f.untitled.c_str());
        if (f.dirty) e->SetAttribute("dirty", true);
        s->InsertEndChild(e);
    }
    XMLPrinter pr;
    doc.Print(&pr);
    paths::writeFileAtomic(sessionPath(), pr.CStr());
}

} // namespace cam
