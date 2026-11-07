// JAZMÍN, la luna de Malva. Es perla pálida: muy clarita para que contraste con el
// lila del planeta. Es un objeto aparte con su propia órbita y su propio shader.
//
// Capas que afectan el color:
//   1. Mares suaves de color azul pálido
//   2. Cráteres con rayos de salpicadura
//   3. Polvo fino (variación de grano)
//   4. Brillo perlado en el borde, que cambia con el ángulo
#include "shader.h"

using namespace glm;

namespace {
const float MARIA_SCALE = 1.9f;      // más alto = mares más pequeños
const float CRATER_SCALE = 8.0f;     // más alto = cráteres más pequeños
const float CRATER_DENSITY = 0.45f;  // de 0 a 1, qué tan lleno de cráteres
const float PEARL_STRENGTH = 0.55f;  // brillo perlado del borde
}  // namespace

vec4 moonShader(const Fragment& f, const Uniforms& u) {
    const vec3 dir = normalize(f.objPos);
    const vec3 N = normalize(f.normal);
    const vec3 V = normalize(u.cameraPos - f.worldPos);
    const vec3 L = normalize(u.lightPos - f.worldPos);
    const float fp = shade::footprint(f, u);

    // --- Capa 1: mares ---
    float maria = noise::fbm(dir * MARIA_SCALE, noise::octavesFor(MARIA_SCALE, fp, 5), u.seed + 1u);
    vec3 albedo = mix(vec3(0.96f, 0.94f, 0.96f), vec3(0.72f, 0.80f, 0.94f), smoothstep(-0.02f, 0.18f, maria));

    // --- Capa 3: polvo ---
    float dust = noise::fbm(dir * 16.0f, noise::octavesFor(16.0f, fp, 4), u.seed + 2u);
    albedo *= 0.96f + dust * 0.14f;

    // --- Capa 2: cráteres con rayos ---
    vec3 slope(0.0f);
    noise::scatterOnSphere(dir, CRATER_SCALE, u.seed + 3u, [&](const vec3& center, uint32_t h) {
        if (noise::toUnit(h) > CRATER_DENSITY) return;
        h = noise::nextHash(h);
        float radius = (0.14f + 0.28f * noise::toUnit(h)) / CRATER_SCALE;
        vec3 delta = dir - center;
        float dist = length(delta);
        float d = dist / radius;
        // los rayos solo salen de algunos cráteres; si no, parece una telaraña
        bool hasRays = (noise::nextHash(h) % 5u) == 0u;
        if (d > (hasRays ? 5.0f : 1.15f)) return;
        // el ángulo define unos rayos finos que salen del cráter
        vec2 uv = noise::tangentCoords(dir, center, 1.0f);
        float angle = std::atan2(uv.y, uv.x);
        float rays = smoothstep(0.75f, 1.0f, std::sin(angle * 13.0f + noise::toUnit(h) * 30.0f));
        float rayMask = hasRays ? rays * (1.0f - smoothstep(1.0f, 5.0f, d)) * step(1.0f, d) : 0.0f;
        albedo = mix(albedo, vec3(1.0f), rayMask * 0.5f);
        if (d < 1.15f) {
            albedo = mix(albedo, albedo * vec3(0.80f, 0.80f, 0.92f), 1.0f - smoothstep(0.4f, 0.95f, d));
            float rim = std::exp(-(d - 1.0f) * (d - 1.0f) * 42.0f);
            albedo = mix(albedo, vec3(1.0f), rim * 0.55f);
            if (dist > 1e-5f) slope += (delta / dist) * (d < 1.0f ? d * 0.5f : -rim);
        }
    });

    vec3 Nbump = normalize(N - u.rotation * slope);
    float shadow = shade::sphereShadow(f.worldPos, L, u.occluderCenter, u.occluderRadius);
    vec3 lit = shade::pastelLight(albedo, Nbump, L, V, shadow);

    // --- Capa 4: brillo perlado ---
    // fresnel: entre más rasante la vista, más sube. La paleta es rosa-celeste, como perla.
    float fresnel = std::pow(1.0f - shade::saturate(dot(N, V)), 2.6f);
    vec3 pearl = shade::cosPalette(fresnel * 1.3f + dust * 0.4f, vec3(0.86f, 0.86f, 0.95f), vec3(0.14f, 0.14f, 0.05f), vec3(1.0f), vec3(0.0f, 0.33f, 0.6f));
    lit = mix(lit, pearl * (0.45f + 0.55f * shadow), fresnel * PEARL_STRENGTH);

    return shade::opaque(lit);
}
