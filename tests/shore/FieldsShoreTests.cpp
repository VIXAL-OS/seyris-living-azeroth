// Offline integration tests for the shore worker inside the real env/Fields.cpp: the same frames run
// with the worker off (the original UpdateShore) and on must leave every cell's shore values bit for
// bit the same, across grid moves (toroidal slots), late-loading terrain, map changes, world leave,
// worker errors and the setting switching. The world is a stub (terrain, liquids, materials); nothing
// of the game or the WXL core is involved. Also measures the original pass's cost per call.
#include "env/Climate.hpp"
#include "env/Fields.hpp"
#include "env/Regional.hpp"
#include "env/Shelter.hpp"
#include "env/ShoreField.hpp"
#include "env/TerrainHeight.hpp"
#include "env/Wind.hpp"
#include "features/GrassPerf.hpp"
#include "features/GroundMaterialTable.hpp"
#include "features/SurfaceCoverTable.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <float.h>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace la = wxl_livingazeroth;
namespace sh = wxl_livingazeroth::fields::shore;

// --- the stub world -------------------------------------------------------------------------------
namespace
{
    struct World
    {
        int  map = 571;
        bool lateA = false, lateB = false; // two patches of terrain that load later
        bool nanCell = false;              // one cell's height comes back NaN
        bool realClock = false;            // grassperf::Now: real time (benchmarks) or frozen (no fill budget)
    } g_world;

    bool In(float x, float y, float x0, float y0, float x1, float y1) { return x >= x0 && x < x1 && y >= y0 && y < y1; }
    bool Disc(float x, float y, float cx, float cy, float r) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) < r * r; }
    float Ground(float x, float y) { return 10.0f + 2.0f * std::sin(x / 17.0f) + 1.5f * std::cos(y / 23.0f) - 0.004f * (x - 1000.0f); }
    bool Loaded(float x, float y)
    {
        if (In(x, y, 1000.0f, 1080.0f, 1008.0f, 1086.0f)) return false;            // a hole: never there
        if (!g_world.lateA && In(x, y, 1086.0f, 1040.0f, 1112.0f, 1066.0f)) return false;
        if (!g_world.lateB && In(x, y, 900.0f, 930.0f, 924.0f, 956.0f)) return false;
        return true;
    }
}

