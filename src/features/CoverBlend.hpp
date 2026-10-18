// Rules of how the surface cover mixes its look at a spot (features/SurfaceCover.cpp, Combine,
// Sample and UploadLook), kept apart so they can be checked offline (tests/cover). Snow comes out
// exactly as before; they matter for covers that aren't white, like ash, where the plain cover
// colour (snow white) would show as pale fringes and blotches.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace wxl_livingazeroth::cover::blend
{
    /// Which cover textures hold the `slots` texture slots (slotId, 0 = free): the most present by `count` (sampled
    /// cells per texture ID; [0] unused), a resident counting kResidentBonus x, so textures near the cut don't trade
    /// places at every recount -- each trade re-uploads every level's texture weights. True when a slot changed.
    /// With more textures in view than slots (Northrend: snow, ice and the coast sands), the hurried recount traded
    /// them every 2 frames: the cover's draw went from 0.1 to 6 ms a frame ("huge framerate dip", the user,
    /// 2026-10-11).
    constexpr float kResidentBonus = 1.5f;
    inline bool ChooseSlots(const std::vector<unsigned>& count, int* slotId, int slots)
    {
        const int ids = static_cast<int>(count.size()) - 1;
        auto resident = [&](int id) { return std::find(slotId, slotId + slots, id) != slotId + slots; };
        std::vector<int> wanted;
        for (int id = 1; id <= ids; ++id) if (count[id]) wanted.push_back(id);
        auto score = [&](int id) { return static_cast<float>(count[id]) * (resident(id) ? kResidentBonus : 1.0f); };
        std::stable_sort(wanted.begin(), wanted.end(), [&](int a, int b) { return score(a) > score(b); });
        if (static_cast<int>(wanted.size()) > slots) wanted.resize(static_cast<size_t>(slots));
        bool changed = false;
        for (int i = 0; i < slots; ++i) // residents not wanted any more free their slot
            if (slotId[i] && std::find(wanted.begin(), wanted.end(), slotId[i]) == wanted.end()) { slotId[i] = 0; changed = true; }
        for (int id : wanted)
        {
            if (resident(id)) continue;
            for (int i = 0; i < slots; ++i) if (!slotId[i]) { slotId[i] = id; changed = true; break; }
        }
        return changed;
    }

    /// Whether a look naming a texture no slot holds should bring the next recount forward (a place just loaded in):
    /// only while there is room for it. With more textures in view than slots the least present ones go without,
    /// and the recount keeps its own pace.
    inline bool HurryRecount(unsigned texturesInView, int slots) { return texturesInView <= static_cast<unsigned>(slots); }

    /// How much the bare ground's fallen-snow look counts at a spot, from how much of it is painted
    /// with a cover (covered, 0..1) and how much of that is snow or another cover. All of it where
    /// nothing is painted, none from a quarter covered up. Beside painted covers that aren't snow
    /// (sand, ash, mud) it counts only by the painted share that is snow, so their own look keeps
    /// to their ragged edge. Fallen snow hardly lies beside them anyway (the snow share drops by
    /// the same quarter rule).
    inline float BareSnowLookShare(float covered, float snowPainted, float otherPainted)
    {
        float share = 1.0f - (covered * 4.0f > 1.0f ? 1.0f : covered * 4.0f);
        if (otherPainted > 0.0f) share *= snowPainted / (snowPainted + otherPainted);
        return share;
    }

    /// Level 0's cover depth with its trench, as uploaded to the GPU and drawn: the undeformed depth,
    /// raised by the rim (rimShare = the rows' Rim x the panel's multiplier, rim = the slot's 0..1),
    /// pressed down by feet (press 0..1) except for the cover's stiffness (0..1), all of that by k
    /// (trenches fade out toward the level's edge). The client's footprints are lifted by the same
    /// number (features/CoverPrints), so they lie on what is drawn.
    inline float DeformedDepth(float cover, float rimShare, float rim, float press, float stiffness, float k)
    {
        const float deformed = cover * (1.0f + rimShare * rim) * (1.0f - press * (1.0f - stiffness));
        return cover + (deformed - cover) * k;
    }

    // --- mud and trenches (ChromieCraft fork, 2026-10-17: Elwynn's road in a thunderstorm grew a dark, jagged, raised
    // trail behind every walker -- "it also looks like it's trying to draw mud trenches???") ---

    /// How much of a moisture-gated cover (mud) may form where `sealedShare` (0..1) of the ground is painted with sealed
    /// ground (GroundMaterial kFlagSealed: cobbles, paving). Elwynn's road is cobblestones over its dirt texture, the
    /// dirt showing at 7..67% between them (alpha maps), and that dirt grew mud. All of it on open ground (up to
    /// kSealedFrom paved: a stone or two in a verge), none from kSealedFull (a road), smooth between: mud on the
    /// dirt verges, none on the road.
    constexpr float kSealedFrom = 0.05f, kSealedFull = 0.30f;
    inline float MudAllowed(float sealedShare)
    {
        if (!(sealedShare > kSealedFrom)) return 1.0f;
        if (sealedShare >= kSealedFull) return 0.0f;
        const float t = (sealedShare - kSealedFrom) / (kSealedFull - kSealedFrom);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }

    /// One stamp of a trench at a grid vertex `d` yd from where the unit stands: how far it presses (0..1) and its rim
    /// (0..1), for a trench of `radius` yd on a grid of `cell` yd.
    ///
    /// The original: pressed flat inside 0.7 r, its wall to r, a rim from 0.9 r up to 1.25 r and down by 1.6 r. A
    /// human's 0.4 yd trench gave a wall 0.12 yd wide and a rim 0.28 yd wide on level 0's 0.25 yd grid: steps the grid
    /// can't draw, so a walked trench came out as a staircase along the grid's lines -- the jagged edge. The edge
    /// filter: no wall or rim flank narrower than `softness` cells (0 = the original widths), smoothstep-shaped, the
    /// wall around the same middle (0.85 r) and the rim's crest at the wall's top at least. A trench too small for a
    /// wall that wide around its middle (r under ~0.22 yd at 1.5 cells: cats, critters) becomes a dimple one wall wide,
    /// as much shallower as its middle is short of half the wall -- the grid can't draw it smaller, so less of it. From
    /// a trench of about 1.25 yd (0.3 r = 1.5 cells) up the widths are the original ones.
    struct TrenchStamp { float press = 0.0f, rim = 0.0f; };
    struct TrenchShape { float pressIn, pressOut, rimPeak, rimHalf, amplitude; };
    inline TrenchShape TrenchShapeFor(float radius, float cell, float softness)
    {
        const float minWidth = (softness > 0.0f ? softness : 0.0f) * (cell > 0.0f ? cell : 0.0f);
        const float wall = 0.3f * radius > minWidth ? 0.3f * radius : minWidth;
        const float mid = 0.85f * radius;
        TrenchShape s;
        s.amplitude = 1.0f;
        if (mid >= 0.5f * wall) { s.pressIn = mid - 0.5f * wall; s.pressOut = mid + 0.5f * wall; }
        else
        {
            s.pressIn = 0.0f;
            s.pressOut = wall;
            s.amplitude = wall > 0.0f ? mid / (0.5f * wall) : 1.0f;
        }
        s.rimPeak = 1.25f * radius > s.pressOut ? 1.25f * radius : s.pressOut;
        s.rimHalf = 0.35f * radius > minWidth ? 0.35f * radius : minWidth;
        return s;
    }
    inline float SmoothStep(float a, float b, float v)
    {
        if (!(b > a)) return v < a ? 0.0f : 1.0f;
        float t = (v - a) / (b - a);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        return t * t * (3.0f - 2.0f * t);
    }
    inline TrenchStamp TrenchProfile(float d, const TrenchShape& s)
    {
        TrenchStamp out;
        out.press = s.amplitude * (1.0f - SmoothStep(s.pressIn, s.pressOut, d));
        const float off = d > s.rimPeak ? d - s.rimPeak : s.rimPeak - d;
        out.rim = s.amplitude * (1.0f - SmoothStep(0.0f, s.rimHalf, off));
        return out;
    }
    /// How far out a stamp reaches (its rim's outer foot), for the loop over the grid.
    inline float TrenchReach(const TrenchShape& s) { return s.rimPeak + s.rimHalf; }

    /// A rim at a vertex under a new stamp: the foot presses the rim already there down by its press -- a later step
    /// flattens what an earlier one pushed up -- and the new stamp's rim rises only as far as the ground there isn't
    /// already trodden down (`pressedBefore`, the vertex's press before this stamp): a walker leaves no rim behind itself
    /// in its own trench. Before, every step's whole ring stayed (the larger of the two), so a walked path became one
    /// raised band 3.2 r wide -- for stiff mud the whole trail stood up, a dark strip -- instead of a trench between two
    /// rims.
    inline float StampedRim(float before, float press, float ring, float pressedBefore)
    {
        auto c = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
        const float kept = before * (1.0f - c(press));
        const float raised = ring * (1.0f - c(pressedBefore));
        return kept > raised ? kept : raised;
    }

    /// Mud ruts fill in with rain: the share of their relax time left at this rain (StormModel RainLevel 0..1), for a
    /// cell `gated` (0..1) moisture-gated, `fill` (0..1) how much heavy rain shortens it. 1 = as the table says.
    inline float RainRelaxShare(float rain, float gated, float fill)
    {
        auto c = [](float v) { return v < 0.0f || !(v == v) ? 0.0f : (v > 1.0f ? 1.0f : v); };
        return 1.0f - c(fill) * c(rain) * c(gated);
    }

    /// The two strongest of `count` cover textures by share (indices, -1 = none, the stronger
    /// first) and the shares they draw with. The cover draws two textures per spot; with more than
    /// two, those two take the others' share in proportion, so it doesn't fall to the plain cover
    /// colour (three ash textures meeting stay ash). Up to two, the shares are exactly as given.
    inline void StrongestTwo(const float* share, int count, int& a, int& c, float& shareA, float& shareC)
    {
        a = -1; c = -1;
        for (int i = 0; i < count; ++i)
        {
            if (a < 0 || share[i] > share[a]) { c = a; a = i; }
            else if (c < 0 || share[i] > share[c]) c = i;
        }
        shareA = a >= 0 ? share[a] : 0.0f;
        shareC = c >= 0 ? share[c] : 0.0f;
        if (count <= 2) return;
        float all = 0.0f;
        for (int i = 0; i < count; ++i) all += share[i];
        const float top = shareA + shareC;
        if (top > 0.0f) { shareA *= all / top; shareC *= all / top; }
    }

    /// A ground-like cover (one with a tint -- sand, ash, mud: the ground's own texture, not snow's white) lying on
    /// ground the alpha maps paint only partly with it: `bareShare` of the ground there is other textures, whose
    /// average colour is `ground`. Drawn as its own texture alone, the cover was paler than the blend it lay on, in
    /// patches wherever its coverage crossed the edge noise (Borean Tundra's shores: "borean tundra sand is still
    /// kinda whack sadly", the user, 2026-10-15). The shader leans the cover toward its tint by the tint's strength
    /// (colour = lerp(cover, tint, strength)), so the ground's colour goes in there: lerp(lerp(cover, tint, s),
    /// ground, f) is the one lerp(cover, T, A) with 1 - A = (1 - s)(1 - f), T A = tint s (1 - f) + ground f -- a
    /// convex mix, so T stays a colour. No bare share (or no tint: snow) leaves both as they were.
    inline void MatchGround(float tint[3], float& strength, const float ground[3], float bareShare)
    {
        const float f = std::clamp(bareShare, 0.0f, 1.0f), s = std::clamp(strength, 0.0f, 1.0f);
        const float a = 1.0f - (1.0f - s) * (1.0f - f);
        if (f <= 0.0f || a <= 0.0f) return;
        for (int k = 0; k < 3; ++k) tint[k] = (tint[k] * s * (1.0f - f) + ground[k] * f) / a;
        strength = a;
    }

    /// The cover texture weights (8 slots, 0..255) uploaded for a cell with no cover look of its own
    /// (bare ground, no cover row and no fallen snow): those of its neighbour with the most texture,
    /// or none. `neighbours` holds each neighbour's weights, null for one that has no look either.
    /// The weights are filtered across cells, so from a cell of ash to bare rock beside it the ash's
    /// share fell toward nothing while its thinning cover still showed, and the plain cover colour
    /// (snow white) took the rest: a pale band along ash edges ("near some sharp edges it does look
    /// too white", the user, 2026-10-11). Borrowed, the cover keeps its own colour to its edge. A
    /// cell with a look keeps its own weights: plain snow has no texture, its white is meant.
    inline void BorrowedWeights(const uint8_t* const* neighbours, int count, uint8_t out[8])
    {
        int best = -1, bestSum = 0;
        for (int n = 0; n < count; ++n)
        {
            if (!neighbours[n]) continue;
            int sum = 0;
            for (int s = 0; s < 8; ++s) sum += neighbours[n][s];
            if (sum > bestSum) { bestSum = sum; best = n; }
        }
        for (int s = 0; s < 8; ++s) out[s] = best >= 0 ? neighbours[best][s] : 0;
    }
}
