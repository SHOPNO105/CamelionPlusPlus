// Camelion++ - minimal, bounds-checked ELF64 reader (no code is ever executed)
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
#include "ElfReader.h"
#include <elf.h>
#include <cstring>

namespace cam {

uint16_t hostElfMachine()
{
#if defined(__x86_64__)
    return EM_X86_64;
#elif defined(__aarch64__)
    return EM_AARCH64;
#elif defined(__riscv)
    return EM_RISCV;
#elif defined(__powerpc64__)
    return EM_PPC64;
#else
    return EM_NONE;
#endif
}

std::string elfMachineName(uint16_t m)
{
    switch (m) {
    case EM_X86_64: return "x86-64";
    case EM_AARCH64: return "arm64";
    case EM_386: return "x86 (32-bit)";
    case EM_ARM: return "arm (32-bit)";
    case EM_RISCV: return "riscv";
    case EM_PPC64: return "ppc64";
    default: return "machine " + std::to_string(m);
    }
}

std::string hostPluginArch()
{
    switch (hostElfMachine()) {
    case EM_X86_64: return "linux-x64";
    case EM_AARCH64: return "linux-arm64";
    case EM_RISCV: return "linux-riscv64";
    case EM_PPC64: return "linux-ppc64le";
    default: return "linux-unknown";
    }
}

template <typename T>
static bool readAt(const std::string& b, uint64_t off, T& out)
{
    if (off > b.size() || sizeof(T) > b.size() - off) return false;
    std::memcpy(&out, b.data() + off, sizeof(T));
    return true;
}

static bool header(const std::string& b, Elf64_Ehdr& eh, std::string* err)
{
    if (b.size() < sizeof(Elf64_Ehdr) || std::memcmp(b.data(), ELFMAG, SELFMAG) != 0) { if (err) *err = "not an ELF file"; return false; }
    if (static_cast<unsigned char>(b[EI_CLASS]) != ELFCLASS64) { if (err) *err = "not a 64-bit ELF file"; return false; }
    if (static_cast<unsigned char>(b[EI_DATA]) != ELFDATA2LSB) { if (err) *err = "not a little-endian ELF file"; return false; }
    readAt(b, 0, eh);
    if (eh.e_shentsize != sizeof(Elf64_Shdr) && eh.e_shnum) { if (err) *err = "unexpected section header size"; return false; }
    return true;
}

ElfInfo elfInfo(const std::string& b)
{
    ElfInfo i;
    Elf64_Ehdr eh;
    if (!header(b, eh, &i.error)) {
        if (b.size() >= 2 && b[0] == 'M' && b[1] == 'Z') i.error = "this is a Windows DLL/EXE, not a Linux shared object";
        return i;
    }
    i.ok = true;
    i.machine = eh.e_machine;
    i.sharedObject = eh.e_type == ET_DYN;
    if (!i.sharedObject) { i.ok = false; i.error = "not a shared object"; }
    return i;
}

static bool sections(const std::string& b, std::vector<Elf64_Shdr>& out, Elf64_Ehdr& eh)
{
    if (!header(b, eh, nullptr)) return false;
    if (eh.e_shnum == 0 || eh.e_shnum > 4096) return false;
    out.resize(eh.e_shnum);
    for (uint16_t i = 0; i < eh.e_shnum; ++i)
        if (!readAt(b, eh.e_shoff + uint64_t(i) * sizeof(Elf64_Shdr), out[i])) return false;
    return eh.e_shstrndx < eh.e_shnum;
}

static bool cstrAt(const std::string& b, const Elf64_Shdr& strtab, uint32_t off, std::string& out)
{
    if (off >= strtab.sh_size || strtab.sh_offset > b.size() || strtab.sh_size > b.size() - strtab.sh_offset) return false;
    const char* base = b.data() + strtab.sh_offset;
    size_t maxLen = size_t(strtab.sh_size - off);
    size_t n = strnlen(base + off, maxLen);
    if (n == maxLen) return false;
    out.assign(base + off, n);
    return true;
}

static bool sectionBytes(const std::string& b, const Elf64_Shdr& s, std::string& out)
{
    if (s.sh_type == SHT_NOBITS) { out.clear(); return true; }
    if (s.sh_offset > b.size() || s.sh_size > b.size() - s.sh_offset) return false;
    out.assign(b.data() + s.sh_offset, size_t(s.sh_size));
    return true;
}

bool elfSection(const std::string& b, const std::string& name, std::string& out)
{
    Elf64_Ehdr eh; std::vector<Elf64_Shdr> sh;
    if (!sections(b, sh, eh)) return false;
    const Elf64_Shdr& strtab = sh[eh.e_shstrndx];
    for (auto& s : sh) {
        std::string n;
        if (cstrAt(b, strtab, s.sh_name, n) && n == name) return sectionBytes(b, s, out);
    }
    return false;
}

template <typename F>
static bool forEachDynSym(const std::string& b, F f)
{
    Elf64_Ehdr eh; std::vector<Elf64_Shdr> sh;
    if (!sections(b, sh, eh)) return false;
    for (auto& s : sh) {
        if (s.sh_type != SHT_DYNSYM || s.sh_entsize != sizeof(Elf64_Sym) || s.sh_link >= sh.size()) continue;
        const Elf64_Shdr& str = sh[s.sh_link];
        uint64_t n = s.sh_size / sizeof(Elf64_Sym);
        for (uint64_t i = 1; i < n && i < 1000000; ++i) {
            Elf64_Sym sym;
            if (!readAt(b, s.sh_offset + i * sizeof(Elf64_Sym), sym)) return false;
            if (sym.st_shndx == SHN_UNDEF) continue;
            unsigned bind = ELF64_ST_BIND(sym.st_info);
            if (bind != STB_GLOBAL && bind != STB_WEAK) continue;
            std::string name;
            if (!cstrAt(b, str, sym.st_name, name)) continue;
            if (f(name, sym, sh)) return true;
        }
        return true;
    }
    return false;
}

bool elfDynamicSymbols(const std::string& b, std::vector<std::string>& out)
{
    return forEachDynSym(b, [&](const std::string& n, const Elf64_Sym&, const std::vector<Elf64_Shdr>&) { out.push_back(n); return false; });
}

bool elfSymbolData(const std::string& b, const std::string& symbol, std::string& out)
{
    bool found = false;
    forEachDynSym(b, [&](const std::string& n, const Elf64_Sym& sym, const std::vector<Elf64_Shdr>& sh) {
        if (n != symbol || ELF64_ST_TYPE(sym.st_info) != STT_OBJECT || sym.st_shndx >= sh.size()) return false;
        const Elf64_Shdr& s = sh[sym.st_shndx];
        if (s.sh_type == SHT_NOBITS || sym.st_value < s.sh_addr) return true;
        uint64_t rel = sym.st_value - s.sh_addr;
        if (rel > s.sh_size || sym.st_size > s.sh_size - rel) return true;
        uint64_t off = s.sh_offset + rel;
        if (off > b.size() || sym.st_size > b.size() - off) return true;
        out.assign(b.data() + off, size_t(sym.st_size));
        while (!out.empty() && out.back() == '\0') out.pop_back();
        found = true;
        return true;
    });
    return found;
}

} // namespace cam
