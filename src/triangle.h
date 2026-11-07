#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>

#include "framebuffer.h"

// Lo que sale del vertex shader, ya en pantalla.
// Guardo cada varying dividido entre w: así se puede interpolar lineal en
// pantalla y después recuperar el valor real (perspective-correct).
struct ScreenVertex {
    float x, y;       // píxeles (y crece hacia abajo)
    float z;          // profundidad NDC (menor = más cerca)
    float invW;       // 1 / w
    glm::vec3 obj;    // posición en object space / w
    glm::vec3 world;  // posición en world space / w
    glm::vec3 normal; // normal en world space / w
};

// Datos que recibe el fragment shader ya interpolados.
struct Fragment {
    glm::vec3 objPos;
    glm::vec3 worldPos;
    glm::vec3 normal;
};

// Triángulo listo para rasterizar. Uso fixed-point (8 bits de subpíxel) porque
// con enteros el borde compartido entre dos triángulos da EXACTO: ni huecos ni
// píxeles pintados dos veces. Eso importa porque los anillos llevan blending y
// un píxel doble se vería como una línea más brillante.
struct ScreenTriangle {
    ScreenVertex a, b, c;
    int64_t ax, ay, bx, by, cx, cy;
    int64_t area;
    int minX, minY, maxX, maxY;
    int draw;  // índice del draw call al que pertenece
};

constexpr int SUBPIXEL_BITS = 8;
constexpr int64_t SUBPIXEL_ONE = 1 << SUBPIXEL_BITS;

inline int64_t toFixed(float v) { return static_cast<int64_t>(v * SUBPIXEL_ONE + (v < 0 ? -0.5f : 0.5f)); }

inline int64_t edgeFn(int64_t px, int64_t py, int64_t qx, int64_t qy, int64_t x, int64_t y) {
    return (qx - px) * (y - py) - (qy - py) * (x - px);
}

// Prepara el triángulo. Devuelve false si es degenerado, si queda fuera de
// pantalla o si mira para atrás (back-face culling) y no es de dos caras.
inline bool setupTriangle(ScreenTriangle& t, int fbWidth, int fbHeight, bool twoSided) {
    t.ax = toFixed(t.a.x); t.ay = toFixed(t.a.y);
    t.bx = toFixed(t.b.x); t.by = toFixed(t.b.y);
    t.cx = toFixed(t.c.x); t.cy = toFixed(t.c.y);

    t.area = edgeFn(t.ax, t.ay, t.bx, t.by, t.cx, t.cy);
    if (t.area == 0) return false;
    if (t.area < 0) {
        if (!twoSided) return false;
        // en los anillos no hay "atrás": le doy vuelta al triángulo y listo
        std::swap(t.b, t.c);
        std::swap(t.bx, t.cx);
        std::swap(t.by, t.cy);
        t.area = -t.area;
    }

    t.minX = std::max(0, static_cast<int>(std::min({t.ax, t.bx, t.cx}) >> SUBPIXEL_BITS));
    t.minY = std::max(0, static_cast<int>(std::min({t.ay, t.by, t.cy}) >> SUBPIXEL_BITS));
    t.maxX = static_cast<int>(std::min<int64_t>(fbWidth - 1, std::max({t.ax, t.bx, t.cx}) >> SUBPIXEL_BITS));
    t.maxY = static_cast<int>(std::min<int64_t>(fbHeight - 1, std::max({t.ay, t.by, t.cy}) >> SUBPIXEL_BITS));
    return t.minX <= t.maxX && t.minY <= t.maxY;
}

