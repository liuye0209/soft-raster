// lesson0_wireframe.cpp -- Stage 0: TGA output + Bresenham lines + wireframe head.
// Build:  g++ -std=c++17 -O2 -Ireference lesson0_wireframe.cpp -o out/lesson0
// Run (repo root):  ./out/lesson0          argv: [1]=asset dir [2]=output dir
//
// Acceptance gates (program self-checks, exit code non-zero on failure):
//   G1  Bresenham connectivity: for chords covering the main slope classes the
//       visited-pixel count must be max(|dx|,|dy|)+1 and no step may jump.  */
//   G2  the wireframe occupies a plausible share of the canvas (not blank/flooded).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "geometry.h"
#include "model.h"
#include "tga.h"

using std::string;

// Classic all-octant integer Bresenham: plot(px,py) visits every pixel of the
// segment exactly once, in order of the dominant axis. Integer math only.
template <typename Plot>
static void bresenham(int x0, int y0, int x1, int y1, Plot &&plot) {
    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }
    int dx = x1 - x0, dy = std::abs(y1 - y0);
    int err = dx / 2;
    int y = y0, ystep = (y0 < y1) ? 1 : -1;
    for (int x = x0; x <= x1; ++x) {
        plot(steep ? y : x, steep ? x : y);
        err -= dy;
        if (err < 0) { y += ystep; err += dx; }
    }
}

struct Chord { int x0, y0, x1, y1; };

int main(int argc, char **argv) {
    string asset_dir = argc > 1 ? argv[1] : "assets";
    string out_dir   = argc > 2 ? argv[2] : "out";
    auto t0 = std::chrono::steady_clock::now();

    // chords cover: shallow / steep / horizontal / vertical / reversed / diagonal
    const std::vector<Chord> chords = {
        {10, 10, 300, 20},    {500, 20, 520, 300},
        {10, 40, 300, 40},    {40, 50, 40, 250},
        {300, 250, 20, 45},   {600, 600, 200, 200},
    };

    // ---------- G1: Bresenham oracle ----------
    int oracle_fail = 0;
    for (const Chord &c : chords) {
        std::set<std::pair<int, int>> seen;
        int lastx = std::numeric_limits<int>::max(), lasty = std::numeric_limits<int>::max();
        int jumps = 0;
        bresenham(c.x0, c.y0, c.x1, c.y1, [&](int px, int py) {
            seen.insert({px, py});
            if (lastx != std::numeric_limits<int>::max() &&
                (std::abs(px - lastx) > 1 || std::abs(py - lasty) > 1))
                ++jumps;
            lastx = px, lasty = py;
        });
        int expect = std::max(std::abs(c.x1 - c.x0), std::abs(c.y1 - c.y0)) + 1;
        bool ok = int(seen.size()) == expect && jumps == 0;
        if (!ok) ++oracle_fail;
        std::printf("  chord (%4d,%4d)->(%4d,%4d): seen=%d expect=%d jumps=%d -> %s\n",
                    c.x0, c.y0, c.x1, c.y1, int(seen.size()), expect, jumps, ok ? "OK" : "FAIL");
    }

    // ---------- G2: wireframe render ----------
    Model model;
    if (!model.load((asset_dir + "/african_head.obj").c_str())) {
        std::fprintf(stderr, "cannot load %s/african_head.obj\n", asset_dir.c_str());
        return 1;
    }
    model.normalize();                      // unit-ish sphere centered at origin

    const int W = 1024, H = 1024;
    TGAImage canvas(W, H, 3);
    for (size_t i = 0; i < model.nfaces(); ++i) {
        auto F = model.face(i);
        vec3 p[3];
        for (int k = 0; k < 3; ++k) {
            vec3 v = model.verts[F.v[k]];   // [-1,1] -> canvas, y flipped: memory row 0 = image TOP
            p[k] = vec3((v.x + 1) / 2 * (W - 1), (1 - (v.y + 1) / 2) * (H - 1), v.z);
        }
        for (int e = 0; e < 3; ++e)         // the 3 triangle edges (Bresenham users)
            bresenham(int(p[e].x + 0.5), int(p[e].y + 0.5),
                      int(p[(e + 1) % 3].x + 0.5), int(p[(e + 1) % 3].y + 0.5),
                      [&](int px, int py) { canvas.set(px, py, vec3(255, 255, 255)); });
    }

    for (int i = 0; i < 140; ++i) {         // gradient legend, bottom-left corner
        double t = i / 140.0;
        vec3 c(255 * t, 0, 255 * (1 - t));
        canvas.set(16 + i, 16 + int(t * 40), c);
        canvas.set(16 + i, 14, c);
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
    std::printf("wireframe: %zu faces, lit pixels=%.2f%%, mean=(%.1f,%.1f,%.1f)\n",
                model.nfaces(), frac, sum.x / (W * H), sum.y / (W * H), sum.z / (W * H));

    bool g2 = frac > 2.0 && frac < 60.0;
    std::printf("gate G1 bresenham: %s (%d/%d chords)\n",
                oracle_fail == 0 ? "OK" : "FAIL", int(chords.size()) - oracle_fail, int(chords.size()));
    std::printf("gate G2 coverage : %.2f%% -> %s\n", frac, g2 ? "OK" : "FAIL");

    canvas.write(out_dir + "/lesson0_wireframe.tga");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("wrote %s/lesson0_wireframe.tga (%lld ms)\n", out_dir.c_str(), (long long)ms);
    return (oracle_fail == 0 && g2) ? 0 : 1;
}
