// Two rules of how the surface cover mixes its look at a spot (features/SurfaceCover.cpp, Combine
// and Sample), kept apart so they can be checked offline (tests/cover). Snow comes out exactly as
// before; they matter for covers that aren't white, like ash, where the plain cover colour (snow
// white) would show as pale fringes and blotches.
#pragma once

namespace wxl_livingazeroth::cover::blend
{
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
}
