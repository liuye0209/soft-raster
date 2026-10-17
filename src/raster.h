// raster.h -- the shared rasterization core for lessons 2..5: IShader interface
// and draw_triangle() (screen-space rasterizer with z-buffer).
// ONE source of truth: lessons include this instead of re-deriving the raster
// loop (the back-face sign below cost a real debugging session -- see comment).
//
// Pipeline per triangle:
//   1. shader->vertex() fills sh.clip[0..2] with RAW clip space (w>0 guaranteed:
//      no near-plane clipping, the whole model is in front of the camera there).
//   2. /w divide -> NDC; inline screen mapping (x+1)*W/2, (1-y)*H/2 (row 0 = top).
//   3. back-face cull + degenerate test on the screen-space cross product.
//   4. bounding box x pixel-center barycentrics; reject if any weight < 0.
//   5. perspective-correct depth (1/w interpolant), z-buffer keep-max.
//      depth = 1 - z_ndc in [0,2], LARGER = CLOSER (see geometry.h header).
//   6. shader->fragment(bc) -> optional write + depth update.
#pragma once
#include <algorithm>

#include "geometry.h"
#include "tga.h"

struct IShader {
    vec4 clip[3];                                    // raw clip-space, one per corner
    virtual vec4 vertex(int iface, int k) = 0;
    // bc = SCREEN barycentrics; (x,y) = this fragment's screen pixel (for
    // screen-space derivative tricks and debug dumps)
    virtual bool fragment(vec3 bc, vec3 &color, int x, int y) = 0;
    virtual ~IShader() = default;
};

// raster statistics lessons print for their acceptance gates
inline long long g_fragments = 0, g_written = 0;

inline void draw_triangle(IShader &sh, TGAImage &frame, double *zbuf) {
    for (int k = 0; k < 3; ++k)
        if (sh.clip[k].w <= 1e-6) return;            // behind camera -> skip (no near clip)
    vec4 ndc[3];                                     // local copy; raw w kept in sh.clip
    for (int k = 0; k < 3; ++k)
        ndc[k] = sh.clip[k] * (1.0 / sh.clip[k].w);
    vec2 s[3];
    for (int k = 0; k < 3; ++k)
        s[k] = vec2(frame.width() / 2 * (ndc[k].x + 1),
                    frame.height() / 2 * (1 - ndc[k].y));    // ndc up -> memory row 0 (top-first)

    // screen-space back-face cull + degenerate test in one cross product
    double area2 = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[1].y - s[0].y) * (s[2].x - s[0].x);
    // Screen y is FLIPPED (row 0 = image top), so orientation reverses:
    // a front-facing (CCW in NDC, y-up) triangle is CW here -> area2 < 0.
    // Keep front-facing: cull area2 >= 0 (this also drops degenerate slivers).
    if (area2 >= 0) return;

    int x0 = std::max(0, int(std::min(std::min(s[0].x, s[1].x), s[2].x) + 0.5));
    int x1 = std::min(frame.width() - 1, int(std::max(std::max(s[0].x, s[1].x), s[2].x) + 0.5));
    int y0 = std::max(0, int(std::min(std::min(s[0].y, s[1].y), s[2].y) + 0.5));
    int y1 = std::min(frame.height() - 1, int(std::max(std::max(s[0].y, s[1].y), s[2].y) + 0.5));

    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            vec2 P(x + 0.5, y + 0.5);
            vec3 u(s[2].x - s[0].x, s[1].x - s[0].x, s[0].x - P.x);
            vec3 v(s[2].y - s[0].y, s[1].y - s[0].y, s[0].y - P.y);
            vec3 b3 = cross(u, v);
            if (std::abs(b3.z) < 1e-9) continue;     // zero-area
            vec3 bc(1 - (b3.x + b3.y) / b3.z, b3.y / b3.z, b3.x / b3.z);
            if (bc.x < 0 || bc.y < 0 || bc.z < 0) continue;
            ++g_fragments;

            // perspective-correct depth: 1/w is linear in screen space
            double W = bc.x / sh.clip[0].w + bc.y / sh.clip[1].w + bc.z / sh.clip[2].w;
            double z_ndc = (bc.x * ndc[0].z / sh.clip[0].w + bc.y * ndc[1].z / sh.clip[1].w +
                            bc.z * ndc[2].z / sh.clip[2].w) / W;
            double depth = 1.0 - z_ndc;              // viewport flip: larger = closer
            if (depth < 0 || depth > 2) continue;    // paranoid guard
            int idx = x + y * frame.width();
            if (depth <= zbuf[idx]) continue;        // z-buffer: keep the max depth
            ++g_written;
            vec3 color;
            if (sh.fragment(bc, color, x, y)) frame.set(x, y, color);
            zbuf[idx] = depth;
        }
}
