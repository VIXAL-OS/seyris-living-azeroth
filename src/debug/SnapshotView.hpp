// A snapshot's view (2026-10-22, Lumenveil's queue #10: "one key (and a panel button) that writes your exact situation to
// a file so bug reports don't need guessing"): the lines of a snapshot file (debug/Snapshot) that put the camera in a
// WMO's own space -- its path, the group walked, the placement, the camera's position and direction there, the windows
// open last frame -- written by the runtime and read back by the harnesses (glass_tests GLASS_SNAPSHOT: the game's
// sequence on WARP from that viewpoint; seethrough_tests SEETHROUGH_SNAPSHOT: the survey's probe from that eye), so a
// reported view is drawn again directly. Plain "key: value" lines. Pure (no client): the tests read it as it's written.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace wxl_livingazeroth::snapshot
{
    /// The camera in a WMO's space.
    struct WmoView
    {
        std::string wmo;                 // its path, as the client loaded it
        int   group = -1;                // the group walked (from inside: the camera's room), -1: none known
        bool  inside = false;            // walked from inside (the room's own windows), else from outside
        float yaw = 0.0f, tilt = 0.0f;   // the placement: degrees about the vertical; how far its up leans (degrees)
        float origin[3] = {};            // the placement's origin, world yd
        float cam[3] = {}, forward[3] = {}, up[3] = {};   // the camera in the WMO's space, where it looks, its up
        float fov = 0.0f;                // the camera's field of view, radians (0: not known)
        std::vector<uint32_t> open, shut;   // the group's windows (ordinals) open and shut last frame
    };

    // --- the placement (row vectors, row-major: world = (local, 1) M, as the client's s_mapObjToWorld) ---------------

    /// A 4x4's inverse (row-major); false if it has none.
    inline bool Invert(const float m[16], float out[16])
    {
        double inv[16];
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
        const double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (!std::isfinite(det) || std::fabs(det) < 1e-12) return false;
        for (int i = 0; i < 16; ++i) out[i] = static_cast<float>(inv[i] / det);
        return true;
    }

    /// A point or a direction (w 0) through a row-vector matrix.
    inline void Apply(const float m[16], const float v[3], float w, float out[3])
    {
        float r[3];
        for (int j = 0; j < 3; ++j) r[j] = v[0] * m[j] + v[1] * m[4 + j] + v[2] * m[8 + j] + w * m[12 + j];
        for (int j = 0; j < 3; ++j) out[j] = r[j];
    }

    /// The placement's yaw (degrees: its x axis from world x toward world y), how far its up leans from the vertical
    /// (degrees) and its origin.
    inline void Placement(const float model[16], float& yaw, float& tilt, float origin[3])
    {
        yaw = static_cast<float>(std::atan2(model[1], model[0]) * 180.0 / 3.14159265358979);
        const float l = std::sqrt(model[8] * model[8] + model[9] * model[9] + model[10] * model[10]);
        const float c = l > 0.0f ? model[10] / l : 1.0f;
        tilt = static_cast<float>(std::acos(c < -1.0f ? -1.0f : c > 1.0f ? 1.0f : c) * 180.0 / 3.14159265358979);
        for (int k = 0; k < 3; ++k) origin[k] = model[12 + k];
    }

    inline void Normalize(float v[3])
    {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0.0f) for (int k = 0; k < 3; ++k) v[k] /= l;
    }

    // --- the lines ----------------------------------------------------------------------------------------------------

    constexpr const char* kWmo = "wmo";
    constexpr const char* kGroup = "wmo group";
    constexpr const char* kPlacement = "wmo placement";
    constexpr const char* kCamera = "wmo camera";
    constexpr const char* kForward = "wmo camera forward";
    constexpr const char* kUp = "wmo camera up";
    constexpr const char* kOpen = "wmo open windows";
    constexpr const char* kShut = "wmo shut windows";
    constexpr const char* kFov = "camera fov";

    inline std::string Ordinals(const std::vector<uint32_t>& list)
    {
        if (list.empty()) return "none";
        std::string s;
        for (size_t i = 0; i < list.size(); ++i) s += (i ? ", " : "") + std::to_string(list[i]);
        return s;
    }

    /// The view's lines (its camera fov is the camera section's: FovLine).
    inline std::string FormatView(const WmoView& v)
    {
        char line[600];
        std::string out;
        std::snprintf(line, sizeof(line), "%s: %s\n", kWmo, v.wmo.c_str());
        out += line;
        std::snprintf(line, sizeof(line), "%s: %d (walked from %s)\n", kGroup, v.group, v.inside ? "inside" : "outside");
        out += line;
        std::snprintf(line, sizeof(line), "%s: yaw %.3f degrees, origin %.3f %.3f %.3f, tilt %.2f degrees\n", kPlacement, v.yaw, v.origin[0],
                      v.origin[1], v.origin[2], v.tilt);
        out += line;
        std::snprintf(line, sizeof(line), "%s: %.3f %.3f %.3f\n", kCamera, v.cam[0], v.cam[1], v.cam[2]);
        out += line;
        std::snprintf(line, sizeof(line), "%s: %.4f %.4f %.4f\n", kForward, v.forward[0], v.forward[1], v.forward[2]);
        out += line;
        std::snprintf(line, sizeof(line), "%s: %.4f %.4f %.4f\n", kUp, v.up[0], v.up[1], v.up[2]);
        out += line;
        out += std::string(kOpen) + ": " + Ordinals(v.open) + "\n";
        out += std::string(kShut) + ": " + Ordinals(v.shut) + "\n";
        return out;
    }

    inline std::string FovLine(float fov)
    {
        char line[96];
        std::snprintf(line, sizeof(line), "%s: %.2f degrees (%.4f rad)\n", kFov, fov * 180.0f / 3.14159265f, fov);
        return line;
    }

    namespace detail
    {
        // The value after "key: " when the line is that key's (exactly: "wmo camera" isn't "wmo camera forward").
        inline const char* ValueOf(const std::string& line, const char* key)
        {
            const size_t n = std::strlen(key);
            if (line.size() < n + 1 || line.compare(0, n, key) != 0 || line[n] != ':') return nullptr;
            const char* v = line.c_str() + n + 1;
            while (*v == ' ') ++v;
            return v;
        }
        inline void ReadOrdinals(const char* v, std::vector<uint32_t>& out)
        {
            out.clear();
            while (*v)
            {
                char* end = nullptr;
                const unsigned long x = std::strtoul(v, &end, 10);
                if (end == v) { ++v; continue; }
                out.push_back(static_cast<uint32_t>(x));
                v = end;
            }
        }
    }

    /// The first view in a snapshot's text (its "wmo:" line and those after it, up to the next "wmo:"), and the camera's
    /// fov. True when it names a WMO and places the camera in it.
    inline bool ParseView(const std::string& text, WmoView& v)
    {
        v = WmoView{};
        bool haveCam = false, haveForward = false, inView = false;
        size_t at = 0;
        while (at < text.size())
        {
            size_t end = text.find('\n', at);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(at, end - at);
            at = end + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            const char* x = nullptr;
            if ((x = detail::ValueOf(line, kFov))) { v.fov = static_cast<float>(std::atof(x)) * 3.14159265f / 180.0f; continue; }
            if ((x = detail::ValueOf(line, kWmo)))
            {
                if (inView) { /* the next building's */ inView = false; continue; }
                if (!v.wmo.empty()) continue;
                v.wmo = x;
                inView = !v.wmo.empty() && v.wmo.compare(0, 4, "none") != 0;
                if (!inView) v.wmo.clear();
                continue;
            }
            if (!inView) continue;
            if ((x = detail::ValueOf(line, kGroup))) { v.group = std::atoi(x); v.inside = std::strstr(x, "from inside") != nullptr; }
            else if ((x = detail::ValueOf(line, kPlacement)))
                std::sscanf(x, "yaw %f degrees, origin %f %f %f, tilt %f", &v.yaw, &v.origin[0], &v.origin[1], &v.origin[2], &v.tilt);
            else if ((x = detail::ValueOf(line, kCamera))) haveCam = std::sscanf(x, "%f %f %f", &v.cam[0], &v.cam[1], &v.cam[2]) == 3;
            else if ((x = detail::ValueOf(line, kForward))) haveForward = std::sscanf(x, "%f %f %f", &v.forward[0], &v.forward[1], &v.forward[2]) == 3;
            else if ((x = detail::ValueOf(line, kUp))) std::sscanf(x, "%f %f %f", &v.up[0], &v.up[1], &v.up[2]);
            else if ((x = detail::ValueOf(line, kOpen))) detail::ReadOrdinals(x, v.open);
            else if ((x = detail::ValueOf(line, kShut))) detail::ReadOrdinals(x, v.shut);
        }
        return !v.wmo.empty() && haveCam && haveForward;
    }
}
