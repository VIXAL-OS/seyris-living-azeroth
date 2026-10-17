#include "Config.hpp"

#include "debug/WindView.hpp"
#include "env/Fields.hpp"
#include "env/Fog.hpp"
#include "env/Shelter.hpp"
#include "env/Storm.hpp"
#include "features/DoodadFade.hpp"
#include "features/FogWake.hpp"
#include "features/GroundFog.hpp"
#include "features/GrassInstanced.hpp"
#include "features/GrassMotion.hpp"
#include "features/SurfaceCover.hpp"
#include "features/ModelIce.hpp"
#include "features/TerrainIce.hpp"
#include "features/WmoIce.hpp"
#include "features/TerrainWetness.hpp"
#include "render/PostAA.hpp"
#include "wxl_seyris/SettingsApi.hpp"

namespace wxl_livingazeroth::config
{
    namespace
    {
        constexpr const char* kTag = "wxl-seyris-living-azeroth";

        const WXL_SeyrisSettingsApi* Settings(const WXL_Api* api)
        {
            static const WXL_SeyrisSettingsApi* settings = nullptr;
            static bool resolved = false;
            if (resolved) return settings;
            resolved = true;

            const auto* s = static_cast<const WXL_SeyrisSettingsApi*>(
                api->GetInterface(WXL_SEYRIS_SETTINGS_INTERFACE_NAME, WXL_SEYRIS_SETTINGS_INTERFACE_VERSION));
            if (!s)
                api->Log(WXL_LOG_WARN, kTag, "config: wxl-seyris-tools isn't loaded; using built-in defaults (no WarcraftXL.ini settings).");
            else if (!s->HasFeature("get-float"))
                api->Log(WXL_LOG_WARN, kTag, "config: wxl-seyris-tools %u.%u.%u is too old (needs settings 1.1.0, \"get-float\"); using built-in defaults.",
                         s->major, s->minor, s->patch);
            else
                settings = s;
            return settings;
        }

        bool Bool(const WXL_SeyrisSettingsApi* s, const char* key, const char* description, bool fallback)
        {
            return s->GetBool(kSection, key, description, fallback ? 1 : 0) != 0;
        }

        float Float(const WXL_SeyrisSettingsApi* s, const char* key, const char* description, float fallback)
        {
            return s->GetFloat(kSection, key, description, fallback);
        }

        // In a sub-section.
        int In(const WXL_SeyrisSettingsApi* s, const char* section, const char* key, const char* description, int fallback)
        {
            return s->GetBool(section, key, description, fallback ? 1 : 0) != 0 ? 1 : 0;
        }
        float In(const WXL_SeyrisSettingsApi* s, const char* section, const char* key, const char* description, float fallback)
        {
            return s->GetFloat(section, key, description, fallback);
        }
        int Whole(const WXL_SeyrisSettingsApi* s, const char* section, const char* key, const char* description, int fallback)
        {
            const float v = s->GetFloat(section, key, description, static_cast<float>(fallback));
            return static_cast<int>(v < 0.0f ? v - 0.5f : v + 0.5f);
        }
    }

    float GetFloat(const WXL_Api* api, const char* key, const char* description, float fallback)
    {
        const WXL_SeyrisSettingsApi* s = Settings(api);
        return s ? Float(s, key, description, fallback) : fallback;
    }

