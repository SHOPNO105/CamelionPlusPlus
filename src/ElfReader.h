// Camelion++ - minimal, bounds-checked ELF64 reader (no code is ever executed)
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace cam {

struct ElfInfo {
    bool ok = false;
    bool sharedObject = false;
    uint16_t machine = 0;
    std::string error;
};

ElfInfo elfInfo(const std::string& bytes);
bool elfSection(const std::string& bytes, const std::string& name, std::string& out);
bool elfDynamicSymbols(const std::string& bytes, std::vector<std::string>& out);
// Contents of an exported data object (e.g. `const char camelionPluginVersion[]`)
bool elfSymbolData(const std::string& bytes, const std::string& symbol, std::string& out);

uint16_t hostElfMachine();
std::string elfMachineName(uint16_t m);
std::string hostPluginArch();   // "linux-x64", "linux-arm64", ...

} // namespace cam
