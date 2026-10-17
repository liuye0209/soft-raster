// debug_shadowwinding.cpp -- DEV-only: the debug_winding experiment, redone
// for the LIGHT camera + orthographic projection of lesson5.
// Correlates per-triangle (kept if screen area2 > 0 ... our front-face rule)
// with the geometric normal pointing TOWARDS the light position.
#include <cstdio>
#include <cmath>

#include "geometry.h"
#include "model.h"

int main() {
    Model model;
    if (!model.load("assets/african_head.obj")) return 1;
    model.normalize();

    vec3 L_world = normalized(vec3(-0.35, 0.55, 0.95));
    mat4 VL = lookat(L_world * 2.5, vec3(0, 0, 0), vec3(0, 1, 0));
    mat4 ML = orthographic(-1.3, 1.3, -1.3, 1.3, 0.5, 3.5) * VL;
    const int W = 1024;

    long long kept_front = 0, kept_back = 0, culled_front = 0, culled_back = 0, w_bad = 0, degen = 0;
    for (size_t i = 0; i < model.nfaces(); ++i) {
        auto F = model.face(i);
        vec3 A = model.verts[F.v[0]], B = model.verts[F.v[1]], C = model.verts[F.v[2]];
        vec3 g = normalized(cross(B - A, C - A));
        vec3 to_light = L_world * 2.5 - (A + B + C) / 3.0;
        bool gfront = dot(g, to_light) > 0;        // geometric face towards the sun

        vec4 c[3];
        bool ok = true;
        for (int k = 0; k < 3; ++k) {
            c[k] = ML * hvec(k == 0 ? A : (k == 1 ? B : C));
            if (c[k].w <= 1e-6) ok = false;        // ortho w==1, never triggers
        }
        if (!ok) { ++w_bad; continue; }
        vec2 s[3];
        for (int k = 0; k < 3; ++k)
            s[k] = vec2(W / 2.0 * (c[k].x / c[k].w + 1), W / 2.0 * (1 - c[k].y / c[k].w));
        double area2 = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[1].y - s[0].y) * (s[2].x - s[0].x);
        if (std::abs(area2) < 1e-9) { ++degen; continue; }
        if (area2 < 0) { if (gfront) ++kept_front; else ++kept_back; }
        else           { if (gfront) ++culled_front; else ++culled_back; }
    }
    std::printf("faces=%zu w_bad=%lld degenerate=%lld\n", model.nfaces(), w_bad, degen);
    std::printf("kept   & towards-light: %lld\n", kept_front);
    std::printf("kept   & away-from-light: %lld\n", kept_back);
    std::printf("culled & towards-light: %lld\n", culled_front);
    std::printf("culled & away-from-light: %lld\n", culled_back);
    return 0;
}
