#include "Snapshot.hpp"

#include "SnapshotView.hpp"
#include "UiText.hpp"
#include "../env/ClientWeather.hpp"
#include "../env/Climate.hpp"
#include "../env/Fog.hpp"
#include "../env/FogSettings.hpp"
#include "../env/Lights.hpp"
#include "../env/Regional.hpp"
#include "../env/SeeThroughModel.hpp"
#include "../env/Storm.hpp"
#include "../env/WorldQuery.hpp"
#include "../features/Glass.hpp"
#include "../features/GroundFog.hpp"
#include "../features/ModelLights.hpp"
#include "../features/SeeThrough.hpp"
#include "../features/SpellLights.hpp"
#include "../features/SurfaceCover.hpp"
#include "../features/TerrainLights.hpp"
#include "../features/TreeShadows.hpp"

#include "engine/events/Event.hpp"
#include "game/Camera.hpp"
#include "game/Script.hpp"
#include "game/World.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace wxl_livingazeroth::snapshot
{
    namespace
    {
        namespace ev = wxl::events;
        namespace gw = wxl::game::world;
        namespace sc = seethrough::client;
        constexpr const char* kTag = "wxl-seyris-living-azeroth";
        constexpr float kMinGap = 0.5f;   // seconds: a held key, or the key and a button at once, write one

        const WXL_Api* g_api = nullptr;
        Settings g_settings;
        bool  g_down = false;         // the key's last state: its messages and the poll share it (one press, one snapshot)
        bool  g_pending = false;
        const char* g_by = "";
        float g_since = 1e9f;         // seconds since the last one
        float g_fps = 0.0f, g_frameMs = 0.0f, g_fpsTime = 0.0f;
        unsigned g_fpsFrames = 0;
        char  g_status[400] = "none written yet";

        void Observe(bool down, const char* by)
        {
            if (down && !g_down) { g_pending = true; g_by = by; }
            g_down = down;
        }

        // The window's messages, as the client gets them; not swallowed.
        void __cdecl OnInput(void* /*user*/, const void* args)
        {
            const auto* a = static_cast<const ev::InputArgs*>(args);
            if (!a || g_settings.key <= 0 || static_cast<int>(a->wparam) != g_settings.key) return;
            if (a->message == WM_KEYDOWN || a->message == WM_SYSKEYDOWN) Observe(true, "the key");
            else if (a->message == WM_KEYUP || a->message == WM_SYSKEYUP) Observe(false, "the key");
        }

        // The client's window has the focus (some input paths don't reach the subclassed window: the core polls F9 so too).
        bool Focused()
        {
            const HWND w = GetForegroundWindow();
            DWORD pid = 0;
            if (w) GetWindowThreadProcessId(w, &pid);
            return w && pid == GetCurrentProcessId();
        }

        std::string KeyName(int key)
        {
            if (key <= 0) return "no key: SnapshotKey 0";
            if (key >= VK_F1 && key <= VK_F24) return "F" + std::to_string(key - VK_F1 + 1);
            char name[64] = "";
            const UINT scan = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
            if (scan && GetKeyNameTextA(static_cast<LONG>(scan << 16), name, sizeof(name)) > 0) return name;
            char code[32];
            std::snprintf(code, sizeof(code), "key code %d", key);
            return code;
        }

        // --- what's read ------------------------------------------------------------------------------------------

        bool PlayerFacing(float& facing)
        {
            __try
            {
                const unsigned long long guid = gw::ActivePlayerGuid();
                void* player = guid ? gw::ResolveObject(guid, gw::kTypeMaskPlayer) : nullptr;
                if (!player) return false;
                facing = gw::Facing(player);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        struct CameraNow
        {
            float pos[3] = {}, forward[3] = {}, up[3] = {};
            float fovY = 0.0f, fovX = 0.0f, clientFov = 0.0f;   // radians: the projection's (vertical, across) and the camera object's
            bool  valid = false;
        };

        bool ReadCameraRaw(float pos[3], float viewProj[16], float proj[16], float& clientFov)
        {
            __try
            {
                wxl::game::camera::GetPosition(pos);
                std::memcpy(viewProj, reinterpret_cast<const void*>(sc::kViewProj), sizeof(float) * 16);
                std::memcpy(proj, wxl::game::camera::GetProjection(), sizeof(float) * 16);
                clientFov = wxl::game::camera::GetFov(wxl::game::camera::GetActiveCamera());
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        // Where the camera looks, convention-free: the screen's middle unprojected at the near and the far plane through
        // the client's s_viewProj (camera-relative, row vectors), the nearer of the two (from the eye) first; its up from
        // a point above the middle.
        CameraNow ReadCamera()
        {
            CameraNow c;
            float viewProj[16], proj[16], inv[16];
            if (!ReadCameraRaw(c.pos, viewProj, proj, c.clientFov) || !Invert(viewProj, inv)) return c;
            auto unproject = [&](float x, float y, float z, float out[3]) {
                const float clip[4] = { x, y, z, 1.0f };
                float r[4];
                for (int i = 0; i < 4; ++i) r[i] = clip[0] * inv[i] + clip[1] * inv[4 + i] + clip[2] * inv[8 + i] + clip[3] * inv[12 + i];
                for (int i = 0; i < 3; ++i) out[i] = r[3] != 0.0f ? r[i] / r[3] : 0.0f;
            };
            float a[3], b[3], above[3];
            unproject(0.0f, 0.0f, 0.0f, a);
            unproject(0.0f, 0.0f, 1.0f, b);
            const bool reversed = a[0] * a[0] + a[1] * a[1] + a[2] * a[2] > b[0] * b[0] + b[1] * b[1] + b[2] * b[2];
            const float* nearP = reversed ? b : a;
            const float* farP = reversed ? a : b;
            unproject(0.0f, 0.5f, reversed ? 1.0f : 0.0f, above);
            for (int k = 0; k < 3; ++k) { c.forward[k] = farP[k] - nearP[k]; c.up[k] = above[k] - nearP[k]; }
            Normalize(c.forward);
            const float along = c.up[0] * c.forward[0] + c.up[1] * c.forward[1] + c.up[2] * c.forward[2];
            for (int k = 0; k < 3; ++k) c.up[k] -= along * c.forward[k];
            Normalize(c.up);
            c.fovY = proj[5] > 0.05f && proj[5] < 50.0f ? 2.0f * std::atan(1.0f / proj[5]) : 0.0f;
            c.fovX = proj[0] > 0.05f && proj[0] < 50.0f ? 2.0f * std::atan(1.0f / proj[0]) : 0.0f;
            c.valid = std::isfinite(c.forward[0]) && std::isfinite(c.forward[1]) && std::isfinite(c.forward[2]);
            return c;
        }

        const char* WeatherName(int type)
        {
            static const char* const kNames[] = { "fine", "rain", "snow", "sand" };
            return type >= 0 && type < 4 ? kNames[type] : "?";
        }

        float Degrees(float radians) { return radians * 180.0f / 3.14159265f; }

        // --- the file -----------------------------------------------------------------------------------------------

        struct Text
        {
            std::string s;
            void operator()(const char* format, ...)
            {
                char line[1200];
                va_list args;
                va_start(args, format);
                std::vsnprintf(line, sizeof(line), format, args);
                va_end(args);
                s += line;
                s += '\n';
            }
            void Raw(const std::string& t) { s += t; }
        };

        // The camera in a WMO's space: the walked group's (its placement and s_camPosLocal), or the building the camera is
        // in by its instance's m_invMat.
        void ViewFromModel(const float model[16], const float camLocal[3], const CameraNow& c, WmoView& v)
        {
            Placement(model, v.yaw, v.tilt, v.origin);
            std::memcpy(v.cam, camLocal, sizeof(v.cam));
            float inv[16];
            if (Invert(model, inv))
            {
                Apply(inv, c.forward, 0.0f, v.forward);
                Apply(inv, c.up, 0.0f, v.up);
                Normalize(v.forward);
                Normalize(v.up);
            }
            v.fov = c.fovY;
        }

        // Between rooms (2026-10-23): each open window's room beyond it ("way out" from inside onto the street, "the room"
        // from outside).
        std::string Rooms(const seethrough::WalkedGroup& w)
        {
            if (w.open.empty()) return "none";
            std::string out;
            for (size_t i = 0; i < w.open.size(); ++i)
            {
                const int room = i < w.rooms.size() ? w.rooms[i] : -1;
                out += (i ? ", " : "") + std::to_string(w.open[i]) + " -> " +
                       (room >= 0 ? "group " + std::to_string(room) : w.inside ? std::string("the street") : std::string("its room"));
            }
            return out;
        }

        void Where(Text& t, const world::Snapshot& s)
        {
            t("Where");
            if (!s.inWorld) { t("not in the world (no player)"); return; }
            t("map: %d", s.mapId);
            t("zone: %s; subzone: %s", s.zoneText ? s.zoneText : "", s.subZoneText ? s.subZoneText : "");
            std::string chain;
            for (int i = 0; i < s.areaCount; ++i)
            {
                char part[128];
                std::snprintf(part, sizeof(part), "%s%s (%u)", i ? " < " : "", regional::AreaName(s.areaChain[i]), s.areaChain[i]);
                chain += part;
            }
            t("areas: %s (flags 0x%X)", chain.empty() ? "none" : chain.c_str(), s.areaFlags);
            t("position: %.3f %.3f %.3f", s.playerPos[0], s.playerPos[1], s.playerPos[2]);
            float facing = 0.0f;
            if (PlayerFacing(facing) && std::isfinite(facing)) t("facing: %.4f rad (%.1f degrees)", facing, Degrees(facing));
            else t("facing: not read");
            t("the client says: %s; terrain type %d", s.outdoors ? "outdoors" : "indoors", s.playerTerrainType);
        }

        void Camera(Text& t, const CameraNow& c, const world::Snapshot& s)
        {
            t("");
            t("Camera");
            if (!c.valid) { t("camera: not read"); return; }
            t("camera: %.3f %.3f %.3f", c.pos[0], c.pos[1], c.pos[2]);
            t("camera forward: %.4f %.4f %.4f (yaw %.1f degrees, pitch %.1f degrees)", c.forward[0], c.forward[1], c.forward[2],
              Degrees(std::atan2(c.forward[1], c.forward[0])), Degrees(std::asin(std::max(-1.0f, std::min(1.0f, c.forward[2])))));
            t("camera up: %.4f %.4f %.4f", c.up[0], c.up[1], c.up[2]);
            t.Raw(FovLine(c.fovY));
            t("camera fov across: %.2f degrees; the camera object's: %.2f degrees", Degrees(c.fovX), Degrees(c.clientFov));
            if (s.inWorld)
            {
                const float d[3] = { c.pos[0] - s.playerPos[0], c.pos[1] - s.playerPos[1], c.pos[2] - s.playerPos[2] };
                t("camera from the player: %.2f yd", std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
            }
        }

        // A point's distance to a box (0 inside), and where a ray first enters one (its t; false: never, or behind).
        float BoxDistance(const float p[3], const float lo[3], const float hi[3])
        {
            float s = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float d = p[k] < lo[k] ? lo[k] - p[k] : p[k] > hi[k] ? p[k] - hi[k] : 0.0f;
                s += d * d;
            }
            return std::sqrt(s);
        }
        bool RayBox(const float o[3], const float d[3], const float lo[3], const float hi[3], float& t)
        {
            float t0 = 0.0f, t1 = 1e30f;
            for (int k = 0; k < 3; ++k)
            {
                if (std::fabs(d[k]) < 1e-8f) { if (o[k] < lo[k] || o[k] > hi[k]) return false; continue; }
                float a = (lo[k] - o[k]) / d[k], b = (hi[k] - o[k]) / d[k];
                if (a > b) std::swap(a, b);
                t0 = std::max(t0, a);
                t1 = std::min(t1, b);
                if (t0 > t1) return false;
            }
            t = t0;
            return true;
        }

        void Building(Text& t, const CameraNow& c)
        {
            t("");
            t("The nearest WMO (debug/SnapshotView: glass_tests GLASS_SNAPSHOT and seethrough_tests SEETHROUGH_SNAPSHOT read these lines)");
            std::vector<seethrough::WalkedGroup> walked;
            seethrough::LastWalked(walked);
            std::string viewer;
            float invMat[16] = {};
            std::vector<uint32_t> flags;
            std::vector<float> boxes;
            const bool inBuilding = seethrough::ViewerBuilding(viewer, invMat, flags, boxes);
            // the camera's building, walked from inside last frame: that walk's (its windows' states with it)
            const seethrough::WalkedGroup* primary = nullptr;
            if (inBuilding)
                for (const auto& w : walked)
                    if (w.inside && _stricmp(w.wmo.c_str(), viewer.c_str()) == 0) { primary = &w; break; }
            // every group the client drew last frame, any building (2026-10-24: at Valkyrion the walk's last group was Dalaran's,
            // 1,372 yd off): the one under the screen's centre (the first box the view's middle ray enters, within 500 yd) and
            // the one nearest the camera (its box)
            std::vector<seethrough::DrawnGroup> drawn;
            seethrough::LastDrawn(drawn);
            const seethrough::DrawnGroup* nearest = nullptr;
            const seethrough::DrawnGroup* centre = nullptr;
            float nearestD = 0.0f, centreT = 0.0f, nearestLocal[3] = {}, centreLocal[3] = {};
            if (c.valid)
                for (const auto& g : drawn)
                {
                    float local[3], dir[3], hit = 0.0f;
                    Apply(g.invMat, c.pos, 1.0f, local);
                    Apply(g.invMat, c.forward, 0.0f, dir);
                    const float d = BoxDistance(local, g.lo, g.hi);
                    if (!nearest || d < nearestD) { nearest = &g; nearestD = d; std::memcpy(nearestLocal, local, sizeof(local)); }
                    if (RayBox(local, dir, g.lo, g.hi, hit) && hit <= 500.0f && (!centre || hit < centreT))
                    { centre = &g; centreT = hit; std::memcpy(centreLocal, local, sizeof(local)); }
                }
            const seethrough::DrawnGroup* pick = centre ? centre : nearest;
            const float* pickLocal = centre ? centreLocal : nearestLocal;
            const float pickD = pick ? BoxDistance(pickLocal, pick->lo, pick->hi) : 0.0f;
            // (outside any building: the see-through walk's nearest group when it's the same building or no farther; else the
            // group drawn under the screen's centre, or nearest)
            if (!primary && !inBuilding && !walked.empty() &&
                (!pick || _stricmp(walked.front().wmo.c_str(), pick->wmo.c_str()) == 0 || walked.front().distance <= pickD + 5.0f))
                primary = &walked.front();
            WmoView v;
            std::string how;
            if (primary)
            {
                v.wmo = primary->wmo;
                v.group = static_cast<int>(primary->group);
                v.inside = primary->inside;
                v.open = primary->open;
                v.shut = primary->shut;
                ViewFromModel(primary->model, primary->cam, c, v);
                char line[200];
                std::snprintf(line, sizeof(line), "the see-through walk %s last frame (s_camPosLocal, s_mapObjToWorld), %.1f yd from the group's box",
                              primary->inside ? "from inside (the camera's building)" : "from outside (the nearest group it visited)", primary->distance);
                how = line;
            }
            else if (!inBuilding && pick)
            {
                v.wmo = pick->wmo;
                v.group = static_cast<int>(pick->group);
                v.inside = false;
                float model[16];
                if (Invert(pick->invMat, model)) ViewFromModel(model, pickLocal, c, v);
                char line[300];
                std::snprintf(line, sizeof(line), "the WMO group the client drew last frame %s (%.1f yd from its box; the see-through walk didn't visit it: "
                              "no windows' states)", centre ? "under the screen's centre" : "nearest the camera", pickD);
                how = line;
            }
            else if (inBuilding && c.valid)
            {
                v.wmo = viewer;
                float model[16], local[3];
                Apply(invMat, c.pos, 1.0f, local);
                // the camera's group: the smallest box round it, an interior group's first
                int best = -1;
                float bestVolume = 0.0f;
                bool bestInterior = false;
                for (size_t g = 0; g < flags.size(); ++g)
                {
                    const float* b = &boxes[6 * g];
                    if (local[0] < b[0] || local[1] < b[1] || local[2] < b[2] || local[0] > b[3] || local[1] > b[4] || local[2] > b[5]) continue;
                    const bool interior = !(flags[g] & sc::kExteriorGroup);
                    const float volume = (b[3] - b[0]) * (b[4] - b[1]) * (b[5] - b[2]);
                    if (best < 0 || (interior && !bestInterior) || (interior == bestInterior && volume < bestVolume))
                    { best = static_cast<int>(g); bestVolume = volume; bestInterior = interior; }
                }
                v.group = best;
                v.inside = best >= 0 && bestInterior;
                if (Invert(invMat, model)) ViewFromModel(model, local, c, v);
                char line[240];
                std::snprintf(line, sizeof(line), "the building the camera is in (the client's s_viewerMapObjDef, its m_invMat; not in SeeThroughBuildings.cdbc "
                              "or not walked last frame: no windows' states)%s", best < 0 ? "; the camera in none of its groups' boxes by it" : "");
                how = line;
            }
            if (v.wmo.empty()) t("wmo: none (the camera is in no building, and the see-through walks visited none last frame)");
            else
            {
                t.Raw(FormatView(v));
                t("wmo how: %s", how.c_str());
                if (primary) t("wmo rooms beyond the open windows: %s", Rooms(*primary).c_str());
            }
            for (const auto& w : walked)
            {
                if (&w == primary) continue;
                t("also walked last frame: %s group %u from %s, %.1f yd from its box; open %s (onto %s); shut %s", w.wmo.c_str(), w.group,
                  w.inside ? "inside" : "outside", w.distance, Ordinals(w.open).c_str(), Rooms(w).c_str(), Ordinals(w.shut).c_str());
            }
            if (inBuilding && (!primary || _stricmp(primary->wmo.c_str(), viewer.c_str()) != 0))
                t("the camera's building (the client's): %s", viewer.c_str());
            auto drawnLine = [&](const char* what, const seethrough::DrawnGroup& g, const float local[3], float along) {
                char extra[64] = "";
                if (along >= 0.0f) std::snprintf(extra, sizeof(extra), ", %.1f yd along the view", along);
                t("drawn %s: %s group %u%s%s (%s), %.1f yd from its box%s; its glass batches %u of %u; in SeeThroughBuildings.cdbc: %s", what, g.wmo.c_str(),
                  g.group, g.name.empty() ? "" : " ", g.name.c_str(), (g.flags & sc::kExteriorGroup) ? "exterior" : "interior",
                  BoxDistance(local, g.lo, g.hi), extra, g.glassBatches, g.batches, g.inTable ? "yes" : "no");
            };
            t("drawn last frame: %zu WMO groups (Wmo.RenderGroup)", drawn.size());
            if (centre) drawnLine("under the screen's centre", *centre, centreLocal, centreT);
            if (nearest) drawnLine("nearest the camera", *nearest, nearestLocal, -1.0f);
        }

        void TimeAndWeather(Text& t, const world::Snapshot& s)
        {
            t("");
            t("Time and weather");
            const climate::Clock& c = climate::Now();
            const int sky = static_cast<int>(c.dayFraction * 1440.0f + 0.5f) % 1440;
            t("time of day: %02d:%02d, the sky's (day fraction %.4f)%s; %04d-%02d-%02d", sky / 60, sky % 60, c.dayFraction,
              climate::TimeOverride() >= 0.0f ? " (the climate panel's time override)" : "", c.year, c.month, c.day);
            t("daylight: %.2f; the sky's night %.2f", climate::SunNow().daylight, c.valid ? fog::SkyNight(c.dayFraction) : -1.0f);
            t("weather, the server's (the client's weather object): %s %.2f (raw %.2f)", WeatherName(s.weatherType), s.weatherIntensity, s.weatherRaw);
            const climate::Weather& w = climate::CurrentWeather();
            t("weather, Living Azeroth's: %s %.2f%s", WeatherName(w.type), w.intensity,
              climate::WeatherOverrideType() >= 0 ? " (the climate panel's override)" : "");
            const clientweather::Result& r = clientweather::Last();
            t("the weather asked of the client: %s (state %u%s%s)", r.applied ? "applied" : "none", r.state, r.note && r.note[0] ? "; " : "", r.note ? r.note : "");
            const storm::Status& st = storm::GetStatus();
            t("storm: %s (grade %.2f, storminess %.2f), darkness %.2f, the sky's day %.2f; the day-night colours: %s", storm::PhaseName(static_cast<storm::Phase>(st.phase)),
              st.grade, st.storminess, st.darkness, st.day, storm::BandStatus());
            const storm::Settings& ss = storm::Config();
            t("darker nights: %.2f now (NightDarkness %.2f x night %.2f x city %.2f x frost %.2f x indoors %.2f); the ambient %.0f%% of the client's",
              st.night.total, ss.nightDarkness, st.night.night, st.night.city, st.night.snow, st.night.indoor, 100.0f * (1.0f - st.night.total * storm::NightWeight(1)));
            if (s.inWorld)
            {
                const climate::Row row = climate::Here();
                t("air: %.1f degC, humidity %.0f%%", climate::Temperature(row).total, row.humidity * 100.0f);
            }
        }

        void SettingsNow(Text& t)
        {
            t("");
            t("Settings");
            const seethrough::Settings& st = seethrough::Config();
            t("see-through: SeeThroughWindows %d, SeeThroughFromInside %d, SeeThroughBetweenRooms %d, SeeThroughRedraw %d, SeeThroughOpacity %.2f, SeeThroughStainedTint %.2f, "
              "SeeThroughFrosted %d, SeeThroughFrostBlur %.2f", st.enabled, st.fromInside, st.betweenRooms, st.redraw, st.opacity, st.stainedTint, st.frosted, st.frostBlur);
            const groundfog::Settings& g = groundfog::Config();
            t("fog: on %d, with Modern Water %d, density x%.2f, brightness x%.2f, max distance %.0f yd, far fog %d, half resolution %d, steps %d, "
              "cloud deck %d", g.enabled, g.withModernWater, g.densityScale, g.brightness, g.maxDistance, g.farFog, g.halfResolution, g.steps, g.cloudDeck);
            for (int k = 0; k < fog::kKindCount; ++k)
                t("  fog kind %s: %s", fog::KindName(k), fog::Settings(k).enabled ? "on" : "off");
            const storm::Settings& s = storm::Config();
            t("storm: Enabled %d, Darkness %.2f, NightDarkness %.2f, Flashes %d, Thunder %d, Haze %.2f", s.enabled, s.darkness, s.nightDarkness, s.flashes,
              s.thunder, s.haze);
            const glass::Settings& gl = glass::Config();
            t("glass: Enabled %d, FakeInteriors %d, LitRoomsAtNight %d, FrostedRooms %d, StainedRooms %d, RoomShareDay %.2f, RoomShareNight %.2f, WholeRooms %d",
              gl.enabled, gl.fakeInteriors, gl.litRooms, gl.frostedRooms, gl.stainedRooms, gl.roomShareDay, gl.roomShareNight, gl.wholeRooms);
        }

        void Panels(Text& t, const world::Snapshot& s)
        {
            std::vector<std::string> lines;
            t("");
            t("See-through panel");
            seethrough::StatusLines(lines);
            for (const std::string& l : lines) t("%s", l.c_str());
            lines.clear();
            seethrough::CopyLines(lines);
            for (const std::string& l : lines) t("%s", l.c_str());
            t("");
            t("Fog panel");
            lines.clear();
            groundfog::StatusLines(lines);
            for (const std::string& l : lines) t("%s", l.c_str());
            if (s.inWorld)
                for (int k = 0; k < fog::kKindCount; ++k)
                    t("%-10s %.2f -> %.2f%s", fog::KindName(k), fog::AmountAt(k, s.playerPos[0], s.playerPos[1]), fog::TargetAt(k, s.playerPos[0], s.playerPos[1]),
                      fog::Settings(k).enabled ? "" : "   (off)");
            t("FogSettings.cdbc: %s", fogsettings::Status());
            t("");
            t("Glass");
            t("%s", glass::StatusLine());
            t("");
            t("Lights panel");
            lines.clear();
            lights::SnapshotLines(lines);
            for (const std::string& l : lines) t("%s", l.c_str());
            t("%s", terrainlights::StatusLine());
            t("%s", modellights::StatusLine());
            lines.clear();
            spelllights::SnapshotLines(lines);
            for (const std::string& l : lines) t("%s", l.c_str());
            t("");
            t("Surface cover panel");
            lines.clear();
            cover::StatusLines(lines);
            for (const std::string& l : lines) t("%s", l.c_str());
            t("");
            t("Tree shadows panel");
            lines.clear();
            treeshadows::StatusLines(lines);
            for (const std::string& l : lines) t("%s", l.c_str());
        }

        std::wstring GameFolder()
        {
            wchar_t exe[MAX_PATH];
            const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
            if (!n || n >= MAX_PATH) return L".";
            const std::wstring s(exe, n);
            const size_t slash = s.find_last_of(L"\\/");
            return slash == std::wstring::npos ? L"." : s.substr(0, slash);
        }

        void Announce(const std::string& relative)
        {
            if (!wxl::game::script::Context()) return;
            std::string lua = "if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage(\"Living Azeroth: snapshot written to ";
            for (const char ch : relative)
                if (ch == '\\') lua += "\\\\";
                else if (ch == '"') lua += "\\\"";
                else lua += ch;
            lua += "\", 0.55, 0.8, 1.0) end";
            wxl::game::script::Execute(lua.c_str(), "LivingAzerothSnapshot");
        }

        void Write()
        {
            SYSTEMTIME now{};
            GetLocalTime(&now);
            const world::Snapshot& s = world::Current();
            const CameraNow c = ReadCamera();
            Text t;
            t("Living Azeroth snapshot, %04u-%02u-%02u %02u:%02u:%02u (this computer's time), by %s; the module built %s %s", now.wYear, now.wMonth, now.wDay,
              now.wHour, now.wMinute, now.wSecond, g_by, __DATE__, __TIME__);
            t("frame rate: %.1f fps (%.2f ms a frame, the last second's)", g_fps, g_frameMs);
            t("");
            Where(t, s);
            Camera(t, c, s);
            Building(t, c);
            TimeAndWeather(t, s);
            SettingsNow(t);
            Panels(t, s);

            const std::wstring folder = GameFolder() + L"\\Logs";
            CreateDirectoryW(folder.c_str(), nullptr);
            const std::wstring dir = folder + L"\\la-snapshots";
            CreateDirectoryW(dir.c_str(), nullptr);
            char name[48];
            std::snprintf(name, sizeof(name), "%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
            std::string file;
            FILE* f = nullptr;
            for (int n = 1; n <= 9 && !f; ++n)
            {
                file = std::string(name) + (n > 1 ? "-" + std::to_string(n) : std::string()) + ".txt";
                const std::wstring path = dir + L"\\" + std::wstring(file.begin(), file.end());
                if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
                f = _wfopen(path.c_str(), L"w");
            }
            const std::string relative = "Logs\\la-snapshots\\" + file;
            if (!f)
            {
                std::snprintf(g_status, sizeof(g_status), "not written: %s couldn't be made (error %lu)", relative.c_str(), GetLastError());
                if (g_api) g_api->Log(WXL_LOG_WARN, kTag, "snapshot: %s", g_status);
                return;
            }
            const size_t wrote = std::fwrite(t.s.data(), 1, t.s.size(), f);
            const bool ok = std::fclose(f) == 0 && wrote == t.s.size();
            std::snprintf(g_status, sizeof(g_status), "%s %s (by %s, %.1f KB, %02u:%02u:%02u)", ok ? "written:" : "NOT all written:", relative.c_str(), g_by,
                          t.s.size() / 1024.0, now.wHour, now.wMinute, now.wSecond);
            if (g_api) g_api->Log(ok ? WXL_LOG_INFO : WXL_LOG_WARN, kTag, "snapshot: %s", g_status);
            if (ok) Announce(relative);
        }
    }

    Settings& Config() { return g_settings; }

    void Install(const WXL_Api* api)
    {
        g_api = api;
        if (api) api->Subscribe(static_cast<uint32_t>(ev::Event::OnInput), &OnInput, nullptr);
    }

    void Update(float dt)
    {
        if (std::isfinite(dt) && dt > 0.0f && dt < 5.0f)
        {
            g_since += dt;
            g_fpsTime += dt;
            ++g_fpsFrames;
            if (g_fpsTime >= 1.0f)
            {
                g_fps = static_cast<float>(g_fpsFrames) / g_fpsTime;
                g_frameMs = 1000.0f * g_fpsTime / static_cast<float>(g_fpsFrames);
                g_fpsTime = 0.0f;
                g_fpsFrames = 0;
            }
        }
        if (g_settings.key > 0 && g_settings.key < 256)
            Observe(Focused() && (GetAsyncKeyState(g_settings.key) & 0x8000) != 0, "the key");
        if (!g_pending || g_since < kMinGap) return;
        g_pending = false;
        g_since = 0.0f;
        Write();
    }

    void Request(const char* by)
    {
        g_pending = true;
        g_by = by ? by : "the panel";
    }

    const char* Status() { return g_status; }

    void DrawButton(const WXL_Api* api, const char* id)
    {
        char label[128];
        std::snprintf(label, sizeof(label), "Write a snapshot for a bug report (the key: %s)%s", KeyName(g_settings.key).c_str(), id ? id : "");
        if (api->UiButton(label)) Request("the panel's button");
        char line[460];
        std::snprintf(line, sizeof(line), "Snapshots go to Logs\\la-snapshots in the game folder. Last: %s", g_status);
        ui::Text(api, line);
    }
}