namespace wxl_livingazeroth::terrain
{
    bool HeightAt(float x, float y, float& outZ)
    {
        if (!Loaded(x, y)) return false;
        outZ = Ground(x, y);
        if (g_world.nanCell && std::floor(x / 2.0f) == 500.0f && std::floor(y / 2.0f) == 502.0f) outZ = std::numeric_limits<float>::quiet_NaN();
        return true;
    }
    bool LiquidAt(float x, float y, float& outZ, uint32_t& outType)
    {
        if (g_world.map != 571) return false; // the second map is dry
        if (Disc(x, y, 975.0f, 1015.0f, 26.0f)) { outZ = 12.5f; outType = 1; return true; }        // a lake
        if (Disc(x, y, 1050.0f, 955.0f, 8.0f)) { outZ = Ground(x, y) + 0.6f; outType = 19; return true; } // magma
        if (In(x, y, 1092.0f, 1046.0f, 1106.0f, 1060.0f)) { outZ = Ground(x, y) + 1.0f; outType = 2; return true; } // in late patch A
        return false;
    }
    int  LiquidCategoryOf(uint32_t liquidType) { return liquidType == 19 ? kLiquidMagma : (liquidType == 2 ? kLiquidOcean : kLiquidWater); }
    bool LayerWeightsAt(float x, float y, LayerWeights& out, bool /*swapAxes*/)
    {
        out = LayerWeights{};
        out.layers = 1;
        out.weight[0] = 1.0f;
        out.surface[0].area = 1;
        out.surface[0].texture = (g_world.map == 571 && x >= 1012.0f && x < 1016.0f) ? "lava" : "dirt"; // a lava stream
        return true;
    }
    bool SurfaceAt(float, float, Surface&) { return false; }
}
namespace wxl_livingazeroth::materials
{
    uint32_t Select(uint32_t, int, const char* texturePath, uint32_t, int) { return texturePath && std::strcmp(texturePath, "lava") == 0 ? 7 : 0; }
    uint32_t SelectLiquid(uint32_t, uint32_t, int) { return 0; }
    Values Get(uint32_t materialId)
    {
        Values v;
        if (materialId == 7) { v.hasTemperature = true; v.temperature = 600.0f; v.restMoisture = 0.1f; v.absorbency = 0.2f; }
        return v;
    }
    uint32_t Generation() { return 1; }
}
namespace wxl_livingazeroth::covertable
{
    Values Resolve(uint32_t, int, const char*, uint32_t, int) { return Values{}; }
    bool   IsSnow(uint32_t, uint32_t, int) { return false; }
}
namespace wxl_livingazeroth::regional
{
    uint32_t ZoneOf(uint32_t) { return 1; }
    uint32_t PlayerZone() { return 1; }
    float SnowDepth(int, uint32_t) { return 0.0f; }
    float PaintedKeep(int, uint32_t) { return 1.0f; }
    float RainSoak(int, uint32_t) { return 0.2f; }
    float ZoneTemperature(int, uint32_t) { return 10.0f; }
    float DryingRate(float, float, float) { return 0.001f; }
    const ZoneMap& Map() { static ZoneMap m; return m; }
    double Now() { return 0.0; }
}
namespace wxl_livingazeroth::shelter
{
    float Openness(const float*) { return 1.0f; }
}
namespace wxl_livingazeroth::climate
{
    const Row& For(uint32_t, int) { static Row r; return r; }
    Row At(float, float) { return Row{}; }
    Breakdown Temperature(const Row&) { Breakdown b; b.total = 15.0f; return b; }
    float TemperatureAt(uint32_t, int) { return 15.0f; }
    float SunWarming(const float*, float) { return 0.0f; }
    const Weather& CurrentWeather() { static Weather w; return w; }
    uint32_t Generation() { return 1; }
}
namespace wxl_livingazeroth::wind
{
    float SteadyGround() { return 0.3f; }
}
namespace wxl_livingazeroth::grassperf
{
    double Now()
    {
        if (!g_world.realClock) return 0.0;
        static const double perMs = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return static_cast<double>(f.QuadPart) / 1000.0; }();
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return static_cast<double>(t.QuadPart) / perMs;
    }
}

// --- harness --------------------------------------------------------------------------------------
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__)); } while (false)

namespace
{
    using Clock = std::chrono::steady_clock;
    unsigned g_passed = 0;
    template <class F> void Test(const char* name, F f)
    {
        f();
        ++g_passed;
        std::printf("PASS %s\n", name);
    }
    bool Bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

    // Worker calculations the tests can watch, hold or break (plain functions: sh::ComputeFn).
    std::atomic<DWORD> g_workerTid{ 0 };
    std::atomic<int>   g_computeCalls{ 0 };
    struct Gate
    {
        std::mutex m;
        std::condition_variable cv;
        bool closed = false, entered = false;
        void Close() { std::lock_guard<std::mutex> l(m); closed = true; entered = false; }
        void Open() { std::lock_guard<std::mutex> l(m); closed = false; cv.notify_all(); }
        void Enter()
        {
            std::unique_lock<std::mutex> l(m);
            entered = true;
            cv.notify_all();
            if (!cv.wait_for(l, std::chrono::seconds(10), [&] { return !closed; })) throw std::runtime_error("gate held too long");
        }
        void WaitEntered()
        {
            std::unique_lock<std::mutex> l(m);
            CHECK(cv.wait_for(l, std::chrono::seconds(10), [&] { return entered; }));
        }
    } g_gate;
    sh::Result Watched(const sh::Snapshot& s)
    {
        g_workerTid = GetCurrentThreadId();
        ++g_computeCalls;
        g_gate.Enter();
        return sh::Compute(s);
    }
    sh::Result Broken(const sh::Snapshot&)
    {
        g_workerTid = GetCurrentThreadId();
        ++g_computeCalls;
        throw std::runtime_error("deliberate failure");
    }

    la::world::Snapshot At(float x, float y, int map = 571)
    {
        la::world::Snapshot s;
        s.inWorld = true;
        s.playerPos[0] = x; s.playerPos[1] = y; s.playerPos[2] = 10.0f;
        s.mapId = map;
        return s;
    }
    la::world::Snapshot Away() { return la::world::Snapshot{}; }

