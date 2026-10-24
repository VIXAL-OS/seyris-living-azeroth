#include "TreeShadowModel.hpp"

#include <cmath>
#include <cstring>

namespace wxl_livingazeroth::treeshadow
{
    namespace
    {
        template <class T> void Put(uint8_t* data, size_t offset, T value) { std::memcpy(data + offset, &value, sizeof(T)); }

        // wxl-treewind's own keywords for trees and bushes (its DLL's strings), all lower case.
        const char* const kFoliageWords[] = {
            "tree",     "pine",   "canopy",  "conifer",    "spruce", "cedar",   "redwood", "sapling",     "aspen",
            "birch",    "oak",    "willow",  "palm",       "cypress", "juniper", "larch",  "mangrove",    "maple",
            "olive",    "baobab", "eucalyptus", "bush",    "shrub",  "fern",    "hedge",   "bracken",     "bramble",
            "thicket",  "topiary", "undergrowth",
        };
        // Words that hold one of those without being one: masked first.
        const char* const kNotFoliage[] = {"street", "infernal", "spine", "cloak"};

        uint32_t Hash(const char* s)
        {
            uint32_t h = 2166136261u;
            for (; *s; ++s)
            {
                const char ch = (*s >= 'A' && *s <= 'Z') ? static_cast<char>(*s - 'A' + 'a') : *s;
                h = (h ^ static_cast<uint8_t>(ch)) * 16777619u;
            }
            return h;
        }

        void Account(BandStats& w, Outcome o, double ms, const Casters& cs, bool queried)
        {
            ++w.outcomes[static_cast<int>(o)];
            if (o == Outcome::Drawn) ++w.drawn;
            if (o == Outcome::NoFoliage) ++w.looked;
            if (queried)
            {
                w.ms += ms;
                if (ms > w.maxMs) w.maxMs = ms;
                w.last = cs;
            }
        }
    }

    void SetUpBand(uint8_t* data, int band, const float center[3], float radius, uint32_t textureIndex)
    {
        const size_t i4 = static_cast<size_t>(band) * 4, i12 = static_cast<size_t>(band) * 12, i16 = static_cast<size_t>(band) * 16;
        Put<int32_t>(data, kQdFlags + i4, kQueryStatic);
        Put<int32_t>(data, kQdActive + i4, 1);
        Put<int32_t>(data, kQdUseCull + i4, 0);
        Put<float>(data, kQdPos + i12 + 0, center[0]);
        Put<float>(data, kQdPos + i12 + 4, center[1]);
        Put<float>(data, kQdPos + i12 + 8, center[2]);
        Put<uint8_t>(data, kQdTexIndex + static_cast<size_t>(band), static_cast<uint8_t>(textureIndex ? 1 : 0));
        Put<float>(data, kQdRadius + i4, radius);
        const float bounds[4] = {-radius, radius, -radius, radius};
        const float texBounds[4] = {0.0f, 1.0f, 0.0f, 1.0f};
        std::memcpy(data + kQdBounds + i16, bounds, sizeof(bounds));
        std::memcpy(data + kQdTexBounds + i16, texBounds, sizeof(texBounds));
    }

    void Deactivate(uint8_t* data, int band)
    {
        Put<int32_t>(data, kQdFlags + static_cast<size_t>(band) * 4, 0);
        Put<int32_t>(data, kQdActive + static_cast<size_t>(band) * 4, 0);
        Put<int32_t>(data, kQdUseCull + static_cast<size_t>(band) * 4, 0);
    }

    void Finish(uint8_t* data, int lastBand)
    {
        Put<int32_t>(data, kQdLastBand, lastBand);
        Put<int32_t>(data, kQdUseFrustum, 0);
    }

    bool Due(double& next, double nowMs, float rate)
    {
        if (!(rate > 0.0f)) return true;
        const double period = 1000.0 / rate;
        if (nowMs < next) return false;
        // Keeps the average rate when frames come unevenly, without a burst after a pause (or on the first frame).
        next = nowMs - next > period ? nowMs + period : next + period;
        return true;
    }

