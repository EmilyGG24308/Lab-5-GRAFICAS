#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>

// Ruido procedural calculado por código, sin ninguna imagen de por medio.
// Todo es determinista: misma posición + misma seed = mismo valor, por eso los
// renders salen iguales cada vez.
namespace noise {

// mezcla las coordenadas enteras hasta que parezcan random
inline uint32_t hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed + static_cast<uint32_t>(x) * 0x8da6b343u + static_cast<uint32_t>(y) * 0xd8163841u +
                 static_cast<uint32_t>(z) * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    h *= 0x2545f491u;
    h ^= h >> 13;
    return h;
}

inline float toUnit(uint32_t h) { return static_cast<float>(h & 0xffffffu) / 16777215.0f; }

// saca varios números random de un mismo hash
inline uint32_t nextHash(uint32_t h) {
    h ^= h << 13;
    h ^= h >> 17;
    h ^= h << 5;
    return h;
}

inline float gradDot(uint32_t h, float x, float y, float z) {
    // las 12 direcciones clásicas del Perlin noise
    switch (h & 15u) {
        case 0: case 12: return x + y;
        case 1: case 13: return -x + y;
        case 2: return x - y;
        case 3: return -x - y;
        case 4: return x + z;
        case 5: return -x + z;
        case 6: return x - z;
        case 7: return -x - z;
        case 8: return y + z;
        case 9: case 14: return -y + z;
        case 10: return y - z;
        default: return -y - z;
    }
}

// Perlin noise 3D. Devuelve más o menos [-1, 1].
// Uso 3D (y no lat/long) para que no se note el pellizco en los polos.
inline float perlin(const glm::vec3& p, uint32_t seed = 0u) {
    const int ix = static_cast<int>(std::floor(p.x));
    const int iy = static_cast<int>(std::floor(p.y));
    const int iz = static_cast<int>(std::floor(p.z));
    const float x = p.x - ix, y = p.y - iy, z = p.z - iz;
    // curva quintic: esconde la cuadrícula que se vería con una mezcla lineal
    const float u = x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
    const float v = y * y * y * (y * (y * 6.0f - 15.0f) + 10.0f);
    const float w = z * z * z * (z * (z * 6.0f - 15.0f) + 10.0f);

    const float n000 = gradDot(hash3(ix, iy, iz, seed), x, y, z);
    const float n100 = gradDot(hash3(ix + 1, iy, iz, seed), x - 1, y, z);
    const float n010 = gradDot(hash3(ix, iy + 1, iz, seed), x, y - 1, z);
    const float n110 = gradDot(hash3(ix + 1, iy + 1, iz, seed), x - 1, y - 1, z);
    const float n001 = gradDot(hash3(ix, iy, iz + 1, seed), x, y, z - 1);
    const float n101 = gradDot(hash3(ix + 1, iy, iz + 1, seed), x - 1, y, z - 1);
    const float n011 = gradDot(hash3(ix, iy + 1, iz + 1, seed), x, y - 1, z - 1);
    const float n111 = gradDot(hash3(ix + 1, iy + 1, iz + 1, seed), x - 1, y - 1, z - 1);

    const float a = n000 + u * (n100 - n000);
    const float b = n010 + u * (n110 - n010);
    const float c = n001 + u * (n101 - n001);
    const float d = n011 + u * (n111 - n011);
    const float e = a + v * (b - a);
    const float f = c + v * (d - c);
    return e + w * (f - e);
}

// fBm: suma varias octavas del noise, cada una más fina y más suave.
// Las octavas grandes dan la forma y las chiquitas el detalle.
inline float fbm(glm::vec3 p, int octaves, uint32_t seed = 0u, float gain = 0.5f) {
    float sum = 0.0f, amp = 0.5f, total = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * perlin(p, seed + static_cast<uint32_t>(i) * 131u);
        total += amp;
        amp *= gain;
        p = p * 2.03f + glm::vec3(17.1f, 3.7f, 9.2f);  // el offset evita que todas se alineen en el origen
    }
    return total > 0.0f ? sum / total : 0.0f;
}

// Ridged noise: 1 - |noise| hace crestas filosas donde el noise cruza por cero.
// Sirve para cordilleras y venas. Devuelve [0, 1].
inline float ridged(glm::vec3 p, int octaves, uint32_t seed = 0u) {
    float sum = 0.0f, amp = 0.5f, total = 0.0f, prev = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        float n = 1.0f - std::fabs(perlin(p, seed + static_cast<uint32_t>(i) * 257u));
        n *= n;
        sum += n * amp * prev;  // cada octava pesa más arriba de las crestas anteriores
        total += amp;
        prev = n;
        amp *= 0.5f;
        p = p * 2.07f + glm::vec3(5.3f, 11.9f, 2.1f);
    }
    return total > 0.0f ? sum / total : 0.0f;
}

