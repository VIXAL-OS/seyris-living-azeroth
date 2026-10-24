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
//
// Which models are spell lights (2026-10-24, "icethorn is VERY bright": the Storm Peaks' Icethorn lit its snow and
// tree blue-white): only spell visuals, missiles and enchants. The light there was the client's loot sparkle,
// Particles\LootFX.m2, which CGGameObject_C::CheckForPassiveHighlight 0x711210 (ShowLootEffect 0x70D080) hangs on a
// node a herbalist or miner tracks, a chest, a quest object (SpellVisualEffectName's "HARDCODED Loot Art", scaled by the
// object's height up to 5x): our particles\ rule took it for a spell. The client's own effects (the table's rows named
// HARDCODED: the loot sparkle, breath in cold air and under water, footstep sprays, level-up, the reputation and
// achievement bursts) aren't spells; particles\ holds them and old effects no spell of 3.3.5 uses, so it counts no more;
// the table's files under world\, creature\ and item\ are doodads, creatures and weapons a few spells reuse (wisps,
// rabbits, mage portals, the Saronite rock), and stay theirs; a game object's own model (GameObjectDisplayInfo's: the
// instance portals, the nodes, chests, mailboxes), unattached, stays with the scan's doodad rules even under spells\.
// What counts: files under spells\ (enchants: ItemVisualEffects' and spells\enchantments\), and any model the client
// made as a missile or holds in its missile lists (an arrow or a thrown axe gives light only by its particles and own
// lights: its ribbons, the streak of its flight, and the glow of the rest, its geometry's textures, count for files
// under spells\ alone).
//
// Steady lights (2026-10-24, "spell lights seem flickery in general even if the spell itself doesn't flicker"): a
// light follows its effect over wall time, not frames. Each emitter's live count is smoothed over a quarter of its
// particles' life (the particles fade in and out over theirs; the count steps by one); the brightness eases up and
// down (quickly in a model's first moments: a spell still flares at once), the hue slower; merging and crowding add
// lights in quadrature (a dim light coming or going barely moves a bright one beside it); the cap keeps the lights
// it kept unless another is clearly better, and a light leaving or joining the kept set fades out or in; a light
// merged with another stays merged a little past the merge distance; spell lights have their own room in each cell
// of env/Lights' grid, and the cap never keeps more than fit there.
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

    /// The model files of SpellVisualEffectName.dbc (ID, Name, FileName, ...: field 2), ItemVisualEffects.dbc (ID, Model:
    /// field 1) and GameObjectDisplayInfo.dbc (ID, ModelName: field 1): `fileField` is the string field's index. With
    /// `nameField` >= 0, only the rows whose string there starts with `namePrefix` (any case): SpellVisualEffectName's
    /// Name (field 1) "HARDCODED", the client's own effects. Rows read, or -1 when the bytes aren't such a table.
    int LoadVisualTable(const uint8_t* bytes, size_t size, int fileField, std::unordered_set<std::string>& out,
                        int nameField = -1, const char* namePrefix = nullptr);

    struct VisualSets
    {
        std::unordered_set<std::string> spells, items;   // SpellVisualEffectName's files, ItemVisualEffects'
        std::unordered_set<std::string> hardcoded;       // SpellVisualEffectName's rows named HARDCODED: the client's own
        std::unordered_set<std::string> objects;         // GameObjectDisplayInfo's: game objects' own models
        int spellRows = -1, itemRows = -1, objectRows = -1;   // -1: not read
    };

    /// A model's kind: none for the map's doodads (kFlagMapDoodad), the client's stand-in ErrorCube and its own effects
    /// (HARDCODED), and a game object's own model unless it hangs on another; a missile when the missile lists hold it or
    /// it was made as one; an enchant for ItemVisualEffects' files and spells\enchantments\; an effect for files under
    /// spells\; none for the rest (particles\, world\, creature\, item\, whatever SpellVisualEffectName says).
    Kind Classify(const std::string& path, uint32_t creationFlags, bool inMissileList, const VisualSets& sets, bool attached = false);
    /// Classify in steps, the first once per file: what the file alone says (none, an effect, an enchant), whether it
    /// never gives light (no name, ErrorCube, the client's own effects), whether it's a game object's model; then the
    /// model's flags, the missile lists and whether it hangs on another model.
    Kind PathKind(const std::string& path, const VisualSets& sets);
    bool NeverLit(const std::string& path, const VisualSets& sets);
    bool ObjectModel(const std::string& path, const VisualSets& sets);
    Kind KindOf(Kind pathKind, bool neverLit, bool objectModel, uint32_t creationFlags, bool inMissileList, bool attached);

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
    // Time constants in seconds of wall time (each frame's own length, however long).
    constexpr float kYoungSeconds = 0.3f;   // a model's first moments: it flares at once
    constexpr float kAttackSeconds = 0.05f; // up while young
    constexpr float kRiseSeconds = 0.15f;   // up afterwards (its particles coming and going don't flicker it)
    constexpr float kFallSeconds = 0.20f;   // down (it dies away; a model gone keeps fading where it was)
    constexpr float kHueSeconds = 0.30f;    // the hue (emitters coming and going don't flicker it; kAttackSeconds while young)
    struct Fade { float level = 0.0f; float hue[3] = {}; bool started = false; float age = 0.0f; };
    /// One frame of `dt` s toward `target` (rgb): the level (its brightest channel) rises and falls at their pace (rising
    /// at kAttackSeconds in its first kYoungSeconds), the hue follows.
    void StepFade(Fade& f, const float target[3], float dt);
    /// The faded colour now.
    void FadedColour(const Fade& f, float out[3]);
    /// An emitter's live particles over wall time: smoothed over a quarter of their life (kAttackSeconds .. 0.25 s, and
    /// kAttackSeconds while the model is young or none is alive), so a steady emitter's count stepping by one as particles
    /// are born and die doesn't flicker.
    float CountSeconds(float particleLife, bool young);
    void StepCount(float& count, float live, float seconds, float dt);
    /// How far an exponential ease with this time constant goes in `dt` (1 - exp(-dt / seconds)).
    float Approach(float dt, float seconds);

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
        // Carried from frame to frame, one per model (Select reads them and leaves this frame's):
        float age = 0.0f;              // s since the model was first followed (young: kYoungSeconds)
        float presence = 0.0f;         // 0 .. 1: how far it is in the kept set (it fades in and out as it joins and leaves)
        bool  kept = false;            // in the kept set (in: last frame's; out: this frame's)
        const void* label = nullptr;   // the cluster it was in (in: last frame's; out: this frame's)
    };
    struct MergeStats
    {
        unsigned in = 0, inRange = 0, merged = 0, clusters = 0, dropped = 0, out = 0, crowded = 0;
        unsigned leaving = 0;    // drawn while fading out of the kept set
        unsigned waiting = 0;    // better than the cap's last but no room in a cell of the grid yet (comes in when there is)
        unsigned overflow = 0;   // dropped at once: no room (a light that moved into a full cell)
    };
    /// Spell lights' room in env/Lights' grid: cells `cellSize` yd square, at world multiples of it, `perCell` spell lights
    /// each (0: no limit). A light takes every cell its radius reaches (as env/Lights BuildGrid puts it there).
    struct CellRoom { float cellSize = 16.0f; int perCell = 0; };
    constexpr float kMergeHold = 1.3f;       // lights merged last frame stay merged up to this x Settings::mergeDistance
    constexpr float kKeepBonus = 1.25f;      // a light kept last frame stays unless another scores this much better
    constexpr float kPresenceSeconds = 0.2f; // a light joining or leaving the kept set fades in or out
    constexpr float kRangeFade = 0.2f;       // lights fade out over the last fifth of Settings::range
    /// The share of N separate spell lights' summed colour a merged one keeps: 1 / sqrt(N) (env/Lights' rule for separate
    /// small flames, 2026-10-23: N sources' energy grows as sqrt N), so a big area spell's dozens of shards glow, not blow out.
    /// Select merges in quadrature: the brightness sqrt(sum b^2), which is this for N equal lights.
    float MergeKeep(unsigned sources);
    /// Brightness x reach, falling with distance from the camera (half at 15 yd): what the cap keeps first.
    float Score(const Light& l, const float eye[3]);
    /// How crowded a spell light is: 1 (itself) + every other's window (1 - (d / its radius)^2)^2 where it reaches this one,
    /// x (its brightness / this one's)^2. A light keeps 1 / sqrt of it: overlapping lights sum in quadrature, as merged
    /// ones do (for N equal lights at one spot sqrt N x one), and a dim one beside a bright one barely changes it.
    float Crowding(const std::vector<Light>& lights, size_t i);
    /// One frame's spell lights from `models` (one per model followed, its faded colour, position and reach; its state from
    /// the frame before) into `out`, by score:
    ///  - range: lights fade out over the last kRangeFade of Settings::range from `eye`, none beyond;
    ///  - of more than 4 x the cap (at least 64), those already drawn and then the best by Score go on;
    ///  - merging, brightest first: closer than Settings::mergeDistance (kMergeHold x it for two merged last frame); the
    ///    brightness sqrt(sum b^2) in the members' mean hue, at their brightness-weighted position, the largest radius +
    ///    half their spread;
    ///  - the cap: the Settings::maxLights best by Score (x kKeepBonus for those kept last frame; ties by model) are wanted;
    ///    those drawn last frame keep their room in the grid's cells (`room`), a wanted one new to it comes in where its
    ///    cells have room (else it waits, and in each full cell hurries out the weakest there it outranks: that one fades
    ///    out over kAttackSeconds and the room is its);
    ///  - presence: a kept light's members fade in over kPresenceSeconds (a young model at once: its own fade ramps it),
    ///    the rest fade out, drawn until gone;
    ///  - crowding: each keeps 1 / sqrt(Crowding) of its colour.
    /// `dt`: this frame's length (s).
    void Select(std::vector<Light>& models, std::vector<Light>& out, const float eye[3], const Settings& s, const CellRoom& room,
                float dt, MergeStats& st);
    /// One frame of Select over lights new this frame (no limit in the grid): what a frame keeps of them, in `lights`.
    void MergeAndCap(std::vector<Light>& lights, const float eye[3], const Settings& s, MergeStats& st);
}
