#include "SeeThroughDraw.hpp"

#include "GlassShader.hpp"
#include "../render/ShaderPatch.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace wxl_livingazeroth::seethrough
{
    namespace
    {
        bool DrawRange(IDirect3DDevice9* dev, const DrawArgs& a, UINT startIndex, UINT primitives)
        {
            return SUCCEEDED(dev->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(a.primitive), static_cast<INT>(a.baseVertex), a.minIndex,
                                                       a.vertices, startIndex, primitives));
        }

        // The entry's open triangles, then the rest, into the buffer; false when it can't be made or filled.
        bool Fill(IDirect3DDevice9* dev, const Deferral::Entry& e, SplitBuffer& buf)
        {
            const UINT n = static_cast<UINT>(e.open.size() + e.rest.size());
            if (!n) return false;
            if (buf.capacity < n)
            {
                buf.Release();
                const UINT capacity = n < 1024 ? 1024 : n + n / 2;
                if (FAILED(dev->CreateIndexBuffer(capacity * 2, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &buf.ib, nullptr)) || !buf.ib)
                { buf.ib = nullptr; return false; }
                buf.capacity = capacity;
            }
            void* p = nullptr;
            if (FAILED(buf.ib->Lock(0, n * 2, &p, 0)) || !p) return false;
            if (!e.open.empty()) std::memcpy(p, e.open.data(), e.open.size() * 2);
            if (!e.rest.empty()) std::memcpy(static_cast<uint8_t*>(p) + e.open.size() * 2, e.rest.data(), e.rest.size() * 2);
            buf.ib->Unlock();
            return true;
        }

        // The tint pass over the range (after the opaque draw): its shader, ZERO / SRCCOLOR (what's there x the shader's
        // colour), depth tested LESS (only where the glass is in front of what's behind it: not where the opaque draw wrote
        // the glass's own depth), no depth written; the batch's shader, its constant, blend and depth test put back.
        bool TintDraw(IDirect3DDevice9* dev, const DrawArgs& a, UINT start, UINT primitives, const PerPixel& in)
        {
            IDirect3DPixelShader9* stock = nullptr;
            dev->GetPixelShader(&stock);
            float keep[4] = {};
            dev->GetPixelShaderConstantF(kTintConstant, keep, 1);
            DWORD func = D3DCMP_LESSEQUAL;
            dev->GetRenderState(D3DRS_ZFUNC, &func);
            const float c[4] = { in.tint, 1.0f, 0.0f, 0.0f };
            dev->SetPixelShaderConstantF(kTintConstant, c, 1);
            dev->SetPixelShader(in.tintShader);
            dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
            const bool ok = DrawRange(dev, a, start, primitives);
            dev->SetRenderState(D3DRS_ZFUNC, func);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            dev->SetPixelShader(stock);
            if (stock) stock->Release();
            dev->SetPixelShaderConstantF(kTintConstant, keep, 1);
            return ok;
        }
    }

    // The blurred copy behind a frosted batch's open windows (after the opaque draw): the composite's shader, the copy at
    // kFrostSampler, no blend, depth tested LESS (only where the glass is in front of what's behind it), no depth written;
    // the batch's shader, its constant, sampler, blend and depth test put back.
    bool FrostDraw(IDirect3DDevice9* dev, const DrawArgs& a, UINT start, UINT primitives, const FrostBuffer& f)
    {
        IDirect3DPixelShader9* stock = nullptr;
        dev->GetPixelShader(&stock);
        float keep[4] = {};
        dev->GetPixelShaderConstantF(kTintConstant, keep, 1);
        DWORD func = D3DCMP_LESSEQUAL, blend = TRUE;
        dev->GetRenderState(D3DRS_ZFUNC, &func);
        dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
        IDirect3DBaseTexture9* tex = nullptr;
        dev->GetTexture(kFrostSampler, &tex);
        const D3DSAMPLERSTATETYPE kinds[] = { D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE };
        DWORD sampler[6] = {};
        for (int k = 0; k < 6; ++k) dev->GetSamplerState(kFrostSampler, kinds[k], &sampler[k]);
        const float c[4] = { 1.0f / static_cast<float>(f.width), 1.0f / static_cast<float>(f.height), 0.0f, 0.0f };
        dev->SetPixelShaderConstantF(kTintConstant, c, 1);
        dev->SetPixelShader(f.composite);
        dev->SetTexture(kFrostSampler, f.blurred);
        const DWORD set[6] = { D3DTADDRESS_CLAMP, D3DTADDRESS_CLAMP, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_NONE, FALSE };
        for (int k = 0; k < 6; ++k) dev->SetSamplerState(kFrostSampler, kinds[k], set[k]);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        const bool ok = DrawRange(dev, a, start, primitives);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, blend);
        dev->SetRenderState(D3DRS_ZFUNC, func);
        for (int k = 0; k < 6; ++k) dev->SetSamplerState(kFrostSampler, kinds[k], sampler[k]);
        dev->SetTexture(kFrostSampler, tex);
        if (tex) tex->Release();
        dev->SetPixelShader(stock);
        if (stock) stock->Release();
        dev->SetPixelShaderConstantF(kTintConstant, keep, 1);
        return ok;
    }

    void FrostBuffer::Release()
    {
        for (IUnknown** u : { reinterpret_cast<IUnknown**>(&blurred), reinterpret_cast<IUnknown**>(&ping), reinterpret_cast<IUnknown**>(&resolved),
                              reinterpret_cast<IUnknown**>(&blur), reinterpret_cast<IUnknown**>(&composite) })
            if (*u) { (*u)->Release(); *u = nullptr; }
        width = height = 0;
        format = D3DFMT_UNKNOWN;
        ready = false;
    }

    const char* FrostBlurText()
    {
        // r5 c223 (the step), r1 the sum: the centre and four taps either side along the step, kFrostWeights
        return "ps_3_0\n"
               "    def c216, 0.20416, 0.18017, 0.12383, 0.06629\n"
               "    def c217, 0.02762, 0, 0, 1\n"
               "    def c218, 1, 2, 3, 4\n"
               "    dcl_texcoord v0.xy\n"
               "    dcl_2d s0\n"
               "    mov r5, c223\n"
               "    texld r0, v0, s0\n"
               "    mul r1, r0, c216.x\n"
               "    mad r2.xy, r5, c218.x, v0\n    texld r0, r2, s0\n    mad r1, r0, c216.y, r1\n"
               "    mad r2.xy, r5, -c218.x, v0\n    texld r0, r2, s0\n    mad r1, r0, c216.y, r1\n"
               "    mad r2.xy, r5, c218.y, v0\n    texld r0, r2, s0\n    mad r1, r0, c216.z, r1\n"
               "    mad r2.xy, r5, -c218.y, v0\n    texld r0, r2, s0\n    mad r1, r0, c216.z, r1\n"
               "    mad r2.xy, r5, c218.z, v0\n    texld r0, r2, s0\n    mad r1, r0, c216.w, r1\n"
               "    mad r2.xy, r5, -c218.z, v0\n    texld r0, r2, s0\n    mad r1, r0, c216.w, r1\n"
               "    mad r2.xy, r5, c218.w, v0\n    texld r0, r2, s0\n    mad r1, r0, c217.x, r1\n"
               "    mad r2.xy, r5, -c218.w, v0\n    texld r0, r2, s0\n    mad r1, r0, c217.x, r1\n"
               "    mov r1.w, c217.w\n"
               "    mov oC0, r1\n";
    }

    const char* FrostCompositeText()
    {
        // the copy at the pixel's place: (vPos + 0.5) x c223.xy (1 / the target's size)
        return "ps_3_0\n"
               "    def c220, 0.5, 1, 0, 0\n"
               "    dcl vPos.xy\n"
               "    dcl_2d s14\n"
               "    mov r0, c223\n"
               "    add r1.xy, vPos, c220.x\n"
               "    mul r1.xy, r1, r0\n"
               "    texld r2, r1, s14\n"
               "    mov r2.w, c220.y\n"
               "    mov oC0, r2\n";
    }

    bool FrostCapture(IDirect3DDevice9* dev, FrostBuffer& f, float softness)
    {
        f.ready = false;
        if (f.failed) return false;
        auto fail = [&](const char* why, bool ever) { std::snprintf(f.why, sizeof(f.why), "%s", why); f.failed = ever; return false; };
        IDirect3DSurface9* target = nullptr;
        IDirect3DSurface9* depth = nullptr;
        if (FAILED(dev->GetRenderTarget(0, &target)) || !target) return fail("no render target", false);
        dev->GetDepthStencilSurface(&depth);
        D3DSURFACE_DESC d{};
        target->GetDesc(&d);
        bool ok = true;
        // the textures (again when the target's size or format changes) and the shaders
        if (d.Width != f.width || d.Height != f.height || d.Format != f.format || !f.blurred || !f.ping)
        {
            IDirect3DPixelShader9* blur = f.blur;
            IDirect3DPixelShader9* composite = f.composite;
            f.blur = f.composite = nullptr;
            f.Release();
            f.blur = blur;
            f.composite = composite;
            const UINT w = std::max(1u, d.Width / kFrostScale), h = std::max(1u, d.Height / kFrostScale);
            if (FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &f.blurred, nullptr)) || !f.blurred ||
                FAILED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, d.Format, D3DPOOL_DEFAULT, &f.ping, nullptr)) || !f.ping)
            { f.Release(); f.blur = blur; f.composite = composite; ok = fail("its copy's textures can't be made in the target's format", true); }
            else if (d.MultiSampleType != D3DMULTISAMPLE_NONE &&
                     (FAILED(dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &f.resolved, nullptr)) || !f.resolved))
            { f.Release(); f.blur = blur; f.composite = composite; ok = fail("the multisampled target can't be resolved", true); }
            else { f.width = d.Width; f.height = d.Height; f.format = d.Format; }
        }
        std::string why;
        std::vector<uint8_t> code;
        if (ok && !f.blur)
        {
            if (!shaderpatch::Assemble(FrostBlurText(), "see-through frost blur", code, why) ||
                FAILED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &f.blur)) || !f.blur)
            { f.blur = nullptr; ok = fail("its blur shader can't be made", true); }
        }
        if (ok && !f.composite)
        {
            if (!shaderpatch::Assemble(FrostCompositeText(), "see-through frost composite", code, why) ||
                FAILED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &f.composite)) || !f.composite)
            { f.composite = nullptr; ok = fail("its composite shader can't be made", true); }
        }
        IDirect3DSurface9 *quarter = nullptr, *ping = nullptr;
        if (ok && (FAILED(f.blurred->GetSurfaceLevel(0, &quarter)) || FAILED(f.ping->GetSurfaceLevel(0, &ping)))) ok = fail("its copy's surfaces", false);
        // the copy: resolved first if multisampled, then a quarter
        if (ok && f.resolved && FAILED(dev->StretchRect(target, nullptr, f.resolved, nullptr, D3DTEXF_NONE))) ok = fail("the resolve failed", false);
        if (ok && FAILED(dev->StretchRect(f.resolved ? f.resolved : target, nullptr, quarter, nullptr, D3DTEXF_LINEAR))) ok = fail("the copy failed", false);
        // the blur: across into ping, then down back into the copy (the device's state put back after: a state block, the
        // render target and depth surface apart)
        IDirect3DStateBlock9* kept = nullptr;
        if (ok && (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &kept)) || !kept)) { kept = nullptr; ok = fail("no state block", false); }
        if (ok)
        {
            const UINT w = std::max(1u, f.width / kFrostScale), h = std::max(1u, f.height / kFrostScale);
            const float step = 0.5f + 3.0f * std::clamp(softness, 0.0f, 1.0f);
            struct Q { float x, y, z, rhw, u, v; };
            const float ex = static_cast<float>(w) - 0.5f, ey = static_cast<float>(h) - 0.5f;
            const Q quad[4] = { { -0.5f, -0.5f, 0, 1, 0, 0 }, { ex, -0.5f, 0, 1, 1, 0 }, { -0.5f, ey, 0, 1, 0, 1 }, { ex, ey, 0, 1, 1, 1 } };
            dev->SetDepthStencilSurface(nullptr);
            dev->SetVertexShader(nullptr);
            dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
            dev->SetPixelShader(f.blur);
            for (const auto& [rs, v] : { std::pair<D3DRENDERSTATETYPE, DWORD>{ D3DRS_ZENABLE, D3DZB_FALSE }, { D3DRS_ZWRITEENABLE, FALSE },
                                         { D3DRS_ALPHABLENDENABLE, FALSE }, { D3DRS_ALPHATESTENABLE, FALSE }, { D3DRS_CULLMODE, D3DCULL_NONE },
                                         { D3DRS_STENCILENABLE, FALSE }, { D3DRS_SCISSORTESTENABLE, FALSE }, { D3DRS_COLORWRITEENABLE, 0xF },
                                         { D3DRS_SRGBWRITEENABLE, FALSE }, { D3DRS_FOGENABLE, FALSE } })
                dev->SetRenderState(rs, v);
            for (const auto& [ss, v] : { std::pair<D3DSAMPLERSTATETYPE, DWORD>{ D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP }, { D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP },
                                         { D3DSAMP_MAGFILTER, D3DTEXF_LINEAR }, { D3DSAMP_MINFILTER, D3DTEXF_LINEAR }, { D3DSAMP_MIPFILTER, D3DTEXF_NONE },
                                         { D3DSAMP_SRGBTEXTURE, FALSE } })
                dev->SetSamplerState(0, ss, v);
            const float across[4] = { step / static_cast<float>(w), 0.0f, 0.0f, 0.0f }, down[4] = { 0.0f, step / static_cast<float>(h), 0.0f, 0.0f };
            D3DVIEWPORT9 vp{ 0, 0, w, h, 0.0f, 1.0f };
            dev->SetRenderTarget(0, ping);
            dev->SetViewport(&vp);
            dev->SetTexture(0, f.blurred);
            dev->SetPixelShaderConstantF(kTintConstant, across, 1);
            ok = SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Q)));
            dev->SetRenderTarget(0, quarter);
            dev->SetViewport(&vp);
            dev->SetTexture(0, f.ping);
            dev->SetPixelShaderConstantF(kTintConstant, down, 1);
            ok = SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Q))) && ok;
            dev->SetTexture(0, nullptr);
            if (!ok) fail("the blur's draws failed", false);
        }
        if (kept) { kept->Apply(); kept->Release(); }
        dev->SetRenderTarget(0, target);
        dev->SetDepthStencilSurface(depth);
        if (quarter) quarter->Release();
        if (ping) ping->Release();
        if (depth) depth->Release();
        target->Release();
        f.ready = ok;
        if (ok) f.why[0] = 0;
        return ok;
    }

    void TintOf(const float rgb[3], float strength, float mask, float out[3])
    {
        const float peak = std::max(std::max(std::max(rgb[0], rgb[1]), rgb[2]), 0.001f);
        for (int k = 0; k < 3; ++k)
        {
            const float hue = std::clamp(rgb[k] / peak, 0.0f, 1.0f);
            const float t = strength * hue + (1.0f - strength);
            out[k] = mask * t + (1.0f - mask);
        }
    }

    const char* TintShaderText()
    {
        // r0 the texel, r1 the lead mask (glass 1), r5 c223 (an instruction reads one constant register), r2 the hue at
        // full value, r3 T, r4 lerp(1, T, the mask x c223.y)
        return "ps_3_0\n"
               "    def c220, 1, 0, 0.001, 0\n"
               "    dcl_texcoord v0.xy\n"
               "    dcl_2d s0\n"
               "    dcl_2d s15\n"
               "    texld r0, v0, s0\n"
               "    texld r1, v0, s15\n"
               "    mov r5, c223\n"
               "    max r2.x, r0.x, r0.y\n"
               "    max r2.x, r2.x, r0.z\n"
               "    max r2.x, r2.x, c220.z\n"
               "    rcp r2.x, r2.x\n"
               "    mul_sat r2.xyz, r0, r2.x\n"
               "    lrp r3.xyz, r5.x, r2, c220.x\n"
               "    mul r1.x, r1.x, r5.y\n"
               "    lrp r4.xyz, r1.x, r3, c220.x\n"
               "    mov r4.w, c220.x\n"
               "    mov oC0, r4\n";
    }

    IDirect3DPixelShader9* CreateTintShader(IDirect3DDevice9* dev, std::string& why)
    {
        std::vector<uint8_t> code;
        if (!dev) { why = "no device"; return nullptr; }
        if (!shaderpatch::Assemble(TintShaderText(), "see-through tint", code, why)) return nullptr;
        IDirect3DPixelShader9* ps = nullptr;
        if (FAILED(dev->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()), &ps)) || !ps) { why = "CreatePixelShader failed"; return nullptr; }
        return ps;
    }

    bool HasStencil(D3DFORMAT f)
    {
        return f == D3DFMT_D24S8 || f == D3DFMT_D24X4S4 || f == D3DFMT_D15S1 || f == D3DFMT_D24FS8;
    }

    const char* DepthFormatName(D3DFORMAT f)
    {
        switch (f)
        {
        case D3DFMT_D24S8: return "D24S8";
        case D3DFMT_D24X8: return "D24X8";
        case D3DFMT_D24X4S4: return "D24X4S4";
        case D3DFMT_D24FS8: return "D24FS8";
        case D3DFMT_D16: return "D16";
        case D3DFMT_D15S1: return "D15S1";
        case D3DFMT_D32: return "D32";
        case D3DFMT_D32F_LOCKABLE: return "D32F";
        case D3DFMT_UNKNOWN: return "none";
        default: return "another";
        }
    }

    bool CaptureState(IDirect3DDevice9* dev, Deferral::Entry& e)
    {
        IDirect3DStateBlock9* block = nullptr;
        if (FAILED(dev->CreateStateBlock(D3DSBT_ALL, &block)) || !block) return false;
        e.state = block;
        e.ps.assign(4 * kPixelConstants, 0.0f);
        if (FAILED(dev->GetPixelShaderConstantF(0, e.ps.data(), kPixelConstants))) e.ps.clear();
        e.vs.assign(4 * kVertexConstants, 0.0f);
        if (FAILED(dev->GetVertexShaderConstantF(0, e.vs.data(), kVertexConstants))) e.vs.clear();
        return true;
    }

    void ApplyCaptured(IDirect3DDevice9* dev, const Deferral::Entry& e, bool constants)
    {
        if (e.state) static_cast<IDirect3DStateBlock9*>(e.state)->Apply();
        if (constants) ApplyConstants(dev, e);
    }

    void ApplyConstants(IDirect3DDevice9* dev, const Deferral::Entry& e)
    {
        if (e.ps.size() == 4 * kPixelConstants) dev->SetPixelShaderConstantF(0, e.ps.data(), kPixelConstants);
        if (e.vs.size() == 4 * kVertexConstants) dev->SetVertexShaderConstantF(0, e.vs.data(), kVertexConstants);
    }

    void SplitBuffer::Release()
    {
        if (ib) ib->Release();
        ib = nullptr;
        capacity = 0;
    }

    PerPixelDrawn DrawPerPixel(IDirect3DDevice9* dev, const Deferral::Entry& e, const PerPixel& in, SplitBuffer& buf)
    {
        PerPixelDrawn out;
        // the batch's own alpha test (the stock alpha's) moves into the shader (its texkill on c219.y)
        DWORD test = FALSE, func = D3DCMP_ALWAYS, ref = 0;
        dev->GetRenderState(D3DRS_ALPHATESTENABLE, &test);
        dev->GetRenderState(D3DRS_ALPHAFUNC, &func);
        dev->GetRenderState(D3DRS_ALPHAREF, &ref);
        float c[4];
        ReplayConstants(in.opacity, test != FALSE, func, ref, e.first, e.bits, c);
        dev->SetPixelShaderConstantF(glass::kReplayConstant, c, 1);
        const BOOL on = TRUE;
        dev->SetPixelShaderConstantB(glass::kReplaySwitch, &on, 1);
        // the split is the shader's (texkill by c28.y), not the device's alpha test: a driver's alpha-to-coverage or a
        // translation layer may make an alpha test something else
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        // its lead mask (s15; c28.w: whether there is one)
        dev->SetTexture(glass::kLeadSampler, in.lead);
        if (in.lead)
        {
            const DWORD s = glass::kLeadSampler;
            dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
            dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
            dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(s, D3DSAMP_MIPMAPLODBIAS, 0);
            dev->SetSamplerState(s, D3DSAMP_MAXMIPLEVEL, 0);
            dev->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
        }
        float draw[4] = { in.debug ? 1.0f : 0.0f, 1.0f, kSplitAlpha, in.lead ? 1.0f : 0.0f };
        auto setDraw = [&](float sign) { draw[1] = sign; dev->SetPixelShaderConstantF(glass::kReplayDrawConstant, draw, 1); };
        auto setWindow = [&](float state) { const float w[4] = { state, 0.0f, 0.0f, 0.0f }; dev->SetPixelShaderConstantF(glass::kReplayWindowConstant, w, 1); };
        // the tint (stained glass), with its lead mask only: none in the debug view (its colours are the decisions')
        const bool asked = in.tintShader && in.tint > 0.0f;
        const bool tint = asked && in.lead && !in.debug;
        if (asked && !tint) out.untinted = !in.lead ? "no lead mask (yet)" : "the debug view";
        // frosted glass: the frame's blurred copy behind its open windows (none in the debug view)
        const bool frost = in.frost && in.frost->ready && !in.debug;
        IDirect3DIndexBuffer9* own = nullptr;
        if (in.split && !e.open.empty() && Fill(dev, e, buf) && SUCCEEDED(dev->SetIndices(buf.ib))) own = buf.ib;
        if (own)
        {
            const UINT openTris = static_cast<UINT>(e.open.size() / 3), restTris = static_cast<UINT>(e.rest.size() / 3);
            out.split = true;
            out.open = openTris;
            out.rest = restTris;
            bool ok = true;
            // what's opaque, with its depth: the open windows' lead and frame, every other window as the client draws it
            dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
            setDraw(1.0f);
            setWindow(1.0f);
            ok = DrawRange(dev, e.args, 0, openTris) && ok;
            if (restTris)
            {
                setWindow(0.0f);
                ok = DrawRange(dev, e.args, openTris * 3, restTris) && ok;
            }
            // frosted glass: the blurred copy behind the open windows' glass; stained glass: what's behind it x its colour
            if (frost) out.frosted = FrostDraw(dev, e.args, 0, openTris, *in.frost);
            if (frost && in.frostOnly && out.frosted) { out.drawn = ok; return out; }   // (the copy alone: the panel's diagnostic)
            if (tint) { if (TintDraw(dev, e.args, 0, openTris, in)) out.tinted = 1; else out.untinted = "its draw failed"; }
            // the open windows' glass over the room, no depth
            dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            setDraw(-1.0f);
            setWindow(1.0f);
            ok = DrawRange(dev, e.args, 0, openTris) && ok;
            out.drawn = ok;
            return out;
        }
        // whole: the client's indices, the shader's lookup alone
        setWindow(-1.0f);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
        setDraw(1.0f);
        bool ok = DrawRange(dev, e.args, e.args.startIndex, e.args.primitives);
        // frosted glass: the blurred copy where the batch's glass is in front (the open windows' glass); stained glass: what's
        // behind it there x its colour
        if (frost) out.frosted = FrostDraw(dev, e.args, e.args.startIndex, e.args.primitives, *in.frost);
        if (frost && in.frostOnly && out.frosted) { out.drawn = ok; return out; }   // (the copy alone: the panel's diagnostic)
        if (tint) { if (TintDraw(dev, e.args, e.args.startIndex, e.args.primitives, in)) out.tinted = 2; else out.untinted = "its draw failed"; }
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        setDraw(-1.0f);
        ok = DrawRange(dev, e.args, e.args.startIndex, e.args.primitives) && ok;
        out.drawn = ok;
        return out;
    }
}
