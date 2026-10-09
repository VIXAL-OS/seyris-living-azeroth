// Offline tests for env/ShoreField and env/LatestWorker: bit-for-bit parity with Fields.cpp's own
// UpdateShore (reference_shore.hpp, extracted by make_reference.py), the apply rule, grid keys,
// floating-point modes, and the worker's lifecycle. No game, no WXL core.
#include "env/LatestWorker.hpp"
#include "env/ShoreField.hpp"
#include "reference_shore.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <float.h>
#include <limits>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace sh = wxl_livingazeroth::fields::shore;
using Clock = std::chrono::steady_clock;

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__)); } while (false)

namespace
{
    unsigned g_passed = 0;
    template <class F> void Test(const char* name, F f)
    {
        f();
        ++g_passed;
        std::printf("PASS %s\n", name);
    }
    template <class F> void Throws(F f)
    {
        bool caught = false;
        try { f(); } catch (const std::exception&) { caught = true; }
        CHECK(caught);
    }
    bool Bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

    sh::Params ReferenceParams()
    {
        using F = reference::Fixture;
        return { F::kCell, F::kShoreNear, F::kShoreFar, F::kShoreLow, F::kShoreHigh, F::kHeatNear, F::kHeatFar, F::kHeatLow, F::kHeatHigh, F::kHotGroundShare };
    }

    std::vector<reference::Cell> Grid(int n)
    {
        std::vector<reference::Cell> cells(static_cast<size_t>(n) * n);
        for (auto& c : cells) c.valid = true;
        return cells;
    }

    sh::Snapshot SnapshotOf(const std::vector<reference::Cell>& cells, int n)
    {
        sh::Snapshot s;
        s.key = { 7, 571, -16, 24, n };
        s.revision = 3;
        s.params = ReferenceParams();
        s.cells.resize(cells.size());
        for (size_t q = 0; q < cells.size(); ++q)
        {
            const reference::Cell& c = cells[q];
            if (!c.valid) continue;
            sh::Input& in = s.cells[q];
            in.stamp = static_cast<uint32_t>(q + 1);
            in.submerged = c.submerged; in.liquidHot = c.liquidHot; in.z = c.z; in.waterZ = c.waterZ;
            in.liquidMoisture = c.liquidMoisture; in.liquidTemp = c.liquidTemp; in.hotShare = c.hotShare; in.hotTemp = c.hotTemp;
            in.liquid = c.liquid;
        }
        return s;
    }

    void SameCells(const std::vector<reference::Cell>& a, const std::vector<reference::Cell>& b)
    {
        CHECK(a.size() == b.size());
        for (size_t q = 0; q < a.size(); ++q)
        {
            const reference::Cell& x = a[q];
            const reference::Cell& y = b[q];
            CHECK(Bits(x.waterDist, y.waterDist)); CHECK(Bits(x.aboveWater, y.aboveWater)); CHECK(Bits(x.shore, y.shore));
            CHECK(Bits(x.heat, y.heat)); CHECK(x.nearLiquid == y.nearLiquid); CHECK(Bits(x.nearMoisture, y.nearMoisture));
            CHECK(Bits(x.nearTemp, y.nearTemp)); CHECK(x.nearHot == y.nearHot); CHECK(Bits(x.groundHeat, y.groundHeat));
            CHECK(Bits(x.groundHeatTemp, y.groundHeatTemp)); CHECK(Bits(x.groundHeatDist, y.groundHeatDist));
        }
    }

    // The original on one copy, Compute + Apply on another: every field, bit for bit.
    sh::Result AgainstOriginal(const std::vector<reference::Cell>& cells, int n)
    {
        reference::Fixture f{ n, cells };
        f.UpdateShore();
        std::vector<reference::Cell> mine = cells;
        sh::Result r = sh::Compute(SnapshotOf(cells, n));
        CHECK(r.cells.size() == cells.size());
        for (size_t q = 0; q < cells.size(); ++q)
        {
            CHECK((r.cells[q].stamp != 0) == cells[q].valid);
            if (r.cells[q].stamp) sh::Apply(mine[q], r.cells[q]);
        }
        SameCells(f.cells, mine);
        CHECK(r.water == f.g_stats.water);
        CHECK(r.hotGround == f.g_stats.hotGround);
        return r;
    }

