// MALVA, el planeta rocoso. Roca lila, cordilleras, cráteres con lagos celestes.
//
// Capas que afectan el color:
//   1. Regiones de terreno (llanuras lila, tierras altas rosadas, bajos azulados)
//   2. Cordilleras con ridged noise y cumbres con nieve
//   3. Cráteres con borde levantado, salpicadura y algunos con lago celeste
//   4. Vetas de cuarzo rosa (depósitos minerales que brillan con el sol)
//   5. Valles menta y casquetes de escarcha en los polos
#include "shader.h"

using namespace glm;

namespace {

// --- Parámetros para ajustar el look ---
const float REGION_SCALE = 1.5f;     // más alto = continentes más chiquitos
const float MOUNTAIN_SCALE = 3.4f;   // más alto = cordilleras más apretadas
const float MOUNTAIN_BUMP = 0.11f;   // relieve de las montañas en la luz
const float CRATER_SCALE_BIG = 5.0f; // tamaño de los cráteres grandes
const float CRATER_SCALE_SMALL = 12.0f;
const float VEIN_WIDTH = 0.016f;     // grosor de las vetas de cuarzo
const float FROST_START = 0.74f;     // latitud donde empieza la escarcha

// La máscara dice dónde hay montañas: casi solo en las tierras altas.
float mountainHeight(const vec3& dir, uint32_t seed, int octaves) {
    float highland = noise::fbm(dir * REGION_SCALE, 3, seed + 1u);
    float mask = smoothstep(-0.08f, 0.28f, highland);
    return noise::ridged(dir * MOUNTAIN_SCALE, octaves, seed + 2u) * mask;
}

struct CraterSample {
    float darken = 0.0f;  // fondo del cráter
    float rim = 0.0f;     // borde levantado
    float ejecta = 0.0f;  // material salpicado alrededor
    float lake = 0.0f;    // agua en el fondo
    vec3 slope{0.0f};     // pendiente, para mover la normal
};

void addCraters(const vec3& dir, float scale, uint32_t seed, float density, CraterSample& out) {
    noise::scatterOnSphere(dir, scale, seed, [&](const vec3& center, uint32_t h) {
        if (noise::toUnit(h) > density) return;  // density: qué fracción de puntos sí es cráter
        h = noise::nextHash(h);
        float radius = (0.14f + 0.28f * noise::toUnit(h)) / scale;
        vec3 delta = dir - center;
        float dist = length(delta);
        float d = dist / radius;  // 0 en el centro, 1 en el borde
        if (d > 2.2f) return;

        // perfil: bowl adentro, un bump en el borde
        float bowl = 1.0f - smoothstep(0.0f, 0.92f, d);
        float rim = std::exp(-(d - 1.0f) * (d - 1.0f) * 42.0f);
        out.darken = max(out.darken, bowl);
        out.rim = max(out.rim, rim);
        out.ejecta = max(out.ejecta, (1.0f - smoothstep(1.0f, 2.2f, d)) * step(1.0f, d));

        // pendiente: derivada del perfil. Adentro sube hacia el borde; afuera baja.
        float dhdr = d < 1.0f ? d * 1.2f : -2.0f * (d - 1.0f) * 2.4f * rim;
        if (dist > 1e-5f) out.slope += (delta / dist) * dhdr * 0.45f;

        // uno de cada cuatro tiene lago en el fondo
        if ((noise::nextHash(h) % 4u) == 0u) out.lake = max(out.lake, 1.0f - smoothstep(0.48f, 0.60f, d));
    });
}

}  // namespace