// Regresa cuántas octavas valen la pena: si una octava es más fina que un píxel
// solo produce parpadeo y gasta CPU. footprint = tamaño del píxel sobre la superficie.
inline int octavesFor(float baseFreq, float footprint, int maxOctaves) {
    int oct = 1;
    float freq = baseFreq;
    while (oct < maxOctaves && freq * 2.03f * footprint < 0.5f) {
        freq *= 2.03f;
        ++oct;
    }
    return oct;
}

struct VoronoiResult {
    float f1;        // distancia a la celda más cercana
    float f2;        // distancia a la segunda más cercana
    uint32_t id;     // hash de la celda más cercana (color random por celda)
    glm::vec3 point; // el punto de esa celda
};

// Voronoi 3D: divide el espacio en celdas. f2 - f1 es casi 0 justo en el borde entre
// dos celdas, de ahí salen las líneas de los gránulos y las grietas de los cristales.
inline VoronoiResult voronoi(const glm::vec3& p, uint32_t seed = 0u, float jitter = 0.85f) {
    const int ix = static_cast<int>(std::floor(p.x));
    const int iy = static_cast<int>(std::floor(p.y));
    const int iz = static_cast<int>(std::floor(p.z));
    VoronoiResult r{1e9f, 1e9f, 0u, glm::vec3(0.0f)};
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                uint32_t h = hash3(ix + dx, iy + dy, iz + dz, seed);
                uint32_t h2 = nextHash(h);
                uint32_t h3 = nextHash(h2);
                glm::vec3 c(ix + dx + 0.5f + (toUnit(h) - 0.5f) * jitter,
                            iy + dy + 0.5f + (toUnit(h2) - 0.5f) * jitter,
                            iz + dz + 0.5f + (toUnit(h3) - 0.5f) * jitter);
                glm::vec3 d = c - p;
                float dist = d.x * d.x + d.y * d.y + d.z * d.z;
                if (dist < r.f1) {
                    r.f2 = r.f1;
                    r.f1 = dist;
                    r.id = h;
                    r.point = c;
                } else if (dist < r.f2) {
                    r.f2 = dist;
                }
            }
        }
    }
    r.f1 = std::sqrt(r.f1);
    r.f2 = std::sqrt(r.f2);
    return r;
}

// Riega puntos random sobre la esfera unitaria y llama a fn(centro, hash) con los que
// quedan cerca de "dir". Es lo que uso para flores, cráteres, nenúfares y manchas.
// Truco: tomo los puntos de una grilla 3D y solo dejo los que caen en la cáscara de la
// esfera; así no hay costuras ni polos raros como en lat/long.
template <class Fn>
inline void scatterOnSphere(const glm::vec3& dir, float scale, uint32_t seed, Fn&& fn) {
    const glm::vec3 q = dir * scale;
    const int ix = static_cast<int>(std::floor(q.x));
    const int iy = static_cast<int>(std::floor(q.y));
    const int iz = static_cast<int>(std::floor(q.z));
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                uint32_t h = hash3(ix + dx, iy + dy, iz + dz, seed);
                uint32_t h2 = nextHash(h);
                uint32_t h3 = nextHash(h2);
                glm::vec3 c(ix + dx + toUnit(h), iy + dy + toUnit(h2), iz + dz + toUnit(h3));
                float len = glm::length(c);
                if (std::fabs(len - scale) > 0.5f) continue;  // fuera de la cáscara
                fn(c / len, nextHash(h3));
            }
        }
    }
}

// Coordenadas 2D locales alrededor de un punto de la esfera (plano tangente).
// Con esto puedo dibujar una flor o un cráter como si la superficie fuera plana ahí.
inline glm::vec2 tangentCoords(const glm::vec3& dir, const glm::vec3& center, float scale) {
    glm::vec3 helper = std::fabs(center.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 t = glm::normalize(glm::cross(helper, center));
    glm::vec3 b = glm::cross(center, t);
    glm::vec3 d = dir - center;
    return glm::vec2(glm::dot(d, t), glm::dot(d, b)) * scale;
}

}  // namespace noise