    void Apply(const WXL_Api* api)
    {
        const WXL_SeyrisSettingsApi* s = Settings(api);
        if (!s) return;

        // Built-in values are the fallbacks, so the ini starts out matching the code.
        grassinst::Settings& inst = grassinst::Tunables();
        inst.enabled = Bool(s, "InstancedGrass",
            "Draw grass with the instanced renderer (much faster at long grass distances). 0 = the client's own path.", inst.enabled);
        inst.batched = Bool(s, "InstancedGrassBatched",
            "Instanced grass draws all visible grass layers together at the end of the grass pass (much less CPU time). 0 = one layer at a time.",
            inst.batched);
        inst.buildBudgetMs = Float(s, "InstancedGrassBuildBudgetMs",
            "Milliseconds per frame spent preparing grass coming into view; the rest is drawn the client's way until a later frame.",
            inst.buildBudgetMs);
        inst.memoryLimitMB = Float(s, "InstancedGrassMemoryMB",
            "Most memory (MB) the instanced grass may use. It shares the client's 32-bit address space; too high can crash the client.",
            inst.memoryLimitMB);
        inst.maxMultiplier = Float(s, "GrassDensityMaxMultiplier",
            "Instanced grass only: your limit on grass density (plants drawn per placed plant, 1 = never denser, up to 8). How dense comes from GroundEffectDoodadDensity.cdbc.",
            inst.maxMultiplier);
        inst.maxRadius = Float(s, "GrassDensityMaxRadius", "Your limit on how far around you grass is made denser, in yards.", inst.maxRadius);
        inst.maxSpread = Float(s, "GrassDensityMaxSpread", "Your limit on how far an extra plant may move from its plant, in yards.", inst.maxSpread);
        inst.thinEnabled = Bool(s, "GrassThinning",
            "Instanced grass only: far grass draws fewer plants (an even sample), which saves a lot of GPU time at long grass distances.",
            inst.thinEnabled);
        inst.thinStart = Float(s, "GrassThinStart", "Yards from the camera within which every plant is drawn.", inst.thinStart);
        inst.thinEnd = Float(s, "GrassThinEnd", "Yards from the camera where thinning reaches GrassThinMinShare.", inst.thinEnd);
        inst.thinMinShare = Float(s, "GrassThinMinShare", "Share of plants drawn at GrassThinEnd and beyond (0.01 to 1).", inst.thinMinShare);

        grass::Settings& g = grass::Tunables();
        g.enabled = Bool(s, "GrassMotion", "Grass sways in the wind and parts around characters.", g.enabled);
        g.amplitude = Float(s, "GrassSway", "Grass tip movement in yards at full wind.", g.amplitude);
        g.flutter = Float(s, "GrassFlutter", "Extra per-blade shimmer in yards.", g.flutter);
        g.flutterSpeed = Float(s, "GrassFlutterSpeed", "Shimmer speed in radians per second.", g.flutterSpeed);
        g.anchor = Float(s, "GrassStiffBase", "Bottom fraction of each blade that never moves (0..0.9).", g.anchor);
        g.pushStrength = Float(s, "GrassPushStrength", "How far grass leans away from characters, in yards.", g.pushStrength);
        g.radiusScale = Float(s, "GrassPushRadiusScale", "Push radius as a multiple of a character's bounding radius.", g.radiusScale);
        g.minRadius = Float(s, "GrassMinPushRadius", "Smallest push radius in yards.", g.minRadius);
        g.mountedScale = Float(s, "GrassMountedRadiusScale", "Extra push radius factor while mounted.", g.mountedScale);

        postaa::Settings& aa = postaa::Tunables();
        const bool fxaa = Bool(s, "AntiAliasing",
            "Our own anti-aliasing (FXAA). Turn the client's multisampling off to use it: readable depth (for fog etc.) needs it off.",
            aa.mode != postaa::Mode::Off);
        aa.mode = fxaa ? postaa::Mode::Fxaa : postaa::Mode::Off;
        aa.subpixel = Float(s, "AntiAliasingSubpixel", "FXAA subpixel smoothing, 0 (sharpest) .. 1 (softest).", aa.subpixel);

        cover::Configure(
            Bool(s, "SurfaceCover", "Snow (and other) cover on the ground that characters sink into and carve trenches in. Where and how comes from SurfaceCover.cdbc.", cover::Enabled()),
            static_cast<int>(Float(s, "SurfaceCoverLevels", "How far the cover reaches: 1..5 rings of 40, 80, 160, 320, 640 yards.", static_cast<float>(cover::Levels())) + 0.5f),
            Bool(s, "SurfaceCoverTrenches", "Characters and creatures carve trenches in the cover.", cover::Trenches()),
            Float(s, "SurfaceCoverDepth", "Your multiplier on the cover depth from SurfaceCover.cdbc (1 = as the table says).", cover::DepthMultiplier()),
            Float(s, "SurfaceCoverFillBudgetMs", "Milliseconds per frame spent filling the cover around you (on login, teleports, moving). Lower = smoother, slower to appear.", cover::FillBudgetMs()));
        cover::SetLiftBaseEffects(Bool(s, "SurfaceCoverLiftEffects", "Effects at units' feet (auras and other spell effects on the Base attachment) sit on top of the cover instead of under it (within 40 yards of you).", cover::LiftBaseEffects()));

        terrainwet::SetEnabled(Bool(s, "TerrainWetness", "The terrain itself looks darker where the ground is wet (rain, water nearby).", terrainwet::Enabled()));
        terrainwet::SetStrength(Float(s, "TerrainWetnessStrength", "How much darker soaked ground gets, 0..1: the terrain and the surface cover alike.", terrainwet::Strength()));
        terrainice::SetEnabled(Bool(s, "ClearIce", "Icy ground (GroundMaterial rows with Flags 0x1: Storm Peaks ice, Winterspring glacier, Icecrown frozen bones) looks like deep, clear ice.", terrainice::Enabled()));
        terrainice::SetStrength(Float(s, "ClearIceStrength", "How fully icy ground turns to clear ice, 0..1.", terrainice::Strength()));
        wmoice::SetEnabled(Bool(s, "ClearIceBuildings", "Ice built into buildings and dungeons (the Nexus, Icecrown, ice caves, Azjol-Nerub) looks like deep, clear ice too (needs dynamic shadows on).", wmoice::Enabled()));
        modelice::SetEnabled(Bool(s, "ClearIceModels", "Ice models (icebergs, shoreline ice, ice cliffs, frozen waterfalls, Ulduar's ice walls) look like deep, clear ice too (needs dynamic shadows on).", modelice::Enabled()));
        modelice::SetFadeHats(Bool(s, "FadeWaterfallHats", "The waterfall hats (the streaky sheet over a river where it pours over a fall's edge) fade in from their upstream end instead of starting in a jagged edge on the water.", modelice::FadeHats()));
        modelice::SetHideHats(Bool(s, "HideWaterfallHats", "Don't draw the waterfall hats at all (a test switch: the modern water's own run to the edge is subtle).", modelice::HideHats()));
        terrainice::SetClarity(Float(s, "ClearIceClarity", "How much of the deep blue inside shows through the ice's own texture, 0..1.", terrainice::Clarity()));

        fields::SetShoreWorker(Bool(s, "ShoreWorker",
            "Work out the ground's distance to water and hot ground (the shore pass) on a background thread instead of the game thread. Same results, a frame later. 0 = the original pass on the game thread.",
            fields::ShoreWorker()));

        shelter::SetEnabled(Bool(s, "WindShelter", "Roofs and overhangs shelter the ground below from wind.", shelter::Enabled()));
        shelter::SetLeeEnabled(Bool(s, "WindLee", "Walls and cliffs cast a wind shadow on their downwind side.", shelter::LeeEnabled()));

        debug::SetShowArrows(Bool(s, "DebugWindArrows", "Debug: draw the wind as arrows around the player.", debug::ShowArrows()));

        // Ground fog renderer. Where fog forms and its kinds' values come from FogSettings.cdbc.
        {
            const char* f = kSectionFog;
            groundfog::Settings& g = groundfog::Config();
            g.enabled = In(s, f, "Enabled", "Draw the ground fog.", g.enabled);
            g.withModernWater = Whole(s, f, "WithModernWater", "Draw alongside wxl-modern-water, which draws only the water (its fog is the separate wxl-vol-fog): 0 = give way to it.", g.withModernWater);
            g.densityScale = In(s, f, "DensityScale", "Your multiplier on every fog kind's density (1 = as FogSettings.cdbc says).", g.densityScale);
            g.maxDistance = In(s, f, "MaxDistance", "How far the fog is drawn, in yards.", g.maxDistance);
            g.farFog = In(s, f, "FarFog", "Beyond 128 yards the fog continues at each zone's typical level (no wall where the near fog ends).", g.farFog);
            doodadfade::Config().smallUseNext = Whole(s, f, "SmallDoodadsUseNextDistance", "Doodads with a radius under 1 yd (mugs, plates) use the next size's view distance instead of a fixed 30 yd (more doodads drawn).", doodadfade::Config().smallUseNext);
            doodadfade::Config().dissolve = Whole(s, f, "DoodadDissolve", "Doodads fading in at their view distance dissolve in instead, drawn solid so the ground fog covers them (copies of a model drawn together keep the client's fade); while it works every size category fades. 0 = off.", doodadfade::Config().dissolve);
            doodadfade::Config().fadeFrom = Whole(s, f, "DoodadFadeFromCategory", "Doodads of size categories below this don't fade in but pop in, so the ground fog covers them (fading ones show unfogged): 0 = stock, 5 = none fade.", doodadfade::Config().fadeFrom);
            g.handoverYards = In(s, f, "HandoverWidth", "Yards over which the near fog fades into the far fog at the near field's edge (~128 yd): wider = softer seam, less near detail.", g.handoverYards);
            g.beyondMax = In(s, f, "BeyondMaxDistance", "Past MaxDistance, the far fog continues to the terrain drawn there (one long step, cheap).", g.beyondMax);
            g.halfResolution = In(s, f, "HalfResolution", "Draw the fog at half resolution, upscaled by depth (sharp at silhouettes): about a quarter of the GPU time.", g.halfResolution);
            g.steps = Whole(s, f, "StepBudget", "Steps per pixel through the fog (quality, not the look): more = smoother far away, slower.", g.steps);
            g.fogStep = In(s, f, "StepLength", "Step length inside the fog near you, in yards (grows with distance). Smaller = finer, slower.", g.fogStep);
            g.dither = In(s, f, "Dither", "0 = smooth (may show faint bands) .. 1 = grainy.", g.dither);
            g.floorReach = Whole(s, f, "FloorReach", "Light fog starts on the lowest ground this many 2-yard cells around (0..4): higher keeps it off walls, thins it on slopes.", g.floorReach);
            g.noiseAmount = In(s, f, "Patchiness", "How patchy the fog is, 0 (smooth layers) .. 1.", g.noiseAmount);
            g.noiseTileYards = In(s, f, "PatchSize", "Size of the patch pattern's tile in yards (the patches are about 1/8 of it).", g.noiseTileYards);
            g.brightness = In(s, f, "Brightness", "Your multiplier on the fog's colour brightness.", g.brightness);
            g.fogLights = In(s, f, "Lights", "Our lights (torches, braziers, lamps) light the fog around them.", g.fogLights);
            g.lightStrength = In(s, f, "LightStrength", "Your multiplier on our lights' light in the fog.", g.lightStrength);
            g.lightRange = In(s, f, "LightRange", "Lights farther than this from the camera (yards) don't light the fog. Lower = cheaper.", g.lightRange);
            g.cloudDeck = In(s, f, "CloudDeck", "Mountain zones (AreaClimate CloudBase): a deck of cloud in the air from the base up; peaks and buildings stand out of it.", g.cloudDeck);
            g.cloudDeckDensity = In(s, f, "CloudDeckDensity", "Your multiplier on the cloud deck's density.", g.cloudDeckDensity);
            g.cloudDeckInside = In(s, f, "CloudDeckInside", "The cloud deck's density while you're inside it, as a share (back to 1 as you leave it through its soft bottom or top): lower = you see further when flying through an overcast deck.", g.cloudDeckInside);
            g.cloudDeckThickness = In(s, f, "CloudDeckThickness", "The cloud deck's thickness in yards, from its base up.", g.cloudDeckThickness);
            g.cloudDeckRange = In(s, f, "CloudDeckRange", "How far the cloud deck is drawn, in yards.", g.cloudDeckRange);
            g.cloudDeckSmooth = In(s, f, "CloudDeckSmooth", "1 = the cloud deck is worked out at half resolution and blurred onto the screen (smooth; about a quarter of its GPU time), 0 = every pixel (finer, grainy at a distance).", g.cloudDeckSmooth);
            g.cloudDeckGlow = In(s, f, "CloudDeckGlow", "The cloud deck glows around what's much brighter than it (lightning flashes, the moon): your multiplier, 0 = off.", g.cloudDeckGlow);
            g.cloudDeckTint = In(s, f, "CloudDeckTint", "In the cloud deck, see-through models and effects drawn after it (Blizzard's own storm clouds) fog toward the cloud's colour instead of the night fog's.", g.cloudDeckTint);
            g.poolSoftness = In(s, f, "PoolSoftness", "How soft heavy fog's edges are, 0 (crisp top, sharp sides) .. 1: its top blurs over this share of its depth, thin pools fade out.", g.poolSoftness);
            fog::Field().poolSpeed = In(s, f, "PoolSpeed", "How fast heavy fog (each kind's Weight share) runs downhill into hollows: 1 = default, 0 = it stays where it formed.", fog::Field().poolSpeed);
            fog::Field().steepStart = In(s, f, "SteepStart", "Degrees: from this slope a growing share of the ground-hugging fog runs off (it doesn't hang on steep ground).", fog::Field().steepStart);
            fog::Field().steepFull = In(s, f, "SteepFull", "Degrees: from this slope all of it runs off.", fog::Field().steepFull);
            g.forceWaterDepth = In(s, f, "WaterWritesDepth", "While fog is drawn, water writes depth so lakes are fogged at their surface (not their bed).", g.forceWaterDepth);
        }
        // Units pushing the ground fog aside.
        {
            const char* f = kSectionFogWake;
            fogwake::Settings& w = fogwake::Config();
            w.enabled = In(s, f, "Enabled", "Characters and creatures push the ground fog aside and leave a swirling trail.", w.enabled);
            w.strength = In(s, f, "PushMoving", "How much fog a moving unit pushes out, 0..1.", w.strength);
            w.standing = In(s, f, "PushStanding", "How much fog a standing unit pushes out, 0..1.", w.standing);
            w.trailSeconds = In(s, f, "FillSeconds", "Seconds until a trail fills back in.", w.trailSeconds);
            w.radiusScale = In(s, f, "Radius", "Wake radius as a multiple of the unit's radius (at least 0.6 yards).", w.radiusScale);
            w.spread = In(s, f, "Widening", "How much the trail widens as it fades, times its radius.", w.spread);
            w.heightScale = In(s, f, "Height", "How high the push reaches, as a multiple of the unit's height.", w.heightScale);
            w.rim = In(s, f, "Rim", "Fog piled up at the wake's edge, as a share of the push.", w.rim);
            w.swirl = In(s, f, "Swirl", "How far the fog's pattern is dragged and curled along the path.", w.swirl);
        }
        // Storms: rain building up into thunder.
        {
            const char* f = kSectionStorm;
            storm::Settings& t = storm::Config();
            t.enabled = In(s, f, "Enabled", "Rain builds up into storms: light and medium rain darken the sky and bring gusts, haze and the odd flash in the clouds; heavy rain brings strikes with thunder.", t.enabled);
            t.darkness = In(s, f, "Darkness", "How much darker the sky and the light get in full storm, 0..1 (less in lighter rain).", t.darkness);
            t.flashes = In(s, f, "Flashes", "Flashes in the clouds (the sky brightens, no bolt), from light rain on.", t.flashes);
            t.flashStrength = In(s, f, "FlashStrength", "Your multiplier on every flash's brightness.", t.flashStrength);
            t.frequency = In(s, f, "Frequency", "Your multiplier on how often flashes and strikes come (1 = about one a minute in light rain, every 10-20 s in heavy rain).", t.frequency);
            t.thunder = In(s, f, "Thunder", "Heavy rain: lightning strikes, lighting the sky and (near ones) the ground.", t.thunder);
            t.sounds = In(s, f, "ThunderSounds", "A strike's crack, after the sound's travel time (about 3 s per km), quieter the farther.", t.sounds);
            t.soundDistance = In(s, f, "ThunderSoundDistance", "Strikes farther than this (yards) are silent.", t.soundDistance);
            t.strikeLight = In(s, f, "StrikeLight", "Your multiplier on the point light at a strike within light range, 0 = none.", t.strikeLight);
            t.wind = In(s, f, "Gusts", "Your multiplier on the storm's extra gusts, 0 = none.", t.wind);
            t.haze = In(s, f, "Haze", "Extra haze in full storm, 0..1 (on top of the rain's own), 0 = none.", t.haze);
            t.bolts = In(s, f, "Bolts", "Experimental: a lightning bolt model at strikes (our own models in the client's scene).", t.bolts);
            t.boltSize = In(s, f, "BoltSize", "The bolt's size, x Call Lightning's (about 11.5 yards tall).", t.boltSize);
            t.boltRange = In(s, f, "BoltRange", "Strikes farther than this (yards) get no bolt model.", t.boltRange);
            t.scorch = In(s, f, "Scorch", "Experimental: a scorch mark on the terrain where a near strike lands.", t.scorch);
            t.scorchSeconds = In(s, f, "ScorchSeconds", "Seconds the scorch mark takes to fade.", t.scorchSeconds);
        }

        api->Log(WXL_LOG_INFO, kTag, "config: settings read from [%s], [%s], [%s], [%s] in WarcraftXL.ini.", kSection, kSectionFog, kSectionFogWake, kSectionStorm);
    }
}
