#include "../include/SDKDumper.hpp"

#include <Windows.h>
#include <shlobj.h>
#include <psapi.h>
#include <dbghelp.h>

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <io.h>
#include <fcntl.h>
// MinHook removed: no runtime hooking included in SDKDumperProject

namespace fs = std::filesystem;

namespace SDKDumper {

// Console logging helpers ----------------------------------------------------
static HANDLE g_hConsole = nullptr;
static void InitConsole() {
    if (g_hConsole) return;
    if (!AllocConsole()) return;
    FILE *fDummy;
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONIN$", "r", stdin);
    SetConsoleTitleW(L"SDKDumper Debug Console");
    SetConsoleOutputCP(CP_UTF8);
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
}

static void CloseConsole() {
    if (!g_hConsole) return;
    FreeConsole();
    g_hConsole = nullptr;
}

static void ConsoleSetColor(WORD attr) {
    if (g_hConsole) SetConsoleTextAttribute(g_hConsole, attr);
}

static void ConsolePrintf(WORD color, const char *fmt, ...) {
    if (!g_hConsole) InitConsole();
    ConsoleSetColor(color);
    va_list ap; va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    ConsoleSetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    fflush(stdout);
}

static void PrintProgressBar(int percent, const std::string &label) {
    if (!g_hConsole) InitConsole();
    const int width = 40;
    int filled = (percent * width) / 100;
    std::string bar;
    bar.reserve(width+10);
    bar += '[';
    for (int i=0;i<filled;i++) bar += '=';
    if (filled < width) bar += '>';
    for (int i=filled+1;i<width;i++) bar += ' ';
    bar += ']';

    // Color: green for main bar, cyan for label
    ConsoleSetColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    printf("\r%s %3d%% ", bar.c_str(), percent);
    ConsoleSetColor(FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    printf("%s", label.c_str());
    fflush(stdout);
    ConsoleSetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

// End console helpers --------------------------------------------------------


static std::string Utf8FromWide(const std::wstring &w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), size, nullptr, nullptr);
    return s;
}

static std::wstring GetLocalAppDataPath() {
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &path))) {
        std::wstring s(path);
        CoTaskMemFree(path);
        return s;
    }
    return L"";
}

// Check whether a memory range is readable in this process (covers multi-page ranges)
static bool IsReadableRange(const void *addr, size_t size) {
    if (!addr || size == 0) return false;
    const uint8_t *p = (const uint8_t*)addr;
    size_t remaining = size;
    while (remaining) {
        MEMORY_BASIC_INFORMATION mbi;
        SIZE_T ret = VirtualQuery(p, &mbi, sizeof(mbi));
        if (ret == 0) return false;
        if (mbi.State != MEM_COMMIT) return false;
        DWORD prot = mbi.Protect;
        if (prot & PAGE_GUARD) return false;
        if (prot & PAGE_NOACCESS) return false;
        const DWORD readableFlags = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if ((prot & readableFlags) == 0) return false;
        uintptr_t regionEnd = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        uintptr_t cur = (uintptr_t)p;
        size_t avail = (size_t)(regionEnd - cur);
        size_t chunk = avail < remaining ? avail : remaining;
        p += chunk;
        remaining -= chunk;
    }
    return true;
}

// forward declare HexPrefix since it's used before its definition
static std::string HexPrefix(uintptr_t addr, size_t maxBytes=32);

static bool EnsureDirectory(const std::wstring &dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    return !ec;
}

static std::string Hex64(uint64_t v) {
    std::ostringstream ss;
    ss << "0x" << std::hex << v;
    return ss.str();
}

struct ExportEntry { std::string name; uintptr_t address; uint32_t rva; uint32_t ordinal; };
struct RTTIEntry { std::string name; std::string demangled; uintptr_t address; };
struct VTableEntry { uintptr_t address; uintptr_t col; std::string typeName; std::string demangled; std::vector<std::pair<uintptr_t,std::string>> methods; };
struct ModuleDump { std::string name; std::wstring path; uintptr_t base; uint32_t size; std::vector<ExportEntry> exports; std::vector<RTTIEntry> rttis; std::vector<VTableEntry> vtables; };

