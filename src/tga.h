// tga.h -- minimal Targa (TGA) reader/writer, no external libraries.
//
// Supports exactly what the course needs (and what the bundled assets use):
//   READ:  image types 2 (uncompressed true-color), 3 (uncompressed gray),
//          10 (RLE true-color), 11 (RLE gray); 8/16/24/32 bpp;
//          both origin bits (top-left / bottom-left).
//   WRITE: uncompressed type-2, origin bottom-left (y=0 is the LOWEST row),
//          24 or 32 bpp. That matches the classic tinyrenderer output style.
//
// Internal layout: data_ is a flat vector, pixel (x,y) sits at (y*w_ + x)*ch_.
// After read(), y=0 is ALWAYS the TOP row (raster convention) regardless of the
// file's origin bit, so drawing code never has to think about orientation again.
// OBJ vt convention: v=1 means TOP of the texture -> sample() maps v=1 to y=h-1.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "geometry.h"

struct TGAImage {
    int w_ = 0, h_ = 0, ch_ = 0;              // ch_: 1 = gray, 3 = RGB, 4 = RGBA
    std::vector<uint8_t> data_;

    TGAImage() {}
    TGAImage(int w, int h, int ch) : w_(w), h_(h), ch_(ch), data_(size_t(w) * h * ch, 0) {}
    TGAImage(int w, int h, int ch, const std::vector<uint8_t> &d)
        : w_(w), h_(h), ch_(ch), data_(d) {}

    int width() const { return w_; }
    int height() const { return h_; }
    int channels() const { return ch_; }

    uint8_t *raw(int x, int y) { return data_.data() + (size_t(y) * w_ + x) * ch_; }
    const uint8_t *raw(int x, int y) const { return data_.data() + (size_t(y) * w_ + x) * ch_; }

    // Sample a pixel as a vec3; gray images are replicated on all channels.
    vec3 at(int x, int y) const {
        x = std::clamp(x, 0, w_ - 1);
        y = std::clamp(y, 0, h_ - 1);
        const uint8_t *p = raw(x, y);
        if (ch_ == 1) return vec3(p[0], p[0], p[0]);
        if (ch_ == 3) return vec3(p[2], p[1], p[0]);          // TGA stores BGR
        return vec3(p[2], p[1], p[0]);                        // 32bpp: ignore alpha here
    }
    double alpha_at(int x, int y) const {                     // 0..1, 1.0 unless 32bpp
        if (ch_ != 4) return 1.0;
        x = std::clamp(x, 0, w_ - 1);
        y = std::clamp(y, 0, h_ - 1);
        return raw(x, y)[3] / 255.0;
    }
    // Bilinear sampling, uv in [0,1]^2 (u -> +x, v -> +y, v=1 = TOP row of the image;
    // memory is top-first, so v=1 -> memory row 0).
    vec3 sample(double u, double v) const {
        double x = std::clamp(u, 0.0, 1.0) * (w_ - 1);
        double y = (1.0 - std::clamp(v, 0.0, 1.0)) * (h_ - 1);
        int x0 = int(x), y0 = int(y);
        int x1 = std::min(x0 + 1, w_ - 1), y1 = std::min(y0 + 1, h_ - 1);
        double fx = x - x0, fy = y - y0;
        vec3 a = at(x0, y0), b = at(x1, y0), c = at(x0, y1), d = at(x1, y1);
        return a * ((1 - fx) * (1 - fy)) + b * (fx * (1 - fy)) + c * ((1 - fx) * fy) + d * (fx * fy);
    }
    // Channel-normalized scalar read (for gray maps like the specular one).
    double scalar_sample(double u, double v) const {
        vec3 s = sample(u, v);
        if (ch_ == 1) return s.x;
        return 0.299 * s.x + 0.587 * s.y + 0.114 * s.z;          // no sprintf magic
    }

