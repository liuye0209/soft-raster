// geometry.h -- hand-rolled math for the soft-raster course (vec2/vec3/vec4/mat4).
// No external libraries. Everything here is standard C++ math you can check by hand.
//
// Coordinate conventions used across the course (IMPORTANT to keep coherent):
//   * Model space: Y up, Z towards viewer (right-handed), model normalized to radius ~1.
//   * view/clip: right-handed, camera looks down -Z, NDC z in [-1,+1]: -1 = near, +1 = far
//     (this is the standard GL/right-handed convention).
//   * DEPTH for the z-buffer: flipping z in viewport() gives depth in [0,2] where
//     depth larger = CLOSER (near plane -> 2, far plane -> 0). The z-buffer test
//     is therefore: draw if depth > buffer[].z, buffer initialized to 0.
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

constexpr double PI = 3.14159265358979323846;   // no M_PI (not standard C++)

struct vec2 {
    double x = 0, y = 0;
    vec2() {}
    vec2(double X, double Y) : x(X), y(Y) {}
    vec2 operator+(const vec2 &b) const { return vec2(x + b.x, y + b.y); }
    vec2 operator-(const vec2 &b) const { return vec2(x - b.x, y - b.y); }
    vec2 operator*(double s)        const { return vec2(x * s, y * s); }
    vec2 operator/(double s)        const { return vec2(x / s, y / s); }
    double &operator[](int i)             { return i == 0 ? x : y; }
    double operator[](int i)        const { return i == 0 ? x : y; }
};

struct vec3 {
    double x = 0, y = 0, z = 0;
    vec3() {}
    vec3(double X, double Y, double Z) : x(X), y(Y), z(Z) {}
    vec3 operator+(const vec3 &b) const { return vec3(x + b.x, y + b.y, z + b.z); }
    vec3 operator-(const vec3 &b) const { return vec3(x - b.x, y - b.y, z - b.z); }
    vec3 operator-()                const { return vec3(-x, -y, -z); }
    vec3 operator*(double s)        const { return vec3(x * s, y * s, z * s); }
    vec3 operator/(double s)        const { return vec3(x / s, y / s, z / s); }
    vec3 &operator+=(const vec3 &b)       { x += b.x; y += b.y; z += b.z; return *this; }
    double &operator[](int i)             { return i == 0 ? x : (i == 1 ? y : z); }
    double operator[](int i)        const { return i == 0 ? x : (i == 1 ? y : z); }
};

inline vec3 operator*(double s, const vec3 &a) { return a * s; }
inline double dot(const vec3 &a, const vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(const vec3 &a, const vec3 &b) {
    return vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}
inline vec3 normalized(const vec3 &v) {
    double n = std::sqrt(dot(v, v));
    return n < 1e-12 ? vec3(0, 0, 0) : vec3(v.x / n, v.y / n, v.z / n);
}

struct vec4 {
    double x = 0, y = 0, z = 0, w = 0;
    vec4() {}
    vec4(double X, double Y, double Z, double W) : x(X), y(Y), z(Z), w(W) {}
    double &operator[](int i) { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }
    double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }
    vec4 operator*(double s) const { return vec4(x * s, y * s, z * s, w * s); }
    vec4 operator+(const vec4 &b) const { return vec4(x + b.x, y + b.y, z + b.z, w + b.w); }
    vec4 operator/(double s) const { return vec4(x / s, y / s, z / s, w / s); }
};

// Row-major 4x4 matrix; transforms are applied as v' = M * (x,y,z,1) with column vectors.
// Composition: (A*B) means "B first, then A" (reads right to left, like GLSL).
struct mat4 {
    double m[4][4] = {};
    mat4() { for (int i = 0; i < 4; ++i) m[i][i] = 1.0; }

    mat4 operator*(const mat4 &b) const {
        mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                double s = 0;
                for (int k = 0; k < 4; ++k) s += m[i][k] * b.m[k][j];
                r.m[i][j] = s;
            }
        return r;
    }
    vec4 operator*(const vec4 &v) const {   // matrix * column vector
        vec4 r;
        for (int i = 0; i < 4; ++i)
            r[i] = m[i][0] * v.x + m[i][1] * v.y + m[i][2] * v.z + m[i][3] * v.w;
        return r;
    }
};

inline vec4 hvec(const vec3 &v, double w = 1.0) { return vec4(v.x, v.y, v.z, w); }

// NDC->screen, y INVERTED (memory row 0 = image top) and z flipped:
// depth == larger when closer (see header comment).
//   x_ndc,y_ndc in [-1,1] -> [x..x+w], [y..y+h] with +1 -> row y (TOP)
//   z_ndc  in [-1,1]      -> [0..2],  near(-1) -> 2, far(+1) -> 0
inline mat4 viewport(int x, int y, int w, int h, int depth = 2) {
    double s = depth / 2.0;
    mat4 V;
    V.m[0][0] = w / 2.0;  V.m[0][3] = x + w / 2.0;
    V.m[1][1] = -h / 2.0; V.m[1][3] = y + h / 2.0;          // -1: ndc up = small row index
    V.m[2][2] = -s;       V.m[2][3] = s;                    // flip z
    return V;
}

// Right-handed "look at" (eye looks towards -z after this transform).
inline mat4 lookat(const vec3 &eye, const vec3 &center, const vec3 &up) {
    vec3 f = normalized(center - eye);       // forward (towards the scene)
    vec3 s = normalized(cross(f, up));       // side
    vec3 u = normalized(cross(s, f));        // recomputed up
    mat4 M;
    M.m[0][0] = s.x;  M.m[0][1] = s.y;  M.m[0][2] = s.z;  M.m[0][3] = -dot(s, eye);
    M.m[1][0] = u.x;  M.m[1][1] = u.y;  M.m[1][2] = u.z;  M.m[1][3] = -dot(u, eye);
    M.m[2][0] = -f.x; M.m[2][1] = -f.y; M.m[2][2] = -f.z; M.m[2][3] = dot(f, eye);
    return M;
}

// Standard perspective projection (right-handed, camera looks down -z).
inline mat4 perspective(double fov_deg, double aspect, double near_p, double far_p) {
    double f = 1.0 / std::tan(fov_deg * PI / 180.0 / 2.0);
    mat4 P;
    P.m[0][0] = f / aspect;
    P.m[1][1] = f;
    P.m[2][2] = (far_p + near_p) / (near_p - far_p);
    P.m[2][3] = 2.0 * far_p * near_p / (near_p - far_p);
    P.m[3][2] = -1.0;
    P.m[3][3] = 0.0;                 // mat4() starts as identity: FORGET THIS and clip_w = 1 - z_view (classic bug!)
    return P;
}

// Orthographic projection: keeps straight lines parallel; depth linear in z.
inline mat4 orthographic(double l, double r, double b, double t, double n, double f) {
    mat4 O;
    O.m[0][0] = 2.0 / (r - l);      O.m[0][3] = -(r + l) / (r - l);
    O.m[1][1] = 2.0 / (t - b);      O.m[1][3] = -(t + b) / (t - b);
    O.m[2][2] = -2.0 / (f - n);     O.m[2][3] = -(f + n) / (f - n);
    return O;
}