// Parse exports safely using PE headers
static ModuleDump ParseModule(HMODULE hMod) {
    ModuleDump m;
    WCHAR wpath[MAX_PATH] = {0};
    if (GetModuleFileNameW(hMod, wpath, MAX_PATH)) m.path = wpath;
    CHAR aname[MAX_PATH] = {0};
    if (GetModuleFileNameA(hMod, aname, MAX_PATH)) m.name = aname;

    MODULEINFO mi = {};
    if (GetModuleInformation(GetCurrentProcess(), hMod, &mi, sizeof(mi))) {
        m.base = (uintptr_t)mi.lpBaseOfDll;
        m.size = (uint32_t)mi.SizeOfImage;
    }

    BYTE *base = (BYTE*)hMod;
    // Safely validate PE headers and exports using readable-range checks
    if (!IsReadableRange(base, sizeof(IMAGE_DOS_HEADER))) return m;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return m;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (!IsReadableRange(nt, sizeof(IMAGE_NT_HEADERS))) return m;
    if (nt->Signature != IMAGE_NT_SIGNATURE) return m;
    auto &opt = nt->OptionalHeader;
    auto expDir = opt.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (expDir.VirtualAddress == 0) return m;
    IMAGE_EXPORT_DIRECTORY *exp = (IMAGE_EXPORT_DIRECTORY*)(base + expDir.VirtualAddress);
    if (!IsReadableRange(exp, sizeof(IMAGE_EXPORT_DIRECTORY))) return m;
    // Validate name/address tables
    DWORD *names = (DWORD*)(base + exp->AddressOfNames);
    WORD *ords = (WORD*)(base + exp->AddressOfNameOrdinals);
    DWORD *funcs = (DWORD*)(base + exp->AddressOfFunctions);
    if (!IsReadableRange(names, sizeof(DWORD) * exp->NumberOfNames)) return m;
    if (!IsReadableRange(ords, sizeof(WORD) * exp->NumberOfNames)) return m;
    if (!IsReadableRange(funcs, sizeof(DWORD) * exp->NumberOfFunctions)) return m;
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const char *ename = (const char*)(base + names[i]);
        if (!IsReadableRange(ename, 1)) continue;
        // ensure string is readable up to some reasonable length
        size_t maxLen = 256;
        size_t len = 0;
        while (len < maxLen && IsReadableRange(ename + len, 1) && ename[len]) ++len;
        std::string nameStr;
        if (len > 0) nameStr.assign(ename, ename + len);
        WORD ord = ords[i];
        DWORD fnrva = funcs[ord];
        ExportEntry e;
        e.name = nameStr;
        e.rva = fnrva;
        e.address = m.base + fnrva;
        e.ordinal = (uint32_t)(exp->Base + ord);
        m.exports.push_back(e);
    }

    return m;
}

// Demangle MSVC decorated names using UnDecorateSymbolName
static std::string DemangleMSVC(const std::string &mangled) {
    if (mangled.empty()) return {};
    char buf[1024] = {0};
    if (UnDecorateSymbolName(mangled.c_str(), buf, (DWORD)sizeof(buf), UNDNAME_COMPLETE)) {
        return std::string(buf);
    }
    return mangled;
}

static bool InRange(uintptr_t base, uint32_t size, uintptr_t addr) {
    return addr >= base && addr < base + size;
}

