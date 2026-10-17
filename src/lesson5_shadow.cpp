// lesson5_shadow.cpp -- Stage 5 (final): shadow mapping + the finished head.
// Build:  g++ -std=c++17 -O2 -Isrc src/lesson5_shadow.cpp -o out/lesson5.exe
// Run (dev root):  ./out/lesson5.exe       argv: [1]=asset dir [2]=out dir [3]=bias
//
// bias (default 0.03) is not a magic number: print the per-fragment
// (shadow_z - depth_light) histogram over sun-facing fragments and pick a value
// that swallows the quantization spike (almost everything with |delta| < 0.01)
// while keeping the real occluders (deltas up to +0.77 down here).  Typical
// outcome: the delta distribution is TRIMODAL -- a +-0.006 rasterization spike
// around zero, near-zero grazing/noise fragments at deep negative delta where
// the sun-facing map legitimately has no entry (surfaces that face the sun by
// their SMOOTH vertex normal but are geometrically backfacing to it, e.g. the
// hairline), and the true shadow pile at large positive delta.
//
// Shadow mapping in two passes, all of it standard C++:
//   PASS 1 "shadow map": render the model's DEPTH from the light's point of
//        view. The light is a directional sun; its camera is an orthographic
//        projection built like the main one (lookat + ortho + the same depth
//        convention). A shader with fragment()->false drops the frame write
//        while raster.h still fills the z-buffer.
//   PASS 2 "the image": per-fragment, project the world position into the
//        light's screen; if that pixel is FARTHER from the sun than the
//        closest surface recorded there, the fragment is in shadow:
//              shadowed  <=>  shadow_z - depth_light > bias
//        (depth is larger = CLOSER, so "closer blocker exists" means the map
//        value exceeds ours). In shadow the fragment keeps only the ambient
//        term and loses its specular. bias absorbs acne (z-fighting at the
//        lit/silhouette boundary caused by raster quantization).
//
// Depth bookkeeping, once more (geometry.h): depth in [0,2], LARGER = CLOSER
// to the render camera -- which for the light pass is the sun. Keep-max zbuf.
//
// Acceptance gates:
//   G1  shadow map is live: filled share > 15% and a real depth spread > 0.05
//   G2  per-fragment frame orthonormal (worst |dot| < 1e-9)
//   G3  shadow share sane: 5%..45% of written main-pass fragments
//   G4  final image coverage sane: 25..70% of the canvas
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "geometry.h"
#include "model.h"
#include "raster.h"
#include "tga.h"

using std::string;

// tangent of one face along +u, orthogonalized against the geometric normal
// (lesson3 helper, unchanged).
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
    T = T - ng * dot(T, ng);
    if (dot(T, T) < 1e-20) {
        T = p2 - p0;
        T = T - ng * dot(T, ng);
    }
    return T;                                          // unnormalized: vertices average
}

