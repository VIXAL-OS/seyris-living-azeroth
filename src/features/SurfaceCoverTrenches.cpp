#include "SurfaceCoverInternal.hpp"

#include "../env/Actors.hpp"

#include <cmath>
#include <vector>

namespace wxl_livingazeroth::cover::detail
{
    // Level 0's trench state, slot = (world cell mod kTex): how far pressed down (0..1) and the
    // rim's strength (0..1; its height is a share of the local cover depth). Both relax over time.
    std::vector<float> g_press(kTex * kTex, 0.0f);
    std::vector<float> g_rim(kTex * kTex, 0.0f);

    // Only cells with a trench change from frame to frame. They're kept in a list (relaxed, and
    // re-uploaded with a dirty rectangle) instead of walking all 147k cells twice per frame. The
    // rest of level 0 keeps its static cover and the trench fade-out factor from the last full
    // pass (a full pass runs whenever coverDirty is set: recentre, refill, settings).
    std::vector<int>     g_active;                            // slots with press or rim > 0
    std::vector<uint8_t> g_isActive(kTex * kTex, 0);
    std::vector<int>     g_changed;                           // slots to re-upload this frame
    bool                 g_changedPending = false;            // changes not uploaded yet
    std::vector<float>   g_static0(kTex * kTex, 0.0f);        // level 0 cover without trenches
    std::vector<float>   g_deformK(kTex * kTex, 0.0f);        // level 0 trench strength (fade-out)

    void MarkActive(int s)
    {
        if (g_isActive[s]) return;
        g_isActive[s] = 1;
        g_active.push_back(s);
        g_changed.push_back(s);
    }

    // Level 0's rim share and relax time per slot, from the rows of the cell's materials (blended
    // like everything else; the panel's multipliers apply on top). Per cell, not per player: a
    // cover whose prints only show through their rims (mud below the terrain) must keep its rims
    // when the player steps onto other ground.
    std::vector<float> g_rimCell(kTex * kTex, 0.3f);
    std::vector<float> g_relaxCell(kTex * kTex, 30.0f);
    // ...and how much of it is sand (rows with kFlagRipplePrints): there feet carve no trench, and the prints
    // the client puts down get still rings around them (CoverRipples.hpp).
    std::vector<float> g_rippleCell(kTex * kTex, 0.0f);
    ripples::Set                       g_ripples;
    ripples::Set                       g_wmoRipples(64); // prints on building floors (features/WmoIce draws them)
    unsigned                           g_wmoRipplesMade = 0;
    // Each unit's last two prints, newest first (x, y pairs): which way its next one points.
    struct UnitPrints { float xy[4] = {}; int count = 0; };
    std::unordered_map<unsigned long long, UnitPrints> g_unitPrints;
    float                              g_rippleClock = 0.0f;
    unsigned                           g_ripplesMade = 0;
    unsigned                           g_ripplesShaped = 0;   // of them, following their print's own outline
    std::vector<coverprints::NewPrint> g_newPrints;

    unsigned g_drawPatches = 0, g_emptyLevels = 0;
    unsigned g_snowLookups = 0; // actual SnowAt calls in the last cover draw
    unsigned g_stamps = 0;

