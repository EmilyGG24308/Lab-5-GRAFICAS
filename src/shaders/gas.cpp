// PEONÍA, el gigante gaseoso: como algodón de azúcar en órbita.
//
// Capas que afectan el color:
//   1. Bandas latitudinales (la paleta rosa, crema, celeste y lila)
//   2. Turbulencia (ruido estirado en longitud y domain warping)
//   3. Tormentas con forma de rosa (remolinos que además doblan las bandas)
//   4. Óvalos blancos pequeños, en cadena dentro de una banda
//   5. Bruma de los polos y del borde
//   + la sombra que le hace su propio anillo (no es patrón, pero sí cambia el color)
#include "shader.h"

using namespace glm;

namespace {

// --- Parámetros para ajustar el look ---
const float BAND_COUNT = 5.2f;          // más alto = más bandas
const float BAND_WOBBLE = 0.075f;       // qué tanto ondulan los bordes de las bandas
const float TURBULENCE_STRETCH = 7.0f;  // qué tan alargadas son las nubes
const float DRIFT_SPEED = 0.05f;        // velocidad de las corrientes
const float HAZE_STRENGTH = 0.55f;      // bruma polar

const vec3 BAND_COLORS[] = {
    {0.99f, 0.74f, 0.84f},  // rosa algodón
    {1.00f, 0.93f, 0.88f},  // crema
    {0.66f, 0.85f, 0.99f},  // celeste
    {0.98f, 0.82f, 0.86f},  // durazno rosado
    {0.80f, 0.74f, 0.97f},  // lila
    {0.99f, 0.66f, 0.79f},  // rosa más fuerte
    {0.78f, 0.92f, 0.99f},  // azul claro
};
const int BAND_COLOR_COUNT = 7;

struct Storm {
    vec3 axis;
    float radius;
    float swirl;
};

// Tormentas fijas. Moverlas o cambiar su tamaño es tocar esta tabla.
const Storm STORMS[] = {
    {normalize(vec3(0.72f, -0.33f, 0.60f)), 0.30f, 3.4f},   // la "Gran Rosa"
    {normalize(vec3(-0.45f, 0.42f, 0.78f)), 0.17f, -3.0f},
    {normalize(vec3(-0.80f, -0.18f, -0.55f)), 0.21f, 3.2f},
    {normalize(vec3(0.30f, 0.26f, -0.90f)), 0.14f, -3.2f},
};

// Rota p alrededor de un eje (fórmula de Rodrigues). Roto más cerca del centro, y por
// eso las bandas se enrollan alrededor de la tormenta en vez de pasar por encima.
vec3 swirlAround(const vec3& p, const vec3& axis, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return p * c + cross(axis, p) * s + axis * dot(axis, p) * (1.0f - c);
}

vec3 bandColor(float x) {
    float scaled = fract(x) * BAND_COLOR_COUNT;
    int i = static_cast<int>(scaled) % BAND_COLOR_COUNT;
    int j = (i + 1) % BAND_COLOR_COUNT;
    // smoothstep para que una banda pase a la otra sin una línea dura
    return mix(BAND_COLORS[i], BAND_COLORS[j], smoothstep(0.18f, 0.82f, scaled - std::floor(scaled)));
}

}  // namespace

