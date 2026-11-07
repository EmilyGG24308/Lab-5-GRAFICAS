#include "pipeline.h"

#include <glm/gtc/matrix_transform.hpp>

#include <atomic>
#include <cmath>
#include <thread>

namespace {

// Vértice en clip space, antes de dividir entre w.
struct ClipVertex {
    glm::vec4 clip;
    glm::vec3 obj, world, normal;
};

// El vertex shader: pasa de object space a clip space y prepara los varyings.
// No hace nada de color, todo el look sale del fragment shader.
ClipVertex vertexShader(const glm::vec3& pos, const glm::vec3& normal, const DrawCall& draw,
                        const glm::mat4& viewProj) {
    ClipVertex out;
    glm::vec4 world = draw.model * glm::vec4(pos, 1.0f);
    out.clip = viewProj * world;
    out.obj = pos;
    out.world = glm::vec3(world);
    out.normal = draw.uniforms.rotation * normal;
    return out;
}

ClipVertex lerpClip(const ClipVertex& a, const ClipVertex& b, float t) {
    return {a.clip + (b.clip - a.clip) * t, a.obj + (b.obj - a.obj) * t,
            a.world + (b.world - a.world) * t, a.normal + (b.normal - a.normal) * t};
}

ScreenVertex toScreen(const ClipVertex& v, int width, int height) {
    float invW = 1.0f / v.clip.w;
    ScreenVertex s;
    s.x = (v.clip.x * invW * 0.5f + 0.5f) * width;
    s.y = (1.0f - (v.clip.y * invW * 0.5f + 0.5f)) * height;  // en pantalla y va para abajo
    s.z = v.clip.z * invW;
    s.invW = invW;
    s.obj = v.obj * invW;
    s.world = v.world * invW;
    s.normal = v.normal * invW;
    return s;
}

// Recorte contra el near plane (Sutherland-Hodgman, solo ese plano). Sin esto, un
// triángulo que pasa por detrás de la cámara se proyecta volteado y sale cualquier cosa.
int clipNear(const ClipVertex in[3], ClipVertex out[4]) {
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        const ClipVertex& cur = in[i];
        const ClipVertex& next = in[(i + 1) % 3];
        float dCur = cur.clip.z + cur.clip.w;
        float dNext = next.clip.z + next.clip.w;
        if (dCur >= 0.0f) out[count++] = cur;
        if ((dCur >= 0.0f) != (dNext >= 0.0f)) out[count++] = lerpClip(cur, next, dCur / (dCur - dNext));
    }
    return count;
}

void processDraw(const DrawCall& draw, int drawIndex, const glm::mat4& viewProj, const Framebuffer& fb,
                 std::vector<ScreenTriangle>& output) {
    const Model& mesh = *draw.mesh;
    std::vector<ClipVertex> verts(mesh.vertices.size());
    for (size_t i = 0; i < verts.size(); ++i)
        verts[i] = vertexShader(mesh.vertices[i], mesh.normals[i], draw, viewProj);

    for (const Face& face : mesh.faces) {
        ClipVertex in[3] = {verts[face.vertexIndices[0]], verts[face.vertexIndices[1]],
                            verts[face.vertexIndices[2]]};
        ClipVertex clipped[4];
        int n = clipNear(in, clipped);
        for (int i = 1; i + 1 < n; ++i) {
            ScreenTriangle t;
            t.a = toScreen(clipped[0], fb.width, fb.height);
            t.b = toScreen(clipped[i], fb.width, fb.height);
            t.c = toScreen(clipped[i + 1], fb.width, fb.height);
            t.draw = drawIndex;
            if (setupTriangle(t, fb.width, fb.height, draw.twoSided)) output.push_back(t);
        }
    }
}

}  // namespace