    // A trench under one grounded unit: pressed flat inside r, a rim between r and 1.6 r.
    void Stamp(const float pos[3], float radius)
    {
        const Level& L = g_levels[0];
        float ground;
        if (!terrain::HeightAt(pos[0], pos[1], ground) || std::fabs(pos[2] - ground) > 0.35f) return; // airborne, swimming, on a WMO
        ++g_stamps;
        const float outer = radius * 1.6f, rimPeak = radius * 1.25f, rimHalf = radius * 0.35f;
        const int i0 = static_cast<int>(std::floor((pos[0] - outer) / L.cell)), i1 = static_cast<int>(std::ceil((pos[0] + outer) / L.cell));
        const int j0 = static_cast<int>(std::floor((pos[1] - outer) / L.cell)), j1 = static_cast<int>(std::ceil((pos[1] + outer) / L.cell));
        for (int j = j0; j <= j1; ++j)
            for (int i = i0; i <= i1; ++i)
            {
                if (i < L.gridI || i >= L.gridI + kGridVerts || j < L.gridJ || j >= L.gridJ + kGridVerts) continue;
                const float dx = i * L.cell - pos[0], dy = j * L.cell - pos[1];
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d >= outer) continue;
                const int s = SlotOf(i, j);
                // Sand keeps no trench: its prints get rings instead (AddRipple).
                const float keep = g_settings.ripples ? 1.0f - g_rippleCell[s] : 1.0f;
                if (d < radius)
                {
                    const float t = (d < radius * 0.7f ? 1.0f : (radius - d) / (radius * 0.3f)) * keep;
                    if (t > g_press[s]) { g_press[s] = t; MarkActive(s); }
                }
                const float r = (1.0f - std::fabs(d - rimPeak) / rimHalf) * keep;
                if (r > g_rim[s]) { g_rim[s] = r; MarkActive(s); }
            }
    }

    // Rings around a print the client just put down, where it lies in sand on level 0's grid: sized by the
    // print of the nearest unit that makes prints (it was put down under one of its feet), lasting the cover's
    // relax time there. Not on WMO floors: that unit stands on the terrain.
    void AddRipple(const coverprints::NewPrint& p)
    {
        const actors::Actor* unit = nullptr;
        float best = 4.0f;
        for (const actors::Actor& a : actors::Nearby())
        {
            if (!a.hasFootprint) continue;
            const float d = std::hypot(a.pos[0] - p.x, a.pos[1] - p.y);
            if (d < best) { best = d; unit = &a; }
        }
        if (!unit) return;
        // Which way it points: along the unit's stride (CoverRipples.hpp).
        if (g_unitPrints.size() > 512) g_unitPrints.clear();
        UnitPrints& history = g_unitPrints[unit->guid];
        ripples::Ripple r;
        const bool directed = ripples::StrideDirection(history.xy, history.count, p.x, p.y, r.axis);
        history.xy[2] = history.xy[0]; history.xy[3] = history.xy[1];
        history.xy[0] = p.x; history.xy[1] = p.y;
        history.count = std::min(history.count + 1, 2);
        ripples::Outline(unit->footprintLength, unit->footprintWidth, directed, r.along, r.across);
        r.x = p.x; r.y = p.y;
        r.born = g_rippleClock;
        // The rim follows the print's own outline where the client's placement of its texture could be read (an
        // r0 of a hand's breadth to two yards); else the oval.
        const auto shape = [&]()
        {
            const bool own = p.mapped && ripples::ShapeFromPrint(static_cast<int>(p.texture), p.map, p.x, p.y, r.shape) &&
                             r.shape.r0 > 0.01f && r.shape.r0 < 2.0f;
            if (!own && !ripples::ShapeFromOval(p.x, p.y, r.axis, r.along, r.across, r.shape)) r.shape.r0 = 0.0f;
            return own;
        };
        float ground;
        if (!terrain::HeightAt(unit->pos[0], unit->pos[1], ground) || std::fabs(unit->pos[2] - ground) > 0.35f)
        {
            // On a building's floor (the Caverns of Time, the snowy caves): features/WmoIce draws these on its sand
            // and snow floors only. Kept as long as snow's fill-in time; on sand they fade by sand's (WmoRings).
            if (shape()) ++g_ripplesShaped;
            if (!(r.shape.r0 > 0.0f)) return;
            r.life = ripples::LifeFor(kWmoSnowRelaxSeconds * g_settings.relaxMul);
            g_wmoRipples.Add(r);
            ++g_wmoRipplesMade;
            return;
        }
        const Level& L = g_levels[0];
        if (!L.haveGrid) return;
        const int i = static_cast<int>(std::lround(p.x / L.cell)), j = static_cast<int>(std::lround(p.y / L.cell));
        if (i < L.gridI || i >= L.gridI + kGridVerts || j < L.gridJ || j >= L.gridJ + kGridVerts) return;
        const int s = SlotOf(i, j);
        if (g_rippleCell[s] < 0.5f || g_static0[s] < kMinDrawnDepth) return;
        const bool shaped = shape();
        if (!(r.shape.r0 > 0.0f)) return;
        r.life = ripples::LifeFor(g_relaxCell[s] * g_settings.relaxMul);
        g_ripples.Add(r);
        ++g_ripplesMade;
        if (shaped) ++g_ripplesShaped;
    }

    void Simulate(float dt)
    {
        g_rippleClock += dt;
        g_ripples.Expire(g_rippleClock);
        g_wmoRipples.Expire(g_rippleClock);
        g_newPrints.clear();
        coverprints::TakeNewPrints(g_newPrints);
        if (g_settings.ripples)
            for (const coverprints::NewPrint& p : g_newPrints) AddRipple(p);

        // Changes the last frame didn't upload (the cover wasn't drawn) would be lost: redo level 0
        // in full instead.
        if (g_changedPending) g_levels[0].coverDirty = true;
        g_changed.clear();

        for (size_t k = 0; k < g_active.size();)
        {
            const int s = g_active[k];
            const float seconds = g_relaxCell[s] * g_settings.relaxMul;
            const float relax = seconds > 0.1f ? dt / seconds : 1.0f;
            g_press[s] = g_press[s] > relax ? g_press[s] - relax : 0.0f;
            g_rim[s]   = g_rim[s] > relax ? g_rim[s] - relax : 0.0f;
            g_changed.push_back(s);
            if (g_press[s] > 0.0f || g_rim[s] > 0.0f) { ++k; continue; }
            g_isActive[s] = 0; // flat again: uploaded once more, then left alone
            g_active[k] = g_active.back();
            g_active.pop_back();
        }

        g_stamps = 0;
        if (!g_settings.stamp) return;
        // Sized like the client's own footprints (CreatureModelData, x the unit's scale): a frog
        // leaves a small trench, a tauren a wide one. Models without footprints (bats, birds and
        // other fliers that "walk" in the air) leave none.
        for (const actors::Actor& a : actors::Nearby())
        {
            if (!a.hasFootprint) continue;
            const float foot = a.footprintLength > a.footprintWidth ? a.footprintLength : a.footprintWidth;
            float r = (foot > 0.0f ? foot * kFootprintToTrench : (a.effectiveRadius > 0.0f ? a.effectiveRadius : 0.4f)) * g_settings.stampScale;
            if (r < 0.06f) r = 0.06f;
            if (r > 2.5f) r = 2.5f;
            Stamp(a.pos, r);
        }
        g_changedPending = !g_changed.empty();
    }
}
