// A deferred glass batch's state and its per-pixel redraw (features/SeeThrough's capture and Replay; tests/glass's
// "sequence" draws through the same functions, the game's order): the state captured with every pixel shader constant
// (CaptureState: a state block alone loses c128 and up) and applied with them (ApplyCaptured); then the glass's look with
// its redraw switched on (b11, c219, c28, c33: features/GlassShader), its lead mask at s15, in two draws split in the
// shader by the alpha (the opaque with its depth, then the open windows' glass over the room, SRCALPHA, no depth).
//  - Split by window when the batch's triangles are known and the client's index buffer is seen to hold them (Deferral::
//    Entry::open, rest: SplitByWindow): its open windows' triangles drawn with c33.x 1 (open whatever the shader's window
//    lookup finds) and the rest with 0, from an index buffer of our own. Else the whole batch in the client's own indices,
//    c33.x -1 (the lookup's alone).
//  - Stained glass's tint (2026-10-22: "the coloured glass should stay coloured over the room (tint, not just blend); and
//    from inside, the street through the coloured glass"): blended at the opacity, the glass's colour lay over the room as
//    a veil and the room behind kept its own colours. Before the glass is blended over it, what's behind an open window's
//    glass is multiplied by the glass's colour (TintOf: lerp(1, the texel's hue at full value, the strength), by its lead
//    mask: none on the lead and frame), as light through coloured glass is, in a pass of its own: its own small pixel
//    shader (TintShaderText, the client's vertex shader's TEXCOORD0, the batch's texture at s0 and its lead mask at s15;
//    ZERO / SRCCOLOR), not the stock WMO variants, whose largest uses 2032 of its 2048 slots. It's drawn after the opaque
//    draw and before the glass is blended, depth tested LESS: the opaque draw wrote the depth of everything that stays
//    opaque (the open windows' lead and frame, every shut window) and none of the open windows' glass, so with the same
//    triangles and vertex shader the tint passes exactly where the glass is in front of what's behind it. Split: over the
//    open windows' triangles; whole: over the batch's. In game (2026-10-22, Northshire Abbey) the client's index buffer
//    can't be read, so the redraw is whole, and the first build's whole tint went through the stencil, which the client's
//    depth surface may not have: "0 tinted over their open windows, 0 through the stencil". The depth needs neither.
#pragma once

#include "../env/SeeThroughModel.hpp"

#include <d3d9.h>

#include <cstdint>
#include <string>

namespace wxl_livingazeroth::seethrough
{
    /// The split redraw's index buffer (managed, 16-bit, grown as needed).
    struct SplitBuffer
    {
        IDirect3DIndexBuffer9* ib = nullptr;
        UINT capacity = 0;   // indices
        void Release();
    };

    struct PerPixel
    {
        float opacity = 0.35f;              // SeeThroughOpacity
        bool debug = false;                 // the panel's debug view
        IDirect3DTexture9* lead = nullptr;  // its lead mask (null: the light's mask)
        bool split = true;                  // draw split by window when the entry has its triangles
        IDirect3DPixelShader9* tintShader = nullptr;   // stained glass's tint pass (CreateTintShader; null, or no lead mask: none)
        float tint = 0.0f;                  // its strength (SeeThroughStainedTint; 0: none)
    };

    struct PerPixelDrawn
    {
        bool drawn = false;                 // every draw went through
        bool split = false;                 // split by window (else whole, the lookup's alone)
        uint32_t open = 0, rest = 0;        // triangles drawn open and shut (split)
        int tinted = 0;                     // the tint: 0 none, 1 over the open windows' triangles, 2 over the batch's (whole)
        const char* untinted = nullptr;     // why not, when a tint was asked for
    };

    /// The device's state at a batch's draw into `e` (e.state: a D3DSBT_ALL state block) and, apart, every pixel shader
    /// float constant (e.ps; the vertex shader's too, e.vs, up to kVertexConstants, the same way: no harm if they come
    /// back whole): on WARP a state block puts back only c0..c127 of the pixel shader's (tests/glass's "sequence"), and the
    /// glass's view (c180..c183: the camera, the view's rows), look (c192..c221) and the redraw's window lookup need the
    /// rest. "every see-through window pixel is solid YELLOW" (the debug view, in game, 2026-10-21): with c181..c183 not
    /// put back the lookup's point is the camera's for every pixel, in no window; the harness gives the same yellow.
    bool CaptureState(IDirect3DDevice9* dev, Deferral::Entry& e);
    /// Its state applied, then its pixel shader constants put back (`constants` false: the state block alone, as before).
    void ApplyCaptured(IDirect3DDevice9* dev, const Deferral::Entry& e, bool constants = true);
    /// Its shaders' constants put back (ApplyCaptured's second half).
    void ApplyConstants(IDirect3DDevice9* dev, const Deferral::Entry& e);
    constexpr unsigned kPixelConstants = 224;   // ps_3_0's float constants
    constexpr unsigned kVertexConstants = 256;  // vs_3_0's (a hardware device's; a software one's beyond are left)

    /// The redraw of `e` on `dev` with its state applied (its pixel shader has the redraw: HasReplaySwitch). Leaves the
    /// device's state changed (the caller puts it back).
    PerPixelDrawn DrawPerPixel(IDirect3DDevice9* dev, const Deferral::Entry& e, const PerPixel& in, SplitBuffer& buf);

    constexpr float kSplitAlpha = 0.999f;   // the two draws: alpha at least this is the opaque one's

    /// The tint pass's colour for a texel (`rgb` 0..1; its pixel shader's twin): T = lerp(1, the hue at full value
    /// (rgb / its largest channel), `strength`), then lerp(1, T, `mask`): what's behind the glass is multiplied by it.
    void TintOf(const float rgb[3], float strength, float mask, float out[3]);
    /// The tint pass's pixel shader (ps_3_0 text): c223.x the strength, c223.y 1 (0: no tint).
    const char* TintShaderText();
    constexpr int kTintConstant = 223;
    /// The tint pass's shader on `dev` (assembled from TintShaderText), null and why when it can't be made.
    IDirect3DPixelShader9* CreateTintShader(IDirect3DDevice9* dev, std::string& why);
    /// Whether a depth surface's format has a stencil, and its name (the panel's).
    bool HasStencil(D3DFORMAT depthFormat);
    const char* DepthFormatName(D3DFORMAT depthFormat);
}
