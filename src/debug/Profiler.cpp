#include "Profiler.hpp"
#include "Pages.hpp"

#include "engine/events/Event.hpp"
#include "game/Gx.hpp"

#include <windows.h>
#include <d3d9.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace wxl_livingazeroth::prof
{
    namespace
    {
        namespace ev = wxl::events;
        namespace gx = wxl::game::gx;

        constexpr const char* kPanelTitle = "wxl-seyris-living-azeroth: frame profiler";
        const char* const kNames[kCount] = {
            "other (game logic, unlisted)", "world: rest (sky, misc.)", "terrain", "grass: client rest",
            "M2 models", "M2 animation (main thread)", "particles", "ribbons", "WMOs", "liquids",
            "shadow maps", "weather", "UI + overlay",
            "Lua (addons + Blizzard UI)", "game: units & objects update", "game: world tick (after render)",
            "mods: per-frame update (OnUpdate)", "present (incl. waiting for GPU)",
            "ours: climate / wind / fields", "ours: lights (scan, grid, baking)", "ours: light textures",
            "ours: surface cover update", "ours: surface cover draw", "ours: anti-aliasing",
            "ours: fog field", "ours: fog draw",
            "ours: fog uploads", "ours: fog lights", "ours: surface cover uploads", "ours: sun shafts",
            "grass: client re-bake", "ours: grass wind grid", "ours: grass chunk constants",
            "ours: grass layer prepare", "ours: grass layer bind", "ours: grass layer draws",
        };

        const WXL_Api* g_api = nullptr;
        int   g_enabled = 0;     // panel
        int   g_gpu = 1;         // panel: timestamp queries
        int   g_sample = 1;      // panel: main thread by module
        bool  g_frameOn = false; // this frame is being measured (fixed at the frame's start)
        DWORD g_mainThread = 0;

        // Categories that issue no GPU work of their own and are entered very often: they move the CPU
        // clock but not the GPU timeline, which keeps the timestamp count per frame small.
        // --- category stack and accounting ----------------------------------------------------------
        constexpr int kMaxDepth = 256;
        int     g_stack[kMaxDepth];
        int     g_depth = 0;
        int64_t g_last = 0;          // QPC at the last transition
        int64_t g_qpcFreq = 1;

        struct FrameAccum
        {
            int64_t  cpu[kCount] = {};
            uint32_t draws[kCount] = {};
            uint64_t tris[kCount] = {};
        } g_frame;

        int Current() { return g_depth > 0 ? g_stack[g_depth - 1] : kOther; }

        // The category GPU time goes to: the innermost one that isn't CPU only.
        int GpuCurrent()
        {
            for (int i = g_depth - 1; i >= 0; --i)
                if (!CpuOnly(g_stack[i])) return g_stack[i];
            return kOther;
        }

        int64_t Now()
        {
            LARGE_INTEGER t;
            QueryPerformanceCounter(&t);
            return t.QuadPart;
        }

        bool OnMainThread() { return GetCurrentThreadId() == g_mainThread; }

        // --- client functions wrapped by the generic stubs (see below) --------------------------------
        // A named core hook point, or (address != 0) a raw client address for functions the core has no
        // point for: those were read off the 12340 disassembly and a sampled profile of the main thread.
        struct HookDef { const char* point; uintptr_t address; int category; };
        const HookDef kHooks[] = {
            { "World.Render",                  0, kWorld },
            { "Adt.RenderChunks",              0, kTerrain },
            { "Adt.RenderChunksSinglePass",    0, kTerrain },
            { "Adt.RenderDetailDoodads",       0, kDetail },
            { "M2.SceneDraw",                  0, kM2 },
            { "M2.SceneRenderDraw",            0, kM2 },
            { "M2.SceneAnimate",               0, kM2Anim },
            { "M2.DrawBatchedParticles",       0, kParticles },
            { "M2.DrawParticleBatch",          0, kParticles },
            { "M2.DrawRibbonBatch",            0, kRibbons },
            { "Wmo.SceneRenderInstanceGroups", 0, kWmo },
            { "Gx.LiquidRenderPass",           0, kLiquids },
            { "M2.RenderBatchListShadowMap",   0, kShadows },
            { "Wmo.RenderShadowMapGroups",     0, kShadows },
            { "Weather.Render",                0, kWeather },
            { "Gx.WorldRenderFinalize",        0, kWorldTick },  // FrameWorldUpdate, PrepareUpdate, World tick
            // lua_pcall (core offsets engine/Lua.hpp kLuaPCall): every script call, events and OnUpdate.
            { "prof: lua_pcall",               0x0084EC50, kLua },
            // The object manager's per-frame update: the clock, the active player, then every object
            // (unit movement, collision against the map, entity links).
            { "prof: object update",           0x006F1490, kUnits },
            // The engine's per-frame tick that calls the frame pump (Frame.Pump), where the core emits
            // OnUpdate: wraps every module's OnUpdate handler.
            { "prof: frame pump caller",       0x004020E0, kModsTick },
            // The flip: clear, then the Gx device's present slot (+0x98): EndScene, Present, the queue wait.
            { "prof: flip",                    0x00682A00, kPresent },
        };
        constexpr int kHookCount = static_cast<int>(sizeof(kHooks) / sizeof(kHooks[0]));
        constexpr int kMaxHooks = 24;
        static_assert(kHookCount <= kMaxHooks, "add stubs");

        struct RetEntry { int slot; uintptr_t ret; };
        RetEntry g_ret[kMaxDepth];
        int      g_retDepth = 0;
        bool     g_hooked[kMaxHooks] = {};
        unsigned g_hookCalls[kMaxHooks] = {};   // this frame

        // --- GPU timestamps ---------------------------------------------------------------------
        // A ring of frames; each holds its timestamps in issue order and, per timestamp, the category
        // whose interval it closes (-1 = the frame's start).
        constexpr int      kGpuFrames = 4;
        constexpr unsigned kMaxStamps = 4096;
        struct GpuFrame
        {
            std::vector<IDirect3DQuery9*> stamps;
            std::vector<int> cats;
            unsigned used = 0;
            IDirect3DQuery9* disjoint = nullptr;
            IDirect3DQuery9* freq = nullptr;
            bool pending = false;
            bool overflow = false;
        };
        GpuFrame g_gpuFrames[kGpuFrames];
        int  g_gpuCur = 0;
        IDirect3DDevice9* g_gpuDevice = nullptr;
        bool g_gpuSupported = true;
        bool g_gpuFrameOn = false;   // stamps are being issued this frame
        unsigned g_gpuNotReady = 0, g_gpuDisjoint = 0;

        void ReleaseGpu()
        {
            for (GpuFrame& f : g_gpuFrames)
            {
                for (IDirect3DQuery9* q : f.stamps) if (q) q->Release();
                f.stamps.clear(); f.cats.clear(); f.used = 0;
                if (f.disjoint) f.disjoint->Release();
                if (f.freq) f.freq->Release();
                f.disjoint = f.freq = nullptr;
                f.pending = f.overflow = false;
            }
            g_gpuDevice = nullptr;
            g_gpuFrameOn = false;
        }

        void Stamp(int closes)
        {
            if (!g_gpuFrameOn) return;
            GpuFrame& f = g_gpuFrames[g_gpuCur];
            if (f.used >= kMaxStamps) { f.overflow = true; return; }
            if (f.used == f.stamps.size())
            {
                IDirect3DQuery9* q = nullptr;
                if (FAILED(g_gpuDevice->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &q)) || !q) { f.overflow = true; return; }
                f.stamps.push_back(q);
                f.cats.push_back(0);
            }
            f.stamps[f.used]->Issue(D3DISSUE_END);
            f.cats[f.used] = closes;
            ++f.used;
        }

        // Charges the time since the last transition to the current category, then stamps the GPU
        // (not when only a CPU-only category opens or closes: the GPU category doesn't change).
        void Transition(bool stamp = true)
        {
            const int64_t now = Now();
            g_frame.cpu[Current()] += now - g_last;
            g_last = now;
            if (stamp) Stamp(GpuCurrent());
        }

        // --- main thread by module (sampled) -----------------------------------------------------
        // A background thread suspends the main thread about a thousand times a second, reads its
        // instruction and stack pointers and resumes it at once; nothing between the two allocates or
        // takes a lock, so the main thread can't be left waiting on the sampler. A sample whose
        // instruction pointer is in Windows or driver code is charged to the first game or extension
        // module found above it on the stack (the first value there that points just past a call into
        // one of them), so each module's row includes the Direct3D, driver and CRT work it calls.
        // Wall-clock sampling: waits count as well as work, like the frame time they explain.
        struct SampledModule
        {
            uintptr_t lo, hi;
            bool      system;
            char      name[48];
        };
        constexpr int kMaxModules = 256;
        constexpr int kUnattributed = kMaxModules;        // counter slot for samples with no module
        constexpr unsigned kMaxStackScan = 8192;          // dwords
        SampledModule g_mods[kMaxModules];                // sorted by lo; rebuilt only while stopped
        int g_modCount = 0;
        volatile LONG g_modSamples[kMaxModules + 1];      // sampler: ++, main thread: read and clear
        volatile LONG g_modSystem[kMaxModules + 1];       // of which the instruction pointer was in system code
        HANDLE g_samplerThread = nullptr;
        HANDLE g_samplerTarget = nullptr;
        volatile LONG g_samplerStop = 0;
        uintptr_t g_stackBase = 0;
        int64_t g_samplerWindowStart = 0;

        bool StartsWithI(const wchar_t* s, const wchar_t* prefix) { return _wcsnicmp(s, prefix, std::wcslen(prefix)) == 0; }

        void BuildModuleTable()
        {
            g_modCount = 0;
            wchar_t windows[MAX_PATH] = {};
            const UINT wl = GetSystemWindowsDirectoryW(windows, MAX_PATH);
            HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
            if (snap == INVALID_HANDLE_VALUE) return;
            MODULEENTRY32W me;
            me.dwSize = sizeof(me);
            for (BOOL ok = Module32FirstW(snap, &me); ok && g_modCount < kMaxModules; ok = Module32NextW(snap, &me))
            {
                SampledModule& m = g_mods[g_modCount++];
                m.lo = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                m.hi = m.lo + me.modBaseSize;
                const wchar_t* name = me.szModule;
                // Windows, its driver store, and the C runtimes (the client ships its own msvcr80): their
                // time belongs to whoever called them.
                m.system = (wl > 0 && wl < MAX_PATH && _wcsnicmp(me.szExePath, windows, wl) == 0) ||
                           StartsWithI(name, L"msvc") || StartsWithI(name, L"ucrtbase") || StartsWithI(name, L"vcruntime") ||
                           StartsWithI(name, L"api-ms-") || _wcsicmp(name, L"dbghelp.dll") == 0;
                if (!WideCharToMultiByte(CP_UTF8, 0, name, -1, m.name, sizeof(m.name), nullptr, nullptr)) m.name[0] = '\0';
                m.name[sizeof(m.name) - 1] = '\0';
                if (char* dot = std::strrchr(m.name, '.')) *dot = '\0';
            }
            CloseHandle(snap);
            std::sort(g_mods, g_mods + g_modCount, [](const SampledModule& a, const SampledModule& b) { return a.lo < b.lo; });
        }

        int FindModule(uintptr_t a)
        {
            int lo = 0, hi = g_modCount - 1;
            while (lo <= hi)
            {
                const int mid = (lo + hi) / 2;
                if (a < g_mods[mid].lo) hi = mid - 1;
                else if (a >= g_mods[mid].hi) lo = mid + 1;
                else return mid;
            }
            return -1;
        }

        // Does r point just past a call instruction? (call rel32, and call r/m32 in its encodings.)
        bool LooksLikeReturn(uintptr_t r, const SampledModule& m)
        {
            if (r < m.lo + 7) return false;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(r);
            if (p[-5] == 0xE8) return true;                                                    // call rel32
            if (p[-2] == 0xFF && ((p[-1] & 0xF8) == 0xD0 ||                                    // call reg
                                  ((p[-1] & 0xF8) == 0x10 && (p[-1] & 7) != 4 && (p[-1] & 7) != 5))) return true; // call [reg]
            if (p[-3] == 0xFF && (((p[-2] & 0xF8) == 0x50 && (p[-2] & 7) != 4) || p[-2] == 0x14)) return true;   // [reg+d8], [sib]
            if (p[-4] == 0xFF && p[-3] == 0x54) return true;                                   // [sib+d8]
            if (p[-6] == 0xFF && (p[-5] == 0x15 || ((p[-5] & 0xF8) == 0x90 && (p[-5] & 7) != 4))) return true; // [d32], [reg+d32]
            if (p[-7] == 0xFF && p[-6] == 0x94) return true;                                   // [sib+d32]
            return false;
        }

        // Runs with the main thread suspended: reads only, no allocation, no locks.
        int AttributeSample(uintptr_t eip, uintptr_t esp, bool& viaSystem)
        {
            viaSystem = false;
            const int at = FindModule(eip);
            if (at >= 0 && !g_mods[at].system) return at;
            viaSystem = true;
            const uintptr_t top = g_stackBase;
            if (!top || esp >= top || (esp & 3) != 0) return -1;
            const uintptr_t end = (std::min)(top, esp + static_cast<uintptr_t>(kMaxStackScan) * 4);
            __try
            {
                for (uintptr_t p = esp; p + 4 <= end; p += 4)
                {
                    const uintptr_t v = *reinterpret_cast<const uint32_t*>(p);
                    const int k = FindModule(v);
                    if (k < 0 || g_mods[k].system) continue;
                    if (LooksLikeReturn(v, g_mods[k])) return k;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
            return -1;
        }

        DWORD WINAPI SamplerMain(void*)
        {
            // A high-resolution timer where Windows has one (1803+), else the normal one (the sample rate
            // then follows the system timer, which only lowers the count, not the shares).
            HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
            if (!timer) timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
            if (timer)
            {
                LARGE_INTEGER due;
                due.QuadPart = -10000; // 1 ms
                if (!SetWaitableTimer(timer, &due, 1, nullptr, nullptr, FALSE)) { CloseHandle(timer); timer = nullptr; }
            }
            while (!g_samplerStop)
            {
                if (timer) WaitForSingleObject(timer, 50);
                else Sleep(1);
                if (g_samplerStop) break;
                if (SuspendThread(g_samplerTarget) == static_cast<DWORD>(-1)) continue;
                CONTEXT ctx;
                std::memset(&ctx, 0, sizeof(ctx));
                ctx.ContextFlags = CONTEXT_CONTROL;
                int at = -1;
                bool viaSystem = false;
                if (GetThreadContext(g_samplerTarget, &ctx)) at = AttributeSample(ctx.Eip, ctx.Esp, viaSystem);
                ResumeThread(g_samplerTarget);
                const int slot = at >= 0 ? at : kUnattributed;
                InterlockedIncrement(&g_modSamples[slot]);
                if (viaSystem) InterlockedIncrement(&g_modSystem[slot]);
            }
            if (timer) CloseHandle(timer);
            return 0;
        }

        void StopSampler()
        {
            if (!g_samplerThread) return;
            InterlockedExchange(&g_samplerStop, 1);
            WaitForSingleObject(g_samplerThread, 2000);
            CloseHandle(g_samplerThread);
            g_samplerThread = nullptr;
            if (g_samplerTarget) CloseHandle(g_samplerTarget);
            g_samplerTarget = nullptr;
        }

        // Main thread only.
        void StartSampler()
        {
            if (g_samplerThread) return;
            BuildModuleTable();
            if (g_modCount == 0) return;
            g_stackBase = reinterpret_cast<uintptr_t>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
            g_samplerTarget = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
            if (!g_samplerTarget) return;
            for (int i = 0; i <= kMaxModules; ++i) { g_modSamples[i] = 0; g_modSystem[i] = 0; }
            g_samplerStop = 0;
            g_samplerWindowStart = Now();
            g_samplerThread = CreateThread(nullptr, 64 * 1024, &SamplerMain, nullptr, 0, nullptr);
            if (!g_samplerThread)
            {
                CloseHandle(g_samplerTarget);
                g_samplerTarget = nullptr;
                return;
            }
            SetThreadPriority(g_samplerThread, THREAD_PRIORITY_HIGHEST);
        }

        // --- the averaged results the panel shows ---------------------------------------------------
        struct Window
        {
            int64_t  cpu[kCount] = {};
            uint64_t draws[kCount] = {}, tris[kCount] = {};
            double   gpu[kCount] = {};     // ms, summed over the frames with GPU results
            unsigned frames = 0, gpuFrames = 0;
            int64_t  frameTicks = 0;
            double   gpuFrameMs = 0.0;
            uint64_t hookCalls[kMaxHooks] = {};
        } g_window;
        struct ModuleRow { char name[48]; double share, systemShare; };
        struct Shown
        {
            double   cpuMs[kCount] = {}, gpuMs[kCount] = {};
            double   draws[kCount] = {}, tris[kCount] = {};
            double   frameMs = 0.0, gpuFrameMs = 0.0;
            unsigned frames = 0, gpuFrames = 0;
            double   hookCalls[kMaxHooks] = {};   // per frame
            std::vector<ModuleRow> modules;        // biggest first
            unsigned samples = 0;
            double   sampleRate = 0.0;             // per second
            bool     valid = false;
        } g_shown;
        int64_t g_frameStart = 0, g_windowStart = 0;

        // Takes the sampler's counts since the last call (main thread).
        void CollectSamples(Shown& s, int64_t now)
        {
            if (!g_samplerThread) return;
            LONG counts[kMaxModules + 1], system[kMaxModules + 1];
            unsigned total = 0;
            for (int i = 0; i <= kMaxModules; ++i)
            {
                counts[i] = InterlockedExchange(&g_modSamples[i], 0);
                system[i] = InterlockedExchange(&g_modSystem[i], 0);
                total += static_cast<unsigned>(counts[i]);
            }
            const double seconds = static_cast<double>(now - g_samplerWindowStart) / static_cast<double>(g_qpcFreq);
            g_samplerWindowStart = now;
            s.samples = total;
            s.sampleRate = seconds > 0.0 ? total / seconds : 0.0;
            if (!total) return;
            for (int i = 0; i <= kMaxModules; ++i)
            {
                if (!counts[i] || (i < kMaxModules && i >= g_modCount)) continue;
                ModuleRow row;
                std::snprintf(row.name, sizeof(row.name), "%s", i == kUnattributed ? "(unattributed)" : g_mods[i].name);
                row.share = static_cast<double>(counts[i]) / total;
                row.systemShare = static_cast<double>(system[i]) / total;
                s.modules.push_back(row);
            }
            std::sort(s.modules.begin(), s.modules.end(), [](const ModuleRow& a, const ModuleRow& b) { return a.share > b.share; });
        }

        void ReadGpuFrame(GpuFrame& f)
        {
            if (!f.pending) return;
            f.pending = false;
            if (f.overflow || !f.disjoint || !f.freq || f.used < 2) return;
            BOOL disjoint = TRUE;
            UINT64 freq = 0;
            if (f.disjoint->GetData(&disjoint, sizeof(disjoint), 0) != S_OK || f.freq->GetData(&freq, sizeof(freq), 0) != S_OK)
            { ++g_gpuNotReady; return; }
            if (disjoint || !freq) { ++g_gpuDisjoint; return; }
            std::vector<UINT64> t(f.used);
            for (unsigned k = 0; k < f.used; ++k)
                if (f.stamps[k]->GetData(&t[k], sizeof(UINT64), 0) != S_OK) { ++g_gpuNotReady; return; }
            const double toMs = 1000.0 / static_cast<double>(freq);
            for (unsigned k = 1; k < f.used; ++k)
            {
                const int c = f.cats[k];
                if (c >= 0 && c < kCount && t[k] >= t[k - 1]) g_window.gpu[c] += static_cast<double>(t[k] - t[k - 1]) * toMs;
            }
            if (t[f.used - 1] >= t[0]) g_window.gpuFrameMs += static_cast<double>(t[f.used - 1] - t[0]) * toMs;
            ++g_window.gpuFrames;
        }

        // Present: closes the frame that just ended, folds it into the window, and opens the next.
        void __cdecl OnFrame(void* /*user*/, const void* /*args*/)
        {
            if (!g_mainThread) g_mainThread = GetCurrentThreadId();
            const int64_t now = Now();

            if (g_frameOn)
            {
                Transition();
                Stamp(GpuCurrent());
                if (g_gpuFrameOn)
                {
                    GpuFrame& f = g_gpuFrames[g_gpuCur];
                    f.disjoint->Issue(D3DISSUE_END);
                    f.freq->Issue(D3DISSUE_END);
                    f.pending = true;
                }
                for (int c = 0; c < kCount; ++c)
                {
                    g_window.cpu[c] += g_frame.cpu[c];
                    g_window.draws[c] += g_frame.draws[c];
                    g_window.tris[c] += g_frame.tris[c];
                }
                for (int k = 0; k < kHookCount; ++k) g_window.hookCalls[k] += g_hookCalls[k];
                g_window.frameTicks += now - g_frameStart;
                ++g_window.frames;
            }
            g_frame = FrameAccum{};
            for (int k = 0; k < kHookCount; ++k) g_hookCalls[k] = 0;
            // This event fires inside the client's flip (and anything else still open): those wrappers
            // close in the next frame, so their categories carry over into it.
            g_depth = 0;
            for (int i = 0; i < g_retDepth && g_depth < kMaxDepth; ++i) g_stack[g_depth++] = kHooks[g_ret[i].slot].category;

            // Publish about once a second.
            if (now - g_windowStart >= g_qpcFreq)
            {
                Shown s;
                if (g_window.frames)
                {
                    const double n = g_window.frames, toMs = 1000.0 / static_cast<double>(g_qpcFreq);
                    for (int c = 0; c < kCount; ++c)
                    {
                        s.cpuMs[c] = static_cast<double>(g_window.cpu[c]) * toMs / n;
                        s.draws[c] = static_cast<double>(g_window.draws[c]) / n;
                        s.tris[c] = static_cast<double>(g_window.tris[c]) / n;
                        if (g_window.gpuFrames) s.gpuMs[c] = g_window.gpu[c] / g_window.gpuFrames;
                    }
                    for (int k = 0; k < kHookCount; ++k) s.hookCalls[k] = static_cast<double>(g_window.hookCalls[k]) / n;
                    s.frameMs = static_cast<double>(g_window.frameTicks) * toMs / n;
                    s.gpuFrameMs = g_window.gpuFrames ? g_window.gpuFrameMs / g_window.gpuFrames : 0.0;
                    s.frames = g_window.frames;
                    s.gpuFrames = g_window.gpuFrames;
                    s.valid = true;
                }
                CollectSamples(s, now);
                g_shown = s;
                g_window = Window{};
                g_windowStart = now;
            }

            // The sampler follows the panel (started and stopped here, on the main thread).
            const bool wantSampler = g_enabled != 0 && g_sample != 0;
            if (wantSampler && !g_samplerThread) StartSampler();
            else if (!wantSampler && g_samplerThread) StopSampler();

            // The next frame.
            g_frameOn = g_enabled != 0;
            g_frameStart = now;
            g_last = now;
            g_gpuFrameOn = false;
            if (!g_frameOn || !g_gpu || !g_gpuSupported) return;
            auto* dev = static_cast<IDirect3DDevice9*>(gx::RawDevice());
            if (!dev) return;
            if (dev != g_gpuDevice)
            {
                ReleaseGpu();
                if (FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP, nullptr)) || FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, nullptr)) ||
                    FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, nullptr)))
                { g_gpuSupported = false; return; }
                g_gpuDevice = dev;
            }
            g_gpuCur = (g_gpuCur + 1) % kGpuFrames;
            GpuFrame& f = g_gpuFrames[g_gpuCur];
            ReadGpuFrame(f); // the oldest frame, about to be reused
            if (!f.disjoint && FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &f.disjoint))) f.disjoint = nullptr;
            if (!f.freq && FAILED(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &f.freq))) f.freq = nullptr;
            if (!f.disjoint || !f.freq) return;
            f.used = 0;
            f.overflow = false;
            f.disjoint->Issue(D3DISSUE_BEGIN);
            g_gpuFrameOn = true;
            Stamp(-1);
        }
    }

    // CPU time goes to the innermost category; a GPU timestamp is issued only when the innermost
    // category that owns GPU time changes (it closes that one's interval).
    void Enter(int category)
    {
        if (!g_frameOn || !OnMainThread()) return;
        if (g_depth >= kMaxDepth) return;
        Transition(!CpuOnly(category));
        g_stack[g_depth++] = category;
    }

    void CountDraws(unsigned draws, uint64_t triangles)
    {
        if (!g_frameOn || !OnMainThread()) return;
        g_frame.draws[Current()] += draws;
        g_frame.tris[Current()] += triangles;
    }

    void Leave(int category)
    {
        if (!g_frameOn || !OnMainThread()) return;
        // The topmost matching entry (scopes that don't nest perfectly, e.g. an event-bracketed one
        // around a hooked function, still come out right).
        for (int i = g_depth - 1; i >= 0; --i)
            if (g_stack[i] == category)
            {
                Transition(!CpuOnly(category));
                for (int k = i; k + 1 < g_depth; ++k) g_stack[k] = g_stack[k + 1];
                --g_depth;
                return;
            }
    }
}

