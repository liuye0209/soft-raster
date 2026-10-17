// debug_turntable.cpp -- DEV-only: three cameras (front / right / top), headlight
// shading. Whatever the head really is, one of the three must be bright and show
// the face cleanly. Dark everywhere => shader/interpolation bug, not conventions.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "geometry.h"
#include "model.h"
#include "tga.h"

struct IShader {
    vec4 clip[3];
    virtual vec4 vertex(int iface, int k) = 0;
    virtual bool fragment(vec3 bc, vec3 &color, int, int) = 0;
    virtual ~IShader() = default;
};

static void draw_triangle(IShader &sh, TGAImage &frame, double *zbuf) {
    for (int k = 0; k < 3; ++k)
        if (sh.clip[k].w <= 1e-6) return;
    vec4 ndc[3];
    for (int k = 0; k < 3; ++k) ndc[k] = sh.clip[k] * (1.0 / sh.clip[k].w);
    vec2 s[3];
    for (int k = 0; k < 3; ++k)
        s[k] = vec2(frame.width() / 2 * (ndc[k].x + 1), frame.height() / 2 * (1 - ndc[k].y));
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
            if (std::abs(b3.z) < 1e-9) continue;
            vec3 bc(1 - (b3.x + b3.y) / b3.z, b3.y / b3.z, b3.x / b3.z);
            if (bc.x < 0 || bc.y < 0 || bc.z < 0) continue;
            double W = bc.x / sh.clip[0].w + bc.y / sh.clip[1].w + bc.z / sh.clip[2].w;
            double z_ndc = (bc.x * ndc[0].z / sh.clip[0].w + bc.y * ndc[1].z / sh.clip[1].w +
                            bc.z * ndc[2].z / sh.clip[2].w) / W;
            double depth = 1.0 - z_ndc;
            int idx = x + y * frame.width();
            if (depth <= zbuf[idx] || depth < 0 || depth > 2) continue;
            vec3 color;
            if (sh.fragment(bc, color)) { frame.set(x, y, color); zbuf[idx] = depth; }
        }
}

int main() {
    Model model;
    if (!model.load("assets/african_head.obj")) return 1;
    model.normalize();

    const int W = 512, H = 512;
    mat4 vp = viewport(0, 0, W, H, 2);
    const char *names[3] = {"front", "right", "top"};
    vec3 dirs[3] = {vec3(0, 0, 1), vec3(1, 0, 0), vec3(0, 1, 0.001)};

    for (int c = 0; c < 3; ++c) {
        vec3 eye = dirs[c] * 2.0;
        mat4 M = perspective(50.0, 1.0, 0.1, 10.0) * lookat(eye, vec3(0, 0, 0), vec3(0, 1, 0));

        struct HeadShader : IShader {
            Model *model = nullptr;
            const mat4 *M = nullptr;
            vec3 vnormal[3];
            // instrumentation: min/max of interpolated n.z over written fragments
            double nz_min = 1e9, nz_max = -1e9;
            long long fr_count = 0;
            vec4 vertex(int iface, int k) override {
                auto F = model->face(size_t(iface));
                vnormal[k] = model->norms[F.n[k]];
                return (*M) * hvec(model->verts[F.v[k]]);
            }
            bool fragment(vec3 bc, vec3 &color, int, int) override {
                double Wc = bc.x / clip[0].w + bc.y / clip[1].w + bc.z / clip[2].w;
                vec3 n = (vnormal[0] * (bc.x / clip[0].w) + vnormal[1] * (bc.y / clip[1].w) +
                          vnormal[2] * (bc.z / clip[2].w)) / Wc;
                n = normalized(n);
                ++fr_count;
                if (n.z < nz_min) nz_min = n.z;
                if (n.z > nz_max) nz_max = n.z;
                double light = std::max(0.0, dot(n, vec3(0, 0, 1)));   // headlight in VIEW space
                color = vec3(255, 255, 255) * light;
                return true;
            }
        } sh;
        sh.model = &model;
        sh.M = &M;

        TGAImage frame(W, H, 3);
        std::vector<double> zb(size_t(W) * H, 0.0);
        for (size_t i = 0; i < model.nfaces(); ++i) {
            for (int k = 0; k < 3; ++k) sh.clip[k] = sh.vertex(int(i), k);
            draw_triangle(sh, frame, zb.data());
        }
        char path[64];
        std::snprintf(path, sizeof(path), "out/debug_turn_%s.tga", names[c]);
        frame.write(path);
        std::printf("%s: fragments=%lld  n.z in [%.3f, %.3f]\n", names[c], sh.fr_count, sh.nz_min, sh.nz_max);
    }
    return 0;
}
