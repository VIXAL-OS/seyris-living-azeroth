// The rooms behind window glass: one room per window (GlassWindows.cdbc, tools/gen_glass.py), the rules apart from the
// client (tests/glass checks them offline). The shader (features/GlassShader) finds the window a pixel of glass is in
// through a lookup table this builds (WindowTable: per WMO file and group, a grid of cells over its windows, each cell
// listing the windows that reach into it), ray-casts that window's own room box (RoomBoxOf: the opening extruded inward,
// a little wider than the window, the floor below its sill), shades its floor, walls and ceiling with real client
// interior textures (TextureSets, by the building's palette) and draws furniture in it as cut-outs at their depths (the
// atlas FurnitureAtlas draws, chosen by the building's class: RoomKindOf).
//
// "Rebuild the fake rooms behind Living Azeroth's window glass as convincing 3D interior-mapped rooms ... actual 3D fake
// room renders instead of flat 2D ones" (the user, 2026-10-18). Before, the rooms were cells of the world 3.5 x 3.2 yd
// that didn't line up with the windows (a pane could show two rooms with a seam), then blurred into flat stripes.
//
// The room's identity is the window's: its ordinal in its WMO and its group (the table's), hashed with the building's
// seed (env/GlassModel's LitHash) for whether it's lit at night; tools/gen_window_lights.py gives each window light the
// same keys (WindowRooms.cdbc), so the light in front dims exactly with the rooms behind its glass.
#pragma once