vec4 rockyShader(const Fragment& f, const Uniforms& u) {
    const vec3 dir = normalize(f.objPos);
    const vec3 V = normalize(u.cameraPos - f.worldPos);
    const vec3 L = normalize(u.lightPos - f.worldPos);
    const float fp = shade::footprint(f, u);

    // --- Capa 1: regiones de terreno ---
    float continents = noise::fbm(dir * REGION_SCALE + vec3(3.1f), noise::octavesFor(REGION_SCALE, fp, 6), u.seed + 1u);
    float soil = noise::fbm(dir * 6.5f, noise::octavesFor(6.5f, fp, 5), u.seed + 3u);
    const vec3 lowlands(0.60f, 0.64f, 0.88f);   // bajos azulados
    const vec3 plains(0.73f, 0.63f, 0.88f);     // llanuras lila
    const vec3 highlands(0.90f, 0.70f, 0.80f);  // tierras altas rosadas
    vec3 albedo = mix(lowlands, plains, smoothstep(-0.32f, -0.02f, continents + soil * 0.14f));
    albedo = mix(albedo, highlands, smoothstep(0.02f, 0.30f, continents + soil * 0.14f));
    albedo *= 0.94f + soil * 0.16f;  // este noise le da variation a la roca

    // --- Capa 5a: valles menta ---
    // pequeños parches en las zonas bajas, un guiño a "todo está floreciendo"
    float meadow = smoothstep(0.08f, 0.26f, noise::fbm(dir * 4.0f + vec3(9.0f), 4, u.seed + 4u));
    meadow *= 1.0f - smoothstep(-0.22f, 0.04f, continents);
    albedo = mix(albedo, vec3(0.70f, 0.90f, 0.80f), meadow * 0.85f);

    // --- Capa 2: cordilleras ---
    int mountainOctaves = noise::octavesFor(MOUNTAIN_SCALE, fp, 6);
    float mountains = mountainHeight(dir, u.seed, mountainOctaves);
    albedo = mix(albedo, vec3(0.56f, 0.46f, 0.70f), smoothstep(0.26f, 0.5f, mountains));   // las faldas más oscuras
    albedo = mix(albedo, vec3(0.98f, 0.95f, 0.99f), smoothstep(0.54f, 0.74f, mountains));  // las cumbres con nieve

    // --- Capa 3: cráteres ---
    CraterSample crater;
    addCraters(dir, CRATER_SCALE_BIG, u.seed + 11u, 0.42f, crater);
    if (fp * CRATER_SCALE_SMALL < 0.5f) addCraters(dir, CRATER_SCALE_SMALL, u.seed + 12u, 0.30f, crater);
    albedo = mix(albedo, albedo * vec3(0.92f, 0.88f, 1.02f) + vec3(0.05f), crater.ejecta * 0.55f);
    albedo = mix(albedo, albedo * vec3(0.66f, 0.62f, 0.82f), crater.darken * 0.75f);
    albedo = mix(albedo, vec3(0.96f, 0.86f, 0.92f), crater.rim * 0.5f);

    // --- Capa 4: vetas de cuarzo rosa ---
    // abs(noise) casi cero forma una línea sinuosa. Las saco de la nieve y de los lagos.
    float veinNoise = noise::fbm(dir * 4.6f + vec3(soil * 0.35f), noise::octavesFor(4.6f, fp, 5), u.seed + 5u);
    float vein = 1.0f - smoothstep(VEIN_WIDTH * 0.25f, VEIN_WIDTH, std::fabs(veinNoise));
    vein *= 1.0f - smoothstep(0.4f, 0.55f, mountains);
    vein *= 1.0f - crater.lake;
    albedo = mix(albedo, vec3(1.0f, 0.72f, 0.84f), vein * 0.7f);

    // --- Capa 5b: escarcha polar ---
    float frostEdge = FROST_START + soil * 0.10f;
    float frost = smoothstep(frostEdge, frostEdge + 0.10f, std::fabs(dir.y));
    albedo = mix(albedo, vec3(0.91f, 0.95f, 1.0f), frost);

    // el lago va al final porque el agua tapa todo lo demás
    float specular = vein * 0.25f + frost * 0.08f;
    float shininess = 40.0f;
    if (crater.lake > 0.0f) {
        albedo = mix(albedo, vec3(0.60f, 0.84f, 0.98f), crater.lake);
        specular = mix(specular, 0.3f, crater.lake);
        shininess = 90.0f;
    }

    // relieve: normal movida por las montañas + la pendiente de los cráteres
    int bumpOctaves = min(mountainOctaves, 4);
    vec3 bumped = shade::bumpNormal(dir, mat3(1.0f), 0.006f, MOUNTAIN_BUMP, [&](const vec3& p) {
        return mountainHeight(p, u.seed, bumpOctaves);
    });
    vec3 flatness = normalize(mix(bumped - crater.slope, dir, crater.lake));  // el agua es plana
    vec3 N = u.rotation * flatness;

    float shadow = shade::sphereShadow(f.worldPos, L, u.occluderCenter, u.occluderRadius);
    return shade::opaque(shade::pastelLight(albedo, N, L, V, shadow, specular, shininess));
}
