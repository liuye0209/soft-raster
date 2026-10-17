// debug_winding2.cpp -- DEV-only: measure the OBJ winding convention directly
// in OBJECT space, independent of any camera:
//   for faces whose mean vn normal is strongly +z (outward towards viewer):
//     cross2d = (B-A) x (C-A) z-component in the xy-plane
//     cross2d > 0  -> CCW when viewed from +z (standard OBJ front-face rule)
//   plus: global agreement between geometric normal sign and vn sign.
#include <cstdio>
#include <cmath>

#include "geometry.h"
#include "model.h"

int main() {
    Model model;
    if (!model.load("assets/african_head.obj")) return 1;
    model.normalize();

    long long strong_front = 0, ccw = 0, cw = 0;
    long long strong_back = 0, ccw_b = 0, cw_b = 0;
    long long agree = 0, disagree = 0, deadzone = 0;

    for (size_t i = 0; i < model.nfaces(); ++i) {
        auto F = model.face(i);
        vec3 A = model.verts[F.v[0]], B = model.verts[F.v[1]], C = model.verts[F.v[2]];
        vec3 g = normalized(cross(B - A, C - A));
        vec3 vn = (model.norms[F.n[0]] + model.norms[F.n[1]] + model.norms[F.n[2]]) / 3.0;
        double cross2d = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);

        if (vn.z > 0.5) {
            ++strong_front;
            if (cross2d > 0) ++ccw; else ++cw;
        } else if (vn.z < -0.5) {
            ++strong_back;
            if (cross2d > 0) ++ccw_b; else ++cw_b;
        }

        if (std::abs(g.z) < 0.1 || std::abs(vn.z) < 0.1) ++deadzone;
        else if ((g.z > 0) == (vn.z > 0)) ++agree; else ++disagree;
    }

    std::printf("strong-front faces (vn.z>+0.5): %lld   CCW-from-+z: %lld   CW: %lld\n",
                strong_front, ccw, cw);
    std::printf("strong-back  faces (vn.z<-0.5): %lld   CCW-from-+z: %lld   CW: %lld\n",
                strong_back, ccw_b, cw_b);
    std::printf("geom vs vn sign: agree=%lld  disagree=%lld  deadzone=%lld\n",
                agree, disagree, deadzone);
    return 0;
}