// The C side of the stubs (no FPU or SSE state is used here: integer timing only, the queries are
// device calls -- the leave stub saves the FPU state around this anyway).
extern "C" int __cdecl LaProfHookEnter(int slot, uintptr_t ret)
{
    using namespace wxl_livingazeroth::prof;
    if (!g_frameOn || !OnMainThread() || g_retDepth >= kMaxDepth) return 0;
    g_ret[g_retDepth++] = { slot, ret };
    ++g_hookCalls[slot];
    Enter(kHooks[slot].category);
    return 1;
}

extern "C" uintptr_t __cdecl LaProfHookLeave(int /*slot*/)
{
    using namespace wxl_livingazeroth::prof;
    const RetEntry e = g_ret[--g_retDepth];
    Leave(kHooks[e.slot].category);
    return e.ret;
}

// Plain C globals, so the inline assembly below can name them: per stub, the next link of its chain,
// and its leave stub.
extern "C" void* g_laProfOriginal[24] = {};
extern "C" void* g_laProfLeave[24] = {};

// Each hooked function gets a naked stub. On entry (main thread, profiler on) it records the caller's
// return address, opens the category and replaces the return address with its leave stub; then it
// jumps to the next link of the chain with every register as it found them. The leave stub closes the
// category and returns to the saved address, keeping eax/edx and the FPU state (a float result)
// intact. No signature or calling convention is involved.
//
// pushfd + pushad = 36 bytes, so the caller's return address sits at [esp + 36] in the enter stub.
// The leave stub pushes a slot for the return address first, then flags, registers and the 108-byte
// FPU state: the slot is at [esp + 36] again once the FPU state is popped.
#define LA_PROF_STUBS(N)                                                                               \
    extern "C" __declspec(naked) void LaProfLeave##N()                                                \
    {                                                                                                  \
        __asm push eax                                                                                 \
        __asm pushfd                                                                                   \
        __asm pushad                                                                                   \
        __asm sub esp, 108                                                                             \
        __asm fnsave [esp]                                                                             \
        __asm push N                                                                                   \
        __asm call LaProfHookLeave                                                                     \
        __asm add esp, 4                                                                               \
        __asm frstor [esp]                                                                             \
        __asm add esp, 108                                                                             \
        __asm mov dword ptr [esp + 36], eax                                                            \
        __asm popad                                                                                    \
        __asm popfd                                                                                    \
        __asm ret                                                                                      \
    }                                                                                                  \
    extern "C" __declspec(naked) void LaProfEnter##N()                                                \
    {                                                                                                  \
        __asm pushfd                                                                                   \
        __asm pushad                                                                                   \
        __asm push dword ptr [esp + 36]                                                                \
        __asm push N                                                                                   \
        __asm call LaProfHookEnter                                                                     \
        __asm add esp, 8                                                                               \
        __asm mov ecx, dword ptr [esp + 36]                                                            \
        __asm test eax, eax                                                                            \
        __asm cmovnz ecx, dword ptr [g_laProfLeave + N * 4]                                            \
        __asm mov dword ptr [esp + 36], ecx                                                            \
        __asm popad                                                                                    \
        __asm popfd                                                                                    \
        __asm jmp dword ptr [g_laProfOriginal + N * 4]                                                 \
    }

