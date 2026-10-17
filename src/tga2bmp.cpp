// tga2bmp.cpp -- DEV-ONLY utility: convert TGA -> BMP so the desktop can preview.
// Not part of the renderer; not shipped with the skill's required outputs.
// Build: g++ -std=c++17 -O2 -Isrc tga2bmp.cpp -o out/tga2bmp
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "tga.h"

static uint32_t u32le(uint32_t v) { return v; }

int main(int argc, char **argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: tga2bmp in.tga out.bmp\n"); return 2; }
    TGAImage img;
    if (!img.read(argv[1])) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    int w = img.width(), h = img.height();
    int stride = (w * 3 + 3) & ~3;                       // rows padded to 4 bytes
    std::vector<uint8_t> pixels(stride * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            vec3 c = img.at(x, y);
            uint8_t *p = pixels.data() + y * stride + x * 3;
            p[0] = uint8_t(c.z); p[1] = uint8_t(c.y); p[2] = uint8_t(c.x);   // BMP stores BGR
        }
    }
    uint32_t filesize = 54 + u32le(uint32_t(pixels.size()));
    uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M';
    std::memcpy(header + 2, &filesize, 4);
    header[10] = 54;                                     // pixel data offset
    uint32_t ui = 40;             std::memcpy(header + 14, &ui, 4);          // info header size
    ui = uint32_t(w);             std::memcpy(header + 18, &ui, 4);
    ui = uint32_t(h);             std::memcpy(header + 22, &ui, 4);          // positive h: bottom-up
    ui = 1;                       std::memcpy(header + 26, &ui, 2);          // planes
    ui = 24;                      std::memcpy(header + 28, &ui, 2);          // bpp
    uint32_t datasize = u32le(uint32_t(pixels.size()));
    std::memcpy(header + 34, &datasize, 4);
    ui = 2835;                    std::memcpy(header + 38, &ui, 4);          // 72dpi, both axes

    FILE *f = std::fopen(argv[2], "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    std::fwrite(header, 1, 54, f);
    std::fwrite(pixels.data(), 1, pixels.size(), f);
    std::fclose(f);
    std::printf("%s -> %s (%dx%d)\n", argv[1], argv[2], w, h);
    return 0;
}