int main(int argc, char **argv) {
    string asset_dir = argc > 1 ? argv[1] : "assets";
    string out_dir   = argc > 2 ? argv[2] : "out";
    double bias      = argc > 3 ? std::atof(argv[3]) : 0.03;
    auto t0 = std::chrono::steady_clock::now();

    Model model;
    if (!model.load((asset_dir + "/african_head.obj").c_str())) return 1;
    model.normalize();

    TGAImage diffuse, nm, spec;
    if (!diffuse.read(asset_dir + "/african_head_diffuse.tga")) return 1;
    if (!nm.read(asset_dir + "/african_head_nm_tangent.tga")) return 1;
    if (!spec.read(asset_dir + "/african_head_spec.tga")) return 1;

    // ---- per-vertex tangent accumulation (lesson3) ----
    std::vector<vec3> vtan(model.verts.size(), vec3(0, 0, 0));
    for (size_t i = 0; i < model.nfaces(); ++i) {
        auto F = model.face(i);
        vec3 T = face_tangent(model.verts[F.v[0]], model.verts[F.v[1]], model.verts[F.v[2]],
                              model.texs[F.t[0]], model.texs[F.t[1]], model.texs[F.t[2]]);
        for (int k = 0; k < 3; ++k) vtan[F.v[k]] += T;
    }
    for (auto &t : vtan) t = normalized(t);

    // ---- the sun: one world direction, same as lesson4 ----
    vec3 L_world = normalized(vec3(-0.35, 0.55, 0.95));

    // main camera (lessons 2..4)
    vec3 eye = normalized(vec3(0.45, 0.3, 1.0)) * 2.0;
    mat4 V = lookat(eye, vec3(0, 0, 0), vec3(0, 1, 0));
    mat4 M = perspective(50.0, 1.0, 0.1, 10.0) * V;
    vec4 Lv = V * hvec(L_world, 0.0);
    const vec3 L = normalized(vec3(Lv.x, Lv.y, Lv.z));   // view-space sun for shading

    // light camera for the shadow pass: sun outside the model, orthographic
    mat4 VL = lookat(L_world * 2.5, vec3(0, 0, 0), vec3(0, 1, 0));
    mat4 ML = orthographic(-1.3, 1.3, -1.3, 1.3, 0.5, 3.5) * VL;

    const int W = 1024, H = 1024;

    // ~~~~~~~~~ PASS 1: shadow map ~~~~~~~~~
    struct DepthShader : IShader {
        Model *model = nullptr;
        const mat4 *ML = nullptr;
        vec4 vertex(int iface, int k) override {
            auto F = model->face(size_t(iface));
            return (*ML) * hvec(model->verts[F.v[k]]);
        }
        bool fragment(vec3 bc, vec3 &, int, int) override { return false; }   // depth only
    } depth_shader;
    depth_shader.model = &model;
    depth_shader.ML = &ML;

    // the "frame" here is only the rasterizer's metric (W x H); nothing is
    // written to it because the fragment shader returns false.
    TGAImage shadow_dummy(W, H, 3);
    std::vector<double> smap(size_t(W) * H, 0.0);
    for (size_t i = 0; i < model.nfaces(); ++i) {
        for (int k = 0; k < 3; ++k) depth_shader.clip[k] = depth_shader.vertex(int(i), k);
        draw_triangle(depth_shader, shadow_dummy, smap.data());   // depth only
    }
    double smin = 2, smax = 0;
    long long sfilled = 0;
    for (double z : smap)
        if (z > 1e-6) { ++sfilled; smin = std::min(smin, z); smax = std::max(smax, z); }
    double sfill = 100.0 * sfilled / double(W * H);
    bool g1 = sfill > 15.0 && (smax - smin) > 0.05;
    std::printf("gate G1 shadow map: filled %.2f%% depth [%.3f, %.3f] -> %s\n",
                sfill, smin, smax, g1 ? "OK" : "FAIL");
    // dump the shadow map as a gray image for the report
    TGAImage smap_img(W, H, 1);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double d = smap[size_t(y) * W + x] / 2.0 * 255.0;   // depth 2 -> 255 white (near sun)
            smap_img.set(x, y, vec3(d, d, d));
        }
    smap_img.write(out_dir + "/lesson5_shadowmap.tga");

    // ~~~~~~~~~ PASS 2: the final shaded render ~~~~~~~~~
    struct HeadShader : IShader {
        Model *model = nullptr;
        TGAImage *diffuse = nullptr, *nm = nullptr, *spec = nullptr;
        const std::vector<vec3> *vtan = nullptr;
        const mat4 *M = nullptr, *V = nullptr, *ML = nullptr;
        const double *smap = nullptr;
        vec3 L;
        double bias = 0, shadow_split = 0.5;             // shadow dim factor
        vec2 vuv[3];
        vec3 vn[3], vt[3];
        vec4 scl[3];                                     // shadow-space clip per corner
        double ortho_err = 0;
        long long wr_total = 0, shadowed = 0, facing_shadowed = 0;
        double facing_limit = 0.1;                       // n.l above this = "should be lit"
        // instrumentation: light-depth mismatch on fragments that face the sun
        long long facing_cnt = 0;
        double dl_mean = 0, dl_max = -1e9, dl_min = 1e9;              // shadow_z - depth_light
        // delta histogram (facing fragments; the honest way to pick a bias)
        long long dl_neg = 0, dl_q1 = 0, dl_q2 = 0, dl_q3 = 0, dl_hi = 0;

        vec4 vertex(int iface, int k) override {
            auto F = model->face(size_t(iface));
            vuv[k] = model->texs[(std::max)(F.t[k], 0)];
            vn[k] = model->norms[F.n[k]];
            vt[k] = (*vtan)[F.v[k]];
            vec3 w = model->verts[F.v[k]];
            scl[k] = (*ML) * hvec(w);
            return (*M) * hvec(w);
        }
        bool fragment(vec3 bc, vec3 &color, int x, int y) override {
            double Wc = bc.x / clip[0].w + bc.y / clip[1].w + bc.z / clip[2].w;
            vec2 uv = (vuv[0] * (bc.x / clip[0].w) + vuv[1] * (bc.y / clip[1].w) +
                       vuv[2] * (bc.z / clip[2].w)) / Wc;
            vec3 n_smooth = normalized((vn[0] * (bc.x / clip[0].w) + vn[1] * (bc.y / clip[1].w) +
                                        vn[2] * (bc.z / clip[2].w)) / W);
            // lesson3 frame: tangent orthogonalized against the smooth normal
            vec3 n_obj = n_smooth;
            vec3 T = (vt[0] * (bc.x / clip[0].w) + vt[1] * (bc.y / clip[1].w) +
                      vt[2] * (bc.z / clip[2].w)) / Wc;
            T = T - n_smooth * dot(T, n_smooth);
            if (dot(T, T) > 1e-24) {
                T = normalized(T);
                vec3 B = cross(n_smooth, T);
                double worst = std::max(std::abs(dot(T, n_smooth)), std::abs(dot(B, n_smooth)));
                if (worst > ortho_err) ortho_err = worst;
                vec3 tn = nm->sample(uv.x, uv.y) / 255.0 * 2.0 - vec3(1, 1, 1);
                n_obj = T * tn.x + B * tn.y + n_smooth * tn.z;
            }
            vec4 nv = (*V) * hvec(n_obj, 0.0);
            vec3 n_view = normalized(vec3(nv.x, nv.y, nv.z));
            double ndl_raw = dot(n_view, L);
            double ndl = std::max(0.0, ndl_raw);

            // ---- shadow test: project into the light's screen ----
            vec4 sc = (scl[0] * (bc.x / clip[0].w) + scl[1] * (bc.y / clip[1].w) +
                       scl[2] * (bc.z / clip[2].w)) / Wc;
            double sx = (sc.x / sc.w + 1.0) * 0.5 * 1024;
            double sy = (1.0 - sc.y / sc.w) * 0.5 * 1024;      // row 0 = top, same raster map
            int ix = std::clamp(int(sx), 0, 1023), iy = std::clamp(int(sy), 0, 1023);
            double depth_light = 1.0 - sc.z / sc.w;            // same "larger = closer" flip
            double shadow_z = smap[size_t(iy) * 1024 + ix];
            bool in_shadow = (shadow_z - depth_light) > bias;
            double lit = in_shadow ? (1.0 - shadow_split) : 1.0;
            ++wr_total;
            if (ndl_raw > facing_limit) {
                ++facing_cnt;
                double delta = shadow_z - depth_light;
                dl_mean += delta;
                if (delta > dl_max) dl_max = delta;
                if (delta < dl_min) dl_min = delta;
                if (delta < 0) ++dl_neg; else if (delta < 0.01) ++dl_q1;
                else if (delta < 0.02) ++dl_q2; else if (delta < 0.05) ++dl_q3;
                else ++dl_hi;
            }
            if (in_shadow) {
                ++shadowed;
                if (ndl_raw > facing_limit) ++facing_shadowed;  // diagnostic: acne estimate
            }

            vec3 c = diffuse->sample(uv.x, uv.y) * (0.1 + 0.9 * ndl) * lit;
            if (!in_shadow) {
                vec3 r = n_view * (2.0 * ndl_raw) - L;          // reflect, RAW n.l
                double rv = std::max(0.0, r.z);
                double s = spec->scalar_sample(uv.x, uv.y) / 255.0;
                c = c + vec3(255, 255, 255) * (1.4 * s * std::pow(rv, 8.0));
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
    shader.ML = &ML;
    shader.smap = smap.data();
    shader.L = L;
    shader.bias = bias;

    TGAImage final_img(W, H, 3);
    std::vector<double> zbuf(size_t(W) * H, 0.0);
    for (size_t i = 0; i < model.nfaces(); ++i) {
        for (int k = 0; k < 3; ++k) shader.clip[k] = shader.vertex(int(i), k);
        draw_triangle(shader, final_img, zbuf.data());
    }

    std::printf("bias=%.3f  shadow share %.2f%% (%lld/%lld), facing-but-shadowed %.2f%%\n",
                bias, 100.0 * shader.shadowed / double(shader.wr_total ? shader.wr_total : 1),
                shader.shadowed, shader.wr_total,
                100.0 * shader.facing_shadowed / double(shader.shadowed ? shader.shadowed : 1));
    if (shader.facing_cnt)
        std::printf("instrument: facing (shadow_z - depth_light): mean=%.4f  [%.4f, %.4f]  buckets: neg %.1f%% | <.01 %.1f%% | .01-.02 %.1f%% | .02-.05 %.1f%% | >.05 %.1f%%\n",
                    shader.dl_mean / shader.facing_cnt, shader.dl_min, shader.dl_max,
                    100.0 * shader.dl_neg / shader.facing_cnt, 100.0 * shader.dl_q1 / shader.facing_cnt,
                    100.0 * shader.dl_q2 / shader.facing_cnt, 100.0 * shader.dl_q3 / shader.facing_cnt,
                    100.0 * shader.dl_hi / shader.facing_cnt);
    std::printf("gate G2 frag frame worst |dot| = %.2e -> %s\n", shader.ortho_err,
                (shader.ortho_err < 1e-9) ? "OK" : "FAIL");
    bool g2 = shader.ortho_err < 1e-9;

    long long filled = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (final_img.at(x, y).x + final_img.at(x, y).y + final_img.at(x, y).z > 1e-6) ++filled;
    double cover = 100.0 * filled / double(W * H);
    double facing_total = double(shader.facing_cnt ? shader.facing_cnt : 1);
    double facing_shadow_share = 100.0 * shader.facing_shadowed / facing_total;
    bool g3 = facing_shadow_share > 5.0 && facing_shadow_share < 45.0;
    bool g4 = cover > 25.0 && cover < 70.0;
    std::printf("gate G3 facing-shadow share [5%%,45%%]: %.2f%% -> %s\n",
                facing_shadow_share, g3 ? "OK" : "FAIL");
    std::printf("gate G4 coverage: %.2f%% -> %s\n", cover, g4 ? "OK" : "FAIL");

    final_img.write(out_dir + "/lesson5_shadow.tga");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("wrote %s/lesson5_shadowmap.tga + %s/lesson5_shadow.tga (%lld ms)\n",
                out_dir.c_str(), out_dir.c_str(), (long long)ms);
    return (g1 && g2 && g3 && g4) ? 0 : 1;
}
