#include "render_d3d9.h"

#include <cstring>
#include <vector>

#include "log.h"
#include "render_math.h"

namespace yap::render {
namespace {
struct Vtx {
    float x, y, z;
    D3DCOLOR color;
    float u, v;
};
constexpr DWORD kFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

struct Geometry {
    std::vector<Vtx> verts;
    std::vector<uint16_t> idx16;  // used when every index fits (the common case)
    std::vector<uint32_t> idx32;  // otherwise; needs MaxVertexIndex > 0xFFFF, which all D3D9 cards since ~2004 have
};

D3DMATRIX identity() {
    D3DMATRIX m{};
    m._11 = m._22 = m._33 = m._44 = 1.f;
    return m;
}

bool has_stencil(IDirect3DDevice9* dev) {
    IDirect3DSurface9* ds = nullptr;
    if (FAILED(dev->GetDepthStencilSurface(&ds)) || !ds) return false;
    D3DSURFACE_DESC d{};
    ds->GetDesc(&d);
    ds->Release();
    return d.Format == D3DFMT_D24S8 || d.Format == D3DFMT_D24X4S4 || d.Format == D3DFMT_D15S1 || d.Format == D3DFMT_D24FS8;
}
}  // namespace

bool D3D9::begin_frame(Rml::Vector2i& size) {
    if (!dev_) return false;
    IDirect3DSurface9* rt = nullptr;
    if (FAILED(dev_->GetRenderTarget(0, &rt)) || !rt) return false;
    D3DSURFACE_DESC rd{};
    rt->GetDesc(&rd);
    rt->Release();
    size = {static_cast<int>(rd.Width), static_cast<int>(rd.Height)};

    // Everything we touch goes back exactly as found, so RenderWare's render-state cache (and its
    // cached FVF/shader/index-buffer pointers) stays truthful. State blocks don't reliably include
    // the transforms, so those are saved by hand (as imgui_impl_dx9 does).
    if (FAILED(dev_->CreateStateBlock(D3DSBT_ALL, &saved_)) || !saved_) return false;
    saved_->Capture();
    dev_->GetTransform(D3DTS_WORLD, &saved_world_);
    dev_->GetTransform(D3DTS_VIEW, &saved_view_);
    dev_->GetTransform(D3DTS_PROJECTION, &saved_proj_);

    stencil_ = has_stencil(dev_);
    stencil_ref_ = 1;
    const D3DVIEWPORT9 vp{0, 0, rd.Width, rd.Height, 0.f, 1.f};
    dev_->SetViewport(&vp);
    dev_->SetPixelShader(nullptr);
    dev_->SetVertexShader(nullptr);
    dev_->SetFVF(kFvf);
    dev_->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    dev_->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    dev_->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev_->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    dev_->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);  // RmlUi colours and textures are premultiplied
    dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev_->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    // GTA's stencil shadows / RenderWare may leave these non-default; the state block restores them.
    dev_->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
    dev_->SetRenderState(D3DRS_STENCILMASK, 0xFFFFFFFF);
    dev_->SetRenderState(D3DRS_STENCILWRITEMASK, 0xFFFFFFFF);
    dev_->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    dev_->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE);
    dev_->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
    dev_->SetRenderState(D3DRS_WRAP0, 0);
    dev_->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev_->SetRenderState(D3DRS_RANGEFOGENABLE, FALSE);
    dev_->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev_->SetRenderState(D3DRS_CLIPPING, TRUE);
    dev_->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev_->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_RESULTARG, D3DTA_CURRENT);
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev_->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    dev_->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev_->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    dev_->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);

    D3DMATRIX proj{};
    render_math::ortho_projection(static_cast<float>(rd.Width), static_cast<float>(rd.Height), &proj._11);
    const D3DMATRIX id = identity();
    dev_->SetTransform(D3DTS_PROJECTION, &proj);
    dev_->SetTransform(D3DTS_VIEW, &id);
    dev_->SetTransform(D3DTS_WORLD, &id);
    if (stencil_) dev_->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 1.f, 0);  // the game is done with it by now
    return true;
}

void D3D9::end_frame() {
    if (!saved_) return;
    saved_->Apply();
    saved_->Release();
    saved_ = nullptr;
    dev_->SetTransform(D3DTS_WORLD, &saved_world_);
    dev_->SetTransform(D3DTS_VIEW, &saved_view_);
    dev_->SetTransform(D3DTS_PROJECTION, &saved_proj_);
}

Rml::CompiledGeometryHandle D3D9::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) {
    auto* g = new Geometry;
    g->verts.reserve(vertices.size());
    for (const Rml::Vertex& v : vertices)
        g->verts.push_back({v.position.x, v.position.y, 0.f,
                            render_math::d3d_color(v.colour.red, v.colour.green, v.colour.blue, v.colour.alpha),
                            v.tex_coord.x, v.tex_coord.y});
    if (vertices.size() <= 0xFFFF) {
        g->idx16.reserve(indices.size());
        for (int i : indices) g->idx16.push_back(static_cast<uint16_t>(i));
    } else {
        g->idx32.reserve(indices.size());
        for (int i : indices) g->idx32.push_back(static_cast<uint32_t>(i));
    }
    return reinterpret_cast<Rml::CompiledGeometryHandle>(g);
}