LA_PROF_STUBS(0)  LA_PROF_STUBS(1)  LA_PROF_STUBS(2)  LA_PROF_STUBS(3)
LA_PROF_STUBS(4)  LA_PROF_STUBS(5)  LA_PROF_STUBS(6)  LA_PROF_STUBS(7)
LA_PROF_STUBS(8)  LA_PROF_STUBS(9)  LA_PROF_STUBS(10) LA_PROF_STUBS(11)
LA_PROF_STUBS(12) LA_PROF_STUBS(13) LA_PROF_STUBS(14) LA_PROF_STUBS(15)
LA_PROF_STUBS(16) LA_PROF_STUBS(17) LA_PROF_STUBS(18) LA_PROF_STUBS(19)
LA_PROF_STUBS(20) LA_PROF_STUBS(21) LA_PROF_STUBS(22) LA_PROF_STUBS(23)

namespace wxl_livingazeroth::prof
{
    namespace
    {
        void (*const kEnterStubs[kMaxHooks])() = {
            &LaProfEnter0, &LaProfEnter1, &LaProfEnter2, &LaProfEnter3, &LaProfEnter4, &LaProfEnter5, &LaProfEnter6, &LaProfEnter7,
            &LaProfEnter8, &LaProfEnter9, &LaProfEnter10, &LaProfEnter11, &LaProfEnter12, &LaProfEnter13, &LaProfEnter14, &LaProfEnter15,
            &LaProfEnter16, &LaProfEnter17, &LaProfEnter18, &LaProfEnter19, &LaProfEnter20, &LaProfEnter21, &LaProfEnter22, &LaProfEnter23 };
        void (*const kLeaveStubs[kMaxHooks])() = {
            &LaProfLeave0, &LaProfLeave1, &LaProfLeave2, &LaProfLeave3, &LaProfLeave4, &LaProfLeave5, &LaProfLeave6, &LaProfLeave7,
            &LaProfLeave8, &LaProfLeave9, &LaProfLeave10, &LaProfLeave11, &LaProfLeave12, &LaProfLeave13, &LaProfLeave14, &LaProfLeave15,
            &LaProfLeave16, &LaProfLeave17, &LaProfLeave18, &LaProfLeave19, &LaProfLeave20, &LaProfLeave21, &LaProfLeave22, &LaProfLeave23 };

