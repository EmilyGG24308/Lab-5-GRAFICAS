// NENÚFAR, el océano de nácar. Todo el planeta es agua como el interior de una concha,
// con islas que son hojas de nenúfar gigantes.
//
// Capas que afectan el color:
//   1. Profundidad (azul claro en mar abierto, rosa arena en lo poco profundo)
//   2. Olas (senos deformados por noise; también mueven la normal y el brillo)
//   3. Iridiscencia de nácar (la paleta cambia con el ángulo de la cámara)
//   4. Nenúfares gigantes (hoja redonda con muesca, y un loto rosado en el centro)
//   5. Espuma alrededor de las hojas y corrientes
//   6. Nubes finas en espiral
#include "shader.h"

using namespace glm;

namespace {

// --- Parámetros para ajustar el look ---
const float WAVE_FREQ = 44.0f;       // más alto = olas más finas
const float WAVE_BUMP = 0.0030f;      // qué tanto mueven la luz
const float PAD_SCALE = 4.6f;        // más alto = nenúfares más chiquitos
const float IRIDESCENCE = 0.62f;     // de 0 a 1, cuánto nácar
const float CLOUD_START = 0.17f;     // más bajo = más nubes

float waveHeight(const vec3& p, float t, uint32_t seed) {
    // suma de senos en varias direcciones. El noise deforma la coordenada para que las
    // crestas no queden como líneas rectas perfectas.
    vec3 q = p + vec3(noise::perlin(p * 3.0f, seed), noise::perlin(p * 3.0f + vec3(7.0f), seed), 0.0f) * 0.16f;
    float h = std::sin(dot(q, vec3(0.8f, 0.5f, 0.2f)) * WAVE_FREQ + t) * 0.5f;
    h += std::sin(dot(q, vec3(-0.3f, 0.75f, 0.6f)) * WAVE_FREQ * 1.7f + t * 1.3f) * 0.3f;
    h += std::sin(dot(q, vec3(0.5f, -0.2f, 0.85f)) * WAVE_FREQ * 3.1f + t * 1.9f) * 0.15f;
    return h;
}

}  // namespace