#include "GlassModel.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace wxl_livingazeroth::glass
{
    /// One window: the panes of one opening (mullions between them), in its WMO's own space (GlassWindows.cdbc).
    struct Window
    {
        uint32_t path = 0;                  // PathHash of its WMO
        uint32_t group = 0, ordinal = 0;    // its group; its index in its WMO (the room's key: ordinal, group)
        uint32_t kind = Kind::kClear;       // clear, leaded, frosted (a blurred room) or stained (a faint tinted one: its own grids)
        uint32_t flags = 0;
        float center[3] = {};
        float normal[3] = { 1, 0, 0 };      // unit; the shader puts the room on the side away from the eye
        float halfWidth = 0.5f, halfHeight = 0.5f;   // in WindowFrame's axes
        float thickness = 0.0f;             // how far off its plane the lookup reaches (yd; 0: rooms::kThickness)
    };

    /// The window's axes (the table's, the generator's and the shader's): along = unit(-n.y, n.x, 0), or x when n is
    /// within 0.01 of vertical (a skylight); up = n x along.
    void WindowFrame(const float n[3], float along[3], float up[3]);

    namespace rooms
    {
        /// The room box about the window (its frame, yd): wider than the opening by kSideOf of its width plus kSide, at
        /// least kMinWidth; the floor below the sill (kSill of its height, kSillMin .. kSillMax yd), the ceiling above the
        /// lintel (kHead of it, kHeadMin .. kHeadMax), at least kMinHeight tall.
        constexpr float kSideOf = 0.25f, kSide = 0.3f, kMinWidth = 2.2f;
        constexpr float kSill = 0.5f, kSillMin = 0.35f, kSillMax = 1.0f, kHead = 0.25f, kHeadMin = 0.25f, kHeadMax = 0.6f;
        constexpr float kMinHeight = 2.5f;
        /// The lookup: a pixel is in a window within kMargin yd of its rectangle and its thickness of its plane
        /// (GlassWindows.Thickness: its glass's depth off the plane and a little; kThickness without one).
        constexpr float kMargin = 0.03f, kThickness = 0.3f;
        /// The table: kWidth texels a row (RGBA, 32-bit floats), kEntryTexels per window in a cell, at most kMaxPerCell
        /// windows a cell (the shader's loop), at most kMaxCells cells a group.
        constexpr int kWidth = 1024, kEntryTexels = 8, kMaxPerCell = 32, kMaxCells = 1 << 16;
        /// A grid's cells: about kCellTarget windows' worth of space each, kCellMin .. kCellMax yd.
        constexpr float kCellMin = 1.5f, kCellMax = 24.0f;

        /// Faces: the shade by day (the light coming in at the window; side walls, floor, ceiling, back wall) and at night
        /// (the lamp's), the corners darker within kAoRadius yd (by kAo at the corner), the floor darker, the ceiling the
        /// dimmest.
        constexpr float kFaceDay[4] = { 0.85f, 0.6f, 0.4f, 0.95f };
        constexpr float kFaceNight[4] = { 0.9f, 0.8f, 0.55f, 0.85f };
        constexpr float kAoRadius = 0.6f, kAo = 0.55f, kAoMean = 0.82f;   // (far away the corners' mean)
        /// Farther from the window is darker: 1 / (1 + kFar x depth (yd)), plus c86.y by day (room::kFarDay).
        constexpr float kFar = 0.06f;
        /// The lamp: across (hashed) within kLampAcross of the half width, kLampHeight yd above the floor, kLampDepth[0] ..
        /// + [1] of the depth back; its light kLampBase + kLampGain / (1 + kLampK d^2), d in yd.
        constexpr float kLampAcross = 0.6f, kLampHeight = 1.5f, kLampDepth[2] = { 0.3f, 0.45f };
        constexpr float kLampK = 0.55f, kLampGain = 1.4f, kLampBase = 0.25f;
        /// The furniture: the back layer kBackGap yd in front of the back wall, the middle one kMidDepth[0] .. + [1] of the
        /// depth back; a prop's middle across within kPropAcross of (the half width - kPropHalf). The atlas: kTiles tiles of
        /// kTileSize yd square (x -half .. half across its middle, y 0 .. kTileSize up from the floor), 8 x 2 of them,
        /// kTileTexels texels each, its cut-outs' edges a signed distance within kSdfRange yd.
        constexpr float kBackGap = 0.25f, kMidDepth[2] = { 0.36f, 0.24f };
        constexpr float kPropAcross = 0.7f, kPropHalf = 0.9f;
        constexpr float kPropHash[3] = { 7.31f, 3.71f, 5.17f };   // the hash's sub-picks: a prop's place, the middle's depth, the gap
        constexpr int kTiles = 16, kTileTexels = 128, kAtlasWidth = 8 * kTileTexels, kAtlasHeight = 2 * kTileTexels;
        constexpr float kTileSize = 2.4f, kSdfRange = 0.06f;
        /// The furniture's stuff: stone and metal kStoneGrey; a thing's hue kThingTint of its rainbow (the rest grey);
        /// a glow (fire, candles) kGlow x (kGlowNight x the night's light + kGlowDay x the day's).
        constexpr float kStoneGrey = 0.62f, kThingTint = 0.55f;
        constexpr float kGlow[3] = { 1.0f, 0.58f, 0.24f };
        constexpr float kGlowNight = 1.6f, kGlowDay = 0.35f;
        /// The textures' mips by the footprint: kTextureTexels texels a repeat (the BLPs' 256).
        constexpr float kTextureTexels = 256.0f;
        /// Curtains, just behind the glass: kCurtainDepth yd in, a gap between them kGapMin .. + kGapSpan of the room's half
        /// width (its middle within kGapShift of it), folds kFolds a yard (+- kFoldAmp, gone where a pixel's footprint
        /// passes 1 / kFoldFade yd).
        constexpr float kCurtainDepth = 0.1f, kGapMin = 0.35f, kGapSpan = 0.35f, kGapShift = 0.3f, kFolds = 3.2f;
        constexpr float kFoldAmp = 0.35f, kFoldFade = 10.0f;
        /// The textures' share of the building's tint (the rest the texture's own colour), with RoomTextures on.
        constexpr float kTintWithTextures = 0.45f;
        /// The room's light is the light the client puts on the glass: its colour's luminance over the texel's (at least
        /// kTexelFloor; the painted glass's own pattern, its highlights and its lead, aren't the room's), at most
        /// kLightCap, x kGlassAlbedo (a window texture's glass about this bright: the room as bright as that glass would be).
        constexpr float kTexelFloor = 0.04f, kLightCap = 2.0f, kGlassAlbedo = 0.55f;
        /// The room's look fades to its flat mean from kDetailNear to kDetailFar yd (textures, furniture, corners).
        constexpr float kDetailNear = 45.0f, kDetailFar = 90.0f;
    }

    /// The room behind a window (its frame: across a, up b about the window's middle; c into the room), yd.
    struct RoomBox { float halfWidth = 1.1f, floor = -1.25f, ceiling = 1.25f; };
    RoomBox RoomBoxOf(float halfWidth, float halfHeight);

    // --- the furniture --------------------------------------------------------------------------------------------
    enum Prop : uint8_t
    {
        kPropNone = 0, kPropTable = 1, kPropBed = 2, kPropCounter = 3, kPropShelves = 4, kPropFireplace = 5, kPropWardrobe = 6,
        kPropPainting = 7, kPropRack = 8, kPropBanner = 9, kPropBarrels = 10, kPropDesk = 11, kPropRoundTable = 12,
        kPropArmour = 13, kPropPews = 14, kPropCandles = 15
    };
    /// A class's rooms: how deep (yd), and the props the middle and the back layers pick from (four each, by the room's
    /// hash).
    struct RoomKind { float depth = 4.0f; uint8_t mid[4] = {}; uint8_t back[4] = {}; };
    const RoomKind& RoomKindOf(uint32_t cls);

    /// The atlas: kAtlasWidth x kAtlasHeight texels, A8R8G8B8 as 0xAARRGGBB: A the cut-out's signed distance (0.5 at its
    /// edge, more inside: 0.5 + d / (2 kSdfRange), d yd inside), R its shade (the bevel darker at the edges), G its stuff
    /// (0 wood, 1/3 cloth, 2/3 stone and metal, 1 a glow: fire, candles), B a thing's hue (0: none; books, bottles).
    void FurnitureAtlas(std::vector<uint32_t>& texels);
    /// The same at a point of a tile (x across its middle, y up from the floor, yd): signed distance (negative inside),
    /// and its stuff (0..3), for the tests.
    float PropDistance(int tile, float x, float y, int* stuff = nullptr);

    // --- the textures -----------------------------------------------------------------------------------------------
    /// A palette's interior: the floor, walls, ceiling and the furniture's wood (client BLPs), and how many yards each
    /// repeats over.
    struct TextureSet
    {
        const char* name;
        const char* floor; const char* wall; const char* ceiling; const char* wood;
        float scale[4];   // yd per repeat: floor, wall, ceiling, wood
    };
    constexpr int kTextureSets = 10;
    const TextureSet& TextureSetAt(int index);
    /// The set for a style (its palette and class: a human farm the rustic one); a building with no style the human one.
    int TextureSetOf(uint32_t palette, uint32_t cls);

    // --- the lookup table -------------------------------------------------------------------------------------------
    /// Every WMO's windows, per WMO and group a grid over them, as one texture of RGBA floats (kWidth wide): per grid
    /// cell a texel (its first window's texel, how many); per window in a cell kEntryTexels texels:
    ///   0..2: its rows across, up and out (xyz, w the offset) over (half width + kMargin), (half height + kMargin) and
    ///         its thickness: the point (WMO space, w = 1) dotted with them within 1 inside it;
    ///   3: (half width + kMargin, half height + kMargin, its thickness, its ordinal).
    /// A cell lists its windows by ordinal: where two overlap the shader takes the lower (as gen_window_lights does);
    ///   4: (the room's half width, its floor, its ceiling (yd about the window's middle), 1 frosted: a room only for a
    ///      batch that lets frosted windows have one, c26.y);
    ///   5: (its middle xyz, its group).
    /// Stained windows have grids of their own (Find's `stained`): a stained batch's pixel finds only them, any other
    /// batch's never (so the others find what they found before stained glass had rooms).
    class WindowTable
    {
    public:
        struct Grid
        {
            float min[3] = {};      // the corner, WMO space
            float cell = 1.0f;      // yd
            uint32_t n[3] = {};     // cells across x, y, z
            uint32_t base = 0;      // the first cell's texel
            uint32_t windows = 0, entries = 0, worst = 0;   // windows, entries over its cells, the most in a cell
        };
        static constexpr uint32_t kStainedGrid = 0x80000000u;   // a stained window's group in its grid's key
        void Build(std::vector<Window> windows);
        const Grid* Find(uint32_t path, uint32_t group, bool stained = false) const;
        /// The texels (RGBA), kWidth x Height().
        const std::vector<float>& Texels() const { return m_texels; }
        int Height() const { return m_height; }
        size_t Windows() const { return m_windows.size(); }
        size_t Grids() const { return m_grids.size(); }
        const std::vector<Window>& All() const { return m_windows; }
        /// The window the shader finds at `local` (WMO space) in a grid, as it does (the same texels, its arithmetic):
        /// the entry's first texel, or -1. `rows` (optional): the point dotted with its rows (across, up, out; within 1).
        int Lookup(const Grid& g, const float local[3], float rows[3] = nullptr) const;
        /// An entry's texel k (RGBA).
        const float* Texel(int index) const { return &m_texels[static_cast<size_t>(index) * 4]; }

    private:
        static uint64_t Key(uint32_t path, uint32_t group) { return (static_cast<uint64_t>(path) << 32) | group; }
        std::vector<Window> m_windows;
        std::unordered_map<uint64_t, Grid> m_grids;
        std::vector<float> m_texels;
        int m_height = 1;
    };

    /// The shader's ray through a window into its room (the CPU's twin): the point and the way to the eye in WMO space
    /// (unit), the table's entry, the room's depth: which face it meets (RoomFace), how far along the ray (yd), where (the
    /// window's frame: across, up, deep).
    struct RoomRay { RoomFace face = kBackWall; float t = 0; float hit[3] = {}; float across = 0, up = 0; bool ceiling = false; };
    bool CastRoom(const WindowTable& table, int entry, const float local[3], const float toEye[3], float depth, RoomRay& out);

    /// A room's depth (yd): its class's x (1 + kDepthSpread (h - 0.5)), h its hash's last.
    constexpr float kDepthSpread = 0.3f;
    float RoomDepthOf(uint32_t cls, float hash);
}