    // A plausible 128 x 128 grid: rolling ground, a lake, a magma pool, a lava stream, a few holes.
    std::vector<reference::Cell> RealisticGrid()
    {
        constexpr int n = 128;
        std::vector<reference::Cell> cells(n * n);
        for (int b = 0; b < n; ++b)
            for (int a = 0; a < n; ++a)
            {
                reference::Cell& c = cells[static_cast<size_t>(b) * n + a];
                const float x = a * 2.0f, y = b * 2.0f;
                c.valid = !(a >= 90 && a < 94 && b >= 20 && b < 23);
                c.z = 10.0f + 2.0f * std::sin(x / 17.0f) + 1.5f * std::cos(y / 23.0f) - 0.01f * x;
                // Cached values from a previous pass, which UpdateShore leaves alone where nothing is in range.
                c.aboveWater = 0.25f; c.nearMoisture = 0.5f; c.nearTemp = 3.0f; c.groundHeatTemp = 7.0f;
                const float lake = (x - 60.0f) * (x - 60.0f) + (y - 140.0f) * (y - 140.0f);
                const float magma = (x - 200.0f) * (x - 200.0f) + (y - 40.0f) * (y - 40.0f);
                if (lake < 30.0f * 30.0f && c.z < 10.5f) { c.submerged = true; c.waterZ = 10.5f; c.liquid = 1; }
                if (magma < 9.0f * 9.0f) { c.submerged = true; c.waterZ = c.z + 0.5f; c.liquid = 19; c.liquidHot = true; c.liquidTemp = 1000.0f; c.liquidMoisture = 0.0f; }
                if (x >= 150.0f && x < 154.0f) { c.hotShare = 0.8f; c.hotTemp = 600.0f; }
            }
        return cells;
    }

    template <class I, class O> typename sh::LatestWorker<I, O>::Completion Await(sh::LatestWorker<I, O>& w)
    {
        const auto end = Clock::now() + std::chrono::seconds(5);
        while (Clock::now() < end)
        {
            if (auto v = w.TryTake()) return std::move(*v);
            std::this_thread::yield();
        }
        throw std::runtime_error("worker completion timed out");
    }

    struct Gate
    {
        std::mutex m;
        std::condition_variable cv;
        bool entered = false, release = false;
        void Enter()
        {
            std::unique_lock<std::mutex> lock(m);
            entered = true;
            cv.notify_all();
            if (!cv.wait_for(lock, std::chrono::seconds(5), [&] { return release; })) throw std::runtime_error("gate timed out");
        }
        void WaitEntered()
        {
            std::unique_lock<std::mutex> lock(m);
            CHECK(cv.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }));
        }
        void Release() { std::lock_guard<std::mutex> lock(m); release = true; cv.notify_all(); }
    };

    struct Timing { double median = 0, p95 = 0, max = 0; };
    Timing Summarize(std::vector<double> ms)
    {
        std::sort(ms.begin(), ms.end());
        return { ms[ms.size() / 2], ms[(ms.size() * 95) / 100], ms.back() };
    }

    void SetRounding(unsigned mode)
    {
        unsigned int x87 = 0, sse = 0;
        __control87_2(mode, _MCW_RC, &x87, &sse);
    }
}

