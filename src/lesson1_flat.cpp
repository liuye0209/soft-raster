// lesson1_flat.cpp -- Stage 1: triangle fill (barycentric) + flat shading.
// Build:  g++ -std=c++17 -O2 -Ireference lesson1_flat.cpp -o out/lesson1
// Run (repo root):  ./out/lesson1          argv: [1]=asset dir [2]=output dir
//
// What's new vs lesson0: real filled triangles via barycentric coordinates,
// flat shading from the face normal (n.z), and an intentional artifact: with
// NO z-buffer yet, triangle paint order decides overlaps (visible as ghost
// facial features). That artifact is exactly the motivation for lesson2.
//
// Acceptance gates:
//   G1  fill a known triangle and sample its centroid: must be filled.
//   G2  for every accepted fragment |w0+w1+w2 - 1| < 1e-9 (partition invariant).
//   G3  the shaded head fills a plausible share of the canvas.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>

#include "geometry.h"
#include "model.h"
#include "tga.h"

using std::string;

vec3 barycentric(const vec2 &A, const vec2 &B, const vec2 &C, const vec2 &P) {
    vec3 u(C.x - A.x, B.x - A.x, A.x - P.x);
    vec3 v(C.y - A.y, B.y - A.y, A.y - P.y);
    vec3 w = cross(u, v);
    if (std::abs(w.z) < 1e-9) return vec3(-1, 0, 0);            // degenerate / zero area
    return vec3(1.0 - (w.x + w.y) / w.z, w.y / w.z, w.x / w.z); // (w0,w1,w2)
}

struct Canvas {                                                // tiny facade to log gates
    TGAImage &img;
    long long fragments = 0;
    double max_sum_err = 0;
};
static Canvas *g_canvas = nullptr;

static void fill_triangle(TGAImage &img, vec2 A, vec2 B, vec2 C, const vec3 &color) {
    vec2 lo(std::min({A.x, B.x, C.x}), std::min({A.y, B.y, C.y}));
    vec2 hi(std::max({A.x, B.x, C.x}), std::max({A.y, B.y, C.y}));
    int x0 = std::max(0, int(lo.x + 0.5)), x1 = std::min(img.width() - 1, int(hi.x + 0.5));
    int y0 = std::max(0, int(lo.y + 0.5)), y1 = std::min(img.height() - 1, int(hi.y + 0.5));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            vec3 w = barycentric(A, B, C, vec2(x + 0.5, y + 0.5));
            if (w.x < 0 || w.y < 0 || w.z < 0) continue;
            if (g_canvas) {
                ++g_canvas->fragments;
                g_canvas->max_sum_err = std::max(g_canvas->max_sum_err, std::abs(w.x + w.y + w.z - 1.0));
            }
            img.set(x, y, color);
        }
}

int main(int argc, char **argv) {
    string asset_dir = argc > 1 ? argv[1] : "assets";
    string out_dir   = argc > 2 ? argv[2] : "out";
    auto t0 = std::chrono::steady_clock::now();

    Model model;
    if (!model.load((asset_dir + "/african_head.obj").c_str())) {
        std::fprintf(stderr, "cannot load %s/african_head.obj\n", asset_dir.c_str());
        return 1;
    }
    model.normalize();

    const int W = 1024, H = 1024;
    TGAImage canvas(W, H, 3);
    Canvas tracker{canvas};
    g_canvas = &tracker;

    // ---------- G1: single-triangle fill test ----------
    vec2 tri[3] = {vec2(20, 20), vec2(460, 150), vec2(120, 380)};
    fill_triangle(canvas, tri[0], tri[1], tri[2], vec3(255, 170, 30));
    for (int e = 0; e < 3; ++e) {                               // crisp white outline
        vec2 P0 = tri[e], P1 = tri[(e + 1) % 3];
        int steps = int(std::max(std::abs(P1.x - P0.x), std::abs(P1.y - P0.y))) + 1;
        for (int s = 0; s <= steps; ++s) {
            vec2 p = P0 + (P1 - P0) * (double(s) / steps);
            canvas.set(int(p.x + 0.5), int(p.y + 0.5), vec3(255, 255, 255));
        }
    }
    vec2 centroid = (tri[0] + tri[1] + tri[2]) * (1.0 / 3.0);
    vec3 cpx = canvas.at(int(centroid.x), int(centroid.y));
    bool g1 = cpx[0] > 200 && cpx[1] > 120 && cpx[2] < 100;     // the orange fill shows
    std::printf("gate G1 centroid fill: rgb=(%.0f,%.0f,%.0f) -> %s\n",
                cpx[0], cpx[1], cpx[2], g1 ? "OK" : "FAIL");

    // ---------- shaded head ----------
    long long drawn = 0;
    for (size_t i = 0; i < model.nfaces(); ++i) {
        auto F = model.face(i);
        vec3 P[3];
        for (int k = 0; k < 3; ++k) {
            vec3 v = model.verts[F.v[k]];
            P[k] = vec3((v.x + 1) / 2 * (W - 1), (1 - (v.y + 1) / 2) * (H - 1), v.z);
        }
        vec3 n = normalized(cross(model.verts[F.v[1]] - model.verts[F.v[0]],
                                  model.verts[F.v[2]] - model.verts[F.v[0]]));
        double intensity = n.z;                                 // camera looks down -z: n.z>0 faces us
        if (intensity <= 0) continue;                           // back-face cull
        fill_triangle(canvas, vec2(P[0].x, P[0].y), vec2(P[1].x, P[1].y), vec2(P[2].x, P[2].y),
                      vec3(255, 255, 255) * intensity);
        ++drawn;
    }

    long long lit = 0;
    vec3 sum(0, 0, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            vec3 p = canvas.at(x, y);
            if (std::abs(p[0]) + std::abs(p[1]) + std::abs(p[2]) > 30) ++lit;
            sum += p;
        }
    double frac = 100.0 * lit / double(W * H);
    bool g2 = tracker.max_sum_err < 1e-9 && tracker.fragments > 100000;
    bool g3 = frac > 15.0 && frac < 75.0;

    std::printf("flat: %zu faces (%lld drawn), fragments=%lld, lit=%.2f%%\n",
                model.nfaces(), drawn, tracker.fragments, frac);
    std::printf("gate G2 barycentric sum: max_err=%.2e -> %s\n", tracker.max_sum_err, g2 ? "OK" : "FAIL");
    std::printf("gate G3 coverage: %.2f%% -> %s\n", frac, g3 ? "OK" : "FAIL");

    canvas.write(out_dir + "/lesson1_flat.tga");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("wrote %s/lesson1_flat.tga (%lld ms)\n", out_dir.c_str(), (long long)ms);
    return (g1 && g2 && g3) ? 0 : 1;
}
