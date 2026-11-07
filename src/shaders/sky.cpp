// Cielo de fondo: un atardecer espacial. Oscurito para que los pasteles se luzcan,
// pero sin llegar a negro puro, porque eso rompería la vibe de cuento.
#include "shader.h"

using namespace glm;

namespace {
const uint32_t SKY_SEED = 9091u;
}

// Nebulosa: es tan suave que el pipeline solo la calcula cada 8 píxeles y la interpola.
// Eso le quita casi todo el costo al fondo y no se nota ninguna diferencia.
vec3 nebulaShader(const vec3& dir) {
    // dos capas de noise con warp, una rosa y otra celeste
    float warp = noise::fbm(dir * 2.2f, 4, SKY_SEED);
    float pinkCloud = smoothstep(-0.06f, 0.42f, noise::fbm(dir * 1.7f + vec3(warp), 5, SKY_SEED + 1u));
    float blueCloud = smoothstep(-0.02f, 0.42f, noise::fbm(dir * 2.1f + vec3(4.0f) - vec3(warp), 5, SKY_SEED + 2u));

    vec3 color(0.070f, 0.060f, 0.135f);
    color = mix(color, vec3(0.255f, 0.130f, 0.280f), pinkCloud * 0.75f);
    color = mix(color, vec3(0.120f, 0.195f, 0.340f), blueCloud * 0.70f);
    return color;
}

vec3 starsShader(const vec3& dir, float pixelSize, int x, int y) {
    vec3 color(0.0f);

    // estrellitas: una por celda como máximo, con posición random dentro de la celda.
    // El radio depende del pixelSize para que midan lo mismo en cualquier resolución.
    const float scale = 150.0f;
    vec3 q = dir * scale;
    int ix = static_cast<int>(std::floor(q.x)), iy = static_cast<int>(std::floor(q.y)), iz = static_cast<int>(std::floor(q.z));
    uint32_t h = noise::hash3(ix, iy, iz, SKY_SEED + 3u);
    if (noise::toUnit(h) < 0.16f) {
        uint32_t a = noise::nextHash(h), b = noise::nextHash(a), c = noise::nextHash(b), d = noise::nextHash(c);
        vec3 star(ix + 0.2f + 0.6f * noise::toUnit(a), iy + 0.2f + 0.6f * noise::toUnit(b), iz + 0.2f + 0.6f * noise::toUnit(c));
        float size = pixelSize * scale * (0.6f + 1.3f * noise::toUnit(d));
        float brightness = 1.0f - smoothstep(size * 0.2f, size, length(q - star));
        if (brightness > 0.0f) {
            vec3 tint = noise::toUnit(noise::nextHash(d)) < 0.5f ? vec3(1.0f, 0.86f, 0.92f) : vec3(0.84f, 0.92f, 1.0f);
            color += tint * brightness * (0.35f + 0.65f * noise::toUnit(d));
        }
    }

    // dithering chiquito: sin esto, el degradado oscuro se ve con escalones en 8 bits
    float dither = fract(52.9829189f * fract(0.06711056f * x + 0.00583715f * y)) - 0.5f;
    color += vec3(dither / 255.0f);
    return color;
}