int main()
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try
    {
        Test("dry grid: no liquid in range keeps UpdateShore's untouched fields", []
        {
            auto g = Grid(8);
            g[4].nearTemp = 123.0f; g[4].aboveWater = 4.0f; g[4].nearMoisture = 0.7f; g[4].groundHeatTemp = 55.0f;
            const sh::Result r = AgainstOriginal(g, 8);
            CHECK(r.water == 0 && r.hotGround == 0);
            CHECK(!r.cells[4].water && !r.cells[4].hot);
        });
        Test("known water distances and shoreline falloff", []
        {
            auto g = Grid(8);
            g[0].submerged = true; g[0].liquid = 7;
            const sh::Result r = AgainstOriginal(g, 8);
            CHECK(r.cells[0].shore == 1.0f && r.cells[1].waterDist == 2.0f && r.cells[3].shore == 0.0f && r.cells[1].nearLiquid == 7);
        });
        Test("hot water, raised ground and painted heat", []
        {
            auto g = Grid(8);
            g[5].submerged = true; g[5].liquidHot = true; g[5].liquidTemp = 1000.0f; g[5].waterZ = 2.0f;
            g[12].z = 8.0f; g[23].hotShare = 0.8f; g[23].hotTemp = 99.0f;
            CHECK(AgainstOriginal(g, 8).hotGround == 1);
        });
        Test("unloaded cells, equal-distance tie order", []
        {
            auto g = Grid(8);
            g[1].submerged = true; g[1].liquid = 8; g[3].submerged = true; g[3].liquid = 9;
            g[9].valid = false; g[9].shore = 0.234f;
            const sh::Result r = AgainstOriginal(g, 8);
            CHECK(r.cells[9].stamp == 0);
        });
        Test("randomized bit-for-bit parity with Fields.cpp's UpdateShore (300 grids)", []
        {
            std::mt19937 rng(0x335a);
            std::uniform_real_distribution<float> real(-8.0f, 8.0f);
            for (int k = 0; k < 300; ++k)
            {
                const int n = k % 50 == 0 ? 128 : 1 + static_cast<int>(rng() % 40);
                auto g = Grid(n);
                for (auto& c : g)
                {
                    c.valid = rng() % 5 != 0; c.submerged = rng() % 13 == 0; c.liquidHot = rng() % 2 == 0;
                    c.z = real(rng); c.waterZ = real(rng); c.liquid = 1 + rng() % 8;
                    c.liquidMoisture = (rng() % 100) / 100.0f; c.liquidTemp = real(rng) * 100.0f;
                    c.hotShare = (rng() % 100) / 100.0f; c.hotTemp = real(rng) * 10.0f;
                    c.nearTemp = real(rng); c.nearMoisture = real(rng); c.aboveWater = real(rng); c.groundHeatTemp = real(rng);
                    c.waterDist = real(rng); c.shore = real(rng); c.heat = real(rng); c.groundHeat = real(rng); c.groundHeatDist = real(rng);
                }
                AgainstOriginal(g, n);
            }
        });
        Test("realistic 128-square grid parity", [] { AgainstOriginal(RealisticGrid(), 128); });
        Test("model numbers come from Params (Fields' constants), not copies", []
        {
            auto g = Grid(16);
            g[0].submerged = true;
            sh::Snapshot s = SnapshotOf(g, 16);
            const sh::Result a = sh::Compute(s);
            s.params.shoreFar = 30.0f;
            const sh::Result b = sh::Compute(s);
            CHECK(!Bits(a.cells[5].shore, b.cells[5].shore));
        });
        Test("rejects malformed grids and non-finite inputs; ignores non-finite where no cell is", []
        {
            auto g = Grid(8);
            sh::Snapshot s = SnapshotOf(g, 8);
            s.key.size = 129; Throws([&] { sh::Compute(s); });
            s = SnapshotOf(g, 8); s.key.size = 0; Throws([&] { sh::Compute(s); });
            s = SnapshotOf(g, 8); s.cells.pop_back(); Throws([&] { sh::Compute(s); });
            s = SnapshotOf(g, 8); s.cells[3].z = std::numeric_limits<float>::quiet_NaN(); Throws([&] { sh::Compute(s); });
            s = SnapshotOf(g, 8); s.cells[3].hotTemp = std::numeric_limits<float>::infinity(); Throws([&] { sh::Compute(s); });
            s = SnapshotOf(g, 8); s.cells[3].stamp = 0; s.cells[3].z = std::numeric_limits<float>::quiet_NaN(); sh::Compute(s);
        });
        Test("a reused result buffer (any old contents) applies exactly like a fresh one", []
        {
            const auto cells = RealisticGrid();
            const sh::Snapshot s = SnapshotOf(cells, 128);
            const sh::Result fresh = sh::Compute(s);
            sh::Result reused;
            reused.cells.resize(s.cells.size());
            std::mt19937 rng(7);
            std::uniform_real_distribution<float> junk(-1.0e6f, 1.0e6f);
            for (auto& o : reused.cells)
            {
                o.stamp = rng(); o.water = rng() % 2 != 0; o.hot = rng() % 2 != 0; o.nearHot = rng() % 2 != 0; o.nearLiquid = rng();
                o.waterDist = junk(rng); o.aboveWater = junk(rng); o.shore = junk(rng); o.heat = junk(rng); o.nearMoisture = junk(rng);
                o.nearTemp = junk(rng); o.groundHeat = junk(rng); o.groundHeatTemp = junk(rng); o.groundHeatDist = junk(rng);
            }
            reused.water = 12345; reused.hotGround = 678;
            sh::ComputeInto(s, reused);
            CHECK(reused.water == fresh.water && reused.hotGround == fresh.hotGround);
            auto a = cells, b = cells;
            for (size_t q = 0; q < cells.size(); ++q)
            {
                CHECK(fresh.cells[q].stamp == reused.cells[q].stamp);
                if (fresh.cells[q].stamp) { sh::Apply(a[q], fresh.cells[q]); sh::Apply(b[q], reused.cells[q]); }
            }
            SameCells(a, b);
        });
        Test("a job hands both buffers back for the next pass", []
        {
            sh::Snapshot s = SnapshotOf(RealisticGrid(), 128);
            s.spare.resize(s.cells.size());
            const sh::Output* spare = s.spare.data();
            const sh::Input* inputs = s.cells.data();
            const sh::Result r = sh::RunJob(s, nullptr);
            CHECK(r.cells.data() == spare && r.inputs.data() == inputs && s.cells.empty() && s.spare.empty());
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL); // RunJob lowers its thread's
        });
        Test("the calculation doesn't touch its snapshot", []
        {
            const sh::Snapshot s = SnapshotOf(RealisticGrid(), 128);
            const sh::Snapshot before = s;
            sh::Compute(s);
            CHECK(std::memcmp(s.cells.data(), before.cells.data(), s.cells.size() * sizeof(sh::Input)) == 0);
        });
        Test("grid keys: epoch, map, origin and size each reject", []
        {
            const sh::GridKey k{ 4, 571, -16, 24, 128 };
            CHECK(sh::SameGrid(k, k));
            sh::GridKey o = k; ++o.worldEpoch; CHECK(!sh::SameGrid(k, o));
            o = k; o.mapId = 0; CHECK(!sh::SameGrid(k, o));
            o = k; ++o.firstI; CHECK(!sh::SameGrid(k, o));
            o = k; --o.firstJ; CHECK(!sh::SameGrid(k, o));
            o = k; o.size = 64; CHECK(!sh::SameGrid(k, o));
        });
        Test("the worker computes under the copying thread's floating-point modes", []
        {
            const sh::Snapshot base = SnapshotOf(RealisticGrid(), 128);
            const sh::FpControl original = sh::CaptureFpControl();
            CHECK(original.valid);
            SetRounding(_RC_CHOP);
            const sh::Result mainChop = sh::Compute(base);
            sh::Snapshot withFp = base;
            withFp.fp = sh::CaptureFpControl();
            sh::Snapshot withoutFp = base;
            withoutFp.fp.valid = false;
            SetRounding(_RC_NEAR);
            sh::LatestWorker<sh::Snapshot, sh::Result> w([](sh::Snapshot& s) { return sh::RunJob(s, nullptr); });
            w.Submit(withFp);
            const sh::Result workerChop = *Await(w).value;
            w.Submit(withoutFp);
            const sh::Result workerNear = *Await(w).value;
            w.Stop();
            bool sameAsMain = true, differs = false;
            for (size_t q = 0; q < base.cells.size(); ++q)
            {
                sameAsMain &= Bits(mainChop.cells[q].shore, workerChop.cells[q].shore) && Bits(mainChop.cells[q].heat, workerChop.cells[q].heat) &&
                              Bits(mainChop.cells[q].groundHeat, workerChop.cells[q].groundHeat) && Bits(mainChop.cells[q].waterDist, workerChop.cells[q].waterDist);
                differs |= !Bits(mainChop.cells[q].shore, workerNear.cells[q].shore) || !Bits(mainChop.cells[q].groundHeat, workerNear.cells[q].groundHeat);
            }
            CHECK(sameAsMain);
            CHECK(differs); // the test can tell: without the modes, the worker's answer differs
            CHECK(sh::CaptureFpControl().sse == original.sse);
        });
        Test("worker runs on another thread with identical results", []
        {
            std::atomic<DWORD> tid{ 0 };
            sh::LatestWorker<sh::Snapshot, sh::Result> w([&](sh::Snapshot& s) { tid = GetCurrentThreadId(); return sh::RunJob(s, nullptr); });
            const sh::Snapshot s = SnapshotOf(RealisticGrid(), 128);
            w.Submit(s);
            auto r = Await(w);
            CHECK(!r.error);
            CHECK(tid != 0 && tid != GetCurrentThreadId());
            const sh::Result direct = sh::Compute(s);
            for (size_t q = 0; q < s.cells.size(); ++q)
                CHECK(Bits(direct.cells[q].shore, r.value->cells[q].shore) && Bits(direct.cells[q].groundHeat, r.value->cells[q].groundHeat));
            CHECK(r.value->computeMs >= 0.0);
        });
        Test("one waiting slot: requests coalesce, the newest wins", []
        {
            Gate gate;
            sh::LatestWorker<int, int> w([&](int x) { if (x == 1) gate.Enter(); return x * 2; });
            w.Submit(1);
            gate.WaitEntered();
            CHECK(!w.TryTake());
            w.Submit(2);
            const uint64_t id = w.Submit(3);
            gate.Release();
            auto r = Await(w);
            CHECK(r.id == id && *r.value == 6);
            const auto st = w.GetStats();
            CHECK(st.replaced == 1 && st.discarded == 1);
        });
        Test("invalidation drops a calculation already running", []
        {
            Gate gate;
            sh::LatestWorker<int, int> w([&](int x) { if (x == 1) gate.Enter(); return x; });
            w.Submit(1);
            gate.WaitEntered();
            w.Invalidate();
            const uint64_t id = w.Submit(9);
            gate.Release();
            auto r = Await(w);
            CHECK(r.id == id && *r.value == 9 && w.GetStats().discarded == 1);
        });
        Test("calculation exceptions arrive as errors; later work recovers", []
        {
            sh::LatestWorker<int, int> w([](int x) { if (x < 0) throw std::runtime_error("deliberate"); return x; });
            w.Submit(-1);
            auto bad = Await(w);
            CHECK(bad.error && !bad.value);
            w.Submit(5);
            CHECK(*Await(w).value == 5);
        });
        Test("only the creating thread may use the worker", []
        {
            sh::LatestWorker<int, int> w([](int x) { return x; });
            bool rejected = false;
            std::thread other([&] { try { w.Submit(4); } catch (const std::logic_error&) { rejected = true; } });
            other.join();
            CHECK(rejected);
        });
        Test("stop cancels the waiting job and joins the running one; the thread is gone", []
        {
            Gate gate;
            std::atomic<DWORD> tid{ 0 };
            sh::LatestWorker<int, int> w([&](int x) { tid = GetCurrentThreadId(); if (x == 1) gate.Enter(); return x; });
            w.Submit(1);
            gate.WaitEntered();
            HANDLE h = OpenThread(SYNCHRONIZE, FALSE, tid);
            CHECK(h != nullptr);
            CHECK(WaitForSingleObject(h, 0) == WAIT_TIMEOUT);
            w.Submit(2);
            std::thread release([&] { std::this_thread::sleep_for(std::chrono::milliseconds(30)); gate.Release(); });
            w.Stop();
            release.join();
            CHECK(WaitForSingleObject(h, 0) == WAIT_OBJECT_0); // joined: it has exited
            CloseHandle(h);
            CHECK(!w.TryTake());
            CHECK(!w.Joinable());
            Throws([&] { w.Submit(3); });
            w.Stop();
        });
        Test("destroying it on another thread joins instead of throwing", []
        {
            auto* w = new sh::LatestWorker<int, int>([](int x) { return x; });
            w->Submit(1);
            std::thread other([&] { delete w; });
            other.join();
        });
        Test("rapid replacement and repeated invalidation stay bounded", []
        {
            sh::LatestWorker<int, int> w([](int x) { return x; });
            for (int i = 0; i < 2000; ++i) { if (i % 17 == 0) w.Invalidate(); w.Submit(i); }
            auto r = Await(w);
            CHECK(*r.value == 1999 && w.GetStats().submitted == 2000);
        });

        // Cost of the calculation itself on a realistic grid (what moves off the game thread).
        {
            const auto cells = RealisticGrid();
            const sh::Snapshot s = SnapshotOf(cells, 128);
            std::vector<double> compute, reuse, original;
            sh::Result kept;
            for (int k = 0; k < 300; ++k)
            {
                auto t0 = Clock::now();
                const sh::Result r = sh::Compute(s);
                compute.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
                CHECK(r.cells.size() == cells.size());
                t0 = Clock::now();
                sh::ComputeInto(s, kept);
                reuse.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
                reference::Fixture f{ 128, cells };
                t0 = Clock::now();
                f.UpdateShore();
                original.push_back(std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
            }
            const Timing c = Summarize(compute), u = Summarize(reuse), o = Summarize(original);
            std::printf("TIMING ShoreField::Compute 128x128, fresh result buffer: median %.3f ms, p95 %.3f ms, max %.3f ms (300 runs)\n", c.median, c.p95, c.max);
            std::printf("TIMING ShoreField::ComputeInto 128x128, reused buffer (as on the worker): median %.3f ms, p95 %.3f ms, max %.3f ms\n", u.median, u.p95, u.max);
            std::printf("TIMING extracted UpdateShore on a flat array: median %.3f ms, p95 %.3f ms, max %.3f ms (300 runs)\n", o.median, o.p95, o.max);
        }

        std::printf("%u test groups passed\n", g_passed);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::printf("FAIL %s\n", e.what());
        return 1;
    }
}
