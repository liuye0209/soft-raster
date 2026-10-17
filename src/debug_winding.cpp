// debug_winding.cpp -- DEV-only: for the FRONT camera, correlate per-triangle
//   (a) screen-space area2 sign (kept if > 0 by draw_triangle)
//   (b) geometric normal z in OBJECT space (cross of edges, normalized)
//   (c) mean vertex-normal (vn) z
// Answers: does the culling keep front-facing or back-facing triangles,
// and is the OBJ winding consistent with the outward vn normals?
#include <cstdio>
#include <cmath>

#include "geometry.h"
#include "model.h"

int main() {
    Model model;
    if (!model.load("assets/african_head.obj")) return 1;
    model.normalize();

    vec3 eye = vec3(0, 0, 1) * 2.0;
    mat4 M = perspective(50.0, 1.0, 0.1, 10.0) * lookat(eye, vec3(0, 0, 0), vec3(0, 1, 0));
    const int W = 512, H = 512;

    long long kept_front = 0, kept_back = 0, culled_front = 0, culled_back = 0;
    long long w_bad = 0, degenerate = 0;
    long long vn_front_kept = 0, vn_back_kept = 0, vn_front_culled = 0, vn_back_culled = 0;

    for (size_t i = 0; i < model.nfaces(); ++i) {
        auto F = model.face(i);
        vec3 A = model.verts[F.v[0]], B = model.verts[F.v[1]], C = model.verts[F.v[2]];
        vec3 g = normalized(cross(B - A, C - A));      // geometric normal (object space)
        vec3 vn = (model.norms[F.n[0]] + model.norms[F.n[1]] + model.norms[F.n[2]]) / 3.0;

        vec4 c[3];
        bool ok = true;
        for (int k = 0; k < 3; ++k) {
            c[k] = M * hvec(k == 0 ? A : (k == 1 ? B : C));
            if (c[k].w <= 1e-6) ok = false;
        }
        if (!ok) { ++w_bad; continue; }

        vec2 s[3];
        for (int k = 0; k < 3; ++k)
            s[k] = vec2(W / 2 * (c[k].x / c[k].w + 1), H / 2 * (1 - c[k].y / c[k].w));
        double area2 = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[1].y - s[0].y) * (s[2].x - s[0].x);
        if (std::abs(area2) < 1e-9) { ++degenerate; continue; }

        bool kept = area2 > 0;
        bool gfront = g.z > 0;      // geometric normal towards +z camera
        bool vnfront = vn.z > 0;

        if (kept) { if (gfront) ++kept_front; else ++kept_back; }
        else      { if (gfront) ++culled_front; else ++culled_back; }

        if (kept) { if (vnfront) ++vn_front_kept; else ++vn_back_kept; }
        else      { if (vnfront) ++vn_front_culled; else ++vn_back_culled; }
    }

    std::printf("faces total=%zu  w_bad=%lld  degenerate=%lld\n\n", model.nfaces(), w_bad, degenerate);
    std::printf("GEOMETRIC normal vs culling (front camera):\n");
    std::printf("  kept   & geom-front(+z): %lld\n", kept_front);
    std::printf("  kept   & geom-back (-z): %lld\n", kept_back);
    std::printf("  culled & geom-front(+z): %lld\n", culled_front);
    std::printf("  culled & geom-back (-z): %lld\n\n", culled_back);
    std::printf("VN (mean) vs culling (front camera):\n");
    std::printf("  kept   & vn-front(+z): %lld\n", vn_front_kept);
    std::printf("  kept   & vn-back (-z): %lld\n", vn_back_kept);
    std::printf("  culled & vn-front(+z): %lld\n", vn_front_culled);
    std::printf("  culled & vn-back (-z): %lld\n", vn_back_culled);
    return 0;
}
