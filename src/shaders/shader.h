#pragma once

#include <glm/glm.hpp>

#include <cstdint>

#include "../noise.h"
#include "../triangle.h"

// Lo que el renderer le pasa a cada shader. Son los mismos datos de siempre:
// posición, normal, luz, cámara y tiempo. Ninguna imagen, ningún material.
struct Uniforms {
    glm::mat3 rotation{1.0f};     // object space -> world space (sin escala)
    glm::vec3 center{0.0f};       // centro del cuerpo en el mundo
    float radius = 1.0f;          // radio del cuerpo en el mundo
    glm::vec3 cameraPos{0.0f};
    glm::vec3 lightPos{0.0f};     // el sol
    float time = 0.0f;
    float pixelSize = 0.001f;     // tamaño de un píxel a distancia 1
    uint32_t seed = 0u;           // cada cuerpo tiene su seed fija

    // otra esfera que le puede hacer sombra a este cuerpo (radio 0 = ninguna)
    glm::vec3 occluderCenter{0.0f};
    float occluderRadius = 0.0f;

    // datos del anillo, para que el gigante gaseoso reciba su sombra
    glm::vec3 ringNormal{0.0f, 1.0f, 0.0f};
    float ringInner = 0.0f;       // en radios del planeta (0 = sin anillo)
    float ringOuter = 0.0f;

    // para los shells de atmósfera
    glm::vec3 glowColor{1.0f};
    float shellRadius = 1.0f;     // radio del shell en radios del cuerpo
};

using FragmentShader = glm::vec4 (*)(const Fragment&, const Uniforms&);

// --- Shaders de cuerpos (cada uno en su propio .cpp dentro de src/shaders/) ---
glm::vec4 starShader(const Fragment& f, const Uniforms& u);      // Helia, la estrella
glm::vec4 rockyShader(const Fragment& f, const Uniforms& u);     // Malva, planeta rocoso
glm::vec4 gasShader(const Fragment& f, const Uniforms& u);       // Peonía, gigante gaseoso
glm::vec4 gardenShader(const Fragment& f, const Uniforms& u);    // Floralia, jardín de flores
glm::vec4 oceanShader(const Fragment& f, const Uniforms& u);     // Nenúfar, océano de nácar
glm::vec4 crystalShader(const Fragment& f, const Uniforms& u);   // Hortensia, mundo de cristal
glm::vec4 moonShader(const Fragment& f, const Uniforms& u);      // Jazmín, la luna
glm::vec4 ringShader(const Fragment& f, const Uniforms& u);      // los anillos de Peonía
glm::vec4 coronaShader(const Fragment& f, const Uniforms& u);    // corona de Helia
glm::vec4 haloShader(const Fragment& f, const Uniforms& u);      // brillo de atmósfera

// Fondo: nebulosa pastel y estrellitas. Solo corre donde no hay ningún cuerpo.
glm::vec3 nebulaShader(const glm::vec3& dir);
glm::vec3 starsShader(const glm::vec3& dir, float pixelSize, int x, int y);

// Densidad del anillo en t = 0 (borde interno) .. 1 (borde externo).
// Va aparte porque el planeta también la usa para calcular la sombra que recibe.
float ringDensity(float t, uint32_t seed);

// --- Helpers compartidos ---
namespace shade {

inline float saturate(float v) { return glm::clamp(v, 0.0f, 1.0f); }

inline glm::vec4 opaque(const glm::vec3& c) { return glm::vec4(glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f)), 1.0f); }

// Cuánto mide un píxel sobre la superficie, en radios del cuerpo. Con esto apago el
// detalle que no se alcanza a ver, así el planeta lejano no parpadea.
inline float footprint(const Fragment& f, const Uniforms& u) {
    return glm::length(u.cameraPos - f.worldPos) * u.pixelSize / u.radius;
}

// Paleta con cosenos (la de Iñigo Quilez): perfecta para el arcoíris pastel del nácar.
inline glm::vec3 cosPalette(float t, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) {
    return a + b * glm::cos(6.28318f * (c * t + d));
}

// Sombra suave de una esfera: mira qué tan cerca pasa de ella el rayo hacia el sol.
// Devuelve 1 = totalmente iluminado, 0 = sombra total.
inline float sphereShadow(const glm::vec3& pos, const glm::vec3& lightDir, const glm::vec3& center, float radius) {
    if (radius <= 0.0f) return 1.0f;
    glm::vec3 toCenter = center - pos;
    float along = glm::dot(toCenter, lightDir);
    if (along <= 0.0f) return 1.0f;  // la esfera está para el otro lado
    float missDist = glm::length(toCenter - lightDir * along);
    return glm::smoothstep(radius * 0.72f, radius * 1.25f, missDist);  // borde suave a propósito
}

// Iluminación que uso en todos los planetas. En vez de irse a negro, la sombra se
// vuelve violeta: da ese look de ilustración y mantiene la vibe pastel.
// shadowAmount: 1 = lit, 0 = en sombra. specular/shininess: brillo tipo agua o cristal.
inline glm::vec3 pastelLight(const glm::vec3& albedo, const glm::vec3& N, const glm::vec3& L, const glm::vec3& V,
                             float shadowAmount = 1.0f, float specular = 0.0f, float shininess = 60.0f) {
    float ndl = glm::dot(N, L);
    // smoothstep en vez de max(0, ndl): el terminador queda suave, no una línea dura
    float diff = glm::smoothstep(-0.18f, 0.75f, ndl) * shadowAmount;
    glm::vec3 shadowSide = albedo * glm::vec3(0.20f, 0.17f, 0.36f);  // baja el valor, deja lo violeta
    glm::vec3 litSide = albedo * glm::vec3(1.04f, 1.0f, 0.97f);      // la luz del sol es apenas cálida
    glm::vec3 c = glm::mix(shadowSide, litSide, diff);
    if (specular > 0.0f && ndl > 0.0f) {
        glm::vec3 H = glm::normalize(L + V);
        c += glm::vec3(1.0f, 0.97f, 0.94f) * specular * std::pow(saturate(glm::dot(N, H)), shininess) * shadowAmount;
    }
    return c;
}

// Normal con bump: mueve la normal según la pendiente de una función de altura.
// Evalúa la altura 3 veces (diferencias finitas), así que usar pocas octavas acá.
template <class HeightFn>
inline glm::vec3 bumpNormal(const glm::vec3& dir, const glm::mat3& rotation, float eps, float strength, HeightFn&& height) {
    glm::vec3 helper = std::fabs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    glm::vec3 t = glm::normalize(glm::cross(helper, dir));
    glm::vec3 b = glm::cross(dir, t);
    float h0 = height(dir);
    float du = (height(glm::normalize(dir + t * eps)) - h0) / eps;
    float dv = (height(glm::normalize(dir + b * eps)) - h0) / eps;
    return rotation * glm::normalize(dir - (t * du + b * dv) * strength);
}

}  // namespace shade
