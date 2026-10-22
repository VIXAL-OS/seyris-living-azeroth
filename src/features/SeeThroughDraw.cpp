#include "SeeThroughDraw.hpp"

#include "GlassShader.hpp"
#include "../render/ShaderPatch.hpp"

#include <algorithm>
#include <cstring>
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

        void Stencil(IDirect3DDevice9* dev, D3DCMPFUNC func, D3DSTENCILOP pass, D3DSTENCILOP zfail)
        {
            dev->SetRenderState(D3DRS_STENCILENABLE, TRUE);
            dev->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
            dev->SetRenderState(D3DRS_STENCILFUNC, func);
            dev->SetRenderState(D3DRS_STENCILREF, kTintBit);
            dev->SetRenderState(D3DRS_STENCILMASK, kTintBit);
            dev->SetRenderState(D3DRS_STENCILWRITEMASK, kTintBit);
            dev->SetRenderState(D3DRS_STENCILPASS, pass);
            dev->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
            dev->SetRenderState(D3DRS_STENCILZFAIL, zfail);
        }

        // A draw of the range with the tint pass's shader (its constant put back after): `colour` the tint itself (ZERO /
        // SRCCOLOR: what's there x the shader's colour), else no colour written (the stencil's clearing). The batch's shader
        // and blend put back for the draws after.
        bool TintDraw(IDirect3DDevice9* dev, const DrawArgs& a, UINT start, UINT primitives, const PerPixel& in, bool colour)
        {
            IDirect3DPixelShader9* stock = nullptr;
            dev->GetPixelShader(&stock);
            float keep[4] = {};
            dev->GetPixelShaderConstantF(kTintConstant, keep, 1);
            DWORD write = 0;
            dev->GetRenderState(D3DRS_COLORWRITEENABLE, &write);
            const float c[4] = { in.tint, 1.0f, 0.0f, 0.0f };
            dev->SetPixelShaderConstantF(kTintConstant, c, 1);
            dev->SetPixelShader(in.tintShader);
            dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
            if (!colour) dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            const bool ok = DrawRange(dev, a, start, primitives);
            dev->SetRenderState(D3DRS_COLORWRITEENABLE, write);
            dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            dev->SetPixelShader(stock);
            if (stock) stock->Release();
            dev->SetPixelShaderConstantF(kTintConstant, keep, 1);
            return ok;
        }
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
        const bool tint = in.tintShader && in.tint > 0.0f && in.lead && !in.debug;
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
            // stained glass: what's behind the open windows' glass x its colour
            if (tint && TintDraw(dev, e.args, 0, openTris, in, true)) out.tinted = 1;
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
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
        setDraw(-1.0f);
        if (tint && in.stencil)
        {
            // stained glass: the stencil's bit cleared under the batch, set where the see-through draw will blend (its
            // texkill keeps the open windows' glass), then what's behind there x the glass's colour, the bit cleared again
            Stencil(dev, D3DCMP_ALWAYS, D3DSTENCILOP_ZERO, D3DSTENCILOP_ZERO);
            bool marked = TintDraw(dev, e.args, e.args.startIndex, e.args.primitives, in, false);
            DWORD write = 0;
            dev->GetRenderState(D3DRS_COLORWRITEENABLE, &write);
            dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
            Stencil(dev, D3DCMP_ALWAYS, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
            marked = DrawRange(dev, e.args, e.args.startIndex, e.args.primitives) && marked;
            dev->SetRenderState(D3DRS_COLORWRITEENABLE, write);
            Stencil(dev, D3DCMP_EQUAL, D3DSTENCILOP_ZERO, D3DSTENCILOP_ZERO);
            if (marked && TintDraw(dev, e.args, e.args.startIndex, e.args.primitives, in, true)) out.tinted = 2;
            dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
        }
        ok = DrawRange(dev, e.args, e.args.startIndex, e.args.primitives) && ok;
        out.drawn = ok;
        return out;
    }
}
