#include "Pages.hpp"

#include "MemoryTag.hpp"

#include "../wxl_seyris/ControlsApi.hpp"

#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace wxl_livingazeroth::pages
{
    namespace
    {
        constexpr const char* kTag = "wxl-seyris-living-azeroth";
        constexpr const char* kPrefix = "wxl-seyris-living-azeroth: ";

        struct Item { const char* title; WXL_PanelFn draw; };
        std::vector<Item> g_panels, g_quick;
        bool g_published = false;

        // Without the control panel's pages: one overlay panel, its sections picked from a list. WarcraftXL's
        // overlay holds 16 panels for all modules together and drops the rest without a word; this module's
        // own ~16 filled it, and a new one pushed fog, lights and the light editor out ("I don't see this
        // option at all", the user, 2026-10-15).
        const WXL_Api* g_api = nullptr;
        std::vector<WXL_PanelFn> g_overlayDraw; // the panels that went to the overlay's section list
        std::vector<const char*> g_names;      // their titles without the module prefix
        const char* g_overlayTitle = kTag;     // the first one's full title
        int g_section = 0;

        const char* ShortTitle(const char* title)
        {
            const size_t prefix = std::strncmp(title, kPrefix, std::strlen(kPrefix)) == 0 ? std::strlen(kPrefix) : 0;
            return title + prefix;
        }

        // Every panel and quick feature is drawn through one of these, so what drawing allocates is charged to the memory
        // readout's panels: one function per index (the control panel's user pointer isn't relied on); past the table's
        // end a panel is registered as it is.
        template <size_t N>
        void __cdecl PanelThunk(void* /*user*/)
        {
            memuse::Scope memory(memuse::kPanels);
            if (N < g_panels.size()) g_panels[N].draw(nullptr);
        }
        template <size_t N>
        void __cdecl QuickThunk(void* /*user*/)
        {
            memuse::Scope memory(memuse::kPanels);
            if (N < g_quick.size()) g_quick[N].draw(nullptr);
        }
        template <size_t... I>
        constexpr std::array<WXL_PanelFn, sizeof...(I)> PanelThunks(std::index_sequence<I...>) { return { &PanelThunk<I>... }; }
        template <size_t... I>
        constexpr std::array<WXL_PanelFn, sizeof...(I)> QuickThunks(std::index_sequence<I...>) { return { &QuickThunk<I>... }; }
        constexpr auto kPanelThunks = PanelThunks(std::make_index_sequence<40>{});
        constexpr auto kQuickThunks = QuickThunks(std::make_index_sequence<16>{});
        WXL_PanelFn PanelDraw(size_t n) { return n < kPanelThunks.size() ? kPanelThunks[n] : g_panels[n].draw; }
        WXL_PanelFn QuickDraw(size_t n) { return n < kQuickThunks.size() ? kQuickThunks[n] : g_quick[n].draw; }
        void __cdecl OnlyPanel(void* /*user*/)
        {
            memuse::Scope memory(memuse::kPanels);
            if (!g_overlayDraw.empty()) g_overlayDraw[0](nullptr);
        }

        void __cdecl Sections(void* /*user*/)
        {
            memuse::Scope memory(memuse::kPanels);
            if (!g_api || g_overlayDraw.empty()) return;
            if (g_api->UiCombo) g_api->UiCombo("section##lapages", &g_section, g_names.data(), static_cast<int>(g_names.size()));
            if (g_section < 0 || g_section >= static_cast<int>(g_overlayDraw.size())) g_section = 0;
            if (g_api->UiSeparator) g_api->UiSeparator();
            g_overlayDraw[g_section](nullptr);
        }
    }

    void AddPanel(const char* title, WXL_PanelFn draw) { g_panels.push_back({ title, draw }); }
    void AddQuick(const char* name, WXL_PanelFn draw) { g_quick.push_back({ name, draw }); }

    void Publish(const WXL_Api* api)
    {
        if (g_published) return;
        g_published = true;
        g_api = api;
        const auto* controls = static_cast<const WXL_SeyrisControlsApi*>(
            api->GetInterface(WXL_SEYRIS_CONTROLS_INTERFACE_NAME, WXL_SEYRIS_CONTROLS_INTERFACE_VERSION));
        const bool pages = controls && controls->HasFeature("pages") && controls->AddPage;
        for (size_t n = 0; n < g_panels.size(); ++n)
        {
            const Item& p = g_panels[n];
            if (pages && controls->AddPage(kGroup, ShortTitle(p.title), PanelDraw(n), nullptr)) continue;
            if (g_overlayDraw.empty()) g_overlayTitle = p.title;
            g_overlayDraw.push_back(p.draw);
            g_names.push_back(ShortTitle(p.title));
        }
        // It opens on the fog, the section most often wanted.
        for (size_t n = 0; n < g_names.size(); ++n)
            if (std::strcmp(g_names[n], "fog") == 0) { g_section = static_cast<int>(n); break; }
        if (g_overlayDraw.size() == 1) api->UiAddPanel(g_overlayTitle, &OnlyPanel, nullptr); // one alone keeps its own
        else if (!g_overlayDraw.empty()) api->UiAddPanel(kTag, &Sections, nullptr);
        if (controls && controls->HasFeature("quick-features") && controls->AddQuickFeature)
            for (size_t n = 0; n < g_quick.size(); ++n) controls->AddQuickFeature(kGroup, g_quick[n].title, QuickDraw(n), nullptr);
        api->Log(WXL_LOG_INFO, kTag, "ui: %zu panel(s) as pages of the control panel, %zu as sections of one overlay panel%s; %zu quick feature(s)%s.",
                 g_panels.size() - g_overlayDraw.size(), g_overlayDraw.size(),
                 pages ? "" : " (wxl-seyris-tools' control panel pages not found)", g_quick.size(), pages ? "" : " not shown");
    }
}
