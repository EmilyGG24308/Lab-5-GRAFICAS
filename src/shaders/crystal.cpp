// HORTENSIA, el mundo de cristal de azúcar. Se llama así porque esa flor es rosa o
// azul según el suelo, y este planeta hace lo mismo pero según el ángulo de la luz.
//
// Capas que afectan el color:
//   1. Facetas (cada celda de Voronoi tiene su normal plana y su propio tono)
//   2. Película iridiscente (el color se corre con el ángulo de la cámara)
//   3. Grietas brillantes entre facetas (f2 - f1)
//   4. Vetas internas con parallax (parece que hay algo DENTRO del cristal)
//   5. Escarcha polar y destellos sobre los bordes
#include "shader.h"

using namespace glm;

namespace {

// --- Parámetros para ajustar el look ---
const float FACET_SCALE = 7.0f;        // más alto = facetas más chiquitas
const float FACET_TILT = 0.34f;        // qué tanto se inclina cada faceta (más = más contraste)
const float CRACK_WIDTH = 0.045f;      // grosor de las grietas
const float PARALLAX_DEPTH = 0.09f;    // qué tan "adentro" se ven las vetas
const float FROST_START = 0.77f;       // latitud donde empieza la escarcha

vec3 hashDirection(uint32_t h) {
    uint32_t a = noise::nextHash(h), b = noise::nextHash(a), c = noise::nextHash(b);
    return normalize(vec3(noise::toUnit(a), noise::toUnit(b), noise::toUnit(c)) - vec3(0.5f));
}

}  // namespace

vec4 crystalShader(const Fragment& f, const Uniforms& u) {
    const vec3 dir = normalize(f.objPos);
    const vec3 Ngeo = normalize(f.normal);
    const vec3 V = normalize(u.cameraPos - f.worldPos);
    const vec3 L = normalize(u.lightPos - f.worldPos);
    const float fp = shade::footprint(f, u);

    // --- Capa 1: facetas ---
    // un poquito de warp para que las aristas no sean tan rectas como un balón de fútbol
    vec3 warped = dir + vec3(noise::fbm(dir * 4.0f, 2, u.seed + 1u)) * 0.05f;
    noise::VoronoiResult big = noise::voronoi(warped * FACET_SCALE, u.seed + 2u, 0.95f);
    noise::VoronoiResult small = noise::voronoi(warped * FACET_SCALE * 2.7f, u.seed + 3u, 0.95f);

    // inclino la normal una cantidad random por celda. Como es la misma en toda la
    // celda, la luz le pega plano y se ve como una cara tallada de una gema.
    vec3 tilt = hashDirection(big.id) * FACET_TILT + hashDirection(small.id) * FACET_TILT * 0.4f;
    vec3 facetN = normalize(u.rotation * normalize(dir + tilt));

    float cellTone = noise::toUnit(big.id ^ 0x9e37u);
    const vec3 ice(0.62f, 0.83f, 1.0f);
    const vec3 rose(1.0f, 0.70f, 0.84f);
    const vec3 lilac(0.80f, 0.74f, 1.0f);
    vec3 albedo = cellTone < 0.5f ? mix(ice, lilac, cellTone * 2.0f) : mix(lilac, rose, (cellTone - 0.5f) * 2.0f);

    // grandes zonas que tiran para azul o para rosa, si no parece confeti
    float zone = noise::fbm(dir * 1.6f, 4, u.seed + 4u);
    albedo = mix(albedo, zone > 0.0f ? rose : ice, smoothstep(0.02f, 0.26f, std::fabs(zone)) * 0.5f);

    // --- Capa 2: película iridiscente ---
    // como una burbuja de jabón: según el ángulo, se refuerza un color u otro
    float ndv = shade::saturate(dot(facetN, V));
    vec3 film = shade::cosPalette(ndv * 1.6f + cellTone, vec3(0.84f, 0.84f, 0.95f), vec3(0.18f, 0.18f, 0.08f), vec3(1.0f), vec3(0.02f, 0.36f, 0.66f));
    albedo = mix(albedo, film, 0.42f);

    // --- Capa 4: vetas internas con parallax ---
    // muevo la coordenada en contra de la vista (en object space). Las vetas "se quedan
    // atrás" cuando giras la cámara, y por eso parecen estar dentro del cristal.
    vec3 Vobj = transpose(u.rotation) * V;
    vec3 inside = normalize(dir - Vobj * PARALLAX_DEPTH);
    float veinNoise = noise::fbm(inside * 5.6f, noise::octavesFor(5.6f, fp, 5), u.seed + 5u);
    float veins = 1.0f - smoothstep(0.0f, 0.045f, std::fabs(veinNoise));
    albedo = mix(albedo, vec3(1.0f, 0.86f, 0.95f), veins * 0.75f);

    // luz plana por faceta, con specular fuerte porque es una gema
    float shadow = shade::sphereShadow(f.worldPos, L, u.occluderCenter, u.occluderRadius);
    vec3 lit = shade::pastelLight(albedo, facetN, L, V, shadow, 0.95f, 85.0f);

    // --- Capa 3: grietas ---
    // las grietas brillan por dentro, se ven aunque les toque el lado de la sombra
    float cracksBig = 1.0f - smoothstep(CRACK_WIDTH * 0.3f + fp * 3.0f, CRACK_WIDTH + fp * 7.0f, big.f2 - big.f1);
    float cracksSmall = (1.0f - smoothstep(CRACK_WIDTH * 0.3f, CRACK_WIDTH, small.f2 - small.f1)) * (1.0f - smoothstep(0.0f, 0.02f, fp));
    float cracks = max(cracksBig, cracksSmall * 0.5f);
    lit = mix(lit, vec3(1.0f, 0.90f, 0.96f), cracks * 0.78f);

    // --- Capa 5: escarcha y destellos ---
    float frostNoise = noise::fbm(dir * 9.0f, noise::octavesFor(9.0f, fp, 4), u.seed + 6u);
    float frost = smoothstep(FROST_START, FROST_START + 0.17f, std::fabs(dir.y) + frostNoise * 0.14f);
    vec3 frostColor = shade::pastelLight(vec3(0.95f, 0.98f, 1.0f), Ngeo, L, V, shadow, 0.3f, 30.0f);
    lit = mix(lit, frostColor, frost * 0.88f);

    // puntitos que solo se prenden si su normal random apunta justo hacia la luz
    if (fp * 120.0f < 0.5f) {
        noise::VoronoiResult glint = noise::voronoi(dir * 120.0f, u.seed + 7u);
        float aligned = dot(normalize(u.rotation * hashDirection(glint.id) + Ngeo * 1.6f), normalize(L + V));
        float spark = smoothstep(0.985f, 0.998f, aligned) * (1.0f - smoothstep(0.08f, 0.24f, glint.f1));
        lit += vec3(1.0f) * spark;
    }

    return shade::opaque(lit);
}