        // Draws, at the client's own Gx draw (batch: +0x08 index count; core offsets/engine/Gx.hpp).
        using DeviceDrawFn = void(__fastcall*)(void* device, void* edx, uint32_t* batch, int indexed);
        DeviceDrawFn g_origDraw = nullptr;
        void __fastcall hkDeviceDraw(void* device, void* edx, uint32_t* batch, int indexed)
        {
            if (g_frameOn && OnMainThread())
            {
                const int c = Current();
                ++g_frame.draws[c];
                if (batch) g_frame.tris[c] += batch[2] / 3;
            }
            g_origDraw(device, edx, batch, indexed);
        }

        void __cdecl OnWorldRenderEnd(void*, const void*) { Enter(kUi); }
        void __cdecl OnEndScene(void*, const void*) { Leave(kUi); }

        bool CopyToClipboard(const std::string& text)
        {
            if (!OpenClipboard(nullptr)) return false;
            EmptyClipboard();
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
            bool ok = false;
            if (mem)
            {
                std::memcpy(GlobalLock(mem), text.c_str(), text.size() + 1);
                GlobalUnlock(mem);
                ok = SetClipboardData(CF_TEXT, mem) != nullptr;
                if (!ok) GlobalFree(mem);
            }
            CloseClipboard();
            return ok;
        }

