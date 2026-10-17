// lesson3_normal_mapping.cpp -- Stage 3: tangent-space normal mapping.
// Build:  g++ -std=c++17 -O2 -Isrc src/lesson3_normal_mapping.cpp -o out/lesson3.exe
// Run (dev root):  ./out/lesson3.exe       argv: [1]=asset dir [2]=output dir
//
// New machinery in this stage:
//   * PER-VERTEX tangent accumulation: every face contributes its uv-gradient
//     tangent to its 3 vertices; vertices average them. Tangents interpolated
//     per-fragment are then re-orthogonalized against the smooth interpolated
//     normal, and the frame is completed right-handed with B = N x T.
//     Why not one tangent per face? A face-constant tangent jumps across face
//     borders wherever the uv layout turns (uv islands, rotated patches) and
//     the shading breaks into visible facets -- an object-space normal map
//     renders perfectly smooth on the very same mesh, so the parameterization
//     is what breaks, not the pipeline.
//   * the shading normal becomes a real interpolated quantity:
//         t-space normal from nm_tangent  ->  object space  ->  view space
//   * normals are directions, NOT points: rotate them by the VIEW matrix
//     (lookat) only. Never multiply a normal by the perspective matrix.
//   * lighting = small ambient + lambert (max(0, n . L)), view-space light.
//
// The render runs TWICE with the same camera/light:
//   pass A "flat": shading normal = perspective-correct interpolated vn
//   pass B "nm":   shading normal = TBN-transformed nm_tangent sample
// only pass B carries the extra skin detail; the pair is the visual proof
// that the normal map participates.
//
// Tangent-normal map convention (verified empirically on this asset:
// mean R=0.498 G=0.498 B=0.983, 99% of texels have B>0.85):
//   R = +tangent (u direction), G = +bitangent (v direction), B = +geometry
//   normal; stored as (c/255)*2-1, flat pole at (0.5, 0.5, 1.0).
//
// Acceptance gates:
//   G1  nm_tangent loads (1024x1024, 24bpp)
//   G2  the per-fragment frame is (re-)orthonormal everywhere (worst |dot| < 1e-9)
//   G3  nm-pass coverage in a sane range (25..70% of the canvas)
//   G4  detail share: pixels changing brightness by >6/255 per channel between
//       the two passes cover > 1% of the canvas (the map must move the shading)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "geometry.h"
#include "model.h"
#include "raster.h"
#include "tga.h"

using std::string;

// tangent of one face along +u: solve [p1-p0 p2-p0] * [du -duv]^-1 analogue.
// (p1-p0)*duv2.y - (p2-p0)*duv1.y points along +u whatever the uv handedness;
// scale by 1/det and orthogonalize against the geometric normal afterwards.
static vec3 face_tangent(const vec3 &p0, const vec3 &p1, const vec3 &p2,
                         const vec2 &uv0, const vec2 &uv1, const vec2 &uv2) {
    vec2 duv1 = uv1 - uv0, duv2 = uv2 - uv0;
    double det = duv1.x * duv2.y - duv1.y * duv2.x;
    vec3 T;
    if (std::abs(det) > 1e-12)
        T = ((p1 - p0) * duv2.y - (p2 - p0) * duv1.y) * (1.0 / det);
    else
        T = p1 - p0;                                   // uv collapse fallback
    vec3 ng = normalized(cross(p1 - p0, p2 - p0));     // geometric normal
    T = T - ng * dot(T, ng);                           // orthogonalize (Gram-Schmidt)
    if (dot(T, T) < 1e-20) {                           // grazing fallback: other edge
        T = p2 - p0;
        T = T - ng * dot(T, ng);
    }
    return T;                                          // unnormalized: vertices average
}

