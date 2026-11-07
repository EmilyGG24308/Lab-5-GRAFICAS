// EL VELO, los anillos de Peonía. Tienen malla propia (generateRing) y todo el
// color sale de la distancia al centro del planeta, sin ninguna textura radial.
//
// Capas que afectan el color:
//   1. Densidad por bandas (más o menos polvo según el radio, con huecos limpios)
//   2. Paleta radial (rosa, celeste y crema según la banda)
//   3. Iridiscencia que cambia con el ángulo de la cámara
//   4. Ondas de densidad y granitos brillantes alrededor del anillo
//   + la sombra que el planeta proyecta sobre el anillo
#include "shader.h"

using namespace glm;

namespace {
const float SHEEN = 0.34f;      // cuánta iridiscencia
const float SPARKLE = 0.32f;    // cuánto brillan los granitos
}  // namespace

// t = 0 en el borde interno, t = 1 en el borde externo.
float ringDensity(float t, uint32_t seed) {
    // varias frecuencias de noise 1D: un valor por cada radio
    float wide = noise::perlin(vec3(t * 7.0f, 1.3f, 0.0f), seed + 41u);
    float fine = noise::perlin(vec3(t * 38.0f, 4.1f, 0.0f), seed + 42u);
    float thin = noise::perlin(vec3(t * 110.0f, 7.7f, 0.0f), seed + 43u);
    float density = 0.62f + wide * 0.55f + fine * 0.32f + thin * 0.18f;

    // huecos: como la división de Cassini en Saturno. Para moverlos, cambiar los centros.
    density *= smoothstep(0.012f, 0.03f, std::fabs(t - 0.62f));
    density *= 0.25f + 0.75f * smoothstep(0.004f, 0.018f, std::fabs(t - 0.27f));
    density *= 0.45f + 0.55f * smoothstep(0.002f, 0.009f, std::fabs(t - 0.83f));

    // los bordes se desvanecen para no tener un corte duro
    density *= smoothstep(0.0f, 0.05f, t) * (1.0f - smoothstep(0.93f, 1.0f, t));
    return shade::saturate(density);
}

vec4 ringShader(const Fragment& f, const Uniforms& u) {
    const float r = length(vec2(f.objPos.x, f.objPos.z));
    const float t = (r - u.ringInner) / (u.ringOuter - u.ringInner);
    if (t <= 0.0f || t >= 1.0f) return vec4(0.0f);

    const vec3 V = normalize(u.cameraPos - f.worldPos);
    const vec3 L = normalize(u.lightPos - f.worldPos);
    const float fp = shade::footprint(f, u);

    // --- Capa 1: densidad ---
    float density = ringDensity(t, u.seed);

    // --- Capa 2: paleta radial ---
    float tone = t * 4.2f + noise::perlin(vec3(t * 11.0f, 9.0f, 0.0f), u.seed + 44u) * 0.7f;
    // mezclo solo colores elegidos: con una paleta de cosenos se cuela el verde
    const vec3 pink(1.0f, 0.74f, 0.86f), sky(0.68f, 0.86f, 1.0f), cream(1.0f, 0.94f, 0.90f);
    float wave = 0.5f + 0.5f * std::cos(tone * 6.2832f);
    vec3 color = mix(pink, sky, smoothstep(0.2f, 0.8f, wave));
    color = mix(color, cream, smoothstep(0.55f, 0.95f, 0.5f + 0.5f * std::sin(tone * 13.0f)) * 0.6f);

    // --- Capa 3: iridiscencia ---
    float ndv = std::fabs(dot(u.ringNormal, V));
    float angleTone = 0.5f + 0.5f * std::cos((ndv * 2.0f + t * 2.6f) * 6.2832f);
    color = mix(color, mix(vec3(0.84f, 0.76f, 1.0f), vec3(1.0f, 0.86f, 0.94f), angleTone), SHEEN);

    // --- Capa 4: ondas angulares y granitos ---
    // uso x y z tal cual (no un ángulo), así no aparece una costura en theta = pi
    vec2 flat2(f.objPos.x, f.objPos.z);
    float spin = u.time * 0.05f / std::max(r, 0.1f);
    vec2 rotated(flat2.x * std::cos(spin) - flat2.y * std::sin(spin), flat2.x * std::sin(spin) + flat2.y * std::cos(spin));
    float clumps = noise::perlin(vec3(rotated * 2.6f, t * 16.0f), u.seed + 45u);
    density *= 0.86f + clumps * 0.28f;
    if (fp * 90.0f < 0.5f) {
        float grain = noise::perlin(vec3(rotated * 90.0f, 1.0f), u.seed + 46u);
        color += vec3(1.0f, 0.97f, 0.98f) * smoothstep(0.34f, 0.62f, grain) * SPARKLE;
    }

    // sombra del planeta sobre el anillo: la misma idea del shader de la luna
    float shadow = shade::sphereShadow(f.worldPos, L, u.occluderCenter, u.occluderRadius);
    color *= mix(vec3(0.20f, 0.17f, 0.36f), vec3(1.05f), shadow);

    float alpha = shade::saturate(density * 0.94f);
    return vec4(color * alpha, alpha);  // premultiplicado
}
