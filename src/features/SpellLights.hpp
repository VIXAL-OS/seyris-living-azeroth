// Spell lights, the runtime half (the rules: env/SpellLightModel). Each frame, before our lights are merged and put in
// their grid (env/Lights::Update), the spell visuals the client's M2 scene holds near the camera give a point light each:
// missiles in flight and fading after their hit, the effects of spells on units and on the ground, weapon enchants.
//
// How they're found, every frame, read only (12340, wowdecomp; every layout below is checked against the running exe's
// own instructions first, Guards()):
//  - The scene's model list (CM2Scene +0x08, next at CM2Model +0x0C): CM2Model::AttachToScene 0x834540 puts each new model
//    at its head, so walking from the head down to the first model seen there last frame finds this frame's new ones
//    (a model is known by its address and its loop origin +0x74, the scene's time when CM2Model::Initialize 0x834810 made
//    it: a slot the allocator hands out again is a new model).
//  - The missile lists CMissile::s_inFlightMissileList 0xCA0B58 and s_missileDestroyList 0xCA0B5C (next at +0x164, the
//    model at +0x18).
//  - The light scan's walk over every model in range (env/Lights, a slice a frame) offers each one too (OfferFromScan),
//    which catches what was there before (an aura already up when the feature is switched on).
// A model is a spell light by its file and how it was made (env/SpellLightModel Classify): files under spells\ (the
// enchants ItemVisualEffects.dbc's too), not the client's own effects (SpellVisualEffectName.dbc's rows named HARDCODED:
// the loot sparkle on a tracked herb or ore node, a chest, a quest object; breath, level-up), not a game object's own
// model (GameObjectDisplayInfo.dbc's) unless it hangs on another (+0x48); its creation flags (+0x04: 0x20 = the map's
// own doodads stay with the doodad lights, 0x40 = a missile, whatever its file).
// What it gives off, every frame: its own point lights' live colours (instance +0x1D0, 0xD4 each, CM2Light at +0x68), its
// particle emitters' colours (the file's M2Particle colour and alpha tracks +0x104, +0x114, x their texture's glow colour,
// read once from the client's archives; a big particle counts more, its scale track +0x124) weighted by how many
// particles are alive (CParticleEmitter2 +0x50, the emitters +0x2C4; smoothed over a quarter of the particles' life,
// the lifespan track +0x98, so a steady emitter's count stepping by one doesn't flicker), its ribbons' (their texture x
// their colour and alpha tracks' first keys, while the ribbon state +0x2B8, 0x50 apart, says visible +0x4C), the glow
// of the rest of a spell's file (its geometry's textures and colours +0xA0, x the most visible colour's alpha and
// texture weight +0xA8), x its effective alpha (+0x198). It follows the model (its world matrix +0xB4, or the
// attachment it hangs on, env/Lights AttachedWorld) and fades out with it; a model gone fades where it was. A model the
// client animated within the last kHoldSeconds counts as drawn (a distant one is animated less often), and one not
// looked at closely this frame keeps the colour it last gave for as long.
#pragma once

