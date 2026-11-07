// FLORALIA, el planeta jardín. La superficie entera está cubierta de flores gigantes.
// Cada flor es una fórmula en coordenadas polares: ni una sola imagen.
//
// Capas que afectan el color:
//   1. Praderas (colinas suaves con varios tonos de verde agua)
//   2. Ríos celestes (líneas donde el noise cruza por cero)
//   3. Campos de color (qué zona florece en rosa, en celeste o en crema)
//   4. Flores gigantes, con pétalos, venitas y centro
//   5. Flores medianas y florecitas (la misma fórmula a otras escalas)
//   6. Polen brillante y nubes de pétalos flotando encima
#include "shader.h"

using namespace glm;

namespace {

// --- Parámetros para ajustar el look ---
const float BIG_FLOWER_SCALE = 6.5f;     // más alto = flores grandes más chiquitas
const float MID_FLOWER_SCALE = 15.0f;
const float SMALL_FLOWER_SCALE = 36.0f;
const float RIVER_WIDTH = 0.022f;        // grosor de los ríos
const float CLOUD_COVER = 0.16f;         // más bajo = más nubes

// Una flor en coordenadas locales uv. Devuelve color (rgb) y cobertura (a).
// El truco es |cos(n * ángulo / 2)|: da exactamente n lóbulos, o sea n pétalos.
vec4 flower(const vec2& uv, uint32_t h, const vec3& petalColor, float maxRadius) {
    float r = length(uv);
    if (r > maxRadius) return vec4(0.0f);

    uint32_t k = noise::nextHash(h);
    float n = 5.0f + static_cast<float>(k % 4u);  // de 5 a 8 pétalos
    k = noise::nextHash(k);
    float size = maxRadius * (0.72f + 0.28f * noise::toUnit(k));
    k = noise::nextHash(k);
    float spin = noise::toUnit(k) * 6.2832f;

    float angle = std::atan2(uv.y, uv.x) + spin;
    // primera corona de pétalos
    float lobe = std::fabs(std::cos(angle * n * 0.5f));
    float outline = size * (0.42f + 0.58f * std::pow(lobe, 0.62f));
    float mask = 1.0f - smoothstep(outline - size * 0.03f, outline + size * 0.01f, r);
    if (mask <= 0.0f) return vec4(0.0f);

    float rel = r / outline;
    // degradado: más intenso en la base del pétalo y clarito en la punta
    vec3 color = mix(petalColor * vec3(0.94f, 0.84f, 0.92f), mix(petalColor, vec3(1.0f), 0.46f), smoothstep(0.1f, 1.0f, rel));
    // venitas que salen del centro
    float veins = std::cos(angle * n * 5.0f);
    color *= 0.97f + 0.03f * veins + 0.05f * smoothstep(0.75f, 1.0f, veins) * (1.0f - rel);
    // sombra entre pétalo y pétalo, para que se separen visualmente
    color *= 0.82f + 0.18f * smoothstep(0.0f, 0.34f, lobe);

    // segunda corona, más chica y rotada medio pétalo: le da cuerpo de flor real
    float lobe2 = std::fabs(std::sin(angle * n * 0.5f));
    float outline2 = size * (0.22f + 0.32f * std::pow(lobe2, 0.7f));
    float inner = 1.0f - smoothstep(outline2 - size * 0.025f, outline2 + size * 0.01f, r);
    vec3 innerColor = mix(petalColor * vec3(0.78f, 0.66f, 0.80f), mix(petalColor, vec3(1.0f), 0.62f), smoothstep(0.1f, 1.0f, r / outline2));
    color = mix(color * 0.90f, innerColor, inner);  // el 0.90 es la sombrita que la corona de arriba proyecta

    // centro de la flor: dorado pastel con puntitos
    float centerR = size * 0.15f;
    if (r < centerR) {
        float dots = 0.5f + 0.5f * std::sin(uv.x / size * 95.0f) * std::sin(uv.y / size * 95.0f);
        vec3 center = mix(vec3(1.0f, 0.82f, 0.46f), vec3(1.0f, 0.95f, 0.70f), dots);
        color = mix(color, center, 1.0f - smoothstep(centerR * 0.82f, centerR, r));
    }
    return vec4(color, mask);
}

// Elige el color del pétalo. La cancha de color (field) manda: así salen campos enteros
// de un tono y no un confeti random. Va de rosa a celeste con acentos de otros colores.
vec3 petalPalette(uint32_t h, float field) {
    const vec3 pink(1.0f, 0.60f, 0.76f);
    const vec3 sky(0.56f, 0.80f, 1.0f);
    const vec3 rose(0.98f, 0.45f, 0.66f);
    const vec3 periwinkle(0.70f, 0.70f, 1.0f);
    const vec3 cream(1.0f, 0.93f, 0.88f);
    float pick = noise::toUnit(noise::nextHash(h ^ 0x51ed27u));
    vec3 base = field > 0.0f ? (pick < 0.6f ? pink : rose) : (pick < 0.6f ? sky : periwinkle);
    if (pick > 0.88f) base = cream;
    return base;
}

}  // namespace