// Rasteriza la parte del triángulo que cae en las filas [bandMinY, bandMaxY].
// Es el mismo método del Lab 4 (bounding box + funciones de arista), pero:
//  - interpola los varyings con perspectiva correcta
//  - llama al fragment shader por cada píxel que pasa el z-test (early-z, así no
//    gasto noise en píxeles tapados)
//  - la banda deja dibujar con varios hilos sin que se pisen entre sí
template <class ShadeFn>
inline void triangle(Framebuffer& fb, const ScreenTriangle& t, int bandMinY, int bandMaxY,
                     bool transparent, ShadeFn&& shade) {
    int y0 = std::max(t.minY, bandMinY);
    int y1 = std::min(t.maxY, bandMaxY);
    if (y0 > y1) return;

    // Regla top-left: un píxel justo sobre un borde solo lo pinta uno de los dos
    // triángulos que lo comparten. El bias de -1 saca los bordes que no son top/left.
    auto bias = [](int64_t px, int64_t py, int64_t qx, int64_t qy) -> int64_t {
        int64_t dx = qx - px, dy = qy - py;
        bool topLeft = (dy == 0 && dx > 0) || dy < 0;
        return topLeft ? 0 : -1;
    };
    const int64_t b0 = bias(t.bx, t.by, t.cx, t.cy);
    const int64_t b1 = bias(t.cx, t.cy, t.ax, t.ay);
    const int64_t b2 = bias(t.ax, t.ay, t.bx, t.by);

    const float invArea = 1.0f / static_cast<float>(t.area);
    const int64_t half = SUBPIXEL_ONE / 2;

    int64_t py = (static_cast<int64_t>(y0) << SUBPIXEL_BITS) + half;
    int64_t px = (static_cast<int64_t>(t.minX) << SUBPIXEL_BITS) + half;
    int64_t row0 = edgeFn(t.bx, t.by, t.cx, t.cy, px, py) + b0;
    int64_t row1 = edgeFn(t.cx, t.cy, t.ax, t.ay, px, py) + b1;
    int64_t row2 = edgeFn(t.ax, t.ay, t.bx, t.by, px, py) + b2;

    // cuánto cambia cada arista al moverse un píxel en x / y
    const int64_t dx0 = -(t.cy - t.by) * SUBPIXEL_ONE, dy0 = (t.cx - t.bx) * SUBPIXEL_ONE;
    const int64_t dx1 = -(t.ay - t.cy) * SUBPIXEL_ONE, dy1 = (t.ax - t.cx) * SUBPIXEL_ONE;
    const int64_t dx2 = -(t.by - t.ay) * SUBPIXEL_ONE, dy2 = (t.bx - t.ax) * SUBPIXEL_ONE;

    for (int y = y0; y <= y1; ++y, row0 += dy0, row1 += dy1, row2 += dy2) {
        int64_t e0 = row0, e1 = row1, e2 = row2;
        for (int x = t.minX; x <= t.maxX; ++x, e0 += dx0, e1 += dx1, e2 += dx2) {
            if ((e0 | e1 | e2) < 0) continue;  // algún signo negativo = fuera

            float w0 = static_cast<float>(e0) * invArea;
            float w1 = static_cast<float>(e1) * invArea;
            float w2 = static_cast<float>(e2) * invArea;

            float depth = w0 * t.a.z + w1 * t.b.z + w2 * t.c.z;
            size_t i = fb.index(x, y);
            if (depth >= fb.zbuffer[i]) continue;

            // volver a dividir entre 1/w para recuperar los valores reales
            float invW = w0 * t.a.invW + w1 * t.b.invW + w2 * t.c.invW;
            float W = 1.0f / invW;
            Fragment f;
            f.objPos = (t.a.obj * w0 + t.b.obj * w1 + t.c.obj * w2) * W;
            f.worldPos = (t.a.world * w0 + t.b.world * w1 + t.c.world * w2) * W;
            f.normal = (t.a.normal * w0 + t.b.normal * w1 + t.c.normal * w2) * W;

            glm::vec4 out = shade(f);

            if (transparent) {
                // los anillos y glows no escriben z: no deben tapar lo que venga atrás
                fb.blend(i, out.r, out.g, out.b, out.a);
            } else {
                fb.zbuffer[i] = depth;
                fb.pixels[i] = Color(static_cast<int>(out.r * 255.0f + 0.5f),
                                     static_cast<int>(out.g * 255.0f + 0.5f),
                                     static_cast<int>(out.b * 255.0f + 0.5f));
            }
        }
    }
}
