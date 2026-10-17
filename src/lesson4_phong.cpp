// lesson4_phong.cpp -- Stage 4: Phong lighting (ambient + diffuse + specular).
// Build:  g++ -std=c++17 -O2 -Isrc src/lesson4_phong.cpp -o out/lesson4.exe
// Run (dev root):  ./out/lesson4.exe       argv: [1]=asset dir [2]=output dir
//
// New machinery in this stage (everything from lesson3 is kept and reused):
//   * the light is a real light source: a WORLD-space direction, rotated into
//     view space once (L_view = rot(V) * L_world) so every turnaround shares
//     the same sun. Normals still never touch the perspective matrix.
//   * specular highlight, classic Phong: reflect the light direction about the
//     shading normal, r = 2 (n.l) n - l, and measure r.v with v = (0,0,1) in
//     view space (surface->camera). Power p and gain per pixel come from the
//     scalar specular map (gray texture).
//   * final per-fragment color:
//         diffuse_color * (ambient + kd * max(0, n.l))  +  spec_gain * (r.v)^p
//
// The render runs TWICE with the same light:
//   pass A "diffuse": lesson3's look (nm normals, no specular)
//   pass B "phong":   adds the specular term
// the pair isolates what the highlight changes.
//
// Acceptance gates:
//   G1  spec map loads (1024x1024, gray)
//   G2  the per-fragment frame is (re-)orthonormal everywhere (worst |dot| < 1e-9)
//   G3  phong pass coverage in a sane range (25..70% of the canvas)
//   G4  highlight is present but not blown out: pixels with spec>0.5 stay in
//       (0.005%, 15%) of the canvas
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