void Pipeline::render(Framebuffer& fb, const Camera& camera, const std::vector<DrawCall>& draws) {
    const glm::mat4 view = glm::lookAt(camera.eye, camera.target, camera.up);
    const glm::mat4 proj = glm::perspective(camera.fovY, static_cast<float>(fb.width) / fb.height,
                                            camera.nearPlane, camera.farPlane);
    const glm::mat4 viewProj = proj * view;

    opaque.clear();
    transparent.clear();
    for (size_t i = 0; i < draws.size(); ++i)
        processDraw(draws[i], static_cast<int>(i), viewProj, fb, draws[i].transparent ? transparent : opaque);

    // base de la cámara para sacar el rayo de cada píxel del cielo
    const glm::vec3 forward = glm::normalize(camera.target - camera.eye);
    const glm::vec3 right = glm::normalize(glm::cross(forward, camera.up));
    const glm::vec3 upVec = glm::cross(right, forward);
    const float pixelSize = 2.0f * std::tan(camera.fovY * 0.5f) / fb.height;

    // Reparto la pantalla en bandas horizontales. Cada hilo agarra la siguiente banda
    // libre y dibuja TODO lo de esa banda; como dos hilos nunca tocan la misma fila, no
    // hace falta ningún lock y el resultado no depende de cuántos hilos haya.
    const int bandHeight = 24;
    const int bandCount = (fb.height + bandHeight - 1) / bandHeight;
    std::atomic<int> nextBand{0};

    const int SKY_STEP = 8;
    auto worker = [&]() {
        std::vector<glm::vec3> nebula;       // cada hilo tiene su propia grilla
        std::vector<unsigned char> nebulaReady;
        for (int band = nextBand++; band < bandCount; band = nextBand++) {
            const int y0 = band * bandHeight;
            const int y1 = std::min(fb.height - 1, y0 + bandHeight - 1);

            for (const ScreenTriangle& t : opaque) {
                const DrawCall& d = draws[t.draw];
                triangle(fb, t, y0, y1, false, [&](const Fragment& f) { return d.shader(f, d.uniforms); });
            }

            // el cielo va después: solo se calcula donde nadie pintó (z = infinito)
            auto rayAt = [&](float x, float y) {
                return glm::normalize(forward + right * ((x + 0.5f - fb.width * 0.5f) * pixelSize) +
                                      upVec * ((fb.height * 0.5f - y - 0.5f) * pixelSize));
            };
            // grilla de nebulosa: las esquinas caen en múltiplos de SKY_STEP, así dos bandas
            // vecinas calculan los mismos valores en su borde y no aparece ninguna costura
            const int rows = (y1 / SKY_STEP) - (y0 / SKY_STEP) + 2;
            const int cols = (fb.width - 1) / SKY_STEP + 2;
            nebula.resize(static_cast<size_t>(rows) * cols);
            std::fill(nebulaReady.begin(), nebulaReady.end(), 0);
            nebulaReady.resize(nebula.size(), 0);
            auto corner = [&](int col, int row) -> const glm::vec3& {
                size_t k = static_cast<size_t>(row) * cols + col;
                if (!nebulaReady[k]) {  // solo si de verdad se usa (lazy)
                    nebula[k] = nebulaShader(rayAt(static_cast<float>(col * SKY_STEP), static_cast<float>((row + y0 / SKY_STEP) * SKY_STEP)));
                    nebulaReady[k] = 1;
                }
                return nebula[k];
            };

            for (int y = y0; y <= y1; ++y) {
                const int row = y / SKY_STEP - y0 / SKY_STEP;
                const float fy = static_cast<float>(y % SKY_STEP) / SKY_STEP;
                for (int x = 0; x < fb.width; ++x) {
                    size_t i = fb.index(x, y);
                    if (std::isfinite(fb.zbuffer[i])) continue;
                    const int col = x / SKY_STEP;
                    const float fx = static_cast<float>(x % SKY_STEP) / SKY_STEP;
                    // interpolación bilineal entre las 4 esquinas del bloque
                    glm::vec3 c = glm::mix(glm::mix(corner(col, row), corner(col + 1, row), fx),
                                           glm::mix(corner(col, row + 1), corner(col + 1, row + 1), fx), fy);
                    c = glm::clamp(c + starsShader(rayAt(static_cast<float>(x), static_cast<float>(y)), pixelSize, x, y),
                                   glm::vec3(0.0f), glm::vec3(1.0f));
                    fb.pixels[i] = Color(static_cast<int>(c.r * 255.0f + 0.5f), static_cast<int>(c.g * 255.0f + 0.5f),
                                         static_cast<int>(c.b * 255.0f + 0.5f));
                }
            }

            for (const ScreenTriangle& t : transparent) {
                const DrawCall& d = draws[t.draw];
                triangle(fb, t, y0, y1, true, [&](const Fragment& f) { return d.shader(f, d.uniforms); });
            }
        }
    };

    unsigned threadCount = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> threads;
    threads.reserve(threadCount - 1);
    for (unsigned i = 1; i < threadCount; ++i) threads.emplace_back(worker);
    worker();  // el hilo principal también ayuda
    for (std::thread& t : threads) t.join();
}

void Pipeline::drawLine(Framebuffer& fb, const Camera& camera, const glm::vec3& a, const glm::vec3& b,
                        const glm::vec3& color, float alpha, int thickness) {
    const glm::mat4 viewProj = glm::perspective(camera.fovY, static_cast<float>(fb.width) / fb.height,
                                                camera.nearPlane, camera.farPlane) *
                               glm::lookAt(camera.eye, camera.target, camera.up);
    glm::vec4 ca = viewProj * glm::vec4(a, 1.0f);
    glm::vec4 cb = viewProj * glm::vec4(b, 1.0f);
    if (ca.w <= camera.nearPlane || cb.w <= camera.nearPlane) return;

    glm::vec3 pa = glm::vec3(ca) / ca.w, pb = glm::vec3(cb) / cb.w;
    float ax = (pa.x * 0.5f + 0.5f) * fb.width, ay = (1.0f - (pa.y * 0.5f + 0.5f)) * fb.height;
    float bx = (pb.x * 0.5f + 0.5f) * fb.width, by = (1.0f - (pb.y * 0.5f + 0.5f)) * fb.height;

    int steps = static_cast<int>(std::max(std::fabs(bx - ax), std::fabs(by - ay))) + 1;
    if (steps > 4000) return;
    glm::vec3 premul = color * alpha;
    for (int s = 0; s <= steps; ++s) {
        float t = static_cast<float>(s) / steps;
        int cx = static_cast<int>(ax + (bx - ax) * t);
        int cy = static_cast<int>(ay + (by - ay) * t);
        float z = pa.z + (pb.z - pa.z) * t;
        for (int oy = 0; oy < thickness; ++oy) {
            for (int ox = 0; ox < thickness; ++ox) {
                int x = cx + ox, y = cy + oy;
                if (x < 0 || y < 0 || x >= fb.width || y >= fb.height) continue;
                size_t i = fb.index(x, y);
                if (z < fb.zbuffer[i]) fb.blend(i, premul.r, premul.g, premul.b, alpha);
            }
        }
    }
}