        // The name a hook shows in the calls-per-frame line.
        const char* ShortHookName(int k)
        {
            const char* p = kHooks[k].point;
            return std::strncmp(p, "prof: ", 6) == 0 ? p + 6 : p;
        }

        std::string Report()
        {
            const Shown& s = g_shown;
            std::string out;
            char line[256];
            std::snprintf(line, sizeof(line), "frame: %.2f ms CPU (%.0f fps)%s", s.frameMs, s.frameMs > 0.0 ? 1000.0 / s.frameMs : 0.0,
                          s.gpuFrames ? "" : "; GPU: no results yet (or timing off / unsupported)");
            out += line;
            if (s.gpuFrames)
            {
                std::snprintf(line, sizeof(line), "; GPU frame span %.2f ms (%u of %u frames read back)", s.gpuFrameMs, s.gpuFrames, s.frames);
                out += line;
            }
            out += "\n";
            out += "category                            draws   ktris   CPU ms   GPU ms\n";
            // Biggest first, by whichever of CPU and GPU is larger.
            int order[kCount];
            for (int c = 0; c < kCount; ++c) order[c] = c;
            std::sort(order, order + kCount, [&](int a, int b) { return std::max(s.cpuMs[a], s.gpuMs[a]) > std::max(s.cpuMs[b], s.gpuMs[b]); });
            double draws = 0.0, tris = 0.0;
            for (int k = 0; k < kCount; ++k)
            {
                const int c = order[k];
                draws += s.draws[c]; tris += s.tris[c];
                if (s.cpuMs[c] < 0.005 && s.gpuMs[c] < 0.005 && s.draws[c] < 0.5) continue;
                std::snprintf(line, sizeof(line), "%-34s %6.0f %7.1f %8.2f %8.2f\n", kNames[c], s.draws[c], s.tris[c] / 1000.0, s.cpuMs[c], s.gpuMs[c]);
                out += line;
            }
            std::snprintf(line, sizeof(line), "%-34s %6.0f %7.1f\n", "total", draws, tris / 1000.0);
            out += line;
            // The grass rows together (their GPU time is all in "grass: client rest").
            double grassCpu = s.cpuMs[kDetail], grassDraws = s.draws[kDetail], grassTris = s.tris[kDetail];
            for (int c = kDetailBake; c <= kOursGrassDraw; ++c) { grassCpu += s.cpuMs[c]; grassDraws += s.draws[c]; grassTris += s.tris[c]; }
            std::snprintf(line, sizeof(line), "%-34s %6.0f %7.1f %8.2f %8.2f\n", "grass, all rows", grassDraws, grassTris / 1000.0,
                          grassCpu, s.gpuMs[kDetail]);
            out += line;

            if (!s.modules.empty())
            {
                std::snprintf(line, sizeof(line), "\nmain thread by module (%u samples, %.0f/s; a module's row includes the Windows/driver code it calls)\n",
                              s.samples, s.sampleRate);
                out += line;
                out += "module                              share   ms/frame   in Windows/driver\n";
                int shown = 0;
                for (const ModuleRow& m : s.modules)
                {
                    if (shown >= 16 || (m.share < 0.003 && shown >= 6)) break;
                    std::snprintf(line, sizeof(line), "%-34s %5.1f%% %9.2f %10.1f%%\n", m.name, 100.0 * m.share, m.share * s.frameMs, 100.0 * m.systemShare);
                    out += line;
                    ++shown;
                }
            }

            out += "\ncalls per frame:";
            for (int k = 0; k < kHookCount; ++k)
            {
                if (!g_hooked[k]) continue;
                std::snprintf(line, sizeof(line), " %s %.1f;", ShortHookName(k), s.hookCalls[k]);
                out += line;
            }
            out += "\n";
            return out;
        }

