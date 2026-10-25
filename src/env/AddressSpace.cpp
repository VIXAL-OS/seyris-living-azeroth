// The process's address space, walked a slice at a time. See AddressSpace.hpp.
#include "AddressSpace.hpp"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace wxl_livingazeroth::addrspace
{
    namespace
    {
        uintptr_t g_min = 0, g_max = 0;

        void Bounds()
        {
            if (g_max) return;
            SYSTEM_INFO si{};
            GetSystemInfo(&si);
            g_min = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
            g_max = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
        }

        std::string Lower(std::string s)
        {
            for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        bool StartsWith(const std::string& s, const char* prefix) { return s.compare(0, std::strlen(prefix), prefix) == 0; }
    }

    Walker::Walker()
    {
        Bounds();
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        m_freq = static_cast<double>(f.QuadPart);
    }

    void Walker::Start()
    {
        m_work = Totals{};
        m_work.walk = m_walks + 1;
        m_next = g_min;
        m_top = g_max;   // the highest user address (inclusive)
        m_work.space = static_cast<uint64_t>(g_max - g_min) + 1;
        m_inAlloc = false;
        m_seenPaths.clear();
        m_active = true;
    }

    const std::string& Walker::PathOf(uintptr_t base, Kind kind)
    {
        (void)kind;
        auto seen = m_seenPaths.find(base);
        if (seen != m_seenPaths.end()) return seen->second;
        auto kept = m_paths.find(base);
        if (kept != m_paths.end()) return m_seenPaths.emplace(base, kept->second).first->second;
        wchar_t wide[MAX_PATH * 2];
        std::string path;
        const DWORD n = K32GetMappedFileNameW(GetCurrentProcess(), reinterpret_cast<LPVOID>(base), wide, static_cast<DWORD>(sizeof(wide) / sizeof(wide[0])));
        ++m_work.lookups;
        if (n)
        {
            char utf8[MAX_PATH * 6];
            const int m = WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(n), utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
            if (m > 0) path.assign(utf8, static_cast<size_t>(m));
        }
        return m_seenPaths.emplace(base, path).first->second;
    }

    void Walker::EndAllocation()
    {
        if (!m_inAlloc) return;
        m_inAlloc = false;
        if (m_allocKind == kPrivate)
        {
            auto& top = m_work.largestPrivate;
            const PrivateBlock b{ m_allocBase, m_allocCommitted, m_allocReserved };
            const uint64_t size = b.committed + b.reserved;
            if (top.size() < kLargestPrivateKept) top.push_back(b);
            else
            {
                auto smallest = std::min_element(top.begin(), top.end(), [](const PrivateBlock& a, const PrivateBlock& c) {
                    return a.committed + a.reserved < c.committed + c.reserved;
                });
                if (size > smallest->committed + smallest->reserved) *smallest = b;
            }
            return;
        }
        Owner o;
        o.base = m_allocBase;
        o.committed = m_allocCommitted;
        o.reserved = m_allocReserved;
        o.kind = m_allocKind;
        o.path = PathOf(m_allocBase, m_allocKind);
        o.name = o.path.empty() ? std::string("(shared memory, no file)") : FileName(o.path);
        m_work.owners.push_back(std::move(o));
    }

    void Walker::Region(const void* info)
    {
        const auto& m = *static_cast<const MEMORY_BASIC_INFORMATION*>(info);
        const uint64_t size = m.RegionSize;
        ++m_work.regions;
        if (m.State == MEM_FREE)
        {
            EndAllocation();
            m_work.free += size;
            if (size > m_work.largestFree) { m_work.largestFree = size; m_work.largestFreeAt = reinterpret_cast<uintptr_t>(m.BaseAddress); }
            if (size >= kBigFree) ++m_work.freeOver64;
            return;
        }
        const Kind kind = m.Type == MEM_IMAGE ? kImage : m.Type == MEM_MAPPED ? kMapped : kPrivate;
        const uintptr_t base = reinterpret_cast<uintptr_t>(m.AllocationBase);
        if (!m_inAlloc || base != m_allocBase)
        {
            EndAllocation();
            m_inAlloc = true;
            m_allocBase = base;
            m_allocKind = kind;
            m_allocCommitted = m_allocReserved = 0;
            ++m_work.allocations;
        }
        if (m.State == MEM_COMMIT) { m_work.committed[kind] += size; m_allocCommitted += size; }
        else { m_work.reserved[kind] += size; m_allocReserved += size; }
    }

    bool Walker::Step(double budgetMs)
    {
        if (!m_active) return false;
        LARGE_INTEGER t0{}, t{};
        QueryPerformanceCounter(&t0);
        const double budgetTicks = budgetMs * m_freq / 1000.0;
        bool done = false;
        while (!done)
        {
            for (int i = 0; i < 16 && !done; ++i)
            {
                MEMORY_BASIC_INFORMATION mbi{};
                if (m_next > m_top || !VirtualQuery(reinterpret_cast<LPCVOID>(m_next), &mbi, sizeof(mbi)) || !mbi.RegionSize) { done = true; break; }
                Region(&mbi);
                const uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
                if (next <= m_next || next > m_top) done = true;   // the top (or a wrap past it)
                m_next = next;
            }
            QueryPerformanceCounter(&t);
            if (static_cast<double>(t.QuadPart - t0.QuadPart) >= budgetTicks) break;
        }
        if (done) EndAllocation();
        const double ms = m_freq > 0.0 ? static_cast<double>(t.QuadPart - t0.QuadPart) * 1000.0 / m_freq : 0.0;
        ++m_work.frames;
        m_work.ms += ms;
        m_work.maxStepMs = std::max(m_work.maxStepMs, ms);
        if (!done) return false;
        std::sort(m_work.largestPrivate.begin(), m_work.largestPrivate.end(), [](const PrivateBlock& a, const PrivateBlock& b) {
            return a.committed + a.reserved > b.committed + b.reserved;
        });
        m_work.finishedTick = GetTickCount64();
        m_walks = m_work.walk;
        m_last = std::move(m_work);
        m_work = Totals{};
        m_paths.swap(m_seenPaths);   // the names seen this walk, for the next
        m_seenPaths.clear();
        m_active = false;
        return true;
    }

    void Walker::WalkAll()
    {
        Start();
        while (!Step(1000.0)) {}
    }

    std::string FileName(const std::string& path)
    {
        const size_t slash = path.find_last_of("\\/");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    ImageGroup GroupOf(const Owner& image, uintptr_t exeBase)
    {
        if (image.base == exeBase) return kClient;
        const std::string path = Lower(image.path), name = Lower(image.name);
        if (path.find("\\extensions\\") != std::string::npos) return kExtension;
        if (StartsWith(name, "warcraftx")) return kCore;
        static const char* const kDirect3DNames[] = { "d3d9.dll", "d3d9on12.dll", "d3d8thk.dll", "dxgi.dll", "d3d10warp.dll", "d3d11.dll", "d3d12.dll",
                                                      "d3d12core.dll", "d3dcompiler_47.dll", "d3dcompiler_43.dll", "dxcore.dll" };
        if (StartsWith(name, "d3dx9_")) return kDirect3D;
        for (const char* d : kDirect3DNames)
            if (name == d) return kDirect3D;
        static const char* const kDriverNames[] = { "nvd3dum", "nvwgf2um", "nvldumd", "nvgpucomp", "nvoglv", "nvumdshim", "nvppe", "nvdlist", "aticfx",
                                                    "atiumd", "atidxx", "amdxc", "amdihk", "atiadl", "amdenc", "igd", "igc", "igdumd", "igdusc", "igdgmm",
                                                    "nvspcap", "nvapi", "nvinit", "nvmemmap" };
        if (path.find("\\driverstore\\") != std::string::npos) return kDriver;
        for (const char* d : kDriverNames)
            if (StartsWith(name, d)) return kDriver;
        if (path.find("\\windows\\") != std::string::npos) return kWindows;
        return kOtherImage;
    }

    const char* GroupName(int group)
    {
        static const char* const kNames[kImageGroups] = { "the client", "WarcraftXL", "extensions", "Direct3D", "the graphics driver", "Windows", "other" };
        return group >= 0 && group < kImageGroups ? kNames[group] : "?";
    }
}