vec4 gardenShader(const Fragment& f, const Uniforms& u) {
    const vec3 dir = normalize(f.objPos);
    const vec3 V = normalize(u.cameraPos - f.worldPos);
    const vec3 L = normalize(u.lightPos - f.worldPos);
    const vec3 N = normalize(f.normal);
    const float fp = shade::footprint(f, u);

    // --- Capa 1: praderas ---
    float hills = noise::fbm(dir * 2.4f, noise::octavesFor(2.4f, fp, 6), u.seed + 1u);
    float grass = noise::fbm(dir * 22.0f, noise::octavesFor(22.0f, fp, 3), u.seed + 2u);
    vec3 albedo = mix(vec3(0.66f, 0.88f, 0.84f), vec3(0.78f, 0.94f, 0.86f), smoothstep(-0.22f, 0.24f, hills));
    albedo = mix(albedo, vec3(0.64f, 0.84f, 0.90f), smoothstep(0.08f, 0.4f, -hills));  // las partes bajas tiran a celeste
    albedo *= 0.96f + grass * 0.12f;

    // --- Capa 3: campos de color ---
    // low frequency: decide qué color florece en cada región del planeta.
    // También pinta el pasto con un tinte, porque de lejos no se distinguen las flores.
    vec3 fieldWarp(noise::perlin(dir * 3.1f, u.seed + 3u), noise::perlin(dir * 3.1f + vec3(4.0f), u.seed + 4u), 0.0f);
    float field = noise::fbm(dir * 1.7f + fieldWarp * 0.5f, 4, u.seed + 5u);
    vec3 fieldTint = field > 0.0f ? vec3(1.0f, 0.72f, 0.82f) : vec3(0.72f, 0.84f, 1.0f);
    float fieldStrength = smoothstep(0.0f, 0.2f, std::fabs(field));
    albedo = mix(albedo, fieldTint, fieldStrength * 0.62f);

    // --- Capa 2: ríos ---
    // abs(noise) pequeño = una línea sinuosa que da la vuelta al planeta
    float riverNoise = noise::fbm(dir * 2.1f + fieldWarp * 0.25f + vec3(8.0f), noise::octavesFor(2.1f, fp, 6), u.seed + 6u);
    float widthAA = RIVER_WIDTH + fp * 0.3f;
    float river = 1.0f - smoothstep(widthAA * 0.45f, widthAA, std::fabs(riverNoise));
    float bank = 1.0f - smoothstep(widthAA, widthAA * 2.1f, std::fabs(riverNoise));
    albedo = mix(albedo, vec3(0.96f, 0.95f, 0.86f), bank * 0.8f);  // orilla clara
    albedo = mix(albedo, vec3(0.55f, 0.82f, 1.0f), river);

    // --- Capas 4 y 5: flores ---
    // voy de chicas a grandes para que las grandes queden arriba. No se pintan en los ríos.
    float landMask = 1.0f - bank;
    if (landMask > 0.0f) {
        auto paintFlowers = [&](float scale, uint32_t seed, float density, float maxR) {
            // si la flor mide menos de ~2 px ya no se dibuja: el tinte del campo la reemplaza
            if (fp * scale > 0.33f) return;
            float fade = 1.0f - smoothstep(0.14f, 0.33f, fp * scale);
            noise::scatterOnSphere(dir, scale, seed, [&](const vec3& center, uint32_t h) {
                if (noise::toUnit(h) > density) return;
                vec2 uv = noise::tangentCoords(dir, center, scale);
                vec4 petal = flower(uv, h, petalPalette(h, field), maxR);
                if (petal.a > 0.0f) albedo = mix(albedo, vec3(petal), petal.a * landMask * fade);
            });
        };
        paintFlowers(SMALL_FLOWER_SCALE, u.seed + 13u, 0.85f, 0.44f);
        paintFlowers(MID_FLOWER_SCALE, u.seed + 12u, 0.85f, 0.45f);
        paintFlowers(BIG_FLOWER_SCALE, u.seed + 11u, 0.92f, 0.47f);
    }

    // --- Capa 6: polen ---
    // puntitos random que brillan más con el sol de frente
    float specular = 0.0f;
    if (fp * 70.0f < 0.5f) {
        noise::VoronoiResult pollen = noise::voronoi(dir * 70.0f, u.seed + 14u);
        float spark = (1.0f - smoothstep(0.03f, 0.13f, pollen.f1)) * step(0.82f, noise::toUnit(pollen.id));
        albedo = mix(albedo, vec3(1.0f, 0.97f, 0.78f), spark * 0.9f);
    }
    specular += river * 0.22f;

    vec3 lit = shade::pastelLight(albedo, N, L, V,
                                  shade::sphereShadow(f.worldPos, L, u.occluderCenter, u.occluderRadius), specular, 70.0f);

    // nubes de pétalos: van arriba de todo y dejan pasar un poquito de lo de abajo
    float drift = u.time * 0.012f;
    vec3 cloudP(dir.x * std::cos(drift) - dir.z * std::sin(drift), dir.y, dir.x * std::sin(drift) + dir.z * std::cos(drift));
    float cloud = noise::fbm(cloudP * 3.2f + fieldWarp * 0.3f, noise::octavesFor(3.2f, fp, 5), u.seed + 15u);
    float cloudMask = smoothstep(CLOUD_COVER, CLOUD_COVER + 0.22f, cloud);
    if (cloudMask > 0.0f) {
        vec3 cloudColor = shade::pastelLight(vec3(1.0f, 0.95f, 0.97f), N, L, V);
        lit = mix(lit, cloudColor, cloudMask * 0.78f);
    }
    return shade::opaque(lit);
}