void D3D9::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle texture) {
    const auto* g = reinterpret_cast<const Geometry*>(handle);
    if (!g || g->verts.empty()) return;
    D3DMATRIX world = identity();
    world._41 = translation.x;
    world._42 = translation.y;
    dev_->SetTransform(D3DTS_WORLD, &world);
    dev_->SetTexture(0, reinterpret_cast<IDirect3DTexture9*>(texture));
    // Untextured geometry takes the vertex colour alone (a null texture's sample isn't defined to be white).
    const DWORD op = texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG2;
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, op);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP, op);
    const UINT nv = static_cast<UINT>(g->verts.size());
    if (!g->idx16.empty())
        dev_->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, nv, static_cast<UINT>(g->idx16.size() / 3), g->idx16.data(),
                                     D3DFMT_INDEX16, g->verts.data(), sizeof(Vtx));
    else if (!g->idx32.empty())
        dev_->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, nv, static_cast<UINT>(g->idx32.size() / 3), g->idx32.data(),
                                     D3DFMT_INDEX32, g->verts.data(), sizeof(Vtx));
}

void D3D9::ReleaseGeometry(Rml::CompiledGeometryHandle handle) { delete reinterpret_cast<Geometry*>(handle); }

Rml::TextureHandle D3D9::LoadTexture(Rml::Vector2i&, const Rml::String& source) {
    log::error("render: image files are not supported ({}); use an SVG", source);  // no document loads one
    return 0;
}

Rml::TextureHandle D3D9::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dim) {
    if (!dev_ || dim.x <= 0 || dim.y <= 0) return 0;
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev_->CreateTexture(static_cast<UINT>(dim.x), static_cast<UINT>(dim.y), 1, 0, D3DFMT_A8R8G8B8,
                                   D3DPOOL_MANAGED, &tex, nullptr)) || !tex)
        return 0;
    D3DLOCKED_RECT lr{};
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) {
        tex->Release();
        return 0;
    }
    for (int y = 0; y < dim.y; ++y) {
        const Rml::byte* src = source.data() + static_cast<size_t>(y) * dim.x * 4;
        auto* dst = static_cast<uint8_t*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch;
        for (int x = 0; x < dim.x; ++x, src += 4, dst += 4) {  // RGBA -> BGRA (A8R8G8B8 in memory)
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
        }
    }
    tex->UnlockRect(0);
    return reinterpret_cast<Rml::TextureHandle>(tex);
}

void D3D9::ReleaseTexture(Rml::TextureHandle texture) {
    if (auto* t = reinterpret_cast<IDirect3DTexture9*>(texture)) t->Release();
}

void D3D9::EnableScissorRegion(bool enable) { dev_->SetRenderState(D3DRS_SCISSORTESTENABLE, enable ? TRUE : FALSE); }

void D3D9::SetScissorRegion(Rml::Rectanglei r) {
    const RECT rc{r.Left(), r.Top(), r.Right(), r.Bottom()};
    dev_->SetScissorRect(&rc);
}

// Clip masks as stencil ops, mirroring RmlUi's GL3 backend. Without stencil bits they do nothing
// and clipping falls back to the scissor rectangle (rounded corners clip square).
void D3D9::EnableClipMask(bool enable) {
    if (stencil_) dev_->SetRenderState(D3DRS_STENCILENABLE, enable ? TRUE : FALSE);
}

void D3D9::RenderToClipMask(Rml::ClipMaskOperation op, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) {
    if (!stencil_) return;
    DWORD write = 1;
    switch (op) {
        case Rml::ClipMaskOperation::Set:
            dev_->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 1.f, 0);
            dev_->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
            stencil_ref_ = 1;
            break;
        case Rml::ClipMaskOperation::SetInverse:
            dev_->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 1.f, 1);
            dev_->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
            stencil_ref_ = 1;
            write = 0;
            break;
        case Rml::ClipMaskOperation::Intersect:
            dev_->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_INCR);
            stencil_ref_ += 1;
            break;
    }
    dev_->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    dev_->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
    dev_->SetRenderState(D3DRS_STENCILMASK, 0xFFFFFFFF);
    dev_->SetRenderState(D3DRS_STENCILWRITEMASK, 0xFFFFFFFF);
    dev_->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    dev_->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    dev_->SetRenderState(D3DRS_STENCILREF, write);
    RenderGeometry(geometry, translation, {});
    dev_->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev_->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
    dev_->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
    dev_->SetRenderState(D3DRS_STENCILREF, stencil_ref_);
}

// RmlUi's Matrix4f is column-major with column vectors; its storage read row-major is the
// transpose, which is exactly D3D's row-vector convention, so the 16 floats copy straight across.
// It goes in VIEW so that WORLD can carry each draw's translation: v * world(translation) * view(transform) * proj.
void D3D9::SetTransform(const Rml::Matrix4f* transform) {
    D3DMATRIX m = identity();
    if (transform) std::memcpy(&m, transform->data(), sizeof m);
    dev_->SetTransform(D3DTS_VIEW, &m);
}

}  // namespace yap::render