// tangent of one face along +u, orthogonalized against the geometric normal
// (same helper as lesson3).
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
    T = T - ng * dot(T, ng);                           // Gram-Schmidt (|n|=1 here)
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

    TGAImage diffuse, nm, spec;
    if (!diffuse.read(asset_dir + "/african_head_diffuse.tga")) return 1;
    if (!nm.read(asset_dir + "/african_head_nm_tangent.tga")) return 1;
    bool g1 = spec.read(asset_dir + "/african_head_spec.tga");
    std::printf("gate G1 spec map: %dx%d ch=%d -> %s\n", spec.width(), spec.height(),
                spec.channels(), g1 ? "OK" : "FAIL");

    // ---- per-vertex tangent accumulation (lesson3) ----
    std::vector<vec3> vtan(model.verts.size(), vec3(0, 0, 0));
    for (size_t i = 0; i < model.nfaces(); ++i) {
        auto F = model.face(i);
        vec3 T = face_tangent(model.verts[F.v[0]], model.verts[F.v[1]], model.verts[F.v[2]],
                              model.texs[F.t[0]], model.texs[F.t[1]], model.texs[F.t[2]]);
        for (int k = 0; k < 3; ++k) vtan[F.v[k]] += T;
    }
    for (auto &t : vtan) t = normalized(t);

    // camera as in lessons 2..3. ONE world light, rotated into view space once:
    // the sun does not move when the camera turns around.
    vec3 eye = normalized(vec3(0.45, 0.3, 1.0)) * 2.0;
    mat4 V = lookat(eye, vec3(0, 0, 0), vec3(0, 1, 0));
    mat4 M = perspective(50.0, 1.0, 0.1, 10.0) * V;
    vec4 Lw = V * hvec(normalized(vec3(-0.35, 0.55, 0.95)), 0.0);   // towards light
    const vec3 L = normalized(vec3(Lw.x, Lw.y, Lw.z));
    std::printf("light: world (-0.35, 0.55, 0.95) -> view (%.3f, %.3f, %.3f)\n", L.x, L.y, L.z);

    struct PhongShader : IShader {
        Model *model = nullptr;
        TGAImage *diffuse = nullptr, *nm = nullptr, *spec = nullptr;
        const std::vector<vec3> *vtan = nullptr;
        const mat4 *M = nullptr, *V = nullptr;
        vec3 L;
        bool use_spec = false;
        vec2 vuv[3];
        vec3 vn[3], vt[3];
        double ortho_err = 0;        // worst |dot| of the rebuilt frame (gate G2)
        long long spec_hi = 0;       // fragments with spec term > 0.5 (gate G4)
        // instrumentation: specular magnitudes actually reachable
        double spec_term_max = 0, rv_max = 0, s_max = 0;

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
            // smooth base normal: NORMALIZE the perspective-correct average
            // (unit-length assumption is load-bearing for the frame below)
            vec3 n_smooth = normalized((vn[0] * (bc.x / clip[0].w) + vn[1] * (bc.y / clip[1].w) +
                                        vn[2] * (bc.z / clip[2].w)) / W);
            vec3 n_obj = n_smooth;
            // rebuild the per-fragment frame (lesson3): tangent orthogonalized
            // against the smooth normal, B = N x T closes the right-handed basis
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
            }                    // else: degenerate tangent -> keep smooth normal

            vec4 nv = (*V) * hvec(n_obj, 0.0);                  // rotate into view space
            vec3 n_view = normalized(vec3(nv.x, nv.y, nv.z));
            double ndl_raw = dot(n_view, L);                    // RAW n.l (specular needs it)
            double ndl = std::max(0.0, ndl_raw);                // clamped for diffuse only
            vec3 c = diffuse->sample(uv.x, uv.y) * (0.1 + 0.9 * ndl);   // ambient + diffuse
            if (use_spec) {
                // reflect L about N; MUST use the raw n.l, not the clamped one
                // (with the clamped value, surfaces facing away from the light
                // would reflect -L and can land fake highlights)
                vec3 r = n_view * (2.0 * ndl_raw) - L;
                double rv = std::max(0.0, r.z);                  // v = +z (to camera)
                double s = spec->scalar_sample(uv.x, uv.y) / 255.0;
                double spec_term = 1.4 * s * std::pow(rv, 8.0);
                if (spec_term > 0.5) ++spec_hi;
                if (spec_term > spec_term_max) spec_term_max = spec_term;
                if (rv > rv_max) rv_max = rv;
                if (s > s_max) s_max = s;
                c = c + vec3(255, 255, 255) * spec_term;
            }
            color = c;
            return true;
        }
    } shader;
    shader.model = &model;
    shader.diffuse = &diffuse;
    shader.nm = &nm;
    shader.spec = &spec;
    shader.vtan = &vtan;
    shader.M = &M;
    shader.V = &V;
    shader.L = L;

    const int W = 1024, H = 1024;
    TGAImage dif_img(W, H, 3), pho_img(W, H, 3);
    std::vector<double> zbA(size_t(W) * H, 0.0), zbB(size_t(W) * H, 0.0);

    // pass A: nm normals + diffuse lambert only
    shader.use_spec = false;
    for (size_t i = 0; i < model.nfaces(); ++i) {
        for (int k = 0; k < 3; ++k) shader.clip[k] = shader.vertex(int(i), k);
        draw_triangle(shader, dif_img, zbA.data());
    }
    // pass B: + specular
    shader.use_spec = true;
    for (size_t i = 0; i < model.nfaces(); ++i) {
        for (int k = 0; k < 3; ++k) shader.clip[k] = shader.vertex(int(i), k);
        draw_triangle(shader, pho_img, zbB.data());
    }

    std::printf("raster (both passes): fragments=%lld written=%lld\n", g_fragments, g_written);
    std::printf("instrument: spec_term max=%.3f  rv max=%.3f  s_sampled max=%.3f\n",
                shader.spec_term_max, shader.rv_max, shader.s_max);
    bool g2 = shader.ortho_err < 1e-9;
    std::printf("gate G2 frag frame worst |dot| = %.2e -> %s\n", shader.ortho_err, g2 ? "OK" : "FAIL");

    long long filled = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (pho_img.at(x, y).x + pho_img.at(x, y).y + pho_img.at(x, y).z > 1e-6) ++filled;
    double cover = 100.0 * filled / double(W * H);
    double hi_share = 100.0 * shader.spec_hi / double(W * H);
    bool g3 = cover > 25.0 && cover < 70.0;
    bool g4 = hi_share > 0.005 && hi_share < 15.0;
    std::printf("gate G3 coverage: %.2f%% -> %s\n", cover, g3 ? "OK" : "FAIL");
    std::printf("gate G4 highlight share (spec>0.5): %.4f%% -> %s\n", hi_share, g4 ? "OK" : "FAIL");

    dif_img.write(out_dir + "/lesson4_diffuse.tga");
    pho_img.write(out_dir + "/lesson4_phong.tga");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("wrote %s/lesson4_diffuse.tga + %s/lesson4_phong.tga (%lld ms)\n",
                out_dir.c_str(), out_dir.c_str(), (long long)ms);
    return (g1 && g2 && g3 && g4) ? 0 : 1;
}
