#include "scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

#include "mesh.h"

using glm::vec3;

namespace {

// Índices de los cuerpos. La luna va al final porque depende de Malva.
enum { HELIA, MALVA, PEONIA, FLORALIA, NENUFAR, HORTENSIA, JAZMIN };

glm::mat4 rotationOf(const Body& b, float time) {
    // primero gira sobre su eje, después ese eje se inclina
    glm::mat4 r = glm::rotate(glm::mat4(1.0f), b.axialTilt, vec3(0.0f, 0.0f, 1.0f));
    return glm::rotate(r, time * b.spinSpeed, vec3(0.0f, 1.0f, 0.0f));
}

}  // namespace

Scene::Scene() {
    // Tamaños y distancias NO están a escala real: si no, en la vista del sistema no se
    // vería casi nada. Aquí se puede cambiar cualquier radio, velocidad o color de halo.
    //            nombre       tipo                      shader         radio orbR  orbV    fase  incl  spin   eje   padre  halo                   haloR anilloIn Out seed
    bodyList = {
        {"Helia",     "Estrella",         starShader,    2.6f, 0.0f,  0.0f,   0.0f, 0.0f,  0.04f, 0.12f, -1, {0.0f, 0.0f, 0.0f},    0.0f,  0.0f, 0.0f, 101u},
        {"Malva",     "Planeta rocoso",   rockyShader,   0.85f, 8.8f,  0.070f, 1.5f, 0.03f, 0.12f, 0.35f, -1, {0.82f, 0.72f, 1.0f},  1.11f, 0.0f, 0.0f, 202u},
        {"Peonia",    "Gigante gaseoso",  gasShader,     1.9f, 24.5f, 0.022f, 2.74f, 0.02f, 0.20f, -0.42f, -1, {1.0f, 0.78f, 0.88f},  1.07f, 1.42f, 2.45f, 303u},
        {"Floralia",  "Planeta jardin",   gardenShader,  1.05f, 12.6f, 0.052f, 4.04f, -0.03f, 0.06f, 0.22f, -1, {0.70f, 0.92f, 1.0f},  1.13f, 0.0f, 0.0f, 404u},
        {"Nenufar",   "Oceano de nacar",  oceanShader,   1.15f, 16.0f, 0.040f, 3.3f, 0.02f, 0.05f, 0.18f, -1, {0.66f, 0.86f, 1.0f},  1.14f, 0.0f, 0.0f, 505u},
        {"Hortensia", "Mundo de cristal", crystalShader, 0.8f, 19.4f, 0.031f, 5.17f, -0.02f, 0.09f, 0.50f, -1, {0.96f, 0.76f, 1.0f},  1.10f, 0.0f, 0.0f, 606u},
        {"Jazmin",    "Luna de Malva",    moonShader,    0.26f, 2.05f, 0.30f,  1.0f, 0.20f, 0.09f, 0.10f, MALVA, {0.0f, 0.0f, 0.0f}, 0.0f,  0.0f, 0.0f, 707u},
    };

    // Vistas: las teclas 1..9 y 0 las recorren. El azimuth se mide desde el sol, así la
    // parte iluminada siempre queda hacia la cámara aunque el planeta vaya orbitando.
    //          nombre                 archivo                 cuerpo     compañero  marco  azim  elev  tiempo
    viewList = {
        {"Helia, la estrella",   "01-helia-estrella.png",     HELIA,     -1,       1.42f, 0.0f,  12.0f, 8.0f},
        {"Malva, rocoso",        "02-malva-rocoso.png",       MALVA,     -1,       1.14f, 34.0f, 16.0f, 9.0f},
        {"Peonia, gaseoso",      "03-peonia-gaseoso.png",     PEONIA,    -1,       2.05f, 30.0f, 19.0f, 12.0f},
        {"Floralia, jardin",     "04-floralia-jardin.png",    FLORALIA,  -1,       1.16f, 30.0f, 14.0f, 12.0f},
        {"Nenufar, oceano",      "05-nenufar-oceano.png",     NENUFAR,   -1,       1.16f, 28.0f, 12.0f, 12.0f},
        {"Hortensia, cristal",   "06-hortensia-cristal.png",  HORTENSIA, -1,       1.14f, 34.0f, 14.0f, 12.0f},
        {"Jazmin, la luna",      "07-jazmin-luna.png",        JAZMIN,    -1,       1.25f, 22.0f, 10.0f, 9.0f},
        {"Malva y su luna",      "08-malva-y-luna.png",       MALVA,     JAZMIN,   0.86f, 26.0f, 10.0f, 9.0f},
        {"Anillos de Peonia",    "09-peonia-anillos.png",     PEONIA,    -1,       1.78f, 62.0f, 18.0f, 12.0f},
        {"Sistema completo",     "10-sistema-completo.png",   -1,        -1,       1.0f,  0.0f,  24.0f, 33.0f},
    };

    sphere = generateSphere(128, 72);
    shell = generateSphere(96, 48);  // los halos no necesitan tanta malla
    ringMeshes.resize(bodyList.size());
    for (size_t i = 0; i < bodyList.size(); ++i) {
        if (bodyList[i].ringOuter > 0.0f) ringMeshes[i] = generateRing(bodyList[i].ringInner, bodyList[i].ringOuter, 256);
    }
}

