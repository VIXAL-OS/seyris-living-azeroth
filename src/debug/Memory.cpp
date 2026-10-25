// The memory readout. See Memory.hpp.
#include "Memory.hpp"

#include "MemoryUse.hpp"
#include "Snapshot.hpp"
#include "../env/AddressSpace.hpp"
#include "../env/ClientMemory.hpp"
#include "../features/Crystal.hpp"
#include "../features/FogFar.hpp"
#include "../features/FogWake.hpp"
#include "../features/Glass.hpp"
#include "../features/GlassShafts.hpp"
#include "../features/GrassInstanced.hpp"
#include "../features/GroundFog.hpp"
#include "../features/ModelLights.hpp"
#include "../features/SeeThrough.hpp"
#include "../features/SunShafts.hpp"
#include "../features/SurfaceCover.hpp"
#include "../features/TerrainIce.hpp"
#include "../features/WindowGlow.hpp"
#include "../render/MoistureTexture.hpp"
#include "../render/PostAA.hpp"
#include "../render/SceneDepth.hpp"

#include "game/Gx.hpp"
#include "game/Script.hpp"

#include <windows.h>
#include <d3d9.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace wxl_livingazeroth::memory
{
    namespace
    {
        namespace as = addrspace;
        namespace cm = clientmem;
        constexpr const char* kTag = "wxl-seyris-living-azeroth";
        constexpr double kStepBudgetMs = 0.25;   // each frame's slice of a walk
        constexpr uint64_t kMB = 1ull << 20;

        const WXL_Api* g_api = nullptr;
        Settings g_settings;
        as::Walker* g_walker = nullptr;
        cm::Caches g_caches;
        int  g_guards = -1;                      // -1 not checked, 0 the client's code differs, 1 as expected
        char g_guardText[260] = "not checked yet";
        uint64_t g_nextWalk = 0, g_nextQuick = 0, g_nextCaches = 0, g_cachesRead = 0;
        uint32_t g_cachesPasses = 0;

        // after each walk
        uint64_t g_textureMem = 0;               // GetAvailableTextureMem
        PROCESS_MEMORY_COUNTERS_EX g_counters{};
        MEMORYSTATUSEX g_quick{};
        memuse::Gpu g_gpu[memuse::kTagCount];
        double g_featuresMs = 0.0;
        double g_madeRate = 0.0;                 // LA's heap blocks made a second, between the last two walks
        uint64_t g_madePrev = 0, g_madeTick = 0;
        bool g_firstWalkLogged = false, g_firstCachesLogged = false;

        WarnState g_warn;
        uint32_t g_warnings = 0;
        std::string g_warnText = "none yet";

        char g_log[900] = "memory: not walked yet";
        std::vector<std::string> g_lines;
        uint64_t g_linesTick = 0;
        bool g_linesDirty = true;

        // --- text ---------------------------------------------------------------------------------------------------------
        std::string Format(const char* format, ...)
        {
            char line[1600];
            va_list args;
            va_start(args, format);
            std::vsnprintf(line, sizeof(line), format, args);
            va_end(args);
            return line;
        }

        std::string Mb(uint64_t bytes)
        {
            const double mb = static_cast<double>(bytes) / static_cast<double>(kMB);
            char s[48];
            if (mb >= 100.0)
            {
                // thousands separated: 2,512
                const unsigned long long v = static_cast<unsigned long long>(mb + 0.5);
                if (v >= 1000) std::snprintf(s, sizeof(s), "%llu,%03llu", v / 1000, v % 1000);
                else std::snprintf(s, sizeof(s), "%llu", v);
            }
            else if (mb >= 1.0) std::snprintf(s, sizeof(s), "%.1f", mb);
            else std::snprintf(s, sizeof(s), "%.2f", mb);
            return s;
        }

        std::string Count(uint64_t n)
        {
            char s[32];
            if (n >= 1000000) std::snprintf(s, sizeof(s), "%llu,%03llu,%03llu", n / 1000000, n / 1000 % 1000, n % 1000);
            else if (n >= 1000) std::snprintf(s, sizeof(s), "%llu,%03llu", n / 1000, n % 1000);
            else std::snprintf(s, sizeof(s), "%llu", n);
            return s;
        }

        std::string Ago(uint64_t tick)
        {
            if (!tick) return "never";
            const uint64_t s = (GetTickCount64() - tick) / 1000;
            return s < 120 ? Format("%llu s ago", s) : Format("%llu min ago", s / 60);
        }

        uint64_t ExeBase() { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); }

        // --- the client --------------------------------------------------------------------------------------------------
        bool ReadCode(uintptr_t address, void* out, size_t n)
        {
            __try { std::memcpy(out, reinterpret_cast<const void*>(address), n); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        void CheckGuards()
        {
            static uint8_t buffer[16];
            const cm::Guard* bad = cm::FirstMismatch([](uintptr_t address, uint32_t size) -> const void* {
                return size <= sizeof(buffer) && ReadCode(address, buffer, size) ? buffer : nullptr;
            });
            g_guards = bad ? 0 : 1;
            if (bad)
                std::snprintf(g_guardText, sizeof(g_guardText), "not read (the client's code differs: %s at 0x%08X)", bad->what,
                              static_cast<unsigned>(bad->address));
            else
                std::snprintf(g_guardText, sizeof(g_guardText), "the client's code as expected (%u checks)",
                              static_cast<unsigned>(sizeof(cm::kGuards) / sizeof(cm::kGuards[0])));
            if (g_api) g_api->Log(bad ? WXL_LOG_WARN : WXL_LOG_INFO, kTag, "memory: the client's caches: %s", g_guardText);
        }

        void ReadCaches()
        {
            if (g_guards != 1) return;
            cm::Read(cm::Addresses{}, g_caches);
            g_cachesRead = GetTickCount64();
            ++g_cachesPasses;
            g_linesDirty = true;
            if (!g_firstCachesLogged && g_api && g_caches.read && g_caches.textures)
            {
                g_firstCachesLogged = true;
                g_api->Log(WXL_LOG_INFO, kTag, "memory: the client's caches, first read (%.2f ms, %s): %u textures, %s MB managed, %s MB render targets; "
                           "%u M2 models (%s MB of files); %u WMO roots", g_caches.ms, g_caches.why, g_caches.textures, Mb(g_caches.managedBytes).c_str(),
                           Mb(g_caches.targetBytes).c_str(), g_caches.m2Models, Mb(g_caches.m2FileBytes).c_str(), g_caches.wmoRoots);
            }
        }

        // --- Living Azeroth's features -----------------------------------------------------------------------------------
        struct Reporter { int tag; void (*fn)(memuse::Gpu&); };
        const Reporter kReporters[] = {
            {memuse::kFog, &groundfog::GpuMemory},
            {memuse::kFog, &fogfar::GpuMemory},
            {memuse::kFog, &fogwake::GpuMemory},
            {memuse::kLights, &modellights::GpuMemory},
            {memuse::kEnv, &moisturetex::GpuMemory},
            {memuse::kGlass, &glass::GpuMemory},
            {memuse::kGlass, &glassshafts::GpuMemory},
            {memuse::kSeeThrough, &seethrough::GpuMemory},
            {memuse::kWindows, &windowglow::GpuMemory},
            {memuse::kCover, &cover::GpuMemory},
            {memuse::kGrass, &grassinst::GpuMemory},
            {memuse::kIce, &terrainice::GpuMemory},
            {memuse::kIce, &crystal::GpuMemory},
            {memuse::kShafts, &sunshafts::GpuMemory},
            {memuse::kRender, &postaa::GpuMemory},
            {memuse::kRender, &depth::GpuMemory},
        };

        void Features()
        {
            LARGE_INTEGER f{}, t0{}, t1{};
            QueryPerformanceFrequency(&f);
            QueryPerformanceCounter(&t0);
            for (auto& g : g_gpu) g = memuse::Gpu{};
            for (const Reporter& r : kReporters) r.fn(g_gpu[r.tag]);
            QueryPerformanceCounter(&t1);
            g_featuresMs = f.QuadPart ? static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart) : 0.0;
            uint64_t made = 0;
            for (int t = 0; t < memuse::kTagCount; ++t) made += memuse::Heap(t).made;
            const uint64_t now = GetTickCount64();
            if (g_madeTick && now > g_madeTick) g_madeRate = static_cast<double>(made - g_madePrev) * 1000.0 / static_cast<double>(now - g_madeTick);
            g_madePrev = made;
            g_madeTick = now;
        }

        struct FeatureTotal { int tag; int64_t heap; uint64_t inSpace, video; };
        std::vector<FeatureTotal> FeatureTotals(int64_t& heapAll, uint64_t& spaceAll, uint64_t& videoAll)
        {
            std::vector<FeatureTotal> out;
            heapAll = 0;
            spaceAll = videoAll = 0;
            for (int t = 0; t < memuse::kTagCount; ++t)
            {
                const memuse::HeapUse h = memuse::Heap(t);
                FeatureTotal f{ t, h.bytes > 0 ? h.bytes : 0, g_gpu[t].InAddressSpace(), g_gpu[t].InVideo() };
                heapAll += f.heap;
                spaceAll += f.inSpace;
                videoAll += f.video;
                if (f.heap > 0 || f.inSpace || f.video) out.push_back(f);
            }
            std::sort(out.begin(), out.end(), [](const FeatureTotal& a, const FeatureTotal& b) {
                return static_cast<uint64_t>(a.heap) + a.inSpace > static_cast<uint64_t>(b.heap) + b.inSpace;
            });
            return out;
        }

        // --- warnings ---------------------------------------------------------------------------------------------------
        void Chat(const std::string& text)
        {
            // Only into the 12340 client the guards found (a test, or another client, has no chat to call).
            if (g_guards != 1 || !wxl::game::script::Context()) return;
            std::string lua = "if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage(\"";
            for (const char ch : text)
                if (ch == '\\') lua += "\\\\";
                else if (ch == '"') lua += "\\\"";
                else lua += ch;
            lua += "\", 1.0, 0.55, 0.3) end";
            wxl::game::script::Execute(lua.c_str(), "LivingAzerothMemory");
        }

        void CheckWarn(uint64_t freeBytes, uint64_t largestBytes)
        {
            std::string why;
            if (!ShouldWarn(g_warn, GetTickCount64(), freeBytes, largestBytes, g_settings, why)) return;
            ++g_warnings;
            if (g_caches.tick == 0 || GetTickCount64() - g_caches.tick > 2000) ReadCaches();
            SYSTEMTIME now{};
            GetLocalTime(&now);
            g_warnText = Format("%02u:%02u:%02u, %s (warning %u; a snapshot asked for)", now.wHour, now.wMinute, now.wSecond, why.c_str(), g_warnings);
            const std::string largest = g_warn.largest ? Mb(g_warn.largest) + " MB" : std::string("not walked yet");
            Chat(Format("Living Azeroth: the client's memory (its 32-bit address space) is nearly full: %s MB free, the largest free block %s. "
                        "A snapshot of what uses it goes to Logs\\la-snapshots.", Mb(freeBytes).c_str(), largest.c_str()));
            if (g_api) g_api->Log(WXL_LOG_WARN, kTag, "memory: WARNING %s; %s", why.c_str(), LogPart());
            snapshot::Request("the memory warning");
            g_linesDirty = true;
        }

        uint64_t AvailableTextureMem()
        {
            __try
            {
                auto* dev = static_cast<IDirect3DDevice9*>(wxl::game::gx::RawDevice());
                return dev ? dev->GetAvailableTextureMem() : 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        }

        void OnWalkDone()
        {
            const as::Totals& t = g_walker->Last();
            g_textureMem = AvailableTextureMem();
            g_counters.cb = sizeof(g_counters);
            K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&g_counters), sizeof(g_counters));
            Features();
            g_linesDirty = true;
            if (!g_firstWalkLogged && g_api)
            {
                g_firstWalkLogged = true;
                g_api->Log(WXL_LOG_INFO, kTag, "memory: the first walk of the address space: %u regions in %u frames, %.2f ms (at most %.3f ms in one); "
                           "used %s MB of %s, free %s MB, the largest free block %s MB", t.regions, t.frames, t.ms, t.maxStepMs, Mb(t.Used()).c_str(),
                           Mb(t.space).c_str(), Mb(t.free).c_str(), Mb(t.largestFree).c_str());
            }
            CheckWarn(t.free, t.largestFree);
        }

        // --- the lines -----------------------------------------------------------------------------------------------------
        struct ImageGroups
        {
            uint64_t committed[as::kImageGroups] = {}, reserved[as::kImageGroups] = {};
            uint32_t count[as::kImageGroups] = {};
        };
        ImageGroups Groups(const as::Totals& t)
        {
            ImageGroups g;
            const uintptr_t exe = static_cast<uintptr_t>(ExeBase());
            for (const as::Owner& o : t.owners)
            {
                if (o.kind != as::kImage) continue;
                const int k = as::GroupOf(o, exe);
                g.committed[k] += o.committed;
                g.reserved[k] += o.reserved;
                ++g.count[k];
            }
            return g;
        }

        std::vector<const as::Owner*> Largest(const as::Totals& t, as::Kind kind, size_t n)
        {
            std::vector<const as::Owner*> v;
            for (const as::Owner& o : t.owners)
                if (o.kind == kind) v.push_back(&o);
            std::sort(v.begin(), v.end(), [](const as::Owner* a, const as::Owner* b) { return a->committed + a->reserved > b->committed + b->reserved; });
            if (v.size() > n) v.resize(n);
            return v;
        }

        std::string WalkHeader(const as::Totals& t)
        {
            return Format("Memory: the 32-bit address space, walked every %.0f s (walk #%u, %s): used %s MB of %s (committed %s, reserved %s); free %s MB, "
                          "the largest free block %s MB, %u free blocks of 64 MB or more. Warns below %.0f MB free or a %.0f MB largest block.",
                          g_settings.walkSeconds, t.walk, Ago(t.finishedTick).c_str(), Mb(t.Used()).c_str(), Mb(t.space).c_str(),
                          Mb(t.Committed()).c_str(), Mb(t.Reserved()).c_str(), Mb(t.free).c_str(), Mb(t.largestFree).c_str(), t.freeOver64,
                          g_settings.warnFreeMB, g_settings.warnLargestMB);
        }

        std::string OwnersLine(const as::Totals& t)
        {
            uint32_t images = 0, mapped = 0;
            for (const as::Owner& o : t.owners) (o.kind == as::kImage ? images : mapped)++;
            return Format("  by owner: images (the EXE and the DLLs) %s MB committed + %s reserved in %u; mapped files %s + %s in %u; private (heaps, "
                          "VirtualAlloc, Direct3D's copies of managed textures) %s + %s",
                          Mb(t.committed[as::kImage]).c_str(), Mb(t.reserved[as::kImage]).c_str(), images, Mb(t.committed[as::kMapped]).c_str(),
                          Mb(t.reserved[as::kMapped]).c_str(), mapped, Mb(t.committed[as::kPrivate]).c_str(), Mb(t.reserved[as::kPrivate]).c_str());
        }

        std::string GroupsLine(const as::Totals& t)
        {
            const ImageGroups g = Groups(t);
            std::string s = "  the images by group:";
            for (int k = 0; k < as::kImageGroups; ++k)
                if (g.count[k]) s += Format("%s %s %s MB (%u)", k ? "," : "", as::GroupName(k), Mb(g.committed[k] + g.reserved[k]).c_str(), g.count[k]);
            return s;
        }

        std::string LargestLine(const char* what, const std::vector<const as::Owner*>& v)
        {
            std::string s = Format("  the largest %s:", what);
            if (v.empty()) return s + " none";
            for (size_t i = 0; i < v.size(); ++i) s += Format("%s %s %s MB", i ? "," : "", v[i]->name.c_str(), Mb(v[i]->committed + v[i]->reserved).c_str());
            return s;
        }

        std::string PrivateLine(const as::Totals& t, size_t n)
        {
            std::string s = "  the largest private allocations:";
            for (size_t i = 0; i < t.largestPrivate.size() && i < n; ++i)
            {
                const as::PrivateBlock& b = t.largestPrivate[i];
                s += Format("%s %s MB at 0x%08llX (%s committed)", i ? "," : "", Mb(b.committed + b.reserved).c_str(), static_cast<unsigned long long>(b.base),
                            Mb(b.committed).c_str());
            }
            return s;
        }

        std::string CountersLine()
        {
            return Format("  Windows' counters for the process (as the external memory log's): private %s MB, working set %s MB, peak working set %s MB; "
                          "Direct3D 9's available texture memory %s MB (the driver's estimate)",
                          Mb(g_counters.PrivateUsage).c_str(), Mb(g_counters.WorkingSetSize).c_str(), Mb(g_counters.PeakWorkingSetSize).c_str(),
                          Mb(g_textureMem).c_str());
        }

        void CacheLines(std::vector<std::string>& out, bool all)
        {
            const cm::Caches& c = g_caches;
            if (g_guards != 1) { out.push_back(std::string("The client's caches: ") + g_guardText); return; }
            if (!c.read)
            {
                out.push_back(Format("The client's caches: %s", g_cachesPasses ? c.why : "not read yet"));
                return;
            }
            out.push_back(Format("The client's caches (read %s%s, in %.2f ms; %s): %s textures with a Direct3D texture (of %s), %s MB, each a managed texture: "
                                 "a copy in video memory and Direct3D's copy in this address space; render targets %s MB more (%u, video memory only); "
                                 "%s MB of them uncompressed",
                                 g_settings.cachesSeconds > 0.0f ? Format("every %.0f s, ", g_settings.cachesSeconds).c_str() : "for snapshots and warnings, ",
                                 Ago(c.tick).c_str(), c.ms, c.why, Count(c.created).c_str(), Count(c.textures).c_str(), Mb(c.managedBytes).c_str(),
                                 Mb(c.targetBytes).c_str(), c.targets, Mb(c.UncompressedBytes()).c_str()));
            int order[cm::kKinds];
            for (int k = 0; k < cm::kKinds; ++k) order[k] = k;
            std::sort(order, order + cm::kKinds, [&](int a, int b) { return c.kindBytes[a] > c.kindBytes[b]; });
            std::string kinds = "  textures by kind:";
            bool first = true;
            for (int i = 0; i < cm::kKinds; ++i)
            {
                const int k = order[i];
                if (!c.kindCount[k]) continue;
                kinds += Format("%s %s %s MB (%s)", first ? "" : ",", cm::KindName(k), Mb(c.kindBytes[k]).c_str(), Count(c.kindCount[k]).c_str());
                first = false;
            }
            out.push_back(kinds);
            std::string formats = "  textures by format:";
            first = true;
            for (int f = 0; f < cm::kFormats; ++f)
            {
                if (!c.formatCount[f]) continue;
                formats += Format("%s %s %s MB (%s)", first ? "" : ",", cm::FormatName(f), Mb(c.formatBytes[f]).c_str(), Count(c.formatCount[f]).c_str());
                first = false;
            }
            out.push_back(formats);
            const uint64_t poolStatic = c.poolBytes[0][0] + c.poolBytes[1][0];
            const uint64_t poolDynamic = c.poolBytes[0][1] + c.poolBytes[1][1] + c.poolBytes[0][2] + c.poolBytes[1][2];
            uint32_t pools = 0;
            for (auto& t : c.pools) for (uint32_t n : t) pools += n;
            out.push_back(Format("  the GxTex reuse cache %s of %s MB; the vertex and index pools: %u, static %s MB (managed: a copy in this address space too), "
                                 "dynamic and stream %s MB (usually system memory mapped into this process); M2 models %s (their files %s MB, kept in memory; %u waiting to be freed); WMO roots %u; "
                                 "the base mip level %u%s",
                                 Mb(c.gxCacheSize > 0 ? static_cast<uint64_t>(c.gxCacheSize) : 0).c_str(),
                                 Mb(c.gxCacheMax > 0 ? static_cast<uint64_t>(c.gxCacheMax) : 0).c_str(), pools, Mb(poolStatic).c_str(),
                                 Mb(poolDynamic).c_str(), Count(c.m2Models).c_str(), Mb(c.m2FileBytes).c_str(), c.m2Waiting, c.wmoRoots, c.baseMip,
                                 c.onAtlas ? Format("; %u textures on an atlas (not counted)", c.onAtlas).c_str() : ""));
            const int shown = all ? c.largestCount : std::min(c.largestCount, 5);
            if (shown)
            {
                if (all)
                {
                    out.push_back(Format("  the %d largest textures:", shown));
                    for (int i = 0; i < shown; ++i)
                    {
                        const cm::TextureEntry& e = c.largest[i];
                        out.push_back(Format("    %s MB  %ux%u %s%s  %s  (%s)", Mb(e.bytes).c_str(), e.width, e.height, cm::FormatName(e.format),
                                             e.target ? " render target" : "", e.name[0] ? e.name : "(no name)", cm::KindName(e.kind)));
                    }
                }
                else
                {
                    std::string s = "  the largest textures:";
                    for (int i = 0; i < shown; ++i)
                        s += Format("%s %s %s MB", i ? "," : "", c.largest[i].name[0] ? as::FileName(c.largest[i].name).c_str() : "(no name)",
                                    Mb(c.largest[i].bytes).c_str());
                    out.push_back(s);
                }
            }
        }

        void FeatureLines(std::vector<std::string>& out, bool all)
        {
            int64_t heapAll = 0;
            uint64_t spaceAll = 0, videoAll = 0;
            const std::vector<FeatureTotal> f = FeatureTotals(heapAll, spaceAll, videoAll);
            uint64_t blocks = 0;
            for (int t = 0; t < memuse::kTagCount; ++t) blocks += static_cast<uint64_t>(std::max<int64_t>(0, memuse::Heap(t).blocks));
            out.push_back(Format("Living Azeroth by feature: %s MB in this address space (its heap %s MB in %s blocks, %.0f made a second; Direct3D's copies %s MB), "
                                 "%s MB of video memory%s",
                                 Mb(static_cast<uint64_t>(heapAll) + spaceAll).c_str(), Mb(static_cast<uint64_t>(heapAll)).c_str(), Count(blocks).c_str(),
                                 g_madeRate, Mb(spaceAll).c_str(), Mb(videoAll).c_str(),
                                 memuse::Counting() ? (memuse::ForeignFrees() ? Format("; %u frees of blocks not its own", memuse::ForeignFrees()).c_str() : "")
                                                    : "; the heap isn't counted in this build"));
            std::string compact = " ";
            for (size_t i = 0; i < f.size(); ++i)
            {
                const FeatureTotal& x = f[i];
                const memuse::Gpu& g = g_gpu[x.tag];
                if (all)
                {
                    const memuse::HeapUse h = memuse::Heap(x.tag);
                    out.push_back(Format("  %-36s heap %8s MB in %7s blocks (%s made); Direct3D %u resources: managed %s MB, video only %s MB, "
                                         "dynamic buffers %s MB, system memory %s MB%s", memuse::TagName(x.tag), Mb(static_cast<uint64_t>(x.heap)).c_str(),
                                         Count(static_cast<uint64_t>(std::max<int64_t>(0, h.blocks))).c_str(), Count(h.made).c_str(), g.resources,
                                         Mb(g.managed).c_str(), Mb(g.video).c_str(), Mb(g.dynamic).c_str(), Mb(g.system).c_str(),
                                         g.unreadable ? Format(", %u unreadable", g.unreadable).c_str() : ""));
                }
                else
                {
                    compact += Format("%s %s %s MB", i ? "," : "", memuse::TagName(x.tag), Mb(static_cast<uint64_t>(x.heap) + x.inSpace).c_str());
                    if (g.video > 0 || g.managed > 0) compact += Format(" (video %s)", Mb(x.video).c_str());
                }
            }
            if (!all && !f.empty()) out.push_back(compact);
        }

        std::string CostLine()
        {
            const as::Totals& t = g_walker->Last();
            const double perFrameWalk = g_settings.walkSeconds > 0.0f ? t.ms / std::max(1.0, g_settings.walkSeconds * 60.0) : 0.0;
            const double perFrameCaches = g_settings.cachesSeconds > 0.0f ? g_caches.ms / std::max(1.0, g_settings.cachesSeconds * 60.0) : 0.0;
            return Format("The readout's own cost: the walk %.2f ms over %u frames for %s regions (at most %.3f ms in a frame, %u names looked up), the client's "
                          "caches %.2f ms in one frame, the features' Direct3D %.3f ms: at 60 fps about %.4f ms a frame on average",
                          t.ms, t.frames, Count(t.regions).c_str(), t.maxStepMs, t.lookups, g_caches.ms, g_featuresMs, perFrameWalk + perFrameCaches);
        }

        void BuildLines(std::vector<std::string>& out, bool all)
        {
            if (!g_settings.enabled) { out.push_back("Memory: the readout is off (the Memory section's Readout = 0)."); return; }
            if (!g_walker || !g_walker->HaveWalked()) { out.push_back("Memory: the address space isn't walked yet (the first walk starts with the world)."); return; }
            const as::Totals& t = g_walker->Last();
            out.push_back(WalkHeader(t));
            out.push_back(OwnersLine(t));
            out.push_back(GroupsLine(t));
            out.push_back(LargestLine("images", Largest(t, as::kImage, all ? 40 : 6)));
            out.push_back(LargestLine("mapped files", Largest(t, as::kMapped, all ? 20 : 4)));
            out.push_back(PrivateLine(t, all ? as::kLargestPrivateKept : 4));
            out.push_back(CountersLine());
            CacheLines(out, all);
            FeatureLines(out, all);
            out.push_back(CostLine());
            out.push_back("Memory warnings: " + g_warnText);
        }
    }

    Settings& Config() { return g_settings; }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        CheckGuards();   // before any detour is armed (the core arms them after every WXL_Load)
    }

    void Update(bool inWorld)
    {
        (void)inWorld;
        memuse::Scope tag(memuse::kPanels);
        if (!g_settings.enabled) return;
        if (!g_walker) g_walker = new as::Walker();
        const uint64_t now = GetTickCount64();
        if (now >= g_nextQuick)
        {
            g_nextQuick = now + 1000;
            g_quick.dwLength = sizeof(g_quick);
            if (GlobalMemoryStatusEx(&g_quick)) CheckWarn(g_quick.ullAvailVirtual, 0);
        }
        if (!g_walker->Active() && now >= g_nextWalk)
        {
            g_walker->Start();
            const float s = std::isfinite(g_settings.walkSeconds) ? std::max(1.0f, g_settings.walkSeconds) : 5.0f;
            g_nextWalk = now + static_cast<uint64_t>(s * 1000.0f);
        }
        if (g_walker->Active() && g_walker->Step(kStepBudgetMs)) OnWalkDone();
        if (g_settings.cachesSeconds > 0.0f && std::isfinite(g_settings.cachesSeconds) && now >= g_nextCaches && g_walker->HaveWalked())
        {
            ReadCaches();
            g_nextCaches = now + static_cast<uint64_t>(std::max(5.0f, g_settings.cachesSeconds) * 1000.0f);
        }
    }

    void OverviewLines(std::vector<std::string>& out)
    {
        memuse::Scope tag(memuse::kPanels);
        const uint64_t now = GetTickCount64();
        if (g_linesDirty || now - g_linesTick >= 1000)
        {
            g_lines.clear();
            BuildLines(g_lines, false);
            g_linesTick = now;
            g_linesDirty = false;
        }
        out.insert(out.end(), g_lines.begin(), g_lines.end());
    }

    const char* LogPart()
    {
        memuse::Scope tag(memuse::kPanels);
        if (!g_settings.enabled) return "memory: the readout is off";
        if (!g_walker || !g_walker->HaveWalked()) return "memory: not walked yet";
        const as::Totals& t = g_walker->Last();
        int64_t heapAll = 0;
        uint64_t spaceAll = 0, videoAll = 0;
        FeatureTotals(heapAll, spaceAll, videoAll);
        const cm::Caches& c = g_caches;
        const std::string caches = c.read ? Format("client textures %s MB in %u (uncompressed %s, render targets %s), static pools %s MB, M2 %u models %s MB, "
                                                   "WMO roots %u (read %s)", Mb(c.managedBytes).c_str(), c.created, Mb(c.UncompressedBytes()).c_str(),
                                                   Mb(c.targetBytes).c_str(), Mb(c.poolBytes[0][0] + c.poolBytes[1][0]).c_str(), c.m2Models,
                                                   Mb(c.m2FileBytes).c_str(), c.wmoRoots, Ago(c.tick).c_str())
                                          : std::string("client caches ") + (g_guards == 1 ? c.why : "not read (the client's code differs)");
        const ImageGroups g = Groups(t);
        std::snprintf(g_log, sizeof(g_log), "memory: used %s MB (committed %s, reserved %s), free %s MB, largest free %s MB (%u over 64 MB); private %s MB, images %s MB "
                      "(driver %s, extensions %s), mapped %s MB; %s; LA %s MB (video %s); walk #%u %.2f ms in %u frames; warnings %u",
                      Mb(t.Used()).c_str(), Mb(t.Committed()).c_str(), Mb(t.Reserved()).c_str(), Mb(t.free).c_str(), Mb(t.largestFree).c_str(), t.freeOver64,
                      Mb(t.committed[as::kPrivate] + t.reserved[as::kPrivate]).c_str(), Mb(t.committed[as::kImage] + t.reserved[as::kImage]).c_str(),
                      Mb(g.committed[as::kDriver] + g.reserved[as::kDriver]).c_str(), Mb(g.committed[as::kExtension] + g.reserved[as::kExtension]).c_str(),
                      Mb(t.committed[as::kMapped] + t.reserved[as::kMapped]).c_str(), caches.c_str(),
                      Mb(static_cast<uint64_t>(heapAll) + spaceAll).c_str(), Mb(videoAll).c_str(), t.walk, t.ms, t.frames, g_warnings);
        return g_log;
    }

    void SnapshotLines(std::vector<std::string>& out)
    {
        memuse::Scope tag(memuse::kPanels);
        if (g_settings.enabled && !g_walker) g_walker = new as::Walker();
        if (g_walker && !g_walker->HaveWalked())
        {
            // asked before the first walk finished: one whole walk now (a local walker: the running one carries on)
            as::Walker now;
            now.WalkAll();
            std::swap(*g_walker, now);
            OnWalkDone();
        }
        ReadCaches();
        if (g_walker) Features();
        BuildLines(out, true);
        if (!g_walker || !g_walker->HaveWalked()) return;
        const as::Totals& t = g_walker->Last();
        out.push_back("");
        out.push_back(Format("Every image and mapped file (committed + reserved MB, the allocation base, the file), %zu:", t.owners.size()));
        std::vector<const as::Owner*> all;
        for (const as::Owner& o : t.owners) all.push_back(&o);
        std::sort(all.begin(), all.end(), [](const as::Owner* a, const as::Owner* b) { return a->committed + a->reserved > b->committed + b->reserved; });
        const uintptr_t exe = static_cast<uintptr_t>(ExeBase());
        for (const as::Owner* o : all)
            out.push_back(Format("  %8s + %7s  0x%08llX  %-7s %-20s %s", Mb(o->committed).c_str(), Mb(o->reserved).c_str(), static_cast<unsigned long long>(o->base),
                                 o->kind == as::kImage ? "image" : "mapped", o->kind == as::kImage ? as::GroupName(as::GroupOf(*o, exe)) : "",
                                 o->path.empty() ? o->name.c_str() : o->path.c_str()));
        out.push_back(Format("The address space: %s MB from 0x%08llX up to the highest user address; %u regions, %u allocations; the largest free block at 0x%08llX",
                             Mb(t.space).c_str(), static_cast<unsigned long long>(0x10000), t.regions, t.allocations,
                             static_cast<unsigned long long>(t.largestFreeAt)));
        out.push_back(Format("Settings: Readout %d, WalkSeconds %.0f, ClientCachesSeconds %.0f, WarnFreeMB %.0f, WarnLargestFreeMB %.0f, WarnMinutes %.0f",
                             g_settings.enabled, g_settings.walkSeconds, g_settings.cachesSeconds, g_settings.warnFreeMB, g_settings.warnLargestMB,
                             g_settings.warnMinutes));
        out.push_back(std::string("The client's caches' guards: ") + g_guardText);
    }

    bool ShouldWarn(WarnState& s, uint64_t nowTick, uint64_t freeBytes, uint64_t largestBytes, const Settings& c, std::string& why)
    {
        const auto limit = [](float mb) { return std::isfinite(mb) && mb > 0.0f ? static_cast<uint64_t>(mb * static_cast<float>(kMB)) : 0ull; };
        const uint64_t freeLimit = limit(c.warnFreeMB), largestLimit = limit(c.warnLargestMB);
        const bool lowFree = freeLimit && freeBytes < freeLimit;
        const bool lowLargest = largestLimit && largestBytes && largestBytes < largestLimit;
        if (!lowFree && !lowLargest) return false;
        if (s.warned)
        {
            const float minutes = std::isfinite(c.warnMinutes) && c.warnMinutes > 0.0f ? c.warnMinutes : 0.0f;
            const bool due = nowTick - s.tick >= static_cast<uint64_t>(minutes * 60000.0f);
            const bool worse = s.free >= freeBytes + 100 * kMB || (largestBytes && s.largest && largestBytes * 2 <= s.largest);
            if (!due && !worse) return false;
        }
        why.clear();
        if (lowFree) why = Format("%s MB of the address space free, under WarnFreeMB %.0f", Mb(freeBytes).c_str(), c.warnFreeMB);
        if (lowLargest) why += (why.empty() ? "" : "; ") + Format("the largest free block %s MB, under WarnLargestFreeMB %.0f", Mb(largestBytes).c_str(), c.warnLargestMB);
        s.warned = true;
        s.tick = nowTick;
        s.free = freeBytes;
        if (largestBytes) s.largest = largestBytes;
        return true;
    }
}
