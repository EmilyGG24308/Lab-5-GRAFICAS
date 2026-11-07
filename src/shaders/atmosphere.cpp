// Halo de atmósfera: un shell un poco más grande que el planeta, que solo suma luz.
// Es lo que despega cada planeta del fondo y le da ese borde suave de ilustración.
#include "shader.h"

using namespace glm;

vec4 haloShader(const Fragment& f, const Uniforms& u) {
    vec3 rayDir = normalize(f.worldPos - u.cameraPos);
    vec3 toCenter = u.center - u.cameraPos;
    vec3 closest = u.cameraPos + rayDir * dot(toCenter, rayDir);
    vec3 radial = closest - u.center;
    float height = length(radial) / u.radius - 1.0f;  // negativo = encima del disco
    float maxHeight = u.shellRadius - 1.0f;

    // afuera: se apaga hacia el exterior. adentro: un rim fino que entra al disco.
    float outside = 1.0f - shade::saturate(height / maxHeight);
    float inside = 1.0f - smoothstep(0.0f, 0.08f, -height);
    float glow = height >= 0.0f ? outside * outside : inside * 0.20f;

    // el lado de noche casi no tiene halo, por eso miro hacia dónde queda el sol
    vec3 L = normalize(u.lightPos - u.center);
    float lit = glm::smoothstep(-0.45f, 0.55f, dot(normalize(radial), L));
    vec3 color = u.glowColor * glow * (0.10f + 0.90f * lit);
    return vec4(color * 0.48f, 0.0f);  // baja este 0.48 si el planeta se ve lavado
}