#include "../env/Lights.hpp"
#include "../env/SpellLightModel.hpp"
#include "../env/WorldQuery.hpp"
#include "wxl/PluginApi.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wxl_livingazeroth::spelllights
{
    spelllight::Settings& Config();

    // --- the client (12340) ---------------------------------------------------------------------------------------------
    constexpr uintptr_t kScene = 0x00CD754C;             // CM2Scene* World::s_m2Scene
    constexpr uintptr_t kMissilesInFlight = 0x00CA0B58;  // CMissile* CMissile::s_inFlightMissileList
    constexpr uintptr_t kMissilesImpact = 0x00CA0B5C;    // CMissile* CMissile::s_missileDestroyList (hit, fading out)
    // CMissile
    constexpr size_t kMissileModel = 0x18, kMissileNext = 0x164;
    // CM2Scene
    constexpr size_t kSceneModels = 0x08, kSceneFrame = 0x14;
    // CM2Model
    constexpr size_t kModelFlags = 0x04, kModelScenePrev = 0x08, kModelNext = 0x0C, kModelInit = 0x10, kModelScene = 0x28,
                     kModelShared = 0x2C, kModelFrame = 0x3C, kModelParent = 0x48, kModelLoopOrigin = 0x74,
                     kModelColours = 0xA0, kModelTextureWeights = 0xA8, kModelWorld = 0xB4, kModelEffectiveAlpha = 0x198,
                     kModelLights = 0x1D0, kModelRibbons = 0x2B8, kModelEmitterStates = 0x2C0, kModelEmitters = 0x2C4;
    constexpr size_t kColourStride = 0x20, kColourValue = 0x08, kColourAlpha = 0x1C;           // M2ModelColor
    constexpr size_t kWeightStride = 0x0C, kWeightValue = 0x08;                                 // M2ModelTrack<float>
    constexpr size_t kLightStride = 0xD4, kLightVisibility = 0x60, kLightEnabled = 0x64, kLightCm2 = 0x68; // M2ModelLight
    constexpr size_t kCm2Colour = 0x3C;                                                         // CM2Light m_dirColor
    constexpr size_t kEmitterStateStride = 0x88, kEmitterStateActive = 0x85;                    // M2ModelParticle
    constexpr size_t kEmitterLive = 0x50;                                                       // CParticleEmitter2
    constexpr size_t kRibbonStateStride = 0x50, kRibbonStateVisible = 0x4C;                     // M2ModelRibbon
    // CM2Shared: its file name (lower case, .m2) +0x3C, its data (the file, its arrays' offsets made pointers) +0x150
    constexpr size_t kSharedName = 0x3C, kSharedData = 0x150;
    // M2Data
    constexpr size_t kDataVertices = 0x3C, kDataColours = 0x48, kDataTextures = 0x50, kDataTextureWeights = 0x58,
                     kDataMaterials = 0x70, kDataBoundsRadius = 0xB8, kDataLights = 0x108, kDataRibbons = 0x120,
                     kDataEmitters = 0x128;
    constexpr size_t kMaterialStride = 0x04, kMaterialBlend = 0x02;                            // M2Material
    constexpr size_t kRibbonStride = 0xB0, kRibbonTextures = 0x14, kRibbonMaterials = 0x1C,
                     kRibbonColourTrack = 0x24, kRibbonAlphaTrack = 0x38;                       // M2Ribbon (the file's)
    constexpr size_t kTextureStride = 0x10, kTextureName = 0x08;                                // M2Texture
    constexpr size_t kFileLightStride = 0x9C;                                                   // M2Light (type u16 at +0)
    constexpr size_t kParticleStride = 0x1DC, kParticleTexture = 0x16, kParticleBlend = 0x28, kParticleLifespan = 0x98,
                     kParticleColourTrack = 0x104, kParticleAlphaTrack = 0x114, kParticleScaleTrack = 0x124; // M2Particle
    constexpr float kHoldSeconds = 0.5f;   // animated this recently: drawn; looked at this recently: its colour holds

    /// The exe's own instructions that read what this reads: the feature stays off unless every one is as expected.
    struct Guard { uintptr_t va; uint8_t size; uint8_t bytes[24]; const char* what; };
    const Guard* Guards(size_t& count);
    /// Whether the guards hold for the code `codeAt` returns (the running client's, or a test's copy of an exe file);
    /// `why` names the first that doesn't.
    bool GuardsHold(const uint8_t* (*codeAt)(uintptr_t va, size_t size, void* user), void* user, std::string& why);

    struct Stats
    {
        bool     active = false;
        const char* why = "off";
        unsigned tracked = 0, missiles = 0, effects = 0, enchants = 0;     // spell visuals followed now, by kind
        unsigned headWalked = 0, headNew = 0, fromScan = 0;                 // this frame: the list's head walked, new found; offered by the scan (since the last frame)
        unsigned inFlight = 0, impact = 0;                                  // the missile lists' lengths
        unsigned shown = 0, looked = 0, lit = 0, fading = 0;                // drawn by the client this frame; of those the nearest looked at closely; giving light; gone, fading out
        unsigned inRange = 0, merged = 0, clusters = 0, dropped = 0, drawn = 0; // the frame's lights: in range, merged into clusters, over the cap, drawn
        unsigned leaving = 0, waiting = 0, overflow = 0, cellRoom = 0;      // of those drawn, fading out of the kept set; wanted but no room in a grid cell yet; dropped at once (no room); spell lights a grid cell holds
        unsigned files = 0, filesWaiting = 0;                               // model files known; spell visuals' files still loading
        unsigned textures = 0, texturesWaiting = 0, texturesRead = 0;       // glow colours known; waiting; read this frame
        int      spellRows = -1, itemRows = -1, objectRows = -1;            // SpellVisualEffectName / ItemVisualEffects / GameObjectDisplayInfo rows read
        unsigned clientEffects = 0, objectModels = 0;                       // files of the client's own effects (HARDCODED), of game objects
        double   ms = 0.0, msAverage = 0.0, msMost = 0.0;                   // this frame; over the last second
        double   msFind = 0.0, msTextures = 0.0, msModels = 0.0, msMerge = 0.0; // this frame's steps: finding them, glow colours, each model, merge and cap
    };
    const Stats& GetStats();

    /// Once per frame from env/Lights::Update (in world, our lights on): this frame's spell lights into `out` (appended).
    void Update(float dt, const world::Snapshot& snap, const float eye[3], std::vector<lights::FrameLight>& out);
    /// The light scan (env/Lights) met this model in range: true when it is a spell visual this feature lights (the scan
    /// then leaves its own lights to it). False whenever the feature is off.
    bool OfferFromScan(const void* model);
    /// Forget everything (world left, feature off).
    void Reset();

    struct Drawn { std::string path; spelllight::Kind kind; float pos[3]; float color[3]; float radius; unsigned sources; float distance; float presence; bool kept; };
    /// This frame's drawn spell lights, by score.
    const std::vector<Drawn>& DrawnLights();

    /// The lights panel's section.
    void DrawPanelSection(const WXL_Api* api);
    /// One line for the log and the snapshot.
    const char* StatusLine();
    /// The snapshot's (F11) lines: the settings, this frame's counts and cost, the nearest drawn lights.
    void SnapshotLines(std::vector<std::string>& out);

    // --- tests -------------------------------------------------------------------------------------------------------------
    struct ClientView { const void* scene = nullptr; const void* missiles[2] = {}; };
    using FileReader = bool (*)(const char* path, std::vector<uint8_t>& out);
    /// Where model textures and the three tables are read from (default: the client's archives, render/BlpTexture).
    void SetFileReader(FileReader read);
    /// The indoor test for the lights (default env/Lights IsIndoor; null: every spell light outdoors).
    void SetIndoorTest(bool (*isIndoor)(const float pos[3]));
    /// One frame over `view` instead of the client's globals (guards not checked).
    void FrameForTests(const ClientView& view, float dt, const float eye[3], std::vector<lights::FrameLight>& out);
}