    bool LooksLikeFoliage(const char* path)
    {
        if (!path) return false;
        char low[272];
        size_t n = 0;
        for (; path[n] && n + 1 < sizeof(low); ++n)
        {
            const char ch = path[n];
            low[n] = (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
        }
        low[n] = 0;
        for (const char* word : kNotFoliage)
        {
            const size_t len = std::strlen(word);
            for (char* p = std::strstr(low, word); p; p = std::strstr(p + len, word)) std::memset(p, '_', len);
        }
        for (const char* word : kFoliageWords)
            if (std::strstr(low, word)) return true;
        return false;
    }

    bool FoliageCache::Is(const char* path)
    {
        const uint32_t h = Hash(path);
        const auto it = m_known.find(h);
        if (it != m_known.end()) return it->second;
        if (m_known.size() > 8192) m_known.clear();
        const bool foliage = LooksLikeFoliage(path);
        m_known.emplace(h, foliage);
        return foliage;
    }

    const char* OutcomeText(Outcome o)
    {
        switch (o)
        {
        case Outcome::Drawn: return "drawn again";
        case Outcome::Off: return "off";
        case Outcome::Quality: return "no cached shadows at this shadow quality";
        case Outcome::ClientDoesIt: return "the client redraws its shadows every frame itself (shadow quality 5)";
        case Outcome::NoTreeWind: return "wxl-treewind isn't loaded: nothing sways";
        case Outcome::Foreign: return "another module owns the shadow cache's callbacks: left alone";
        case Outcome::NoMaps: return "the client hasn't made its shadow maps";
        case Outcome::Away: return "you're outside the map it shows (the client is drawing it round you)";
        case Outcome::NotDue: return "drawn recently enough";
        case Outcome::Quiet: return "no tree or bush casts into it (looking again shortly)";
        case Outcome::NoFoliage: return "looked: no tree or bush casts into it";
        case Outcome::Fault: return "a client call faulted: off until the game restarts";
        default: return "?";
        }
    }

    void RunFrame(const Client& c, uint8_t* data, const Settings& s, State& st)
    {
        ++st.frames;
        const int q = c.quality();
        st.quality = q;
        const bool want[2] = {s.enabled != 0, s.enabled != 0 && s.farTrees != 0};

        // What holds for both bands.
        Outcome common = Outcome::Drawn;
        float viewer[3] = {};
        if (st.faulted) common = Outcome::Fault;
        else if (q < 0) common = Outcome::NoMaps;
        else if (q < 3) common = Outcome::Quality;
        else if (q >= 5) common = Outcome::ClientDoesIt;
        else if (!c.treeWind()) common = Outcome::NoTreeWind;
        else if (!c.stockCallbacks()) common = Outcome::Foreign;
        else if (!c.viewer(viewer)) common = Outcome::NoMaps;

        for (int b = 0; b < 2; ++b)
        {
            Outcome o = common;
            double ms = 0.0;
            bool queried = false;
            Casters cs;
            if (!want[b]) o = Outcome::Off;
            else if (o == Outcome::Drawn)
            {
                BandView v;
                if (!c.band(b, v) || !v.textures || !(v.radius > 0.0f)) o = Outcome::NoMaps;
                else
                {
                    const float dx = viewer[0] - v.lastPos[0], dy = viewer[1] - v.lastPos[1];
                    const double now = c.nowMs();
                    if (!(dx * dx + dy * dy <= v.radius * v.radius)) o = Outcome::Away;
                    else if (now < st.quietUntil[b]) o = Outcome::Quiet;
                    else if (!Due(st.next[b], now, b ? s.farRate : s.rate)) o = Outcome::NotDue;
                    else
                    {
                        // The band's shown texture, whole, centred where the client drew it.
                        SetUpBand(data, b, v.lastPos, v.radius, v.current);
                        for (int i = 0; i < b; ++i) Deactivate(data, i);
                        Finish(data, b);
                        queried = true;
                        if (!c.frustum(data, b) || !c.query(data) || !c.casters(b, cs)) o = Outcome::Fault;
                        else if (!cs.foliage)
                        {
                            o = Outcome::NoFoliage;
                            st.quietUntil[b] = c.nowMs() + kQuietMs;
                        }
                        else o = c.render(data) ? Outcome::Drawn : Outcome::Fault;
                        ms = c.nowMs() - now;
                        if (o == Outcome::Fault) st.faulted = true;
                    }
                }
            }
            st.outcome[b] = o;
            Account(st.window[b], o, ms, cs, queried);
            Account(st.sinceLog[b], o, ms, cs, queried);
        }
    }

    void Tick(State& st, double nowMs)
    {
        if (st.windowStart <= 0.0 || nowMs < st.windowStart) { st.windowStart = nowMs; return; }
        if (nowMs - st.windowStart < 1000.0) return;
        st.windowMs = nowMs - st.windowStart;
        for (int b = 0; b < 2; ++b)
        {
            st.second[b] = st.window[b];
            st.window[b] = BandStats{};
        }
        st.windowStart = nowMs;
        st.secondFrames = st.frames;
        st.frames = 0;
    }
}
