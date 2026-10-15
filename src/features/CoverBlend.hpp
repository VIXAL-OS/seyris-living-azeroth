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