        void __cdecl Panel(void*)
        {
            g_api->UiCheckbox("profile the frame (a little overhead while on)", &g_enabled);
            g_api->UiCheckbox("GPU time per category (timestamp queries; read back 3 frames late)", &g_gpu);
            g_api->UiCheckbox("main thread by module (sampled about 1000 times a second; ~1-3% overhead)", &g_sample);
            if (!g_gpuSupported) g_api->UiText("GPU timestamps are not supported by this device.");
            unsigned hooked = 0;
            for (int k = 0; k < kHookCount; ++k) hooked += g_hooked[k] ? 1 : 0;
            char line[640];
            std::snprintf(line, sizeof(line), "%u of %d client passes wrapped; draw counter %s. GPU frames not ready in time: %u, disjoint: %u. "
                          "Times are exclusive (nested categories are not counted twice), averaged over about a second. "
                          "GPU times are spans on the GPU's timeline, waiting included: when the CPU is the limit "
                          "(the GPU span about equals the CPU frame), the GPU's idle time lands in 'other', and the CPU column is the one to read.",
                          hooked, kHookCount, g_origDraw ? "on" : "off", g_gpuNotReady, g_gpuDisjoint);
            g_api->UiText(line);
            if (!g_enabled) { g_api->UiText("(off)"); return; }
            if (!g_shown.valid) { g_api->UiText("(collecting...)"); return; }
            const std::string report = Report();
            if (g_api->UiButton("Copy to clipboard##profcopy")) CopyToClipboard(report);
            size_t at = 0;
            while (at < report.size())
            {
                const size_t end = report.find('\n', at);
                const std::string row = report.substr(at, end == std::string::npos ? std::string::npos : end - at);
                g_api->UiText(row.c_str());
                if (end == std::string::npos) break;
                at = end + 1;
            }
        }
    }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_qpcFreq = f.QuadPart > 0 ? f.QuadPart : 1;
        for (int k = 0; k < kHookCount; ++k)
        {
            g_laProfLeave[k] = reinterpret_cast<void*>(kLeaveStubs[k]);
            void* const stub = reinterpret_cast<void*>(kEnterStubs[k]);
            // First in every chain: everything else on the same point runs inside the measurement. A chain
            // that's already live (core installs some of its own detours at start-up) refuses a new head,
            // so then behind its current head.
            if (kHooks[k].address)
                g_hooked[k] = api->HookAttach(kHooks[k].point, kHooks[k].address, stub, &g_laProfOriginal[k], -1000) != 0 ||
                              api->HookAttach(kHooks[k].point, kHooks[k].address, stub, &g_laProfOriginal[k], 1000) != 0;
            else
                g_hooked[k] = api->HookAttachByName(kHooks[k].point, stub, &g_laProfOriginal[k], -1000) != 0 ||
                              api->HookAttachByName(kHooks[k].point, stub, &g_laProfOriginal[k], 1000) != 0;
            if (!g_hooked[k])
            {
                char msg[160];
                std::snprintf(msg, sizeof(msg), "profiler: hook point '%s' not found; that category stays empty", kHooks[k].point);
                api->Log(WXL_LOG_WARN, "wxl-seyris-living-azeroth", msg);
            }
        }
        // In front on the draw too. Core installs its normal-phase features (m2native-indices among
        // them, at the default priority) after the extensions' chains are live, and a live chain
        // refuses a new head: a link of ours behind the default would make core's feature fail. In
        // front, core's link goes in behind ours. Behind only if the chain is already live here.
        if (!api->HookAttachByName("Gx.DeviceDraw", reinterpret_cast<void*>(&hkDeviceDraw), reinterpret_cast<void**>(&g_origDraw), -1000) &&
            !api->HookAttachByName("Gx.DeviceDraw", reinterpret_cast<void*>(&hkDeviceDraw), reinterpret_cast<void**>(&g_origDraw), 1000))
            g_origDraw = nullptr;
        api->Subscribe(static_cast<uint32_t>(ev::Event::OnFrame), &OnFrame, nullptr);
        api->Subscribe(static_cast<uint32_t>(ev::Event::OnWorldRenderEnd), &OnWorldRenderEnd, nullptr);
        api->Subscribe(static_cast<uint32_t>(ev::Event::OnEndScene), &OnEndScene, nullptr);
        pages::AddPanel(kPanelTitle, &Panel);
    }

    void OnDeviceLost() { ReleaseGpu(); }
}