int main(int argc, char **argv) {
    string asset_dir = argc > 1 ? argv[1] : "assets";
    string out_dir   = argc > 2 ? argv[2] : "out";
    auto t0 = std::chrono::steady_clock::now();

    Model model;
    if (!model.load((asset_dir + "/african_head.obj").c_str())) return 1;
    model.normalize();

    TGAImage diffuse, nm;
    if (!diffuse.read(asset_dir + "/african_head_diffuse.tga")) return 1;
    bool g1 = nm.read(asset_dir + "/african_head_nm_tangent.tga");
    std::printf("gate G1 nm_tangent: %dx%d ch=%d -> %s\n", nm.width(), nm.height(),
                nm.channels(), g1 ? "OK" : "FAIL");

    // ---- per-vertex tangent accumulation (needs the normalized model) ----
    std::vector<vec3> vtan(model.verts.size(), vec3(0, 0, 0));
    for (size_t i = 0; i < model.nfaces(); ++i) {
        auto F = model.face(i);
        vec3 T = face_tangent(model.verts[F.v[0]], model.verts[F.v[1]], model.verts[F.v[2]],
                              model.texs[F.t[0]], model.texs[F.t[1]], model.texs[F.t[2]]);
        for (int k = 0; k < 3; ++k) vtan[F.v[k]] += T;
    }
    for (auto &t : vtan) t = normalized(t);            // average = normalize of the sum

    // same camera as lesson2; V is kept SEPARATE from M = P*V because
    // normals must never pass through the perspective projection.
    vec3 eye = normalized(vec3(0.45, 0.3, 1.0)) * 2.0;
    mat4 V = lookat(eye, vec3(0, 0, 0), vec3(0, 1, 0));
    mat4 M = perspective(50.0, 1.0, 0.1, 10.0) * V;
    const vec3 L = normalized(vec3(0.75, 0.45, 1.0));   // view space, towards light

    struct NmShader : IShader {
        Model *model = nullptr;
        TGAImage *diffuse = nullptr, *nm = nullptr;
        const std::vector<vec3> *vtan = nullptr;
        const mat4 *M = nullptr, *V = nullptr;
        vec3 L;
        bool use_nm = false;
        vec2 vuv[3];
        vec3 vn[3], vt[3];
        double ortho_err = 0;        // worst |dot| of the rebuilt frame (gate G2)
        // instrumentation: how far does the mapped normal deviate from smooth?
        long long ang_cnt = 0;
        double ang_sum = 0, ang_max = 0;          // angle(n_obj, n_smooth), degrees

        vec4 vertex(int iface, int k) override {
            auto F = model->face(size_t(iface));
            vuv[k] = model->texs[(std::max)(F.t[k], 0)];
            vn[k] = model->norms[F.n[k]];
            vt[k] = (*vtan)[F.v[k]];
            return (*M) * hvec(model->verts[F.v[k]]);
        }
        bool fragment(vec3 bc, vec3 &color, int, int) override {
            double W = bc.x / clip[0].w + bc.y / clip[1].w + bc.z / clip[2].w;
            vec2 uv = (vuv[0] * (bc.x / clip[0].w) + vuv[1] * (bc.y / clip[1].w) +
                       vuv[2] * (bc.z / clip[2].w)) / W;
            // SMOOTH base normal, perspective-correct (both passes); a per-face
            // geometric normal here would shade the whole face flat.
            // NORMALIZE before use: the perspective-correct average of unit
            // normals is NOT unit (it shrinks wherever vertex normals spread,
            // e.g. creases), and the Gram-Schmidt projection below is only a
            // true projection against a UNIT normal.
            vec3 n_smooth = normalized((vn[0] * (bc.x / clip[0].w) + vn[1] * (bc.y / clip[1].w) +
                                        vn[2] * (bc.z / clip[2].w)) / W);
            vec3 n_obj = n_smooth;
            if (use_nm) {
                // rebuild the frame for THIS fragment: tangent (interpolated,
                // then orthogonalized against the smooth normal), B = N x T.
                vec3 T = (vt[0] * (bc.x / clip[0].w) + vt[1] * (bc.y / clip[1].w) +
                          vt[2] * (bc.z / clip[2].w)) / W;
                T = T - n_smooth * dot(T, n_smooth);
                if (dot(T, T) > 1e-24) {
                    T = normalized(T);
                    vec3 B = cross(n_smooth, T);
                    double worst = std::max(std::abs(dot(T, n_smooth)), std::abs(dot(B, n_smooth)));
                    if (worst > ortho_err) ortho_err = worst;
                    vec3 tn = nm->sample(uv.x, uv.y) / 255.0 * 2.0 - vec3(1, 1, 1);
                    n_obj = T * tn.x + B * tn.y + n_smooth * tn.z;   // t-space -> object
                    ++ang_cnt;
                    double cosang = std::clamp(dot(normalized(n_obj), normalized(n_smooth)), -1.0, 1.0);
                    double ang = std::acos(cosang) * 180.0 / PI;
                    ang_sum += ang;
                    if (ang > ang_max) ang_max = ang;
                }                // else: degenerate tangent -> keep the smooth normal
            }
            vec4 nv = (*V) * hvec(n_obj, 0.0);                  // rotate into view space
            vec3 n_view = normalized(vec3(nv.x, nv.y, nv.z));
            double light = 0.1 + 0.9 * std::max(0.0, dot(n_view, L));   // ambient + lambert
            color = diffuse->sample(uv.x, uv.y) * light;
            return true;
        }
    } shader;
    shader.model = &model;
    shader.diffuse = &diffuse;
    shader.nm = &nm;
    shader.vtan = &vtan;
    shader.M = &M;
    shader.V = &V;
    shader.L = L;

    const int W = 1024, H = 1024;
    TGAImage flat_img(W, H, 3), nm_img(W, H, 3);
    std::vector<double> zbA(size_t(W) * H, 0.0), zbB(size_t(W) * H, 0.0);

    // pass A: flat (smooth vn) normals
    shader.use_nm = false;
    for (size_t i = 0; i < model.nfaces(); ++i) {
        for (int k = 0; k < 3; ++k) shader.clip[k] = shader.vertex(int(i), k);
        draw_triangle(shader, flat_img, zbA.data());
    }
    // pass B: tangent-space normal-mapped
    shader.use_nm = true;
    for (size_t i = 0; i < model.nfaces(); ++i) {
        for (int k = 0; k < 3; ++k) shader.clip[k] = shader.vertex(int(i), k);
        draw_triangle(shader, nm_img, zbB.data());
    }

    std::printf("raster (both passes): fragments=%lld written=%lld\n", g_fragments, g_written);
    std::printf("gate G2 frag frame worst |dot| = %.2e -> %s\n", shader.ortho_err,
                (shader.ortho_err < 1e-9) ? "OK" : "FAIL");
    if (shader.ang_cnt)
        std::printf("instrument: n_obj vs n_smooth: mean=%.2fdeg max=%.2fdeg\n",
                    shader.ang_sum / shader.ang_cnt, shader.ang_max);
    bool g2 = shader.ortho_err < 1e-9;

    long long filled = 0, changed = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            vec3 a = flat_img.at(x, y), b = nm_img.at(x, y);
            if (std::abs(a.x) + std::abs(a.y) + std::abs(a.z) > 1e-6) ++filled;
            if (std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z) > 18.0) ++changed;
        }
    double cover = 100.0 * filled / double(W * H);
    double detail = 100.0 * changed / double(W * H);
    bool g3 = cover > 25.0 && cover < 70.0;
    bool g4 = detail > 1.0;
    std::printf("gate G3 coverage: %.2f%% -> %s\n", cover, g3 ? "OK" : "FAIL");
    std::printf("gate G4 detail (share of changed pixels, >1%%): %.2f%% -> %s\n", detail, g4 ? "OK" : "FAIL");

    flat_img.write(out_dir + "/lesson3_flat.tga");
    nm_img.write(out_dir + "/lesson3_nm.tga");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("wrote %s/lesson3_flat.tga + %s/lesson3_nm.tga (%lld ms)\n",
                out_dir.c_str(), out_dir.c_str(), (long long)ms);
    return (g1 && g2 && g3 && g4) ? 0 : 1;
}
