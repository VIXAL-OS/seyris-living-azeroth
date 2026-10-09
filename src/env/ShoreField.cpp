#include "ShoreField.hpp"

#include <windows.h>

#include <chrono>
#include <cmath>
#include <float.h>
#include <stdexcept>
#include <utility>

namespace wxl_livingazeroth::fields::shore
{
    namespace
    {
        // UpdateShore's own local constants (Fields.cpp); the model numbers come in through Params.
        constexpr float kInf = 1.0e9f;
        constexpr float kDiag = 1.4142f;

        ComputeFn g_computeForTests = nullptr;

        float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
        float Smooth(float edge0, float edge1, float v) // 1 at edge0 .. 0 at edge1, as Fields.cpp
        {
            const float t = Clamp01((v - edge0) / (edge1 - edge0));
            return 1.0f - t * t * (3.0f - 2.0f * t);
        }

        bool Finite(const Input& c)
        {
            return std::isfinite(c.z) && std::isfinite(c.waterZ) && std::isfinite(c.liquidMoisture) &&
                   std::isfinite(c.liquidTemp) && std::isfinite(c.hotShare) && std::isfinite(c.hotTemp);
        }

        void SetFpControl(const FpControl& f)
        {
#if defined(_M_IX86)
            unsigned int x87 = 0, sse = 0;
            __control87_2(f.x87, _MCW_RC | _MCW_PC, &x87, nullptr);
            __control87_2(f.sse, _MCW_RC | _MCW_DN, nullptr, &sse);
#else
            unsigned int cw = 0;
            _controlfp_s(&cw, f.sse, _MCW_RC | _MCW_DN);
#endif
        }

        // Rounding, precision and denormal modes only: exceptions stay masked on the worker, so a
        // trap the game thread might have unmasked can't fire on a thread nobody handles it on.
        class ScopedFpControl
        {
        public:
            explicit ScopedFpControl(const FpControl& want)
            {
                if (!want.valid) return;
                saved_ = CaptureFpControl();
                SetFpControl(want);
            }
            ~ScopedFpControl() { if (saved_.valid) SetFpControl(saved_); }
            ScopedFpControl(const ScopedFpControl&) = delete;
            ScopedFpControl& operator=(const ScopedFpControl&) = delete;

        private:
            FpControl saved_;
        };
    }

    FpControl CaptureFpControl()
    {
        FpControl f;
#if defined(_M_IX86)
        unsigned int x87 = 0, sse = 0;
        if (__control87_2(0, 0, &x87, &sse)) { f.x87 = x87; f.sse = sse; f.valid = true; }
#else
        unsigned int cw = 0;
        if (_controlfp_s(&cw, 0, 0) == 0) { f.x87 = f.sse = cw; f.valid = true; }
#endif
        return f;
    }

    Result Compute(const Snapshot& in)
    {
        Result out;
        ComputeInto(in, out);
        return out;
    }

