// HELIA, la estrella. Es el único cuerpo que NO es pastel a propósito: la idea es
// que se vea como el Sol de verdad, igual que en las fotos con filtro solar.
//
// Capas que afectan el color:
//   1. Temperatura y limb darkening (el centro blanco-dorado, el borde naranja)
//   2. Granulación (las celditas de convección, con Voronoi)
//   3. Supergránulos y flujo de plasma (manchones grandes con domain warping)
//   4. Manchas solares (umbra oscura + penumbra con fibritas)
//   5. Fáculas (zonas muy brillantes alrededor de las manchas)
//   6. Filamentos (hilos oscuros y curvos cerca de las regiones activas)
#include "shader.h"

using namespace glm;

namespace {

// --- Parámetros para ajustar el look ---
const float GRANULE_SCALE = 44.0f;    // más alto = gránulos más chiquitos
const float GRANULE_STRENGTH = 0.20f; // contraste de los gránulos
const float SPOT_SCALE = 3.1f;        // más alto = más regiones activas
const float SPOT_RATE = 0.30f;        // de 0 a 1, cuántas regiones sí tienen manchas
const float BOILING_SPEED = 0.05f;    // qué tan rápido hierve la superficie

// Las regiones activas son las mismas para manchas, fáculas y filamentos.
struct ActiveRegion {
    float umbra = 0.0f;
    float penumbra = 0.0f;
    float facula = 0.0f;
};

ActiveRegion activeRegions(const vec3& dir, uint32_t seed) {
    ActiveRegion out;
    noise::scatterOnSphere(dir, SPOT_SCALE, seed + 71u, [&](const vec3& center, uint32_t h) {
        if (noise::toUnit(h) > SPOT_RATE) return;
        // como en el Sol real: las manchas viven en bandas de latitud media, no en los polos
        if (std::fabs(center.y) > 0.72f) return;

        // un grupo de 4 manchitas alrededor del centro en vez de una sola
        uint32_t k = noise::nextHash(h);
        float radius = 0.011f + 0.024f * noise::toUnit(k);
        vec2 uv = noise::tangentCoords(dir, center, 1.0f);
        if (dot(uv, uv) > 0.09f) return;  // lejos del grupo, salgo temprano

        // un noise deforma la distancia: las manchas reales no son círculos perfectos
        float wobble = 1.0f + 0.32f * noise::perlin(dir * 42.0f, seed + 72u);

        for (int i = 0; i < 4; ++i) {
            k = noise::nextHash(k);
            float ox = (noise::toUnit(k) - 0.5f) * 0.20f;
            k = noise::nextHash(k);
            float oy = (noise::toUnit(k) - 0.5f) * 0.09f;
            k = noise::nextHash(k);
            float r = radius * (0.35f + 0.65f * noise::toUnit(k));

            vec2 d = uv - vec2(ox, oy);
            float dist = length(d) * wobble;
            // fibras de la penumbra: varían el borde según el ángulo
            float angle = std::atan2(d.y, d.x);
            float fibers = 0.5f + 0.5f * std::sin(angle * 34.0f + 6.0f * noise::toUnit(k));
            float edge = r * (1.75f + 0.28f * fibers);

            out.umbra = max(out.umbra, 1.0f - smoothstep(r * 0.55f, r * 0.85f, dist));
            out.penumbra = max(out.penumbra, 1.0f - smoothstep(r * 0.9f, edge, dist));
        }
        // la fácula es un halo ancho alrededor del grupo entero
        out.facula = max(out.facula, 1.0f - smoothstep(0.05f, 0.26f, length(uv)));
    });
    return out;
}

}  // namespace