// Find code section range inside module
static bool GetCodeRange(BYTE *base, size_t moduleSize, uintptr_t &codeStart, uintptr_t &codeEnd) {
    // Validate headers and section table with readable checks
    if (!IsReadableRange(base, sizeof(IMAGE_DOS_HEADER))) return false;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (!IsReadableRange(nt, sizeof(IMAGE_NT_HEADERS))) return false;
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    if (!IsReadableRange(sec, sizeof(IMAGE_SECTION_HEADER) * nt->FileHeader.NumberOfSections)) return false;
    for (unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
        auto &s = sec[i];
        if (strncmp((char*)s.Name, ".text", 5) == 0 || (s.Characteristics & IMAGE_SCN_CNT_CODE)) {
            codeStart = (uintptr_t)base + s.VirtualAddress;
            codeEnd = codeStart + s.Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

// Heuristic vtable scan and CompleteObjectLocator parsing
static void ScanForVTables(ModuleDump &m, HMODULE hMod) {
    BYTE *base = (BYTE*)hMod;
    if (!base || m.size < sizeof(uintptr_t)*4) return;
    uintptr_t codeStart=0, codeEnd=0;
    if (!GetCodeRange(base, m.size, codeStart, codeEnd)) {
        // Fallback: assume module range
        codeStart = m.base;
        codeEnd = m.base + m.size;
    }

    size_t stride = sizeof(uintptr_t);
    std::vector<uintptr_t> foundVTables;

    for (size_t off = 0; off + stride*3 < m.size; off += stride) {
        uintptr_t cand = (uintptr_t)(base + off);
        uintptr_t p0=0,p1=0,p2=0;
        if (!IsReadableRange((void*)cand, stride * 3)) continue;
        p0 = *(uintptr_t*)(cand);
        p1 = *(uintptr_t*)(cand + stride);
        p2 = *(uintptr_t*)(cand + stride*2);
        if (InRange(m.base, m.size, p0) && p0 >= codeStart && p0 < codeEnd
            && InRange(m.base, m.size, p1) && p1 >= codeStart && p1 < codeEnd
            && InRange(m.base, m.size, p2) && p2 >= codeStart && p2 < codeEnd) {
            // probable vtable
            uintptr_t vtAddr = m.base + off;
            if (!foundVTables.empty() && std::find(foundVTables.begin(), foundVTables.end(), vtAddr) != foundVTables.end()) continue;
            foundVTables.push_back(vtAddr);

            uintptr_t colPtr = 0;
            if (!IsReadableRange((void*)(vtAddr - stride), stride)) continue;
            colPtr = *((uintptr_t*)vtAddr - 1);
            if (!InRange(m.base, m.size, colPtr)) continue;

            uintptr_t pType = 0;
            if (IsReadableRange((void*)(colPtr + 12), stride)) pType = *(uintptr_t*)(colPtr + 12);
            if (!InRange(m.base, m.size, pType)) {
                if (IsReadableRange((void*)(colPtr + 16), stride)) pType = *(uintptr_t*)(colPtr + 16);
            }
            if (!InRange(m.base, m.size, pType)) continue;

            uintptr_t nameAddr = pType + 8;
            std::string name;
            if (IsReadableRange((void*)nameAddr, 3)) {
                const char *s = (const char*)nameAddr;
                size_t L = 0;
                while (L < 256 && IsReadableRange(s + L, 1) && s[L] && isprint((unsigned char)s[L])) ++L;
                if (L > 2) name.assign(s, s+L);
            }

            if (name.empty()) continue;

            VTableEntry v;
            v.address = vtAddr;
            v.col = colPtr;
            v.typeName = name;
            v.demangled = DemangleMSVC(name);

            // Attempt to parse ClassHierarchyDescriptor and enumerate methods from vtable
            // Read ClassHierarchyDescriptor pointer from COL
            uintptr_t phd = 0;
            SIZE_T pointerSize = sizeof(void*);
            bool is64 = (pointerSize == 8);
            // Offsets in CompleteObjectLocator vary by platform; on x64, fields after three DWORDs are pointers
            if (is64) {
                if (IsReadableRange((void*)(colPtr + 16), stride)) {
                    phd = *(uintptr_t*)(colPtr + 16);
                }
            } else {
                if (IsReadableRange((void*)(colPtr + 16), sizeof(uint32_t))) {
                    phd = *(uintptr_t*)(colPtr + 16);
                }
            }

            if (InRange(m.base, m.size, phd)) {
                // parse ClassHierarchyDescriptor: DWORD signature, DWORD attributes, DWORD numBaseClasses, pBaseClassArray (pointer)
                uintptr_t numBase = 0;
                uintptr_t baseArray = 0;
                if (IsReadableRange((void*)phd, 24)) {
                    if (is64) {
                        numBase = *(uint32_t*)(phd + 8);
                        baseArray = *(uintptr_t*)(phd + 16);
                    } else {
                        numBase = *(uint32_t*)(phd + 8);
                        baseArray = *(uintptr_t*)(phd + 12);
                    }
                } else { numBase = 0; baseArray = 0; }

                if (InRange(m.base, m.size, baseArray) && numBase > 0 && numBase < 1024) {
                    for (uintptr_t bi = 0; bi < numBase; ++bi) {
                        uintptr_t bdesc = 0;
                        if (IsReadableRange((void*)(baseArray + bi * pointerSize), pointerSize)) bdesc = *(uintptr_t*)(baseArray + bi * pointerSize);
                        if (!InRange(m.base, m.size, bdesc)) continue;
                        // BaseClassDescriptor contains pTypeDescriptor at offset 0
                        uintptr_t pTypeDesc = 0;
                        if (IsReadableRange((void*)(bdesc + 0), pointerSize)) pTypeDesc = *(uintptr_t*)(bdesc + 0);
                        if (!InRange(m.base, m.size, pTypeDesc)) continue;
                        // read name at pTypeDesc + 8 (typical) or +16 for some
                        uintptr_t nameAddr = pTypeDesc + 8;
                        std::string bname;
                        if (IsReadableRange((void*)nameAddr, 1)) {
                            const char *s = (const char*)nameAddr;
                            size_t L = 0; while (L < 256 && IsReadableRange(s + L, 1) && s[L] && isprint((unsigned char)s[L])) ++L;
                            if (L>0) bname.assign(s, s+L);
                        }
                        // For each vtable entry method, attempt to resolve symbol name
                    }
                }

            }

            // Resolve methods from vtable entries using dbghelp
            HANDLE proc = GetCurrentProcess();
            SymSetOptions(SymGetOptions() | SYMOPT_UNDNAME);
            if (!SymInitialize(proc, nullptr, TRUE)) {
                // can't initialize symbols; still push vtable without methods
            } else {
                // iterate first 64 entries or until non-code
                for (size_t mi = 0; mi < 256; ++mi) {
                    uintptr_t maddr = 0;
                    if (!IsReadableRange((void*)(v.address + mi * stride), stride)) break;
                    maddr = *(uintptr_t*)(v.address + mi * stride);
                    if (!maddr) break;
                    // validate maddr within process
                    // Try to get symbol
                    DWORD64 disp = 0;
                    char symbuf[sizeof(SYMBOL_INFO) + 256] = {0};
                    PSYMBOL_INFO pSym = (PSYMBOL_INFO)symbuf;
                    pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
                    pSym->MaxNameLen = 255;
                    if (SymFromAddr(proc, (DWORD64)maddr, &disp, pSym)) {
                        std::string sname(pSym->Name);
                        std::string dem = DemangleMSVC(sname);
                        v.methods.emplace_back((uintptr_t)maddr, dem);
                    } else {
                        // fallback: just record address and hex prefix
                        std::string prefix = HexPrefix(maddr, 8);
                        v.methods.emplace_back((uintptr_t)maddr, prefix);
                    }
                }
                SymCleanup(proc);
            }

            m.vtables.push_back(v);
        }
    }

    std::sort(m.vtables.begin(), m.vtables.end(), [](auto &a, auto &b){ return a.address < b.address; });
    m.vtables.erase(std::unique(m.vtables.begin(), m.vtables.end(), [](auto &a, auto &b){ return a.address==b.address; }), m.vtables.end());
}

// Heuristic scan for RTTI names inside module's memory (.rdata) by searching for typical MSVC mangled type names
static void ExtractRTTI(ModuleDump &m, HMODULE hMod) {
    BYTE *base = (BYTE*)hMod;
    size_t size = m.size;
    if (!base || size < 4) return;

    auto plausible = [](const char *s)->bool {
        if (!s) return false;
        size_t len = strlen(s);
        if (len < 3 || len > 240) return false;
        // Accept ASCII printable with some common RTTI chars
        for (size_t i=0;i<len;i++) {
            unsigned char c = s[i];
            if (c < 0x20) return false;
        }
        // Must contain typical MSVC mangling like ".?A" or "class " or "struct "
        if (strstr(s, ".?A") || strstr(s, ".?AV") || strstr(s, "class ") || strstr(s, "struct ")) return true;
        return false;
    };

    // Scan for zero-terminated ASCII strings and filter by heuristics.
    const char *data = (const char*)base;
    size_t i = 0;
    while (i + 8 < size) {
        // find printable run
        if (isprint((unsigned char)data[i]) || data[i] == '.' || data[i] == '?') {
            size_t j = i;
            while (j < size && (isprint((unsigned char)data[j]) || data[j] == '.' || data[j] == '?' || data[j] == '@' || data[j]=='_' || data[j]=='$')) j++;
            if (j < size && data[j] == '\0') {
                size_t len = j - i;
                if (len >= 3 && len <= 240) {
                    const char *s = &data[i];
                    if (plausible(s)) {
                        RTTIEntry r;
                        r.name = s;
                        r.demangled = DemangleMSVC(r.name);
                        r.address = m.base + (uintptr_t)i;
                        m.rttis.push_back(r);
                        // advance
                        i = j + 1;
                        continue;
                    }
                }
            }
            i = j + 1;
        } else {
            ++i;
        }
    }

    // De-dup by name
    std::sort(m.rttis.begin(), m.rttis.end(), [](auto &a, auto &b){ return a.name < b.name; });
    m.rttis.erase(std::unique(m.rttis.begin(), m.rttis.end(), [](auto &a, auto &b){ return a.name==b.name; }), m.rttis.end());
}

// Build a small hex prefix for a function, safe memory access
static std::string HexPrefix(uintptr_t addr, size_t maxBytes/*=32*/) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    const unsigned char *p = (const unsigned char*)addr;
    for (size_t i=0;i<maxBytes;i++) {
        if (!IsReadableRange(p + i, 1)) break;
        unsigned char b = *(p + i);
        ss << std::setw(2) << (int)b;
    }
    return ss.str();
}

static void WriteOutputs(const std::wstring &outdir, const std::vector<ModuleDump> &mods) {
    std::string outdiru = Utf8FromWide(outdir);
    fs::create_directories(outdiru);

    // JSON dump
    std::ofstream jf(outdiru + "/sdk_dump.json");
    jf << "{\n  \"modules\": [\n";
    bool fm = true;
    for (const auto &m : mods) {
        if (!fm) jf << ",\n"; fm = false;
        jf << "    {\n";
        jf << "      \"name\": \"" << m.name << "\",\n";
        jf << "      \"base\": \"" << Hex64(m.base) << "\",\n";
        jf << "      \"size\": " << m.size << ",\n";
        jf << "      \"exports\": [\n";
        bool fe=true;
        for (const auto &e : m.exports) {
            if (!fe) jf << ",\n"; fe=false;
            jf << "        { \"name\": \"" << e.name << "\", \"address\": \"" << Hex64(e.address) << "\" }";
        }
        jf << "\n      ],\n";
        jf << "      \"vtables\": [\n";
        bool fv = true;
        for (const auto &v : m.vtables) {
            if (!fv) jf << ",\n"; fv = false;
            jf << "        { \"address\": \"" << Hex64(v.address) << "\", \"col\": \"" << Hex64(v.col) << "\", \"type\": \"" << v.typeName << "\", \"demangled\": \"" << v.demangled << "\" }";
        }
        jf << "\n      ],\n";
        jf << "      \"rtti\": [\n";
        bool fr=true;
        for (const auto &r : m.rttis) {
            if (!fr) jf << ",\n"; fr=false;
            jf << "        { \"type\": \"" << r.name << "\", \"demangled\": \"" << r.demangled << "\", \"address\": \"" << Hex64(r.address) << "\" }";
        }
        jf << "\n      ]\n";
        jf << "    }";
    }
    jf << "\n  ]\n}\n";
    jf.close();

    // signatures file: for each export, dump a placeholder signature using the function name and a hex prefix
    std::ofstream sf(outdiru + "/signatures.txt");
    for (const auto &m : mods) {
        for (const auto &e : m.exports) {
            std::string signame = "sig_" + e.name;
            std::string hexpref = HexPrefix(e.address, 16);
            if (hexpref.empty()) hexpref = "<unreadable>";
            sf << signame << " = " << Hex64(e.address) << " ; prefix=" << hexpref << " ; func=" << e.name << "\n";
        }
    }
    sf.close();

    // RTTI list
    std::ofstream rf(outdiru + "/rtti_types.txt");
    for (const auto &m : mods) {
        for (const auto &r : m.rttis) {
            rf << r.name << " = " << Hex64(r.address) << "\n";
        }
    }
    rf.close();

    // IDA IDC script
    std::ofstream idc(outdiru + "/auto_names.idc");
    if (idc.is_open()) {
        idc << "// Auto-generated names for IDA\n";
        idc << "static main() {\n";
        for (const auto &m : mods) {
            for (const auto &e : m.exports) {
                idc << "  MakeNameEx(" << Hex64(e.address) << ", \"" << e.name << "\", 0);\n";
            }
            for (const auto &v : m.vtables) {
                std::string vn = "vtable_" + v.typeName;
                for (auto &c : vn) if (!isalnum((unsigned char)c)) c = '_';
                idc << "  MakeNameEx(" << Hex64(v.address) << ", \"" << vn << "\", 0);\n";
            }
        }
        idc << "}\n";
        idc.close();
    }

    // Ghidra simple map (address TAB name)
    std::ofstream gh(outdiru + "/ghidra_names.txt");
    if (gh.is_open()) {
        for (const auto &m : mods) {
            for (const auto &e : m.exports) gh << Hex64(e.address) << "\t" << e.name << "\n";
            for (const auto &v : m.vtables) gh << Hex64(v.address) << "\t" << "vtable_" << v.typeName << "\n";
        }
        gh.close();
    }
}

static void DumpAll() {
    std::wstring local = GetLocalAppDataPath();
    if (local.empty()) return;
    std::wstring outdir = local + L"\\SDK_DUMP";
    if (!EnsureDirectory(outdir)) return;

    // Initialize console for progress feedback
    InitConsole();
    ConsolePrintf(FOREGROUND_GREEN | FOREGROUND_INTENSITY, "SDKDumper: starting dump to %s\n", Utf8FromWide(outdir).c_str());

    HMODULE hMods[1024];
    DWORD cbNeeded = 0;
    std::vector<ModuleDump> dumps;
    if (EnumProcessModules(GetCurrentProcess(), hMods, sizeof(hMods), &cbNeeded)) {
        size_t count = cbNeeded / sizeof(HMODULE);
        if (count == 0) {
            ConsolePrintf(FOREGROUND_RED | FOREGROUND_INTENSITY, "No modules found.\n");
        }
        for (size_t i=0;i<count;i++) {
            HMODULE h = hMods[i];
            ModuleDump m = ParseModule(h);
            ExtractRTTI(m, h);
            ScanForVTables(m, h);
            dumps.push_back(std::move(m));

            int pct = (int)(((i+1) * 100) / (count ? count : 1));
            std::string label = m.name.empty() ? "<unknown>" : m.name;
            PrintProgressBar(pct, label);
        }
        // final newline after progress
        printf("\n");
        ConsolePrintf(FOREGROUND_GREEN | FOREGROUND_INTENSITY, "SDKDumper: module parsing complete (%zu modules)\n", dumps.size());
    }

    WriteOutputs(outdir, dumps);
}

static DWORD WINAPI ThreadProc(LPVOID) {
    DumpAll();
    return 0;
}

void StartDumpThread() {
    HANDLE h = CreateThread(nullptr, 0, ThreadProc, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

// MinHook integration removed

// Dll entrypoint
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            StartDumpThread();
            (void)0;
            break;
        case DLL_PROCESS_DETACH:
            (void)0;
            break;
    }
    return TRUE;
}

} // namespace SDKDumper