vec4 gasShader(const Fragment& f, const Uniforms& u) {
    const vec3 dir = normalize(f.objPos);
    const vec3 N = normalize(f.normal);
    const vec3 V = normalize(u.cameraPos - f.worldPos);
    const vec3 L = normalize(u.lightPos - f.worldPos);
    const float fp = shade::footprint(f, u);
    const float t = u.time * DRIFT_SPEED;

    // --- Capa 3a: remolinos ---
    // deformo la coordenada ANTES de calcular las bandas para que se enrollen
    vec3 p = dir;
    float roseMask = 0.0f;
    vec3 roseColor(0.0f);
    for (const Storm& s : STORMS) {
        vec3 d = dir - s.axis;
        float r = length(d) / s.radius;
        if (r > 2.6f) continue;
        p = swirlAround(p, s.axis, s.swirl * std::exp(-r * r * 1.1f));

        // --- Capa 3b: la rosa ---
        // en coordenadas polares: seno de (ángulo + radio) da una espiral de pétalos
        if (r < 1.0f) {
            vec2 uv = noise::tangentCoords(dir, s.axis, 1.0f / s.radius);
            float angle = std::atan2(uv.y, uv.x);
            float petals = 0.5f + 0.5f * std::sin(angle * 5.0f + r * 15.0f * (s.swirl > 0.0f ? 1.0f : -1.0f));
            float mask = 1.0f - smoothstep(0.72f, 1.0f, r);
            vec3 c = mix(vec3(0.96f, 0.46f, 0.66f), vec3(1.0f, 0.86f, 0.90f), petals * 0.75f + r * 0.25f);
            c = mix(vec3(1.0f, 0.97f, 0.94f), c, smoothstep(0.03f, 0.26f, r));  // ojo claro de la tormenta
            roseColor = mix(roseColor, c, mask);
            roseMask = max(roseMask, mask);
        }
    }

    // corrientes: cada latitud gira a distinta velocidad, como en Júpiter
    float shear = t * (1.0f + 0.7f * std::sin(p.y * 11.0f));
    p = vec3(p.x * std::cos(shear) - p.z * std::sin(shear), p.y, p.x * std::sin(shear) + p.z * std::cos(shear));

    // --- Capa 2: turbulencia ---
    // comprimo en y (multiplico y por mucho) para que las nubes se alarguen en longitud
    vec3 stretched(p.x * 1.6f, p.y * TURBULENCE_STRETCH * 1.6f, p.z * 1.6f);
    int turbOctaves = noise::octavesFor(TURBULENCE_STRETCH, fp, 6);
    float wobble = noise::fbm(stretched, min(turbOctaves, 4), u.seed + 1u);
    float clouds = noise::fbm(stretched * 2.3f + vec3(wobble * 2.4f), turbOctaves, u.seed + 2u);

    // --- Capa 1: bandas ---
    // la latitud (ya deformada) elige el color. El wobble hace que el borde ondule.
    float lat = p.y + wobble * BAND_WOBBLE + clouds * 0.018f;
    vec3 albedo = bandColor(lat * BAND_COUNT * 0.5f + 0.5f + 0.13f);

    // variaciones finas dentro de la banda: unas hebras más claras y otras más oscuras
    albedo *= 0.95f + clouds * 0.20f;
    float wisps = smoothstep(0.08f, 0.32f, clouds);
    albedo = mix(albedo, vec3(1.0f, 0.97f, 0.96f), wisps * 0.42f);

    // tormentas encima de las bandas
    albedo = mix(albedo, roseColor, roseMask * 0.94f);

    // --- Capa 4: óvalos blancos ---
    // viven en una sola banda. Los aplano en y para que sean óvalos y no bolitas.
    {
        vec3 q = vec3(dir.x, dir.y * 3.2f, dir.z);
        noise::scatterOnSphere(normalize(q), 16.0f, u.seed + 31u, [&](const vec3& center, uint32_t h) {
            if (std::fabs(dir.y + 0.46f) > 0.13f || noise::toUnit(h) > 0.55f) return;
            float d = length(normalize(q) - center) * 16.0f / (0.22f + 0.2f * noise::toUnit(noise::nextHash(h)));
            float oval = 1.0f - smoothstep(0.55f, 1.0f, d);
            albedo = mix(albedo, vec3(1.0f, 0.98f, 0.96f), oval * 0.88f);
        });
    }

    // --- Capa 5: bruma ---
    // polos desaturados y un velo celeste que crece al llegar al borde del disco
    float mu = shade::saturate(dot(N, V));
    float polar = smoothstep(0.62f, 0.98f, std::fabs(dir.y) + clouds * 0.05f);
    albedo = mix(albedo, vec3(0.86f, 0.87f, 0.98f), polar * HAZE_STRENGTH);
    albedo = mix(albedo, vec3(0.80f, 0.90f, 1.0f), std::pow(1.0f - mu, 3.0f) * 0.55f);

    // sombra del anillo: disparo un rayo al sol y veo dónde pega en el plano del anillo.
    // Si cae dentro del anillo, oscurezco según la densidad en ese radio.
    float shadow = 1.0f;
    if (u.ringOuter > 0.0f) {
        float denom = dot(u.ringNormal, L);
        if (std::fabs(denom) > 1e-4f) {
            float along = dot(u.center - f.worldPos, u.ringNormal) / denom;
            if (along > 0.0f) {
                float r = length(f.worldPos + L * along - u.center) / u.radius;
                float ringT = (r - u.ringInner) / (u.ringOuter - u.ringInner);
                if (ringT > 0.0f && ringT < 1.0f) shadow = 1.0f - ringDensity(ringT, u.seed) * 0.62f;
            }
        }
    }

    return shade::opaque(shade::pastelLight(albedo, N, L, V, shadow));
}
