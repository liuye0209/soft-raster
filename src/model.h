// model.h -- tiny Wavefront .OBJ loader, no dependencies.
// Handles exactly what the course model (african_head) uses:
//   v / vt / vn records, f lines with "v/vt/vn" (or shorter) indices,
//   1-based indices (negative = relative), polygons of any arity -> fan triangulation.
// Also normalizes the model to a unit-ish sphere (radius ~1) centered at origin,
// so camera / shadow constants are independent of the input model's scale.
#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "geometry.h"

struct Model {
    struct Face {
        int v[3], t[3], n[3];    // vertex / texcoord / normal indices per corner
    };
    std::vector<vec3> verts, norms;
    std::vector<vec2> texs;
    std::vector<Face> faces;

    bool load(const char *path) {
        std::ifstream in(path);
        if (!in) return false;
        for (std::string line; std::getline(in, line);) {
            std::istringstream iss(line);
            std::string tag;
            iss >> tag;
            if (tag == "v")   { vec3 p; iss >> p.x >> p.y >> p.z; verts.push_back(p); }
            else if (tag == "vt") { vec2 t; iss >> t.x >> t.y; texs.push_back(t); }
            else if (tag == "vn") { vec3 n; iss >> n.x >> n.y >> n.z; norms.push_back(n); }
            else if (tag == "f") {
                // collect corner records "v/vt/vn" (any slot may be empty), fan-triangulate
                std::vector<int> vi, ti, ni;
                for (std::string spec; iss >> spec;) {
                    int a[3] = {0, 0, 0};
                    int slot = 0;
                    spec += '\0';
                    size_t start = 0;
                    for (size_t c = 0; c <= spec.size(); ++c) {
                        if (spec[c] == '/' || spec[c] == '\0') {
                            if (c > start && slot < 3) a[slot] = std::atoi(spec.substr(start, c - start).c_str());
                            start = c + 1;
                            ++slot;
                        }
                    }
                    auto rel = [&](int i, size_t sz) { return i < 0 ? int(sz + i) : i - 1; };
                    if (a[0] != 0) vi.push_back(rel(a[0], verts.size()));
                    if (a[1] != 0) ti.push_back(rel(a[1],  texs.size()));
                    if (a[2] != 0) ni.push_back(rel(a[2],  norms.size()));
                }
                if (vi.size() < 3) continue;                    // not a polygon, skip
                for (size_t k = 2; k < vi.size(); ++k) {        // fan triangulation
                    Face F;
                    F.v[0] = vi[0];     F.v[1] = vi[k - 1]; F.v[2] = vi[k];
                    F.t[0] = ti.size() > 0 ? ti[0]     : -1; F.t[1] = ti.size() > 0 ? ti[k - 1] : -1; F.t[2] = ti.size() > 0 ? ti[k] : -1;
                    F.n[0] = ni.size() > 0 ? ni[0]     : -1; F.n[1] = ni.size() > 0 ? ni[k - 1] : -1; F.n[2] = ni.size() > 0 ? ni[k] : -1;
                    faces.push_back(F);
                }
            }
            // s, g, usemtl, mtllib ... intentionally ignored (no "material magic")
        }
        return !faces.empty();
    }

    size_t nfaces() const { return faces.size(); }
    Face face(size_t i) const { return faces[i]; }

    void normalize() {    // center bbox at origin, i.e. scale max half-extent to ~1
        vec3 lo = verts[0], hi = verts[0];
        for (auto &v : verts) {
            lo.x = std::min(lo.x, v.x); lo.y = std::min(lo.y, v.y); lo.z = std::min(lo.z, v.z);
            hi.x = std::max(hi.x, v.x); hi.y = std::max(hi.y, v.y); hi.z = std::max(hi.z, v.z);
        }
        vec3 c = (lo + hi) * 0.5;
        double radius = std::max(std::max((hi - lo).x, (hi - lo).y), (hi - lo).z) * 0.5;
        if (radius < 1e-9) return;
        double s = 1.0 / radius;
        for (auto &v : verts) {
            v.x = (v.x - c.x) * s;
            v.y = (v.y - c.y) * s;
            v.z = (v.z - c.z) * s;
        }
    }
};
