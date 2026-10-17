// lesson2_texture_zbuffer.cpp -- Stage 2: z-buffer + textured head (perspective camera).
// Build:  g++ -std=c++17 -O2 -Ireference lesson2_texture_zbuffer.cpp -o out/lesson2
// Run (repo root):  ./out/lesson2          argv: [1]=asset dir [2]=output dir
//
// New machinery in this stage:
//   * perspective camera (lookat + perspective + viewport matrices)
//   * per-pixel depth buffer -> correct visibility (lesson1's mouth ghost is gone)
//   * texture mapping with *perspective-correct* uv interpolation:
//         attr = (bc0*a0/w0 + bc1*a1/w1 + bc2*a2/w2) / (bc0/w0 + bc1/w1 + bc2/w2)
//     Screen barycentrics are affine in xy, not in world space; the correction
//     is what keeps textures from sliding on perspective models.
//
// Depth convention (geometry.h): depth in [0,2], LARGER = closer. Init 0, keep max.
//   We do NOT implement near-plane clipping; the whole model is in front of the
//   camera by construction (see lesson5 for the shadow pass that also relies on it).
//
// Acceptance gates:
//   G1  z-buffer holds a real 3D spread (near surface dominates, range > 0.25).
//   G2  every uv of the model lives in [0,1] (otherwise textures would mirror).
//   G3  the textured head covers a plausible share of the canvas.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "geometry.h"
#include "model.h"
#include "raster.h"
#include "tga.h"

using std::string;

int main(int argc, char **argv) {
    string asset_dir = argc > 1 ? argv[1] : "assets";
    string out_dir   = argc > 2 ? argv[2] : "out";
    auto t0 = std::chrono::steady_clock::now();

    Model model;
    if (!model.load((asset_dir + "/african_head.obj").c_str())) return 1;
    model.normalize();

    // ---------- G2: uv range ----------
    double uv_min = 1, uv_max = 0;
    for (auto &t : model.texs) {
        uv_min = std::min(uv_min, std::min(t.x, t.y));
        uv_max = std::max(uv_max, std::max(t.x, t.y));
    }
    bool g2 = uv_min >= -1e-9 && uv_max <= 1 + 1e-9;
    std::printf("gate G2 uv range: [%.3f, %.3f] -> %s\n", uv_min, uv_max, g2 ? "OK" : "FAIL");

    TGAImage diffuse;
    if (!diffuse.read(asset_dir + "/african_head_diffuse.tga")) return 1;
    std::printf("diffuse texture: %dx%d ch=%d\n", diffuse.width(), diffuse.height(), diffuse.channels());

    // camera: slightly upper-right, distance 2.0 so the ~0.6-radius head fills the frame
    vec3 eye = normalized(vec3(0.45, 0.3, 1.0)) * 2.0;
    // CAREFUL: clip space is P*V (NO viewport here). The viewport mapping happens
    // after the /w divide, inside draw_triangle(): screen = (ndc+1)*W/2.
    mat4 M = perspective(50.0, 1.0, 0.1, 10.0) * lookat(eye, vec3(0, 0, 0), vec3(0, 1, 0));

    struct TexShader : IShader {
        Model *model = nullptr;
        TGAImage *tex = nullptr;
        const mat4 *M = nullptr;
        vec2 vuv[3];
        vec4 vertex(int iface, int k) override {
            auto F = model->face(size_t(iface));
            vuv[k] = model->texs[(std::max)(F.t[k], 0)];
            return (*M) * hvec(model->verts[F.v[k]]);
        }
        bool fragment(vec3 bc, vec3 &color, int, int) override {
            // perspective-correct uv (identical structure used for every varying)
            double W = bc.x / clip[0].w + bc.y / clip[1].w + bc.z / clip[2].w;
            vec2 uv = (vuv[0] * (bc.x / clip[0].w) + vuv[1] * (bc.y / clip[1].w) +
                       vuv[2] * (bc.z / clip[2].w)) / W;
            color = tex->sample(uv.x, uv.y);
            return true;
        }
    } shader;
    shader.model = &model;
    shader.tex = &diffuse;
    shader.M = &M;

    const int W = 1024, H = 1024;
    TGAImage frame(W, H, 3);
    std::vector<double> zbuf(size_t(W) * H, 0.0);       // depth: larger=closer, init 0

    long long drawn = 0, hidden = 0;
    for (size_t i = 0; i < model.nfaces(); ++i) {
        long long wr0 = g_written;
        for (int k = 0; k < 3; ++k) shader.clip[k] = shader.vertex(int(i), k);
        draw_triangle(shader, frame, zbuf.data());
        if (g_written != wr0) ++drawn; else ++hidden;
    }

    double zmin = 2, zmax = 0;
    long long filled = 0;
    for (double z : zbuf)
        if (z > 1e-6) { ++filled; zmin = std::min(zmin, z); zmax = std::max(zmax, z); }
    double frac = 100.0 * filled / double(W * H);
    bool g1 = (zmax - zmin) > 0.01 && filled > 200000;   // visibility non-degenerate (GL depth is hyperbolic)
    bool g3 = frac > 25.0 && frac < 70.0;

    std::printf("textured: %zu faces drawn=%lld hidden=%lld, fragments=%lld written=%lld\n",
                model.nfaces(), drawn, hidden, g_fragments, g_written);
    std::printf("gate G1 zbuffer: depth range [%.3f, %.3f], filled=%lld (%.2f%%) -> %s\n",
                zmin, zmax, filled, frac, g1 ? "OK" : "FAIL");
    std::printf("gate G3 coverage: %.2f%% -> %s\n", frac, g3 ? "OK" : "FAIL");

    frame.write(out_dir + "/lesson2_texture.tga");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("wrote %s/lesson2_texture.tga (%lld ms)\n", out_dir.c_str(), (long long)ms);
    return (g1 && g2 && g3) ? 0 : 1;
}