    void set(int x, int y, const vec3 &c) {                    // colors float in [0,1] or 0..255 ints pass as-is
        x = std::clamp(x, 0, w_ - 1);
        y = std::clamp(y, 0, h_ - 1);
        uint8_t *p = raw(x, y);
        auto put = [&](int ch, double v) {
            p[ch] = uint8_t(std::clamp(v, 0.0, 255.0) + 0.5);
        };
        if (ch_ == 1) { double g = 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]; p[0] = uint8_t(std::clamp(g, 0.0, 255.0) + 0.5); }
        else { put(0, c[2]); put(1, c[1]); put(2, c[0]); }         // store BGR order
    }

    // ---- write: uncompressed, bottom-left origin ----
    bool write(const std::string &path) {
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        uint8_t header[18] = {};
        header[2] = 2;                                            // uncompressed true-color (works for gray too)
        header[12] = w_ & 0xFF;         header[13] = (w_ >> 8) & 0xFF;
        header[14] = h_ & 0xFF;         header[15] = (h_ >> 8) & 0xFF;
        header[16] = uint8_t(ch_ == 4 ? 32 : 24);                  // gray is stored as replicated 24bpp
        header[17] = 0x00;                                         // origin = bottom-left
        out.write(reinterpret_cast<const char *>(header), 18);
        if (ch_ == 1) {                                            // one-channel image written as true-color gray
            std::vector<uint8_t> row_bytes(size_t(w_) * 3);
            for (int y = h_ - 1; y >= 0; --y) {                    // bottom-left origin: bottom row first
                for (int x = 0; x < w_; ++x) {
                    vec3 g = at(x, y);
                    uint8_t v = uint8_t(std::clamp(g.x, 0.0, 255.0) + 0.5);
                    row_bytes[size_t(x) * 3 + 0] = v;
                    row_bytes[size_t(x) * 3 + 1] = v;
                    row_bytes[size_t(x) * 3 + 2] = v;
                }
                out.write(reinterpret_cast<const char *>(row_bytes.data()), std::streamsize(row_bytes.size()));
            }
        } else if (ch_ == 3) {
            for (int y = h_ - 1; y >= 0; --y)                      // bottom-first row order
                out.write(reinterpret_cast<const char *>(raw(0, y)), std::streamsize(w_) * 3);
        } else {
            for (int y = h_ - 1; y >= 0; --y)
                out.write(reinterpret_cast<const char *>(raw(0, y)), std::streamsize(w_) * 4);
        }
        return bool(out);
    }

    // ---- read ----
    bool read(const std::string &path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        uint8_t header[18];
        in.read(reinterpret_cast<char *>(header), 18);
        if (!in) return false;

        int id_len = header[0];
        int cmap_type = header[1];                                 // want 0 for our assets
        int img_type = header[2];
        int w = header[12] + (header[13] << 8);
        int h = header[14] + (header[15] << 8);
        int bpp = header[16];
        int desc = header[17];

        if (cmap_type != 0) { std::fprintf(stderr, "TGA: paletted (%d) not supported: %s\n", cmap_type, path.c_str()); return false; }
        if (img_type != 2 && img_type != 3 && img_type != 10 && img_type != 11) {
            std::fprintf(stderr, "TGA: image type %d not supported\n", img_type);
            return false;
        }
        in.ignore(id_len);                                         // skip the image-id string

        int nch = (bpp == 8) ? 1 : (bpp == 24 ? 3 : (bpp == 32 ? 4 : 0));
        if (nch == 0 || w <= 0 || h <= 0) return false;

        w_ = w; h_ = h; ch_ = nch;
        data_.assign(size_t(w) * h * nch, 0);

        auto put_pixel = [&](uint64_t i, const uint8_t *px) {
            std::memcpy(data_.data() + i * nch, px, nch);
        };

        bool rle = (img_type & 8) != 0;
        uint64_t npix = uint64_t(w) * h;
        if (!rle) {
            std::vector<uint8_t> px(nch);
            for (uint64_t i = 0; i < npix; ++i) {
                in.read(reinterpret_cast<char *>(px.data()), nch);
                put_pixel(i, px.data());
            }
        } else {                                                    // RLE: packet = [ctrl][1..n pixels]
            uint64_t i = 0;
            std::vector<uint8_t> px(nch);
            while (i < npix) {
                uint8_t ctrl = in.get();
                bool raw_packet = (ctrl & 0x80) == 0;
                int runlen = (ctrl & 0x7F) + 1;
                if (raw_packet) {
                    for (int k = 0; k < runlen && i < npix; ++k, ++i) {
                        in.read(reinterpret_cast<char *>(px.data()), nch);
                        put_pixel(i, px.data());
                    }
                } else {
                    in.read(reinterpret_cast<char *>(px.data()), nch);
                    for (int k = 0; k < runlen && i < npix; ++k, ++i) put_pixel(i, px.data());
                }
            }
        }
        if (!in) { std::fprintf(stderr, "TGA: %s ended early\n", path.c_str()); return false; }

        // Canonicalize origin: memory must be TOP-first. Descriptor bit 5 (0x20)
        // set means the file already stores rows top-first; otherwise flip.
        if (!(desc & 0x20)) {
            std::vector<uint8_t> row(size_t(w) * nch);
            int half = h / 2;
            for (int y = 0; y < half; ++y) {
                uint8_t *a = data_.data() + (size_t(y) * w) * nch;
                uint8_t *b = data_.data() + (size_t(h - 1 - y) * w) * nch;
                std::memcpy(row.data(), a, row.size());
                std::memcpy(a, b, row.size());
                std::memcpy(b, row.data(), row.size());
            }
        }
        return true;
    }
};