vec4 starShader(const Fragment& f, const Uniforms& u) {
    const vec3 dir = normalize(f.objPos);
    const vec3 N = normalize(f.normal);
    const vec3 V = normalize(u.cameraPos - f.worldPos);
    const float mu = shade::saturate(dot(N, V));  // 1 en el centro del disco, 0 en el borde
    const float fp = shade::footprint(f, u);
    const float t = u.time * BOILING_SPEED;

    // --- Capa 3: supergránulos ---
    // domain warping: le meto noise a la coordenada del noise, así el patrón se ve
    // como un fluido que se arrastra y no como nubes estáticas
    vec3 warp(noise::fbm(dir * 2.6f + vec3(0.0f, t, 0.0f), 3, u.seed + 1u),
              noise::fbm(dir * 2.6f + vec3(5.2f, t, 1.3f), 3, u.seed + 2u),
              noise::fbm(dir * 2.6f + vec3(1.7f, t, 9.2f), 3, u.seed + 3u));
    vec3 flowDir = dir + warp * 0.34f;
    float flow = noise::fbm(flowDir * 5.2f, noise::octavesFor(5.2f, fp, 5), u.seed + 4u);

    // --- Capa 1: temperatura ---
    // limb darkening: en el borde vemos capas más frías y por eso se oscurece y se
    // pone naranja. Es una aproximación polinómica: mu es el coseno del ángulo de vista.
    float limb = 1.0f - 0.72f * (1.0f - mu) - 0.20f * (1.0f - mu * mu);
    float temperature = limb * 0.90f + flow * 0.22f;

    // --- Capa 2: granulación ---
    // cada celda es una burbuja de plasma: el centro caliente y brillante, y las líneas
    // oscuras (f2 - f1 chico) son donde el gas ya se enfrió y vuelve a bajar.
    // Si la celda sale más chica que un píxel la apago, porque solo daría ruido.
    float granuleFade = 1.0f - smoothstep(0.25f, 0.8f, fp * GRANULE_SCALE);
    if (granuleFade > 0.0f) {
        // poco warp acá: con todo el warp las celdas se estiran y salen remolinos tipo huella digital
        vec3 q = (dir + warp * 0.05f) * GRANULE_SCALE + vec3(0.0f, u.time * 0.12f, 0.0f);
        noise::VoronoiResult cell = noise::voronoi(q, u.seed + 5u);
        float lanes = smoothstep(0.0f, 0.42f, cell.f2 - cell.f1);
        float brightness = lanes * (0.75f + 0.25f * noise::toUnit(cell.id)) - 0.62f;
        // segunda escala más fina para que las celdas no se vean tan perfectas
        float fine = noise::perlin(q * 2.7f, u.seed + 6u);
        temperature += (brightness + fine * 0.12f) * GRANULE_STRENGTH * granuleFade * (0.35f + 0.65f * mu);
    }

    ActiveRegion region = activeRegions(dir, u.seed);

    // --- Capa 5: fáculas ---
    // se notan más hacia el borde, igual que en el Sol: por eso multiplico por (1 - mu)
    if (region.facula > 0.0f) {
        float pattern = noise::ridged(dir * 19.0f + warp, noise::octavesFor(19.0f, fp, 3), u.seed + 7u);
        temperature += region.facula * smoothstep(0.35f, 0.8f, pattern) * (0.10f + 0.34f * (1.0f - mu));
    }

    // De temperatura a color: rampa de naranja profundo a blanco cálido.
    // Súbele a cualquiera de estos y cambia el "tono" de toda la estrella.
    const vec3 deep(0.78f, 0.20f, 0.03f);
    const vec3 orange(0.99f, 0.47f, 0.07f);
    const vec3 gold(1.00f, 0.74f, 0.25f);
    const vec3 hot(1.00f, 0.95f, 0.72f);
    float temp = shade::saturate(temperature);
    vec3 color = mix(deep, orange, smoothstep(0.0f, 0.34f, temp));
    color = mix(color, gold, smoothstep(0.30f, 0.66f, temp));
    color = mix(color, hot, smoothstep(0.66f, 0.98f, temp));

    // --- Capa 6: filamentos ---
    // ridged noise bien filoso: solo queda una línea delgada donde el noise cruza cero.
    // Los dejo solo cerca de regiones activas, si no se llena todo de hilos.
    if (region.facula > 0.01f) {
        float n = noise::fbm((dir + warp * 0.3f) * 7.0f, noise::octavesFor(7.0f, fp, 4), u.seed + 8u);
        float filament = 1.0f - smoothstep(0.004f, 0.022f, std::fabs(n));
        color = mix(color, color * vec3(0.72f, 0.58f, 0.50f), filament * region.facula * 0.55f);
    }

    // --- Capa 4: manchas solares ---
    // van al final porque tapan todo: penumbra café-naranja y umbra casi negra
    color = mix(color, vec3(0.66f, 0.30f, 0.07f), region.penumbra * 0.78f);
    color = mix(color, vec3(0.12f, 0.04f, 0.02f), region.umbra);

    return shade::opaque(color);
}

// Corona: un shell más grande que la estrella, transparente, y con suma de color.
// Por cada píxel saco qué tan lejos del centro pasa el rayo de la cámara: esa
// distancia es la que decide el brillo, no la posición del shell.
vec4 coronaShader(const Fragment& f, const Uniforms& u) {
    vec3 rayDir = normalize(f.worldPos - u.cameraPos);
    vec3 toCenter = u.center - u.cameraPos;
    vec3 closest = u.cameraPos + rayDir * dot(toCenter, rayDir);
    vec3 radial = closest - u.center;
    float impact = length(radial) / u.radius;
    float height = impact - 1.0f;  // 0 justo en el borde del disco
    if (height < -0.004f) return vec4(0.0f);  // dentro del disco no se suma nada

    vec3 dir = transpose(u.rotation) * (radial / (impact * u.radius));
    float maxHeight = u.shellRadius - 1.0f;
    float fadeOut = 1.0f - smoothstep(maxHeight * 0.55f, maxHeight, height);
    float t = u.time * 0.03f;

    // serpentinas: el noise depende solo de la dirección, no de la altura. Por eso
    // salen como rayos que se estiran para afuera, como en un eclipse.
    float streamers = 0.5f + 0.5f * noise::fbm(dir * 3.4f + vec3(t), 4, u.seed + 21u);
    streamers = smoothstep(0.25f, 0.85f, streamers);

    // glow base que cae exponencial con la altura
    float glow = std::exp(-height * 5.5f) * 0.62f + std::exp(-height * 1.6f) * 0.24f * streamers;

    // prominencias: arcos de plasma pegados al borde
    float arcs = noise::ridged(dir * 9.0f + vec3(0.0f, t * 2.0f, 0.0f) + vec3(height * 5.0f), 4, u.seed + 22u);
    float arcHeight = 0.05f + 0.11f * smoothstep(0.42f, 0.9f, 0.5f + 0.5f * noise::perlin(dir * 3.0f, u.seed + 23u));
    float prominence = smoothstep(0.50f, 0.78f, arcs) * (1.0f - smoothstep(arcHeight * 0.5f, arcHeight, height));

    vec3 inner(1.0f, 0.70f, 0.28f);
    vec3 outer(1.0f, 0.82f, 0.60f);
    vec3 color = mix(inner, outer, shade::saturate(height * 2.2f)) * glow;
    color += vec3(1.0f, 0.36f, 0.12f) * prominence * 0.62f;
    color *= fadeOut;
    // alpha 0 = suma pura de luz
    return vec4(color, 0.0f);
}