    void Frame(float dt, const la::world::Snapshot& s)
    {
        g_world.map = s.mapId;
        la::fields::Update(dt, s);
    }

    // Leaves the world (stops the worker, resets the grid) and starts a run in the given mode.
    void Fresh(bool worker, sh::ComputeFn compute = nullptr)
    {
        Frame(0.0f, Away());
        g_world = World{};
        g_gate.Open();
        g_workerTid = 0;
        g_computeCalls = 0;
        sh::SetComputeForTests(compute);
        la::fields::SetShoreWorker(false);
        la::fields::SetShoreWorker(worker);
        CHECK(la::fields::GetStats().shoreWorker != la::fields::ShoreWorkerState::Running);
    }

    // Frames (dt 0: no new pass) until the worker's result is in. Nothing to wait for without it.
    void Settle(const la::world::Snapshot& s)
    {
        const auto end = Clock::now() + std::chrono::seconds(10);
        for (;;)
        {
            Frame(0.0f, s);
            if (!la::fields::GetStats().shorePending) return;
            if (Clock::now() > end) throw std::runtime_error("worker result never arrived");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // Enters the place: everything fills (frozen clock: no budget), a pass runs or is queued.
    void Converge(const la::world::Snapshot& s)
    {
        Frame(0.5f, s);
        Settle(s);
    }

    // Every cell of the grid around the player, as the panel sees it.
    struct State
    {
        int firstI = 0, firstJ = 0;
        std::vector<la::fields::MoistureDetail> cells;
    };
    State Capture(const la::world::Snapshot& s)
    {
        State st;
        st.firstI = static_cast<int>(std::floor(s.playerPos[0] / 2.0f)) - 64;
        st.firstJ = static_cast<int>(std::floor(s.playerPos[1] / 2.0f)) - 64;
        st.cells.resize(128 * 128);
        for (int b = 0; b < 128; ++b)
            for (int a = 0; a < 128; ++a)
            {
                const float pos[3] = { (st.firstI + a + 0.5f) * 2.0f, (st.firstJ + b + 0.5f) * 2.0f, 10.0f };
                st.cells[b * 128 + a] = la::fields::Moisture(pos);
            }
        return st;
    }
    // All the shore pass's fields (Moisture shows the cell's own liquid instead of the nearest for
    // submerged cells; there the near fields are covered by the unit tests).
    bool SameShore(const la::fields::MoistureDetail& x, const la::fields::MoistureDetail& y)
    {
        return x.submerged == y.submerged && Bits(x.waterDistance, y.waterDistance) && Bits(x.heightAboveWater, y.heightAboveWater) &&
               Bits(x.shore, y.shore) && x.liquid == y.liquid && Bits(x.liquidMoisture, y.liquidMoisture) && x.liquidHot == y.liquidHot &&
               Bits(x.liquidTemperature, y.liquidTemperature) && Bits(x.heat, y.heat) && Bits(x.groundHeat, y.groundHeat) &&
               Bits(x.groundHeatTemperature, y.groundHeatTemperature) && Bits(x.groundHeatDistance, y.groundHeatDistance) &&
               Bits(x.hotShare, y.hotShare);
    }
    unsigned Differences(const State& a, const State& b, bool (*skip)(int i, int j) = nullptr)
    {
        CHECK(a.firstI == b.firstI && a.firstJ == b.firstJ);
        unsigned n = 0;
        for (int q = 0; q < 128 * 128; ++q)
        {
            if (skip && skip(a.firstI + q % 128, a.firstJ + q / 128)) continue;
            n += !SameShore(a.cells[q], b.cells[q]);
        }
        return n;
    }
    void Same(const State& a, const State& b) { CHECK(Differences(a, b) == 0); }

    struct Shape { unsigned water = 0, inRange = 0, hot = 0, heated = 0, unloaded = 0; };
    Shape ShapeOf(const State& s)
    {
        Shape k;
        for (const auto& c : s.cells)
        {
            k.water += c.submerged; k.inRange += !c.submerged && c.waterDistance > 0.0f; k.hot += c.hotShare >= 0.5f;
            k.heated += c.groundHeatDistance > 0.0f; k.unloaded += c.rest == 0.0f;
        }
        return k;
    }

    // A handle taken while the thread is known to run (thread ids get reused once it's gone).
    struct Held
    {
        HANDLE h = nullptr;
        explicit Held(DWORD tid) : h(tid ? OpenThread(SYNCHRONIZE, FALSE, tid) : nullptr) { CHECK(h != nullptr); }
        ~Held() { if (h) CloseHandle(h); }
        Held(const Held&) = delete;
        Held& operator=(const Held&) = delete;
        bool Gone() const { return WaitForSingleObject(h, 0) == WAIT_OBJECT_0; }
    };

    struct Timing { double median = 0, p95 = 0, max = 0; };
    Timing Summarize(std::vector<double> ms)
    {
        std::sort(ms.begin(), ms.end());
        return { ms[ms.size() / 2], ms[(ms.size() * 95) / 100], ms.back() };
    }

    using Scenario = std::function<State()>;
    // The same frames with the worker off, then on: identical shore values.
    State Both(const Scenario& run, sh::ComputeFn compute = nullptr)
    {
        Fresh(false);
        const State sync = run();
        Fresh(true, compute);
        const State worker = run();
        Same(sync, worker);
        return sync;
    }
}

int main()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try
    {
        const auto home = At(1000.0f, 1000.0f);

        Test("the stub world has what the shore pass is about", [&]
        {
            Fresh(false);
            Converge(home);
            const Shape k = ShapeOf(Capture(home));
            const la::fields::Stats st = la::fields::GetStats();
            std::printf("     world: %u cells loaded, %u unloaded, %u under liquid, %u with a liquid in range, %u hot ground, %u heated by it\n",
                        st.filled, k.unloaded, k.water, k.inRange, k.hot, k.heated);
            CHECK(st.filled > 15000 && st.filled < 128 * 128 && k.unloaded == 128 * 128 - st.filled);
            CHECK(k.water > 100 && k.inRange > 5000 && k.hot >= 256 && k.heated > 2000);
        });

        Test("parity: worker and original UpdateShore leave identical shore values", [&]
        {
            const unsigned applied = la::fields::GetStats().shoreApplied, stale = la::fields::GetStats().shoreStale;
            Both([&] { Converge(home); return Capture(home); });
            const la::fields::Stats st = la::fields::GetStats();
            CHECK(st.shoreWorker == la::fields::ShoreWorkerState::Running && st.shoreApplied == applied + 1 && st.shoreStale == stale);
            Fresh(false);
            Converge(home);
            const la::fields::Stats sy = la::fields::GetStats();
            CHECK(sy.water == st.water && sy.hotGround == st.hotGround && sy.water > 0 && sy.hotGround > 0);
        });

        Test("the calculation runs on another thread", [&]
        {
            Fresh(true, &Watched);
            g_workerTid = 0;
            Converge(home);
            CHECK(g_workerTid != 0 && g_workerTid != GetCurrentThreadId());
        });

        Test("parity across grid moves (toroidal slots wrap)", [&]
        {
            Both([&]
            {
                Converge(home);
                Converge(At(1040.0f, 1000.0f));
                Converge(At(1012.0f, 1030.0f));
                Converge(At(950.0f, 960.0f));
                return Capture(At(950.0f, 960.0f));
            });
        });

        Test("parity under the game thread's rounding mode (copied to the worker)", [&]
        {
            unsigned int x87 = 0, sse = 0;
            __control87_2(_RC_CHOP, _MCW_RC, &x87, &sse);
            const State chop = Both([&] { Converge(home); return Capture(home); });
            __control87_2(_RC_NEAR, _MCW_RC, &x87, &sse);
            Fresh(false);
            Converge(home);
            CHECK(Differences(chop, Capture(home)) > 0); // the mode matters to these numbers
        });

        Test("while a result is on its way the last values stay; then it applies", [&]
        {
            // Original: patch A (with water) loads, the next half-second pass takes it in.
            Fresh(false);
            Converge(home);
            const State before = Capture(home);
            g_world.lateA = true;
            Frame(0.0f, home);
            Frame(0.5f, home);
            const State after = Capture(home);
            const unsigned changed = Differences(before, after, [](int i, int j) { return i >= 543 && i < 556 && j >= 520 && j < 533; });
            CHECK(changed > 50); // cells outside the patch see its water

            Fresh(true, &Watched);
            Converge(home);
            Same(before, Capture(home));
            g_gate.Close();
            g_world.lateA = true;
            Frame(0.0f, home);
            Frame(0.5f, home); // queued; the worker is held
            g_gate.WaitEntered();
            for (int k = 0; k < 5; ++k) Frame(0.0f, home);
            CHECK(la::fields::GetStats().shorePending);
            CHECK(Differences(before, Capture(home), [](int i, int j) { return i >= 543 && i < 556 && j >= 520 && j < 533; }) == 0);
            g_gate.Open();
            Settle(home);
            Same(after, Capture(home));
        });

        Test("cells filled after the copy keep their fill's values; the rest applies; a new pass follows", [&]
        {
            // Original: pass with patch A, then patch B fills before the next pass.
            Fresh(false);
            Converge(home);
            g_world.lateA = true;
            Frame(0.0f, home);
            Frame(0.5f, home);
            g_world.lateB = true;
            Frame(0.0f, home);
            const State middle = Capture(home);
            Frame(0.5f, home);
            const State last = Capture(home);

            Fresh(true, &Watched);
            Converge(home);
            g_gate.Close();
            g_world.lateA = true;
            Frame(0.0f, home);
            Frame(0.5f, home);     // copy taken with A, without B
            g_gate.WaitEntered();
            g_world.lateB = true;
            Frame(0.0f, home);     // B fills while the worker still works: the inputs moved on
            const unsigned appliedBefore = la::fields::GetStats().shoreApplied;
            g_gate.Open();
            Settle(home);
            CHECK(la::fields::GetStats().shoreApplied == appliedBefore + 1);
            Same(middle, Capture(home));
            Frame(0.5f, home);     // still dirty: the next pass is queued on schedule
            CHECK(la::fields::GetStats().shorePending);
            Settle(home);
            Same(last, Capture(home));
        });

        Test("a result for a grid that moved is dropped (origin)", [&]
        {
            const auto there = At(1030.0f, 1000.0f); // 15 cells: the grid recentres
            Fresh(false);
            Converge(home);
            g_world.lateA = true;
            Frame(0.0f, home);
            Frame(0.0f, there);   // no pass in between: what a dropped result leaves
            const State dropped = Capture(there);
            Frame(0.5f, there);
            const State last = Capture(there);

            Fresh(true, &Watched);
            Converge(home);
            g_gate.Close();
            g_world.lateA = true;
            Frame(0.0f, home);
            Frame(0.5f, home);    // copied at the old origin
            g_gate.WaitEntered();
            const unsigned applied = la::fields::GetStats().shoreApplied, stale = la::fields::GetStats().shoreStale;
            Frame(0.0f, there);   // recentred before the result came back
            g_gate.Open();
            Settle(there);
            CHECK(la::fields::GetStats().shoreApplied == applied && la::fields::GetStats().shoreStale == stale + 1);
            Same(dropped, Capture(there));
            Frame(0.5f, there);
            Settle(there);
            Same(last, Capture(there));
        });

        Test("a result from before a map change is dropped (map, epoch)", [&]
        {
            const auto other = At(1000.0f, 1000.0f, 1); // same place, another (dry) map
            Fresh(false);
            Converge(home);
            Frame(0.0f, other);
            const State fresh = Capture(other);
            Frame(0.5f, other);
            const State last = Capture(other);
            CHECK(ShapeOf(last).inRange == 0 && ShapeOf(last).water == 0);

            Fresh(true, &Watched);
            Converge(home);
            g_gate.Close();
            g_world.lateA = true;
            Frame(0.0f, home);
            Frame(0.5f, home);
            g_gate.WaitEntered();
            const unsigned applied = la::fields::GetStats().shoreApplied, stale = la::fields::GetStats().shoreStale;
            g_world.lateA = false;
            Frame(0.0f, other);   // map change: reset, new epoch
            g_gate.Open();
            Settle(other);
            CHECK(la::fields::GetStats().shoreApplied == applied && la::fields::GetStats().shoreStale == stale + 1);
            Same(fresh, Capture(other));
            Frame(0.5f, other);
            Settle(other);
            Same(last, Capture(other));
        });

        Test("world leave stops and joins the worker mid-calculation; it restarts when needed", [&]
        {
            Fresh(false);
            Converge(home);
            g_world.lateA = true;
            Frame(0.0f, home);
            Frame(0.5f, home);
            const State reference = Capture(home);

            Fresh(true, &Watched);
            Converge(home);
            const Held first(g_workerTid);
            CHECK(!first.Gone());
            g_gate.Close();
            g_world.lateA = true;
            Frame(0.0f, home);
            Frame(0.5f, home);
            g_gate.WaitEntered();
            std::thread opener([] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); g_gate.Open(); });
            la::fields::OnWorldLeave(); // joins: returns once the held calculation finished
            CHECK(first.Gone());
            opener.join();
            CHECK(la::fields::GetStats().shoreWorker == la::fields::ShoreWorkerState::Off && !la::fields::GetStats().shorePending);
            // Back in the same world (no reset here): the pass that was dropped is still owed.
            const int calls = g_computeCalls;
            Frame(0.5f, home);
            Settle(home);
            CHECK(la::fields::GetStats().shoreWorker == la::fields::ShoreWorkerState::Running && g_computeCalls == calls + 1);
            Same(reference, Capture(home));
        });