vec3 Scene::bodyPosition(int index, float time) const {
    const Body& b = bodyList[index];
    vec3 parentPos = b.parent >= 0 ? bodyPosition(b.parent, time) : vec3(0.0f);
    if (b.orbitRadius <= 0.0f) return parentPos;
    float angle = b.orbitPhase + time * b.orbitSpeed;
    vec3 flat(std::cos(angle) * b.orbitRadius, 0.0f, std::sin(angle) * b.orbitRadius);
    // inclino la órbita rotando alrededor de x
    return parentPos + vec3(flat.x, -flat.z * std::sin(b.orbitTilt), flat.z * std::cos(b.orbitTilt));
}

Camera Scene::cameraFor(const ViewPreset& view, float time, float aspect, float orbitYaw, float orbitPitch, float zoom) const {
    Camera cam;
    cam.fovY = glm::radians(30.0f);
    const float pitchRad = glm::clamp(glm::radians(view.elevation) + orbitPitch, glm::radians(-85.0f), glm::radians(85.0f));

    float distance;
    float baseYaw;
    if (view.body < 0) {
        // sistema completo: tiene que caber la órbita más grande
        const Body& outer = bodyList[PEONIA];
        float reach = outer.orbitRadius + outer.radius * outer.ringOuter;
        cam.target = vec3(0.0f);
        // en horizontal hay más espacio, por eso divido entre el aspect
        distance = reach * 1.04f / (std::tan(cam.fovY * 0.5f) * aspect);
        baseYaw = glm::radians(view.azimuth);
    } else {
        const Body& b = bodyList[view.body];
        vec3 pos = bodyPosition(view.body, time);
        float halfExtent = b.radius * view.frameRadius;
        cam.target = pos;
        if (view.companion >= 0) {
            // encuadro la pareja: miro al punto medio, y el marco es lo que mide la pareja
            vec3 other = bodyPosition(view.companion, time);
            cam.target = (pos + other) * 0.5f;
            halfExtent = (glm::length(other - pos) * 0.5f + std::max(b.radius, bodyList[view.companion].radius)) * view.frameRadius;
        }
        distance = halfExtent / std::sin(cam.fovY * 0.5f);
        if (view.body == HELIA) {
            baseYaw = glm::radians(view.azimuth);
        } else {
            vec3 toSun = glm::normalize(-pos);
            baseYaw = std::atan2(toSun.x, toSun.z) + glm::radians(view.azimuth);
        }
    }
    distance *= zoom;

    const float yaw = baseYaw + orbitYaw;
    vec3 offset(std::sin(yaw) * std::cos(pitchRad), std::sin(pitchRad), std::cos(yaw) * std::cos(pitchRad));
    cam.eye = cam.target + offset * distance;
    return cam;
}