    void ComputeInto(const Snapshot& in, Result& out)
    {
        const int n = in.key.size;
        if (n < 1 || n > 128 || in.cells.size() != static_cast<size_t>(n) * n)
            throw std::invalid_argument("shore grid must be square, 1..128 cells per side");
        for (const Input& c : in.cells)
            if (c.stamp && !Finite(c)) throw std::invalid_argument("non-finite shore input");
        const Params& p = in.params;

        out.key = in.key;
        out.revision = in.revision;
        out.water = out.hotGround = 0;
        out.cells.resize(in.cells.size());
        std::vector<float> dist(in.cells.size()), surf(in.cells.size());
        std::vector<int> src(in.cells.size());
        auto at = [n](int a, int b) { return b * n + a; };

        // The sweeps exactly as UpdateShore writes them (relax order decides ties between sources).
        auto relax = [&](int a, int b, int na, int nb, float step)
        {
            if (na < 0 || nb < 0 || na >= n || nb >= n) return;
            const float d = dist[at(na, nb)] + step;
            if (d < dist[at(a, b)]) { dist[at(a, b)] = d; surf[at(a, b)] = surf[at(na, nb)]; src[at(a, b)] = src[at(na, nb)]; }
        };
        auto relaxHot = [&](int a, int b, int na, int nb, float step)
        {
            if (na < 0 || nb < 0 || na >= n || nb >= n) return;
            const float d = dist[at(na, nb)] + step;
            if (d < dist[at(a, b)]) { dist[at(a, b)] = d; src[at(a, b)] = src[at(na, nb)]; }
        };

        for (size_t q = 0; q < in.cells.size(); ++q)
        {
            const Input& c = in.cells[q];
            const bool w = c.stamp && c.submerged;
            dist[q] = w ? 0.0f : kInf;
            surf[q] = w ? c.waterZ : 0.0f;
            src[q] = w ? static_cast<int>(q) : -1;
            out.water += w;
        }
        for (int b = 0; b < n; ++b)
            for (int a = 0; a < n; ++a)
            { relax(a, b, a - 1, b, 1.0f); relax(a, b, a, b - 1, 1.0f); relax(a, b, a - 1, b - 1, kDiag); relax(a, b, a + 1, b - 1, kDiag); }
        for (int b = n - 1; b >= 0; --b)
            for (int a = n - 1; a >= 0; --a)
            { relax(a, b, a + 1, b, 1.0f); relax(a, b, a, b + 1, 1.0f); relax(a, b, a + 1, b + 1, kDiag); relax(a, b, a - 1, b + 1, kDiag); }
        for (size_t q = 0; q < in.cells.size(); ++q)
        {
            const Input& c = in.cells[q];
            Output& r = out.cells[q];
            r.stamp = c.stamp;
            if (!c.stamp) continue;
            if (dist[q] >= kInf || src[q] < 0)
            {
                r.water = false; r.waterDist = -1.0f; r.shore = 0.0f; r.heat = 0.0f; r.nearLiquid = 0; r.nearHot = false;
                continue;
            }
            const Input& w = in.cells[static_cast<size_t>(src[q])];
            r.water = true;
            r.waterDist = dist[q] * p.cell;
            r.aboveWater = c.z - surf[q];
            r.shore = c.submerged ? 1.0f : Smooth(p.shoreNear, p.shoreFar, r.waterDist) * Smooth(p.shoreLow, p.shoreHigh, r.aboveWater);
            r.nearLiquid = w.liquid;
            r.nearMoisture = w.liquidMoisture;
            r.nearHot = w.liquidHot;
            r.nearTemp = w.liquidTemp;
            r.heat = r.nearHot ? (c.submerged ? 1.0f : Smooth(p.heatNear, p.heatFar, r.waterDist) * Smooth(p.heatLow, p.heatHigh, r.aboveWater)) : 0.0f;
        }

        // Hot ground: painted materials with a Temperature heat the ground around them like hot liquids.
        for (size_t q = 0; q < in.cells.size(); ++q)
        {
            const Input& c = in.cells[q];
            const bool h = c.stamp && !c.submerged && c.hotShare >= p.hotGroundShare;
            dist[q] = h ? 0.0f : kInf;
            src[q] = h ? static_cast<int>(q) : -1;
            out.hotGround += h;
        }
        for (int b = 0; b < n; ++b)
            for (int a = 0; a < n; ++a)
            { relaxHot(a, b, a - 1, b, 1.0f); relaxHot(a, b, a, b - 1, 1.0f); relaxHot(a, b, a - 1, b - 1, kDiag); relaxHot(a, b, a + 1, b - 1, kDiag); }
        for (int b = n - 1; b >= 0; --b)
            for (int a = n - 1; a >= 0; --a)
            { relaxHot(a, b, a + 1, b, 1.0f); relaxHot(a, b, a, b + 1, 1.0f); relaxHot(a, b, a + 1, b + 1, kDiag); relaxHot(a, b, a - 1, b + 1, kDiag); }
        for (size_t q = 0; q < in.cells.size(); ++q)
        {
            const Input& c = in.cells[q];
            Output& r = out.cells[q];
            if (!c.stamp) continue;
            if (src[q] < 0 || dist[q] >= kInf) { r.hot = false; r.groundHeat = 0.0f; r.groundHeatDist = -1.0f; continue; }
            const Input& h = in.cells[static_cast<size_t>(src[q])];
            r.hot = true;
            r.groundHeatDist = dist[q] * p.cell;
            r.groundHeatTemp = h.hotTemp;
            r.groundHeat = dist[q] == 0.0f ? c.hotShare
                                           : Smooth(p.heatNear, p.heatFar, r.groundHeatDist) * Smooth(p.heatLow, p.heatHigh, c.z - h.z);
        }
    }

    Result RunJob(Snapshot& in, ComputeFn compute)
    {
        // A cosmetic background job: the game's own threads come first.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        ScopedFpControl fp(in.fp);
        const auto t0 = std::chrono::steady_clock::now();
        Result r;
        if (compute) r = compute(in);
        else
        {
            r.cells = std::move(in.spare);
            ComputeInto(in, r);
        }
        r.computeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        r.inputs = std::move(in.cells);
        return r;
    }

    void      SetComputeForTests(ComputeFn compute) { g_computeForTests = compute; }
    ComputeFn ComputeForTests() { return g_computeForTests; }

    bool PinModule()
    {
        static const char anchor = 0;
        HMODULE module = nullptr;
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                  reinterpret_cast<LPCWSTR>(&anchor), &module) != 0;
    }
}
