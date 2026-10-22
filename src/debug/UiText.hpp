// Panel text (2026-10-22): a line wraps at the panel's right edge instead of running off it, where the running core has
// UiTextWrapped. WarcraftXL core v1.1.272 appended it to WXL_Api (with UiSetPanelOpen, UiIsPanelOpen, UiPanelCount,
// UiPanelTitle and UiTextColored; WXL_API_VERSION unchanged), and the core's own header LA builds against declares
// them. An older core hands a shorter table, so the member is read only when its structSize reaches past it; there the
// line is drawn as before (UiText). A short line looks the same either way.
#pragma once

#include "wxl/PluginApi.h"

#include <cstddef>

namespace wxl_livingazeroth::ui
{
    /// Whether the running core's table carries UiTextWrapped: its structSize reaches past the member, and it's set.
    inline bool HasTextWrapped(const WXL_Api* api)
    {
        return api && api->structSize >= offsetof(WXL_Api, UiTextWrapped) + sizeof(api->UiTextWrapped) && api->UiTextWrapped;
    }

    /// One line of panel text: wrapped at the panel's edge where the core can, else as UiText draws it.
    inline void Text(const WXL_Api* api, const char* text)
    {
        if (HasTextWrapped(api)) api->UiTextWrapped(text);
        else api->UiText(text);
    }
}
