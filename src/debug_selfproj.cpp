// debug_selfproj.cpp -- DEV-only: self-projection test for the shadow map.
// For front-facing vertices: compute their light-space projection, derive the
// integer texel used by the lesson5 sampler, and check +/-2 texels around it
// for a map depth >= own_depth - eps (the map must contain the surface itself
// at (or very near) its own projected position for lit points).
#include <cstdio>
#include <cmath>
#include <vector>

#include "geometry.h"
#include "model.h"
#include "raster.h"
#include "tga.h"

int main() {
    Model model;
    if (!model.load("assets/african_head.obj")) return 1;
    model.normalize();

    vec3 L_world = normalized(vec3(-0.35, 0.55, 0.95));
    mat4 VL = lookat(L_world * 2.5, vec3(0, 0, 0), vec3(0, 1, 0));
    mat4 ML = orthographic(-1.3, 1.3, -1.3, 1.3, 0.5, 3.5) * VL;

    // rerender the shadow map exactly like lesson5 pass 1
    struct DepthShader : IShader {
        Model *model = nullptr;
        const mat4 *ML = nullptr;
        vec4 vertex(int iface, int k) override {
            auto F = model->face(size_t(iface));
            return (*ML) * hvec(model->verts[F.v[k]]);
        }
        bool fragment(vec3 bc, vec3 &, int, int) override { return false; }
    } ds;
    ds.model = &model;
    ds.ML = &ML;
    TGAImage dummy(1024, 1024, 3);
    std::vector<double> smap(size_t(1024) * 1024, 0.0);
    for (size_t i = 0; i < model.nfaces(); ++i) {
        for (int k = 0; k < 3; ++k) ds.clip[k] = ds.vertex(int(i), k);
        draw_triangle(ds, dummy, smap.data());
    }

    // test vertices that are front-facing to the sun (vertex normal . sun > 0.3)
    long long tested = 0, hit0 = 0, hit3 = 0, hit9 = 0, miss = 0;
    double worst = 0;
    for (size_t vi = 0; vi < model.verts.size(); ++vi) {
        vec3 n = model.norms[vi];
        if (dot(n, L_world) < 0.3) continue;
        vec4 sc = ML * hvec(model.verts[vi]);
        double lx = sc.x, ly = sc.y;                       // w == 1 (ortho)
        double depth = 1.0 - sc.z;
        int ix = int((lx + 1.0) * 0.5 * 1024), iy = int((1.0 - ly) * 0.5 * 1024);
        if (ix < 0 || iy < 0 || ix > 1023 || iy > 1023) continue;
        ++tested;
        double own = depth;
        bool h0 = smap[size_t(iy) * 1024 + ix] >= own - 1e-6;
        if (h0) ++hit0;
        bool h3 = h0;
        double best = -1e9;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                int cxx = std::clamp(ix + dx, 0, 1023), cyy = std::clamp(iy + dy, 0, 1023);
                best = std::max(best, smap[size_t(cyy) * 1024 + cxx]);
            }
        if (best >= own - 1e-6) ++hit3;
        if (h0) best = std::max(best, smap[size_t(iy) * 1024 + ix]);
        // 5x5 reach
        double best5 = -1e9;
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                int cxx = std::clamp(ix + dx, 0, 1023), cyy = std::clamp(iy + dy, 0, 1023);
                best5 = std::max(best5, smap[size_t(cyy) * 1024 + cxx]);
            }
        if (best5 >= own - 1e-6) ++hit9;
        else ++miss;
        if (best5 < own - 1e-6 && tested < 6)
            std::printf("MISS v%zu: light(%d,%d) own=%.5f best5=%.5f map(ix,iy)=%.5f\n",
                        vi, ix, iy, own, best5, smap[size_t(iy) * 1024 + ix]);
        double gap = own - best5;
        if (gap > worst) worst = gap;
    }
    std::printf("facing verts tested=%lld  exact-hit %lld  hit3x3 %lld  hit5x5 %lld  miss %lld  worst %.5f\n",
                tested, hit0, hit3, hit9, miss, worst);
    return 0;
}
