#pragma once
// RmlUi 6.3 RenderInterface on the game's D3D9 device, fixed-function only. It holds no
// D3DPOOL_DEFAULT resources (managed textures, UP draws, a per-frame state block), so device
// resets need nothing from us. Game thread only; draw between begin_frame and end_frame.
#include <d3d9.h>

#include <RmlUi/Core/RenderInterface.h>

namespace yap::render {

class D3D9 final : public Rml::RenderInterface {
public:
    // The device textures are created on; set before Context::Update each frame (it may create some).
    void set_device(IDirect3DDevice9* dev) { dev_ = dev; }
    // Saves the full device state, sets ours. `size` = the render target's size. False = skip drawing.
    bool begin_frame(Rml::Vector2i& size);
    // Puts the device back exactly as it was. Safe to call when begin_frame failed or wasn't called.
    void end_frame();

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
    void EnableClipMask(bool enable) override;
    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;
    void SetTransform(const Rml::Matrix4f* transform) override;

private:
    IDirect3DDevice9* dev_ = nullptr;
    IDirect3DStateBlock9* saved_ = nullptr;
    D3DMATRIX saved_world_{}, saved_view_{}, saved_proj_{};
    bool stencil_ = false;  // the depth buffer has stencil bits: clip masks work
    DWORD stencil_ref_ = 1;
};

}  // namespace yap::render