std::vector<DrawCall> Scene::buildDraws(const ViewPreset& view, float time, const Camera& camera, int framebufferHeight) const {
    std::vector<DrawCall> opaques, transparents;
    const float pixelSize = 2.0f * std::tan(camera.fovY * 0.5f) / framebufferHeight;

    for (size_t i = 0; i < bodyList.size(); ++i) {
        const Body& b = bodyList[i];
        const int index = static_cast<int>(i);
        if (view.body >= 0 && index != view.body && index != view.companion) continue;
        const vec3 pos = bodyPosition(index, time);
        const glm::mat4 rot = rotationOf(b, time);

        Uniforms u;
        u.rotation = glm::mat3(rot);
        u.center = pos;
        u.radius = b.radius;
        u.cameraPos = camera.eye;
        u.lightPos = vec3(0.0f);  // el sol está en el origen
        u.time = time;
        u.pixelSize = pixelSize;
        u.seed = b.seed;

        // Malva y Jazmín se hacen sombra entre sí (eclipses)
        if (static_cast<int>(i) == MALVA) {
            u.occluderCenter = bodyPosition(JAZMIN, time);
            u.occluderRadius = bodyList[JAZMIN].radius;
        } else if (static_cast<int>(i) == JAZMIN) {
            u.occluderCenter = bodyPosition(MALVA, time);
            u.occluderRadius = bodyList[MALVA].radius;
        }

        // el anillo no gira con el planeta: sale solo de la inclinación del eje
        glm::mat4 tiltOnly = glm::rotate(glm::mat4(1.0f), b.axialTilt, vec3(0.0f, 0.0f, 1.0f));
        if (b.ringOuter > 0.0f) {
            u.ringInner = b.ringInner;
            u.ringOuter = b.ringOuter;
            u.ringNormal = glm::normalize(glm::mat3(tiltOnly) * vec3(0.0f, 1.0f, 0.0f));
        }

        DrawCall body;
        body.mesh = &sphere;
        body.model = glm::scale(glm::translate(glm::mat4(1.0f), pos) * rot, vec3(b.radius));
        body.uniforms = u;
        body.shader = b.shader;
        opaques.push_back(body);

        if (b.ringOuter > 0.0f) {
            DrawCall ring;
            ring.mesh = &ringMeshes[i];
            ring.model = glm::scale(glm::translate(glm::mat4(1.0f), pos) * tiltOnly, vec3(b.radius));
            ring.uniforms = u;
            ring.uniforms.rotation = glm::mat3(tiltOnly);
            // ahora el que tapa la luz es el propio planeta
            ring.uniforms.occluderCenter = pos;
            ring.uniforms.occluderRadius = b.radius;
            ring.shader = ringShader;
            ring.transparent = true;
            ring.twoSided = true;
            transparents.push_back(ring);
        }

        if (b.haloRadius > 0.0f) {
            DrawCall halo;
            halo.mesh = &shell;
            halo.model = glm::scale(glm::translate(glm::mat4(1.0f), pos), vec3(b.radius * b.haloRadius));
            halo.uniforms = u;
            halo.uniforms.glowColor = b.haloColor;
            halo.uniforms.shellRadius = b.haloRadius;
            halo.shader = haloShader;
            halo.transparent = true;
            transparents.push_back(halo);
        }

        if (static_cast<int>(i) == HELIA) {
            DrawCall corona;
            corona.mesh = &shell;
            const float coronaRadius = 1.75f;  // el tamaño de la corona, en radios de la estrella
            corona.model = glm::scale(glm::translate(glm::mat4(1.0f), pos), vec3(b.radius * coronaRadius));
            corona.uniforms = u;
            corona.uniforms.shellRadius = coronaRadius;
            corona.shader = coronaShader;
            corona.transparent = true;
            transparents.push_back(corona);
        }
    }

    // Lo transparente se dibuja de lo más lejano a lo más cercano, si no, el blending
    // queda al revés.
    std::sort(transparents.begin(), transparents.end(), [&](const DrawCall& a, const DrawCall& b) {
        return glm::length(a.uniforms.center - camera.eye) > glm::length(b.uniforms.center - camera.eye);
    });
    opaques.insert(opaques.end(), transparents.begin(), transparents.end());
    return opaques;
}

void Scene::drawOrbits(Pipeline& pipeline, Framebuffer& fb, const Camera& camera, int thickness) const {
    const int segments = 256;
    for (size_t i = 0; i < bodyList.size(); ++i) {
        const Body& b = bodyList[i];
        if (b.orbitRadius <= 0.0f || b.parent >= 0) continue;  // solo las de los planetas
        vec3 prev;
        for (int s = 0; s <= segments; ++s) {
            float a = 6.2831853f * s / segments;
            vec3 flat(std::cos(a) * b.orbitRadius, 0.0f, std::sin(a) * b.orbitRadius);
            vec3 p(flat.x, -flat.z * std::sin(b.orbitTilt), flat.z * std::cos(b.orbitTilt));
            if (s > 0) pipeline.drawLine(fb, camera, prev, p, vec3(0.82f, 0.76f, 1.0f), 0.20f, thickness);
            prev = p;
        }
    }
}
