// Spell lights (features/SpellLights): spells, glowing projectiles and weapon enchants light what's round them, through our
// point lights (env/Lights: the terrain, the surface cover, M2s and WMOs). This is the part that touches no client memory:
// which models are spell visuals, the colour a model gives off, its light's size, the fade, and a frame's merge and cap.
//
// What the client has (12340; wowdecomp Model2, Object/ObjectClient/Missile_C, Effect_C, Services/ParticleSystem2, checked
// against the exe, features/SpellLights' guards): every spell visual is a CM2Model of the world's M2 scene, made by
// CM2Scene::CreateModel 0x81F8F0 from a SpellVisualEffectName file -- a missile's with creation flags 0x40 (Spell_C, the
// missile list CMissile::s_inFlightMissileList 0xCA0B58 and the impact list s_missileDestroyList 0xCA0B5C), a kit's
// effects with 0 (CEffect, on a unit's attachment or at a place), an enchant's from ItemVisualEffects on its weapon. The
// map's own doodads are made with 0x20 (CMapDoodadDef::CreateModel) and stay with the doodad lights (env/Lights).
// Of the 1,776 spell visual files (SpellVisualEffectName and ItemVisualEffects, extracted 2026-10-24): 22 have a point
// light of their own (Fireball_Missile_High's and Fel_Fireball_Missile_High's are white), 1,346 particle emitters (4,469
// of them additive, 1,171 alpha-blended), 423 geometry alone.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace wxl_livingazeroth::spelllight
{
    struct Settings
    {
        int   enabled = 0;              // experimental: off by default
        float brightness = 1.0f;        // x every spell light
        float radius = 7.0f;            // yd: a light's reach for a model of about 1 yd (grows with its size)
        int   maxLights = 24;           // per frame: the nearest and brightest
        float range = 80.0f;            // yd from the camera
        float mergeDistance = 1.5f;     // yd: closer spell lights merge (sqrt of their summed light: a unit's auras, a hit)
        int   enchants = 1;             // weapon enchants' glows
        float enchantStrength = 0.35f;  // x their light (they're small and always on)
    };
    constexpr int kMaxLightsCap = 64;

    enum class Kind : uint8_t { None, Missile, Effect, Enchant };
    const char* KindName(Kind k);

    // --- which models are spell visuals ---------------------------------------------------------------------------------
    constexpr uint32_t kFlagMapDoodad = 0x20; // CM2Model +0x04: CMapDoodadDef::CreateModel's flags (placed map doodads)
    constexpr uint32_t kFlagMissile = 0x40;   // ... a spell's missile (Spell_C, Unit_C's precast missiles)

    /// Lower case, '/' as '\', .mdx and .mdl as .m2 (CM2Cache::CreateShared 0x81C390 keeps the name so).
    std::string NormalizePath(const char* path);

    /// The model files of SpellVisualEffectName.dbc (ID, Name, FileName, ...: field 2) and ItemVisualEffects.dbc (ID, Model:
    /// field 1): `fileField` is the string field's index. Rows read, or -1 when the bytes aren't such a table.
    int LoadVisualTable(const uint8_t* bytes, size_t size, int fileField, std::unordered_set<std::string>& out);

    struct VisualSets
    {
        std::unordered_set<std::string> spells, items;
        int spellRows = -1, itemRows = -1;   // -1: not read
    };

    /// A model's kind: none for the map's doodads (kFlagMapDoodad) and the client's stand-in ErrorCube; a missile when the
    /// missile lists hold it or it was made as one; an enchant for ItemVisualEffects' files and spells\enchantments\; an
    /// effect for SpellVisualEffectName's files and anything under spells\ or particles\.
    Kind Classify(const std::string& path, uint32_t creationFlags, bool inMissileList, const VisualSets& sets);
    /// Classify in two steps, the first once per file: what the file alone says (none, an effect, an enchant), whether it
    /// never gives light (no name, ErrorCube); then the model's flags and the missile lists.
    Kind PathKind(const std::string& path, const VisualSets& sets);
    bool NeverLit(const std::string& path);
    Kind KindOf(Kind pathKind, bool neverLit, uint32_t creationFlags, bool inMissileList);

    // --- colour -----------------------------------------------------------------------------------------------------------
    float MaxChannel(const float c[3]);
    /// 0 (grey) .. 1 (pure hue): (max - min) / max.
    float Saturation(const float c[3]);
    /// A colour from the file's name where it says one (fire, frost, holy, arcane, shadow, fel, nature, lightning, ...).
    bool KeywordColour(const std::string& path, float out[3]);

    /// A particle emitter's colour over a particle's life (M2PartTrack: times fixed16 0..32767, colour keys 0..255, alpha
    /// keys 0..32767), sampled at 8 points of its life: the alpha-weighted mean colour (0..1) and the mean alpha.
    void EmitterTrackColour(const int16_t* colourTimes, uint32_t colourTimeCount, const float* colours, uint32_t colourCount,
                            const int16_t* alphaTimes, uint32_t alphaTimeCount, const int16_t* alphas, uint32_t alphaCount,
                            float out[3], float& alphaMean);
    /// How much of an emitter's colour is light, by its blend mode (M2Particle +0x28): additive 1, alpha-blended 0.45,
    /// alpha-keyed 0.3, opaque 0.25, modulating 0.
    float BlendWeight(uint8_t blend);
    /// How much an emitter's particles count by their size (the mean of its scale track's keys, yd): sqrt(scale / 0.5 yd),
    /// 0.3 .. 3 (sparks count less than a big glow round the effect).
    float ScaleWeight(float meanScale);

    struct EmitterLook { float color[3]; float weight; };   // colour (track x texture), weight (blend x alpha x size x live share)
    constexpr float kNameLean = 0.5f;   // how far a school's name (KeywordColour) leans a glow's hue
    constexpr unsigned kMaxEmitters = 32;
    struct Look
    {
        EmitterLook emitters[kMaxEmitters] = {};
        unsigned emitterCount = 0;
        float light[3] = {};          // the model's own point lights, summed (the client's live colour: already x alpha)
        bool  hasLight = false;
        float geometry[3] = {};       // the rest of it (geometry, ribbons): its textures' glow x its colours
        float geometryWeight = 0.0f;  // ... as visible as its colours' alpha and texture weights say
        float keyword[3] = {};
        bool  hasKeyword = false;
        float alpha = 1.0f;           // the model's effective alpha (CM2Model +0x198)
    };
    /// What a model gives off this frame (rgb, the brightest channel up to about 1): its own point lights where they shine
    /// (a white or grey one takes its hue from the particles, else from the name), else its glow: its particles' colours
    /// and its geometry's and ribbons' (as bright as they are, x how many particles are alive and how visible the rest
    /// is, x alpha), its hue leant halfway toward the school its name says (kNameLean), a grey one taking the name's hue.
    /// False when it gives off nothing.
    bool Shade(const Look& look, float out[3]);

    // --- size -------------------------------------------------------------------------------------------------------------
    /// The model's size in yd: its file's bounding radius (M2Data +0xB8) x its world scale, 0.3 .. 20.
    float SizeOf(float boundsRadius, float scale);
    /// The light's radius: Settings::radius x 0.55 + 0.35 sqrt(size) (0.7 .. 2.4 of it); an enchant's half that.
    float LightRadius(const Settings& s, Kind kind, float size);
    /// Bigger effects shine a little brighter: 0.8 + 0.2 sqrt(size), 0.8 .. 1.4.
    float SizeGain(float size);
    /// The light sits this far above the model's origin (ground effects' origins are on the ground): 0.25 x size, 0.3 .. 1.5.
    float Lift(float size);

    // --- fade -------------------------------------------------------------------------------------------------------------
    constexpr float kRiseSeconds = 0.06f;   // time constant up (a spell flares at once)
    constexpr float kFallSeconds = 0.20f;   // down (it dies away; a model gone keeps fading where it was)
    constexpr float kHueSeconds = 0.10f;    // the hue (emitters coming and going don't flicker it)
    struct Fade { float level = 0.0f; float hue[3] = {}; bool started = false; };
    /// One frame toward `target` (rgb): the level (its brightest channel) rises and falls at their pace, the hue follows.
    void StepFade(Fade& f, const float target[3], float dt);
    /// The faded colour now.
    void FadedColour(const Fade& f, float out[3]);

    // --- a frame's lights -------------------------------------------------------------------------------------------------
    struct Light
    {
        float pos[3] = {};
        float color[3] = {};
        float radius = 0.0f;
        const void* model = nullptr;
        Kind  kind = Kind::Effect;
        unsigned sources = 1;   // spell lights merged into it
        float score = 0.0f;
    };
    struct MergeStats { unsigned in = 0, inRange = 0, merged = 0, clusters = 0, dropped = 0, out = 0, crowded = 0; };
    /// The share of N separate spell lights' summed colour a merged one keeps: 1 / sqrt(N) (env/Lights' rule for separate
    /// small flames, 2026-10-23: N sources' energy grows as sqrt N), so a big area spell's dozens of shards glow, not blow out.
    float MergeKeep(unsigned sources);
    /// Brightness x reach, falling with distance from the camera (half at 15 yd): what the cap keeps first.
    float Score(const Light& l, const float eye[3]);
    /// How crowded a spell light is: 1 (itself) + every other's window (1 - (d / its radius)^2)^2 where it reaches this one.
    float Crowding(const std::vector<Light>& lights, size_t i);
    /// Lights beyond Settings::range from `eye` go; of more than 4 x the cap (at least 64), the best by Score go on;
    /// lights closer than Settings::mergeDistance merge, brightest first
    /// (colours summed x MergeKeep, position by brightness, the largest radius + half their spread); then the
    /// Settings::maxLights best by Score stay (ties by model, so the choice is stable), sorted by score; last, each keeps
    /// 1 / sqrt(Crowding) of its colour: lights that overlap share their glow, so an area spell's field sums to about the
    /// square root of its pieces' light over the whole area, as merged ones do in one spot. A light alone keeps all of it.
    void MergeAndCap(std::vector<Light>& lights, const float eye[3], const Settings& s, MergeStats& st);
}
