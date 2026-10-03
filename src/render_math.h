#pragma once
// Pure math for render_d3d9.cpp, free of D3D and RmlUi types so test_parser can pin it.
#include <cstdint>

namespace yap::render_math {

// RmlUi's RGBA bytes (already premultiplied) -> D3DCOLOR (0xAARRGGBB).
constexpr uint32_t d3d_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

// D3D projection (row vectors, v * M; row-major) from pixel space (origin top-left, y down) to
// clip space, shifted by D3D9's half pixel so texels land on pixel centres and text stays crisp.
// z in [-10000, 10000] (RmlUi's own backends' ortho range) maps to [0, 1].
inline void ortho_projection(float w, float h, float out[16]) {
    for (int i = 0; i < 16; ++i) out[i] = 0.f;
    out[0] = 2.f / w;
    out[5] = -2.f / h;
    out[10] = 1.f / 20000.f;
    out[12] = -1.f - 1.f / w;  // x' = 2(x - 0.5)/w - 1
    out[13] = 1.f + 1.f / h;   // y' = 1 - 2(y - 0.5)/h
    out[14] = 0.5f;
    out[15] = 1.f;
}

// (x, y, 0, 1) * m, x and y only; for tests.
inline void transform_point(const float m[16], float x, float y, float& ox, float& oy) {
    ox = x * m[0] + y * m[4] + m[12];
    oy = x * m[1] + y * m[5] + m[13];
}

}  // namespace yap::render_math