vec4 oceanShader(const Fragment& f, const Uniforms& u) {
    const vec3 dir = normalize(f.objPos);
    const vec3 V = normalize(u.cameraPos - f.worldPos);
    const vec3 L = normalize(u.lightPos - f.worldPos);
    const vec3 Ngeo = normalize(f.normal);
    const float fp = shade::footprint(f, u);
    const float t = u.time * 0.6f;

    // --- Capa 1: profundidad ---
    float depth = noise::fbm(dir * 2.2f, noise::octavesFor(2.2f, fp, 6), u.seed + 1u);
    const vec3 deep(0.42f, 0.70f, 0.94f);     // mar abierto, azul claro
    const vec3 mid(0.62f, 0.85f, 0.96f);      // celeste lechoso
    const vec3 shallow(1.0f, 0.82f, 0.84f);   // bajo fondo, arena rosada
    vec3 albedo = mix(deep, mid, smoothstep(-0.22f, 0.08f, depth));
    albedo = mix(albedo, shallow, smoothstep(0.14f, 0.34f, depth));

    // --- Capa 2: olas ---
    vec3 waveN = dir;
    // apago las olas al llegar al borde del disco: visto de lado, el seno se aplasta
    // y solo queda un moiré feo
    float waveFade = (1.0f - smoothstep(0.1f, 0.3f, fp * WAVE_FREQ)) * smoothstep(0.25f, 0.75f, dot(Ngeo, V));
    if (waveFade > 0.0f) {
        waveN = shade::bumpNormal(dir, mat3(1.0f), 0.0025f, WAVE_BUMP * waveFade,
                                  [&](const vec3& p) { return waveHeight(p, t, u.seed + 2u); });
    }
    float crest = waveFade > 0.0f ? waveHeight(dir, t, u.seed + 2u) : 0.0f;
    albedo = mix(albedo, albedo + vec3(0.04f), smoothstep(0.35f, 0.8f, crest) * waveFade);

    // --- Capa 3: iridiscencia de nácar ---
    // la paleta depende del ángulo con la cámara, como la película fina de una concha:
    // si das vuelta al planeta, los colores se corren.
    float mu = shade::saturate(dot(Ngeo, V));
    float swirlNoise = noise::fbm(dir * 3.6f + vec3(depth), noise::octavesFor(3.6f, fp, 5), u.seed + 3u);
    float film = mu * 1.1f + swirlNoise * 0.85f + depth * 0.5f;
    vec3 nacre = shade::cosPalette(film, vec3(0.86f, 0.84f, 0.93f), vec3(0.13f, 0.13f, 0.08f), vec3(1.0f), vec3(0.0f, 0.33f, 0.62f));
    float nacreMask = IRIDESCENCE * smoothstep(-0.25f, 0.3f, swirlNoise + 0.1f);
    albedo = mix(albedo, nacre, nacreMask);

    // --- Capa 5a: corrientes ---
    // líneas finas de agua más clara que siguen el contorno del noise de profundidad
    float currents = std::sin(depth * 58.0f + swirlNoise * 9.0f);
    float currentFade = 1.0f - smoothstep(0.003f, 0.012f, fp);
    albedo = mix(albedo, vec3(0.97f, 0.97f, 1.0f), smoothstep(0.88f, 1.0f, currents) * 0.32f * currentFade);

    // --- Capa 4: nenúfares ---
    float padMask = 0.0f, foam = 0.0f;
    vec3 padColor(0.0f);
    noise::scatterOnSphere(dir, PAD_SCALE, u.seed + 11u, [&](const vec3& center, uint32_t h) {
        if (noise::toUnit(h) > 0.40f) return;  // sube este número para tener más hojas
        uint32_t k = noise::nextHash(h);
        float radius = 0.16f + 0.30f * noise::toUnit(k);
        vec2 uv = noise::tangentCoords(dir, center, PAD_SCALE);
        // el borde un poco irregular para que parezca una hoja real
        float wob = 1.0f + 0.05f * noise::perlin(vec3(uv * 6.0f, noise::toUnit(h) * 40.0f), u.seed + 12u);
        float r = length(uv) / (radius * wob);
        if (r > 1.45f) return;
        foam = max(foam, (1.0f - smoothstep(1.0f, 1.22f, r)) * (0.55f + 0.45f * noise::perlin(dir * 60.0f, u.seed + 14u)));
        if (r > 1.03f) return;

        k = noise::nextHash(k);
        float angle = std::atan2(uv.y, uv.x) + noise::toUnit(k) * 6.2832f;
        // la muesca tan típica del nenúfar: una rebanada de pizza que falta
        float wrapped = std::fabs(std::fmod(angle + 12.566f, 6.2832f) - 3.1416f);
        float notch = smoothstep(0.17f, 0.25f, 3.1416f - wrapped);
        float leaf = (1.0f - smoothstep(0.97f, 1.02f, r)) * notch;
        // nervaduras radiales
        float ribs = 0.5f + 0.5f * std::cos(angle * 22.0f);
        vec3 c = mix(vec3(0.62f, 0.88f, 0.84f), vec3(0.82f, 0.96f, 0.90f), r * 0.6f + ribs * 0.25f);
        c = mix(c, vec3(0.93f, 0.98f, 0.88f), smoothstep(0.86f, 0.98f, r));  // bordecito claro

        // loto en el centro: pétalos con la misma fórmula polar de las flores
        float lotus = std::fabs(std::cos(angle * 4.0f));
        float lotusEdge = 0.26f + 0.30f * std::pow(lotus, 0.6f);
        float lotusMask = 1.0f - smoothstep(lotusEdge - 0.02f, lotusEdge + 0.01f, r);
        vec3 lotusC = mix(vec3(0.99f, 0.46f, 0.68f), vec3(1.0f, 0.86f, 0.92f), smoothstep(0.05f, 0.55f, r));
        lotusC = mix(vec3(1.0f, 0.90f, 0.55f), lotusC, smoothstep(0.03f, 0.08f, r));
        c = mix(c, lotusC, lotusMask);
        leaf = max(leaf, lotusMask);

        padColor = mix(padColor, c, leaf);
        padMask = max(padMask, leaf);
    });
    // --- Capa 5b: espuma ---
    albedo = mix(albedo, vec3(1.0f, 0.98f, 0.97f), foam * (1.0f - padMask) * 0.5f);
    albedo = mix(albedo, padColor, padMask);

    // luz: el agua sí brilla (sun glint), la hoja no
    vec3 N = u.rotation * normalize(mix(waveN, dir, padMask));
    float specular = (1.0f - padMask) * 0.34f;
    vec3 lit = shade::pastelLight(albedo, N, L, V, shade::sphereShadow(f.worldPos, L, u.occluderCenter, u.occluderRadius), specular, 180.0f);

    // --- Capa 6: nubes en espiral ---
    // giro la coordenada según la latitud y sale el barrido típico de un ciclón
    float drift = u.time * 0.02f + dir.y * 2.4f;
    vec3 cp(dir.x * std::cos(drift) - dir.z * std::sin(drift), dir.y * 2.0f, dir.x * std::sin(drift) + dir.z * std::cos(drift));
    float cloud = noise::fbm(cp * 2.8f, noise::octavesFor(2.8f, fp, 5), u.seed + 13u);
    float cloudMask = smoothstep(CLOUD_START, CLOUD_START + 0.2f, cloud);
    if (cloudMask > 0.0f) {
        lit = mix(lit, shade::pastelLight(vec3(1.0f, 0.97f, 0.99f), Ngeo, L, V), cloudMask * 0.72f);
    }
    return shade::opaque(lit);
}
