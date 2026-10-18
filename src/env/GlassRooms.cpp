#include "GlassRooms.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace wxl_livingazeroth::glass
{
    namespace
    {
        float Dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
        float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    }

    void WindowFrame(const float n[3], float along[3], float up[3])
    {
        float a[3] = { -n[1], n[0], 0.0f };
        if (a[0] * a[0] + a[1] * a[1] < 1e-4f) { a[0] = 1.0f; a[1] = 0.0f; }
        const float l = std::sqrt(a[0] * a[0] + a[1] * a[1]);
        along[0] = a[0] / l; along[1] = a[1] / l; along[2] = 0.0f;
        up[0] = n[1] * along[2] - n[2] * along[1];
        up[1] = n[2] * along[0] - n[0] * along[2];
        up[2] = n[0] * along[1] - n[1] * along[0];
    }

    RoomBox RoomBoxOf(float hw, float hh)
    {
        using namespace rooms;
        RoomBox b;
        b.halfWidth = std::max(hw * (1.0f + 2.0f * kSideOf) + kSide, 0.5f * kMinWidth);
        const float h = 2.0f * hh;
        const float sill = Clamp(kSill * h, kSillMin, kSillMax), head = Clamp(kHead * h, kHeadMin, kHeadMax);
        b.floor = -hh - sill;
        b.ceiling = hh + head;
        if (b.ceiling - b.floor < kMinHeight) b.ceiling = b.floor + kMinHeight;
        return b;
    }

    const RoomKind& RoomKindOf(uint32_t cls)
    {
        // Per class (RoomClass): how deep, the middle layer's props and the back layer's (four each, by the room's hash).
        static const RoomKind kinds[kClassCount] = {
            /* other   */ { 4.0f, { kPropTable, kPropNone, kPropBarrels, kPropDesk }, { kPropShelves, kPropWardrobe, kPropPainting, kPropFireplace } },
            /* house   */ { 3.6f, { kPropTable, kPropBed, kPropNone, kPropTable }, { kPropFireplace, kPropWardrobe, kPropShelves, kPropPainting } },
            /* farm    */ { 3.5f, { kPropTable, kPropBarrels, kPropNone, kPropBed }, { kPropFireplace, kPropShelves, kPropBarrels, kPropPainting } },
            /* inn     */ { 4.6f, { kPropRoundTable, kPropTable, kPropCounter, kPropRoundTable }, { kPropFireplace, kPropShelves, kPropBarrels, kPropPainting } },
            /* shop    */ { 4.2f, { kPropCounter, kPropCounter, kPropBarrels, kPropDesk }, { kPropShelves, kPropShelves, kPropWardrobe, kPropShelves } },
            /* temple  */ { 5.5f, { kPropPews, kPropNone, kPropPews, kPropCandles }, { kPropBanner, kPropPainting, kPropCandles, kPropBanner } },
            /* keep    */ { 4.6f, { kPropRack, kPropTable, kPropArmour, kPropNone }, { kPropBanner, kPropRack, kPropShelves, kPropFireplace } },
            /* tower   */ { 4.0f, { kPropDesk, kPropTable, kPropNone, kPropDesk }, { kPropShelves, kPropShelves, kPropPainting, kPropCandles } },
            /* city    */ { 4.0f, { kPropTable, kPropCounter, kPropBed, kPropNone }, { kPropShelves, kPropFireplace, kPropWardrobe, kPropPainting } },
            /* ship    */ { 3.0f, { kPropBarrels, kPropTable, kPropNone, kPropBarrels }, { kPropShelves, kPropNone, kPropPainting, kPropBarrels } },
            /* forge   */ { 4.6f, { kPropRack, kPropCounter, kPropBarrels, kPropArmour }, { kPropFireplace, kPropRack, kPropShelves, kPropFireplace } },
            /* commons */ { 4.4f, { kPropCounter, kPropRoundTable, kPropTable, kPropBarrels }, { kPropShelves, kPropFireplace, kPropShelves, kPropBanner } },
            /* quarter */ { 3.6f, { kPropTable, kPropBed, kPropNone, kPropTable }, { kPropFireplace, kPropWardrobe, kPropShelves, kPropPainting } },
            /* cavern  */ { 3.8f, { kPropBarrels, kPropTable, kPropNone, kPropNone }, { kPropShelves, kPropFireplace, kPropNone, kPropBarrels } },
        };
        return kinds[cls < kClassCount ? cls : kClassOther];
    }

    float RoomDepthOf(uint32_t cls, float hash)
    {
        return RoomKindOf(cls).depth * ((hash - 0.5f) * kDepthSpread + 1.0f);
    }

    // --- the furniture's cut-outs ----------------------------------------------------------------------------------------
    namespace
    {
        enum Stuff : int { kWood = 0, kCloth = 1, kStone = 2, kGlow = 3 };
        // A rounded box (half sizes hx, hy, corners r) or a disc (radius hx; hy < 0), its stuff, shade and a thing's hue.
        struct Shape { float cx, cy, hx, hy, r; int stuff; float shade; float hue; };

        float ShapeDistance(const Shape& s, float x, float y)
        {
            const float dx = x - s.cx, dy = y - s.cy;
            if (s.hy < 0.0f) return std::sqrt(dx * dx + dy * dy) - s.hx;
            const float qx = std::fabs(dx) - (s.hx - s.r), qy = std::fabs(dy) - (s.hy - s.r);
            const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
            return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - s.r;
        }

        void Box(std::vector<Shape>& v, float cx, float cy, float hx, float hy, int stuff, float shade, float hue = 0.0f, float r = 0.012f)
        {
            v.push_back({ cx, cy, hx, hy, std::min(r, std::min(hx, hy)), stuff, shade, hue });
        }
        void Disc(std::vector<Shape>& v, float cx, float cy, float radius, int stuff, float shade, float hue = 0.0f)
        {
            v.push_back({ cx, cy, radius, -1.0f, 0.0f, stuff, shade, hue });
        }

        // A steady little generator for the books, bottles and jars.
        struct Rng
        {
            uint32_t s;
            float Next() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; }
        };

        // A row of things on a shelf from x0 to x1 standing on y: books (tall, thin, hued) or jars and bottles.
        void Things(std::vector<Shape>& v, Rng& rng, float x0, float x1, float y, float tallest, bool books)
        {
            float x = x0 + 0.02f;
            while (x < x1 - 0.05f)
            {
                const float gap = rng.Next();
                if (gap < 0.12f) { x += 0.08f + 0.1f * rng.Next(); continue; }
                const float w = books ? 0.025f + 0.025f * rng.Next() : 0.04f + 0.05f * rng.Next();
                const float h = (books ? 0.55f + 0.4f * rng.Next() : 0.35f + 0.5f * rng.Next()) * tallest;
                if (x + 2 * w > x1) break;
                const float hue = 0.05f + 0.95f * rng.Next();
                if (books) Box(v, x + w, y + 0.5f * h, w, 0.5f * h, kCloth, 0.55f + 0.35f * rng.Next(), hue, 0.004f);
                else
                {
                    Box(v, x + w, y + 0.4f * h, w, 0.4f * h, kStone, 0.6f + 0.3f * rng.Next(), hue, 0.02f);
                    Box(v, x + w, y + 0.85f * h, 0.35f * w, 0.15f * h, kStone, 0.5f, hue, 0.004f);   // a neck
                }
                x += 2 * w + 0.008f + (books ? 0.0f : 0.03f);
            }
        }

        std::vector<Shape> Tile(int tile)
        {
            std::vector<Shape> v;
            Rng rng{ 977u + 131u * static_cast<uint32_t>(tile) };
            switch (tile)
            {
            case kPropTable:   // a table and two chairs, side on
                Box(v, 0.0f, 0.80f, 0.72f, 0.045f, kWood, 0.9f);
                Box(v, 0.0f, 0.71f, 0.62f, 0.05f, kWood, 0.62f);
                for (float s : { -1.0f, 1.0f })
                {
                    Box(v, 0.6f * s, 0.38f, 0.045f, 0.38f, kWood, 0.62f);
                    Box(v, 0.98f * s, 0.47f, 0.2f, 0.035f, kWood, 0.78f);
                    Box(v, 1.15f * s, 0.62f, 0.035f, 0.62f, kWood, 0.6f);
                    Box(v, 0.82f * s, 0.23f, 0.03f, 0.23f, kWood, 0.6f);
                    Box(v, 1.15f * s, 1.05f, 0.05f, 0.17f, kWood, 0.7f);
                }
                Box(v, 0.28f, 0.9f, 0.05f, 0.06f, kStone, 0.75f, 0.08f, 0.015f);   // a mug
                Disc(v, -0.25f, 0.88f, 0.07f, kStone, 0.8f, 0.0f);                 // a bowl
                Box(v, -0.25f, 0.85f, 0.11f, 0.03f, kStone, 0.7f);
                break;
            case kPropBed:     // a bed, side on: the frame, the mattress, a blanket, a pillow, the head and foot boards
                Box(v, 0.0f, 0.28f, 1.02f, 0.11f, kWood, 0.65f);
                for (float s : { -1.0f, 1.0f }) Box(v, 0.97f * s, 0.1f, 0.05f, 0.1f, kWood, 0.55f);
                Box(v, 0.0f, 0.45f, 0.98f, 0.07f, kStone, 0.92f, 0.0f, 0.04f);
                Box(v, 0.18f, 0.48f, 0.82f, 0.08f, kCloth, 0.8f, 0.0f, 0.05f);
                Box(v, 0.18f, 0.36f, 0.82f, 0.05f, kCloth, 0.65f, 0.0f, 0.02f);
                Box(v, -0.8f, 0.58f, 0.15f, 0.06f, kStone, 1.0f, 0.0f, 0.05f);
                Box(v, -1.05f, 0.62f, 0.055f, 0.62f, kWood, 0.68f);
                Box(v, -1.05f, 1.27f, 0.09f, 0.04f, kWood, 0.8f);
                Box(v, 1.05f, 0.42f, 0.05f, 0.42f, kWood, 0.68f);
                break;
            case kPropCounter: // a counter or a bar, things on it
                Box(v, 0.0f, 0.5f, 1.12f, 0.5f, kWood, 0.62f);
                for (float s : { -1.0f, 1.0f }) Box(v, 0.55f * s, 0.5f, 0.44f, 0.36f, kWood, 0.48f, 0.0f, 0.03f);
                Box(v, 0.0f, 1.03f, 1.16f, 0.04f, kWood, 0.88f);
                Things(v, rng, -1.05f, -0.15f, 1.07f, 0.3f, false);
                Things(v, rng, 0.35f, 1.05f, 1.07f, 0.3f, false);
                Box(v, 0.1f, 1.12f, 0.15f, 0.05f, kStone, 0.8f, 0.12f, 0.01f);   // a tray, a scale
                break;
            case kPropShelves: // a bookcase: its sides, top and back, five planks, books and jars
                Box(v, 0.0f, 1.1f, 0.76f, 1.08f, kWood, 0.32f, 0.0f, 0.0f);
                for (float s : { -1.0f, 1.0f }) Box(v, 0.79f * s, 1.1f, 0.04f, 1.1f, kWood, 0.66f);
                Box(v, 0.0f, 2.18f, 0.84f, 0.045f, kWood, 0.8f);
                for (float y : { 0.06f, 0.56f, 1.06f, 1.56f })
                {
                    Box(v, 0.0f, y, 0.76f, 0.03f, kWood, 0.8f);
                    Things(v, rng, -0.74f, 0.74f, y + 0.03f, 0.44f, y > 0.3f && rng.Next() < 0.75f);
                }
                Box(v, 0.0f, 2.06f, 0.76f, 0.03f, kWood, 0.8f);
                break;
            case kPropFireplace: // a hearth: the stone surround, the chimney breast, the mantel, the opening and its fire
                Box(v, 0.0f, 1.85f, 0.46f, 0.55f, kStone, 0.6f, 0.0f, 0.0f);
                Box(v, 0.0f, 0.66f, 0.72f, 0.66f, kStone, 0.72f, 0.0f, 0.03f);
                Box(v, 0.0f, 1.34f, 0.84f, 0.05f, kWood, 0.82f);
                Box(v, 0.0f, 0.4f, 0.44f, 0.4f, kStone, 0.1f, 0.0f, 0.08f);
                Box(v, 0.0f, 0.07f, 0.34f, 0.045f, kWood, 0.3f);
                Box(v, 0.0f, 0.2f, 0.28f, 0.11f, kGlow, 1.0f, 0.0f, 0.1f);
                Disc(v, -0.08f, 0.3f, 0.14f, kGlow, 0.85f);
                Disc(v, 0.12f, 0.33f, 0.1f, kGlow, 0.75f);
                Disc(v, 0.0f, 0.44f, 0.07f, kGlow, 0.6f);
                Box(v, -0.55f, 1.45f, 0.035f, 0.07f, kStone, 0.95f);
                Disc(v, -0.55f, 1.56f, 0.03f, kGlow, 0.9f);
                Box(v, 0.5f, 1.47f, 0.07f, 0.09f, kStone, 0.75f, 0.55f, 0.03f);   // a vase
                break;
            case kPropWardrobe: // a wardrobe: its body, crown, doors with panels and knobs, feet
                Box(v, 0.0f, 1.08f, 0.6f, 1.02f, kWood, 0.62f);
                Box(v, 0.0f, 2.14f, 0.66f, 0.05f, kWood, 0.82f);
                for (float s : { -1.0f, 1.0f })
                {
                    Box(v, 0.3f * s, 1.32f, 0.23f, 0.52f, kWood, 0.74f, 0.0f, 0.03f);
                    Box(v, 0.3f * s, 0.5f, 0.23f, 0.22f, kWood, 0.74f, 0.0f, 0.03f);
                    Disc(v, 0.06f * s, 1.05f, 0.025f, kStone, 0.95f, 0.13f);
                    Box(v, 0.52f * s, 0.04f, 0.06f, 0.04f, kWood, 0.5f);
                }
                Box(v, 0.0f, 1.08f, 0.01f, 0.98f, kWood, 0.3f, 0.0f, 0.0f);
                break;
            case kPropPainting: // a painting on the wall, a chest under it
                Box(v, 0.0f, 1.55f, 0.52f, 0.37f, kWood, 0.9f, 0.13f);
                Box(v, 0.0f, 1.55f, 0.43f, 0.28f, kStone, 0.6f, 0.33f, 0.0f);
                Disc(v, 0.12f, 1.62f, 0.09f, kStone, 0.85f, 0.15f);
                Box(v, 0.0f, 1.42f, 0.43f, 0.06f, kStone, 0.45f, 0.3f, 0.0f);
                Box(v, 0.0f, 0.24f, 0.48f, 0.24f, kWood, 0.62f, 0.0f, 0.04f);
                Box(v, 0.0f, 0.44f, 0.5f, 0.04f, kWood, 0.78f, 0.0f, 0.03f);
                Box(v, 0.0f, 0.3f, 0.06f, 0.05f, kStone, 0.9f, 0.13f);
                break;
            case kPropRack: // a weapon rack: posts, bars, swords, spears and a shield
                for (float s : { -1.0f, 1.0f }) Box(v, 0.78f * s, 0.72f, 0.045f, 0.72f, kWood, 0.62f);
                Box(v, 0.0f, 0.3f, 0.8f, 0.035f, kWood, 0.7f);
                Box(v, 0.0f, 1.3f, 0.8f, 0.035f, kWood, 0.7f);
                for (int i = 0; i < 6; ++i)
                {
                    const float x = -0.6f + 0.24f * i;
                    if (i % 3 == 2) { Box(v, x, 1.0f, 0.012f, 1.0f, kWood, 0.6f); Box(v, x, 2.05f, 0.03f, 0.08f, kStone, 0.9f, 0.0f, 0.01f); }
                    else { Box(v, x, 0.8f, 0.02f, 0.62f, kStone, 0.88f, 0.0f, 0.01f); Box(v, x, 1.48f, 0.08f, 0.015f, kStone, 0.7f); Box(v, x, 1.58f, 0.018f, 0.08f, kWood, 0.5f); }
                }
                Disc(v, 0.95f, 0.55f, 0.24f, kWood, 0.7f, 0.02f);
                Disc(v, 0.95f, 0.55f, 0.07f, kStone, 0.9f, 0.13f);
                break;
            case kPropBanner: // a banner on a pole, its emblem
                Box(v, 0.0f, 2.22f, 0.42f, 0.025f, kWood, 0.7f);
                Box(v, 0.0f, 1.55f, 0.3f, 0.65f, kCloth, 0.88f, 0.0f, 0.02f);
                Box(v, -0.15f, 0.88f, 0.15f, 0.08f, kCloth, 0.75f, 0.0f, 0.06f);
                Box(v, 0.15f, 0.88f, 0.15f, 0.08f, kCloth, 0.75f, 0.0f, 0.06f);
                Disc(v, 0.0f, 1.65f, 0.14f, kStone, 0.9f, 0.13f);
                Box(v, 0.0f, 1.65f, 0.03f, 0.2f, kStone, 0.8f, 0.13f, 0.01f);
                break;
            case kPropBarrels: // two barrels and a crate
                for (float x : { -0.62f, 0.02f })
                {
                    Box(v, x, 0.42f, 0.28f, 0.42f, kWood, 0.62f, 0.0f, 0.1f);
                    for (float y : { 0.12f, 0.42f, 0.72f }) Box(v, x, y, 0.29f, 0.025f, kStone, 0.55f, 0.0f, 0.01f);
                }
                Box(v, 0.72f, 0.33f, 0.36f, 0.33f, kWood, 0.72f, 0.0f, 0.02f);
                Box(v, 0.72f, 0.33f, 0.36f, 0.02f, kWood, 0.5f, 0.0f, 0.0f);
                Box(v, 0.72f, 0.33f, 0.02f, 0.33f, kWood, 0.5f, 0.0f, 0.0f);
                Box(v, -0.3f, 0.95f, 0.16f, 0.12f, kCloth, 0.75f, 0.1f, 0.05f);   // a sack
                break;
            case kPropDesk: // a writing desk, its drawers, a lit candle and books
                Box(v, 0.0f, 0.78f, 0.68f, 0.04f, kWood, 0.88f);
                for (float s : { -1.0f, 1.0f })
                {
                    Box(v, 0.45f * s, 0.43f, 0.22f, 0.33f, kWood, 0.6f);
                    Box(v, 0.45f * s, 0.55f, 0.16f, 0.07f, kWood, 0.75f, 0.0f, 0.02f);
                    Box(v, 0.45f * s, 0.3f, 0.16f, 0.07f, kWood, 0.75f, 0.0f, 0.02f);
                }
                Box(v, 0.42f, 0.9f, 0.022f, 0.08f, kStone, 0.95f);
                Disc(v, 0.42f, 1.01f, 0.032f, kGlow, 1.0f);
                Things(v, rng, -0.6f, 0.2f, 0.82f, 0.35f, true);
                Box(v, -0.05f, 1.6f, 0.4f, 0.3f, kStone, 0.55f, 0.08f, 0.0f);   // a map on the wall above
                break;
            case kPropRoundTable: // a round table on a pedestal, two stools, mugs
                Box(v, 0.0f, 0.78f, 0.52f, 0.04f, kWood, 0.88f);
                Box(v, 0.0f, 0.4f, 0.06f, 0.38f, kWood, 0.6f);
                Box(v, 0.0f, 0.04f, 0.26f, 0.04f, kWood, 0.6f);
                for (float s : { -1.0f, 1.0f })
                {
                    Box(v, 0.88f * s, 0.48f, 0.18f, 0.035f, kWood, 0.78f);
                    Box(v, 0.76f * s, 0.23f, 0.025f, 0.23f, kWood, 0.6f);
                    Box(v, 1.0f * s, 0.23f, 0.025f, 0.23f, kWood, 0.6f);
                    Box(v, 0.22f * s, 0.88f, 0.045f, 0.06f, kStone, 0.75f, 0.08f, 0.015f);
                }
                break;
            case kPropArmour: // an armour stand: post, base, cuirass, pauldrons, helm, a shield beside it
                Box(v, 0.0f, 0.9f, 0.035f, 0.9f, kWood, 0.6f);
                Box(v, 0.0f, 0.035f, 0.3f, 0.035f, kWood, 0.6f);
                Box(v, 0.0f, 1.32f, 0.26f, 0.3f, kStone, 0.82f, 0.0f, 0.1f);
                for (float s : { -1.0f, 1.0f }) Disc(v, 0.28f * s, 1.58f, 0.11f, kStone, 0.9f);
                Disc(v, 0.0f, 1.84f, 0.12f, kStone, 0.85f);
                Box(v, 0.0f, 0.95f, 0.22f, 0.08f, kCloth, 0.75f, 0.0f, 0.03f);
                Disc(v, 0.62f, 0.55f, 0.3f, kWood, 0.72f, 0.02f);
                Disc(v, 0.62f, 0.55f, 0.08f, kStone, 0.92f, 0.13f);
                break;
            case kPropPews: // a pew: its seat, back and ends
                Box(v, 0.0f, 0.45f, 1.1f, 0.04f, kWood, 0.85f);
                Box(v, 0.0f, 0.78f, 1.1f, 0.12f, kWood, 0.68f, 0.0f, 0.02f);
                for (float s : { -1.0f, 1.0f }) Box(v, 1.1f * s, 0.48f, 0.06f, 0.48f, kWood, 0.62f, 0.0f, 0.03f);
                Box(v, 0.0f, 0.25f, 1.05f, 0.03f, kWood, 0.55f);
                break;
            case kPropCandles: // a candle stand: three lit candles on a crossbar, drip trays, a footed base
                Box(v, 0.0f, 0.78f, 0.04f, 0.74f, kStone, 0.75f);
                Box(v, 0.0f, 0.05f, 0.26f, 0.05f, kStone, 0.7f, 0.0f, 0.02f);
                Box(v, 0.0f, 0.14f, 0.12f, 0.06f, kStone, 0.8f, 0.0f, 0.03f);
                Box(v, 0.0f, 1.5f, 0.34f, 0.025f, kStone, 0.75f);
                for (float x : { -0.3f, 0.0f, 0.3f })
                {
                    Box(v, x, 1.54f, 0.06f, 0.015f, kStone, 0.8f);
                    Box(v, x, 1.63f, 0.035f, 0.09f, kStone, 0.95f, 0.0f, 0.01f);
                    Disc(v, x, 1.77f, 0.04f, kGlow, 1.0f);
                }
                break;
            default: break;
            }
            return v;
        }

        // A tile's distance at a point, its stuff, shade and hue: the union of its shapes (the last one holding the point on
        // top; outside all of them, the nearest's).
        float TileAt(const std::vector<Shape>& v, float x, float y, int& stuff, float& shade, float& hue)
        {
            float best = 1e9f, nearest = 1e9f;
            int top = -1, near = -1;
            for (size_t i = 0; i < v.size(); ++i)
            {
                const float d = ShapeDistance(v[i], x, y);
                best = std::min(best, d);
                if (d <= 0.0f) top = static_cast<int>(i);
                if (d < nearest) { nearest = d; near = static_cast<int>(i); }
            }
            const int k = top >= 0 ? top : near;
            if (k < 0) { stuff = kWood; shade = 0.5f; hue = 0.0f; return 1e9f; }
            stuff = v[k].stuff;
            // the edge a little darker (a bevel)
            const float inside = top >= 0 ? -ShapeDistance(v[k], x, y) : 0.0f;
            shade = v[k].shade * (0.78f + 0.22f * std::min(1.0f, inside / 0.035f));
            hue = v[k].hue;
            return best;
        }
    }

    float PropDistance(int tile, float x, float y, int* stuff)
    {
        const std::vector<Shape> v = Tile(tile);
        int s = 0;
        float shade = 0, hue = 0;
        const float d = TileAt(v, x, y, s, shade, hue);
        if (stuff) *stuff = s;
        return d;
    }

    void FurnitureAtlas(std::vector<uint32_t>& texels)
    {
        using namespace rooms;
        texels.assign(static_cast<size_t>(kAtlasWidth) * kAtlasHeight, 0);
        for (int tile = 0; tile < kTiles; ++tile)
        {
            const std::vector<Shape> v = Tile(tile);
            const int tx = (tile % 8) * kTileTexels, ty = (tile / 8) * kTileTexels;
            for (int j = 0; j < kTileTexels; ++j)
                for (int i = 0; i < kTileTexels; ++i)
                {
                    const float x = (i + 0.5f) / kTileTexels * kTileSize - 0.5f * kTileSize;
                    const float y = kTileSize - (j + 0.5f) / kTileTexels * kTileSize;
                    int stuff = 0;
                    float shade = 0.5f, hue = 0.0f;
                    float d = v.empty() ? 1e9f : TileAt(v, x, y, stuff, shade, hue);
                    // nothing within a tile's border: its neighbour never bleeds in
                    const float border = std::min(std::min(i + 0.5f, kTileTexels - i - 0.5f), std::min(j + 0.5f, kTileTexels - j - 0.5f)) *
                                         (kTileSize / kTileTexels);
                    d = std::max(d, kSdfRange * 1.5f - border);
                    const float a = Clamp(0.5f - d / (2.0f * kSdfRange), 0.0f, 1.0f);
                    auto byte = [](float f) { return static_cast<uint32_t>(std::lround(Clamp(f, 0.0f, 1.0f) * 255.0f)); };
                    texels[static_cast<size_t>(ty + j) * kAtlasWidth + tx + i] =
                        (byte(a) << 24) | (byte(shade) << 16) | (byte(stuff / 3.0f) << 8) | byte(hue);
                }
        }
    }

    // --- the textures --------------------------------------------------------------------------------------------------
    namespace
    {
        const TextureSet kSets[kTextureSets] = {
            { "human", "dungeons\\textures\\floor\\mm_strmwnd_int_floor_01.blp", "dungeons\\textures\\walls\\mm_strmwnd_int_wall_01.blp",
              "dungeons\\textures\\ceiling\\mm_strmwnd_int_ceiling_01.blp", "dungeons\\textures\\floor\\mm_inn_wood floor_01.blp", { 2.0f, 2.6f, 2.4f, 1.6f } },
            { "rustic", "dungeons\\textures\\floor\\mm_westfall_wood_floor_01.blp", "dungeons\\textures\\walls\\mm_westfall_int_wall_01.blp",
              "dungeons\\textures\\ceiling\\mm_westfall_int_ceiling_02.blp", "dungeons\\textures\\floor\\mm_inn_wood floor_01.blp", { 2.2f, 2.6f, 2.4f, 1.6f } },
            { "dwarf", "dungeons\\textures\\floor\\mm_dwrf_floor_04.blp", "dungeons\\textures\\walls\\mm_dwrf_wall_03.blp",
              "dungeons\\textures\\floor\\mm_dwrf_thndlspn_ceiling_01.blp", "dungeons\\textures\\floor\\mm_strmwnd_int_floor_01.blp", { 2.4f, 2.6f, 2.6f, 1.6f } },
            { "orc", "dungeons\\textures\\floor\\bm_thunder_wdfloor_01.blp", "dungeons\\textures\\walls\\mm_ogrmr_int_wall_02.blp",
              "dungeons\\textures\\ceiling\\mm_ogrmr_ceiling_01.blp", "dungeons\\textures\\floor\\bm_thunder_wdfloor_01.blp", { 2.2f, 2.6f, 2.6f, 1.6f } },
            { "goblin", "dungeons\\textures\\ceiling\\jlo_bbay_ceiling.blp", "dungeons\\textures\\walls\\jlo_bbay_woodwall.blp",
              "dungeons\\textures\\ceiling\\jlo_bbay_ceiling.blp", "dungeons\\textures\\walls\\jlo_bbay_woodwall.blp", { 2.0f, 2.6f, 2.2f, 1.6f } },
            { "vrykul", "dungeons\\textures\\vrykul\\vrykul_boards.blp", "dungeons\\textures\\vrykul\\vrykul_wood_02.blp",
              "dungeons\\textures\\vrykul\\vrykul_beam.blp", "dungeons\\textures\\vrykul\\vrykul_wood_06.blp", { 2.2f, 2.8f, 2.4f, 1.6f } },
            { "elven", "dungeons\\textures\\floor\\jlo_bloodelf_floor_02.blp", "dungeons\\textures\\walls\\jlo_bloodelf_wall_01.blp",
              "dungeons\\textures\\walls\\jlo_bloodelf_wall_01.blp", "dungeons\\textures\\walls\\mm_ne_intwall_01.blp", { 2.4f, 2.8f, 2.8f, 1.6f } },
            { "night elf", "dungeons\\textures\\floor\\mm_ne_floor_02.blp", "dungeons\\textures\\walls\\mm_ne_ds_intwall_01.blp",
              "dungeons\\textures\\ceiling\\mm_ne_ceiling 01.blp", "dungeons\\textures\\walls\\mm_ne_intwall_01.blp", { 2.4f, 2.8f, 2.6f, 1.6f } },
            { "Forsaken", "dungeons\\textures\\forsaken\\fskn_woodfloor.blp", "dungeons\\textures\\forsaken\\fskn_ interiorwall_01light.blp",
              "dungeons\\textures\\forsaken\\fskn_woodceiling.blp", "dungeons\\textures\\forsaken\\fskn_woodplanks.blp", { 2.2f, 2.6f, 2.4f, 1.6f } },
            { "draenei", "dungeons\\textures\\draenei\\jlo_draenei_floor03.blp", "dungeons\\textures\\draenei\\jlo_draenei_wall06.blp",
              "dungeons\\textures\\draenei\\jlo_draenei_ceiling02.blp", "dungeons\\textures\\draenei\\jlo_draenei_wall07.blp", { 2.6f, 2.8f, 2.8f, 2.0f } },
        };
        enum SetIndex { kHumanSet, kRusticSet, kDwarfSet, kOrcSet, kGoblinSet, kVrykulSet, kElvenSet, kNightElfSet, kForsakenSet, kDraeneiSet };
    }

    const TextureSet& TextureSetAt(int index) { return kSets[index >= 0 && index < kTextureSets ? index : 0]; }

    int TextureSetOf(uint32_t palette, uint32_t cls)
    {
        // tools/gen_glass.py PALETTES: HUMAN, DUSK, NORTHERN, SCARLET, ELVEN, DALARAN, NIGHTELF, DRAENEI, VRYKUL, GOBLIN, FORSAKEN,
        // ORC, DWARF, HIDE, HELLFIRE, SPORE, ARCANE, CABIN
        static const int byPalette[18] = { kHumanSet, kRusticSet, kRusticSet, kHumanSet, kElvenSet, kElvenSet, kNightElfSet, kDraeneiSet,
                                           kVrykulSet, kGoblinSet, kForsakenSet, kOrcSet, kDwarfSet, kOrcSet, kHumanSet, kNightElfSet,
                                           kDraeneiSet, kGoblinSet };
        const int set = palette < 18 ? byPalette[palette] : kHumanSet;
        if (set == kHumanSet && (cls == kClassFarm || cls == kClassShip)) return kRusticSet;
        if (cls >= kClassForge && cls <= kClassCavern) return kDwarfSet;
        return set;
    }

    // --- the lookup table -------------------------------------------------------------------------------------------------
    namespace
    {
        // How far off its plane the lookup reaches.
        float Thickness(const Window& w)
        {
            return w.thickness > 0.0f && w.thickness < 4.0f ? w.thickness : rooms::kThickness;
        }

        // The window's rectangle's corners (WMO space) and its rows.
        void Rows(const Window& w, float a[4], float b[4], float n[4])
        {
            using namespace rooms;
            float along[3], up[3];
            WindowFrame(w.normal, along, up);
            const float sa = 1.0f / (w.halfWidth + kMargin), sb = 1.0f / (w.halfHeight + kMargin), sn = 1.0f / Thickness(w);
            for (int k = 0; k < 3; ++k) { a[k] = along[k] * sa; b[k] = up[k] * sb; n[k] = w.normal[k] * sn; }
            a[3] = -Dot3(along, w.center) * sa;
            b[3] = -Dot3(up, w.center) * sb;
            n[3] = -Dot3(w.normal, w.center) * sn;
        }

        void Bounds(const Window& w, float lo[3], float hi[3])
        {
            using namespace rooms;
            float along[3], up[3];
            WindowFrame(w.normal, along, up);
            for (int k = 0; k < 3; ++k)
            {
                const float e = std::fabs(along[k]) * (w.halfWidth + kMargin) + std::fabs(up[k]) * (w.halfHeight + kMargin) +
                                std::fabs(w.normal[k]) * Thickness(w);
                lo[k] = w.center[k] - e;
                hi[k] = w.center[k] + e;
            }
        }
    }

    void WindowTable::Build(std::vector<Window> windows)
    {
        using namespace rooms;
        m_windows = std::move(windows);
        m_grids.clear();
        m_texels.clear();
        // by WMO and group
        std::unordered_map<uint64_t, std::vector<uint32_t>> byGroup;
        for (uint32_t i = 0; i < m_windows.size(); ++i)
        {
            Window& w = m_windows[i];
            const float l = std::sqrt(Dot3(w.normal, w.normal));
            if (!(l > 0.5f) || !(w.halfWidth > 0.0f) || !(w.halfHeight > 0.0f)) continue;
            for (float& v : w.normal) v /= l;
            byGroup[Key(w.path, w.group)].push_back(i);
        }
        // each group's windows by ordinal: a cell lists them so, and where two overlap the shader takes the first
        for (auto& kv : byGroup)
            std::stable_sort(kv.second.begin(), kv.second.end(), [this](uint32_t x, uint32_t y) { return m_windows[x].ordinal < m_windows[y].ordinal; });
        std::vector<uint64_t> keys;
        keys.reserve(byGroup.size());
        for (const auto& kv : byGroup) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        // the cells first (each grid's run), then the entries; built as runs, laid out at the end
        struct Pending { uint64_t key; Grid grid; std::vector<std::vector<uint32_t>> cells; };
        std::vector<Pending> pending;
        size_t cellTotal = 0, entryTotal = 0;
        for (uint64_t key : keys)
        {
            const std::vector<uint32_t>& list = byGroup[key];
            float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
            std::vector<std::array<float, 6>> boxes;
            boxes.reserve(list.size());
            for (uint32_t i : list)
            {
                std::array<float, 6> b;
                Bounds(m_windows[i], b.data(), b.data() + 3);
                for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], b[k]); hi[k] = std::max(hi[k], b[3 + k]); }
                boxes.push_back(b);
            }
            // the cell's size: the volume over the windows (a few a cell), within kCellMin .. kCellMax; smaller while a cell
            // holds more than kMaxPerCell, larger while there'd be more than kMaxCells
            const float vol = std::max(1.0f, (hi[0] - lo[0]) * (hi[1] - lo[1]) * (hi[2] - lo[2]));
            float cell = Clamp(std::cbrt(vol / std::max<size_t>(1, list.size()) * 2.0f), kCellMin, kCellMax);
            Pending p;
            for (int attempt = 0; attempt < 24; ++attempt)
            {
                uint32_t n[3];
                for (int k = 0; k < 3; ++k) n[k] = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil((hi[k] - lo[k]) / cell + 1e-4f)));
                const size_t count = static_cast<size_t>(n[0]) * n[1] * n[2];
                if (count > static_cast<size_t>(kMaxCells)) { cell *= 1.25f; continue; }
                std::vector<std::vector<uint32_t>> cells(count);
                size_t most = 0;
                for (size_t w = 0; w < list.size(); ++w)
                {
                    const auto& b = boxes[w];
                    uint32_t c0[3], c1[3];
                    for (int k = 0; k < 3; ++k)
                    {
                        c0[k] = static_cast<uint32_t>(Clamp(std::floor((b[k] - lo[k]) / cell), 0.0f, static_cast<float>(n[k] - 1)));
                        c1[k] = static_cast<uint32_t>(Clamp(std::floor((b[3 + k] - lo[k]) / cell), 0.0f, static_cast<float>(n[k] - 1)));
                    }
                    for (uint32_t z = c0[2]; z <= c1[2]; ++z)
                        for (uint32_t y = c0[1]; y <= c1[1]; ++y)
                            for (uint32_t x = c0[0]; x <= c1[0]; ++x)
                            {
                                auto& c = cells[x + static_cast<size_t>(n[0]) * (y + static_cast<size_t>(n[1]) * z)];
                                c.push_back(list[w]);
                                most = std::max(most, c.size());
                            }
                }
                p.grid = Grid{};
                for (int k = 0; k < 3; ++k) { p.grid.min[k] = lo[k]; p.grid.n[k] = n[k]; }
                p.grid.cell = cell;
                p.cells = std::move(cells);
                p.grid.worst = static_cast<uint32_t>(most);
                if (most <= static_cast<size_t>(kMaxPerCell) || cell <= kCellMin * 0.5f) break;
                cell *= 0.75f;
            }
            // (a cell still over the loop's bound keeps its first kMaxPerCell)
            p.key = key;
            p.grid.windows = static_cast<uint32_t>(list.size());
            for (auto& c : p.cells)
            {
                if (c.size() > static_cast<size_t>(kMaxPerCell)) c.resize(kMaxPerCell);
                p.grid.entries += static_cast<uint32_t>(c.size());
            }
            cellTotal += p.cells.size();
            entryTotal += p.grid.entries;
            pending.push_back(std::move(p));
        }
        // Layout: the cells (one texel each) from 0, the entries from the next multiple of kEntryTexels (each kEntryTexels
        // texels, never across a row: kWidth is a multiple of it).
        const size_t entryBase = (cellTotal + kEntryTexels - 1) / kEntryTexels * kEntryTexels;
        const size_t total = entryBase + entryTotal * kEntryTexels;
        int height = 1;
        while (static_cast<size_t>(height) * kWidth < total) height *= 2;
        m_height = height;
        m_texels.assign(static_cast<size_t>(height) * kWidth * 4, 0.0f);
        size_t cellAt = 0, entryAt = entryBase;
        for (Pending& p : pending)
        {
            p.grid.base = static_cast<uint32_t>(cellAt);
            for (const auto& c : p.cells)
            {
                float* t = &m_texels[cellAt * 4];
                t[0] = static_cast<float>(entryAt);
                t[1] = static_cast<float>(c.size());
                ++cellAt;
                for (uint32_t i : c)
                {
                    const Window& w = m_windows[i];
                    float* e = &m_texels[entryAt * 4];
                    Rows(w, e, e + 4, e + 8);
                    const RoomBox box = RoomBoxOf(w.halfWidth, w.halfHeight);
                    e[12] = w.halfWidth + kMargin; e[13] = w.halfHeight + kMargin; e[14] = Thickness(w); e[15] = static_cast<float>(w.ordinal);
                    e[16] = box.halfWidth; e[17] = box.floor; e[18] = box.ceiling; e[19] = w.kind == kFrosted ? 1.0f : 0.0f;
                    e[20] = w.center[0]; e[21] = w.center[1]; e[22] = w.center[2]; e[23] = static_cast<float>(w.group);
                    entryAt += kEntryTexels;
                }
            }
            m_grids[p.key] = p.grid;
        }
    }

    const WindowTable::Grid* WindowTable::Find(uint32_t path, uint32_t group) const
    {
        auto it = m_grids.find(Key(path, group));
        return it == m_grids.end() ? nullptr : &it->second;
    }

    int WindowTable::Lookup(const Grid& g, const float local[3], float rows[3]) const
    {
        using namespace rooms;
        // As the shader: the cell (floor of (p - min) / cell), within the grid; its texel, its entries in order, the first
        // whose rows put the point within 1.
        const float inv = 1.0f / g.cell;
        float c[3];
        for (int k = 0; k < 3; ++k)
        {
            c[k] = std::floor((local[k] - g.min[k]) * inv);
            if (!(c[k] >= 0.0f) || c[k] > static_cast<float>(g.n[k] - 1)) return -1;
        }
        const float row = c[1] * static_cast<float>(g.n[0]) + c[0];
        const float index = (c[2] * static_cast<float>(g.n[0] * g.n[1]) + row) + static_cast<float>(g.base);
        const float* cell = Texel(static_cast<int>(index));
        const int first = static_cast<int>(cell[0]), count = std::min(static_cast<int>(cell[1]), kMaxPerCell);
        for (int i = 0; i < count; ++i)
        {
            const int at = first + i * kEntryTexels;
            float r[3];
            for (int k = 0; k < 3; ++k)
            {
                const float* row = Texel(at + k);
                r[k] = row[0] * local[0] + row[1] * local[1] + row[2] * local[2] + row[3];
            }
            if (std::max(std::max(std::fabs(r[0]), std::fabs(r[1])), std::fabs(r[2])) <= 1.0f)
            {
                if (rows) for (int k = 0; k < 3; ++k) rows[k] = r[k];
                return at;
            }
        }
        return -1;
    }

    bool CastRoom(const WindowTable& table, int entry, const float local[3], const float toEye[3], float depth, RoomRay& out)
    {
        if (entry < 0) return false;
        const float* ra = table.Texel(entry);
        const float* rb = table.Texel(entry + 1);
        const float* rn = table.Texel(entry + 2);
        const float* sizes = table.Texel(entry + 3);
        const float* box = table.Texel(entry + 4);
        auto unit = [](const float* r, float o[3]) {
            const float l = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            for (int k = 0; k < 3; ++k) o[k] = r[k] / l;
        };
        float along[3], up[3], n[3];
        unit(ra, along); unit(rb, up); unit(rn, n);
        const float pa = (ra[0] * local[0] + ra[1] * local[1] + ra[2] * local[2] + ra[3]) * sizes[0];
        const float pb = (rb[0] * local[0] + rb[1] * local[1] + rb[2] * local[2] + rb[3]) * sizes[1];
        const float d[3] = { -Dot3(toEye, along), -Dot3(toEye, up), std::max(std::fabs(Dot3(toEye, n)), 1e-4f) };
        const float W = box[0], F = box[1], C = box[2];
        const float ta = (W - (d[0] >= 0.0f ? pa : -pa)) / std::max(std::fabs(d[0]), 1e-4f);
        const float tb = std::fabs((d[1] >= 0.0f ? C : F) - pb) / std::max(std::fabs(d[1]), 1e-4f);
        const float tc = depth / d[2];
        const float t = std::min(std::min(ta, tb), tc);
        out.t = t;
        out.across = pa; out.up = pb;
        out.hit[0] = pa + d[0] * t; out.hit[1] = pb + d[1] * t; out.hit[2] = d[2] * t;
        if (std::min(ta, tb) - tc >= 0.0f) out.face = kBackWall;
        else if (tb - ta >= 0.0f) out.face = kSideWall;
        else out.face = d[1] >= 0.0f ? kCeiling : kFloor;
        out.ceiling = out.face == kCeiling;
        return true;
    }
}