        Test("world leave from another thread only asks; the next Update stops it", [&]
        {
            Fresh(true, &Watched);
            Converge(home);
            const Held worker(g_workerTid);
            std::thread other([] { la::fields::OnWorldLeave(); });
            other.join();
            CHECK(la::fields::GetStats().shoreWorker == la::fields::ShoreWorkerState::Running && !worker.Gone());
            Frame(0.0f, home);
            CHECK(la::fields::GetStats().shoreWorker == la::fields::ShoreWorkerState::Off && worker.Gone());
        });

        Test("logout (out of world) stops the worker", [&]
        {
            Fresh(true, &Watched);
            Converge(home);
            const Held worker(g_workerTid);
            Frame(0.0f, Away());
            CHECK(worker.Gone() && la::fields::GetStats().shoreWorker == la::fields::ShoreWorkerState::Off);
        });

        Test("setting off: never starts a thread, original pass; switching off mid-flight joins", [&]
        {
            Fresh(false, &Watched);
            const unsigned applied = la::fields::GetStats().shoreApplied;
            Converge(home);
            Converge(At(1040.0f, 1000.0f));
            CHECK(g_computeCalls == 0 && g_workerTid == 0);
            CHECK(la::fields::GetStats().shoreWorker == la::fields::ShoreWorkerState::Off && la::fields::GetStats().shoreApplied == applied);
            const State reference = Capture(At(1040.0f, 1000.0f));

            Fresh(true, &Watched);
            Converge(home);
            const Held worker(g_workerTid);
            g_gate.Close();
            Frame(0.5f, At(1040.0f, 1000.0f)); // queued, held
            g_gate.WaitEntered();
            la::fields::SetShoreWorker(false);
            std::thread opener([] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); g_gate.Open(); });
            Frame(0.0f, At(1040.0f, 1000.0f)); // stops (joins) here
            opener.join();
            CHECK(worker.Gone() && la::fields::GetStats().shoreWorker == la::fields::ShoreWorkerState::Off);
            Frame(0.5f, At(1040.0f, 1000.0f)); // the owed pass runs the original way
            Same(reference, Capture(At(1040.0f, 1000.0f)));
        });

        Test("a worker error falls back to the original pass for good; off and on retries", [&]
        {
            Fresh(false);
            Converge(home);
            const State reference = Capture(home);

            Fresh(true, &Broken);
            Frame(0.5f, home);   // queued
            for (int k = 0; k < 10000 && !g_workerTid; ++k) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const Held worker(g_workerTid); // it waits for more work until stopped
            Settle(home);        // the error comes back
            la::fields::Stats st = la::fields::GetStats();
            CHECK(st.shoreWorker == la::fields::ShoreWorkerState::Failed && worker.Gone());
            CHECK(std::strstr(la::fields::ShoreWorkerError(), "deliberate failure") != nullptr);
            CHECK(la::fields::ShoreWorker()); // still wanted, but not used
            Frame(0.5f, home);   // the original pass
            Same(reference, Capture(home));
            const int calls = g_computeCalls;
            Converge(At(1040.0f, 1000.0f));
            CHECK(g_computeCalls == calls); // stays on the original pass

            sh::SetComputeForTests(nullptr);
            la::fields::SetShoreWorker(false);
            la::fields::SetShoreWorker(true);
            CHECK(la::fields::GetStats().shoreWorker == la::fields::ShoreWorkerState::Off && la::fields::ShoreWorkerError()[0] == '\0');
            Converge(home);
            CHECK(la::fields::GetStats().shoreWorker == la::fields::ShoreWorkerState::Running);
        });

        Test("non-finite terrain: the worker refuses, the original pass takes over with its own result", [&]
        {
            Both([&]
            {
                g_world.nanCell = true;
                Converge(home);
                Frame(0.5f, home); // after a fallback: the original pass
                return Capture(home);
            });
            CHECK(la::fields::GetStats().shoreWorker == la::fields::ShoreWorkerState::Failed);
            CHECK(std::strstr(la::fields::ShoreWorkerError(), "non-finite") != nullptr);
        });

        // --- cost --------------------------------------------------------------------------------
        // Each round: refill the grid with the clock frozen (all of it in one frame, no pass: dt 0),
        // then one frame with the real clock in which the pass is due. Original: UpdateShore's own
        // timing. Worker: the copy in that frame and the apply in the frame the result comes back.
        {
            constexpr int kRounds = 300;
            Fresh(false);
            Converge(home);
            std::vector<double> sync, frameSync;
            for (int k = 0; k < kRounds; ++k)
            {
                la::fields::Refill();
                Frame(0.0f, home);
                g_world.realClock = true;
                const auto t0 = Clock::now();
                Frame(0.5f, home);
                frameSync.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
                g_world.realClock = false;
                sync.push_back(la::fields::GetStats().transformMs);
            }
            Fresh(true);
            Converge(home);
            std::vector<double> copy, apply, worker, mainThread;
            for (int k = 0; k < kRounds; ++k)
            {
                la::fields::Refill();
                Frame(0.0f, home);
                g_world.realClock = true;
                Frame(0.5f, home);
                const double c = la::fields::GetStats().shoreCopyMs;
                Settle(home);
                g_world.realClock = false;
                const la::fields::Stats st = la::fields::GetStats();
                copy.push_back(c); apply.push_back(st.shoreApplyMs); worker.push_back(st.transformMs);
                mainThread.push_back(c + st.shoreApplyMs);
            }
            CHECK(la::fields::GetStats().shoreApplied >= kRounds);
            const Timing s = Summarize(sync), f = Summarize(frameSync), c = Summarize(copy), a = Summarize(apply), w = Summarize(worker), m = Summarize(mainThread);
            std::printf("TIMING original UpdateShore, 128x128 (game thread): median %.3f ms, p95 %.3f ms, max %.3f ms (%d calls)\n", s.median, s.p95, s.max, kRounds);
            std::printf("TIMING   whole fields::Update frame containing it (incl. moisture step): median %.3f ms, max %.3f ms\n", f.median, f.max);
            std::printf("TIMING worker mode, game thread: copy median %.3f ms (max %.3f) + apply median %.3f ms (max %.3f); sum median %.3f ms, max %.3f ms\n",
                        c.median, c.max, a.median, a.max, m.median, m.max);
            std::printf("TIMING worker mode, worker thread: calculation median %.3f ms, p95 %.3f ms, max %.3f ms\n", w.median, w.p95, w.max);
            ++g_passed;
        }

        Fresh(false);
        Frame(0.0f, Away());
        std::printf("%u test groups passed\n", g_passed);
        return 0;
    }
    catch (const std::exception& e)
    {
        g_gate.Open();
        std::printf("FAIL %s\n", e.what());
        Frame(0.0f, Away());
        return 1;
    }
}
