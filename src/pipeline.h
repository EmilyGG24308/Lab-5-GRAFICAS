#pragma once

#include <glm/glm.hpp>

#include <vector>

#include "framebuffer.h"
#include "obj_loader.h"
#include "shaders/shader.h"
#include "triangle.h"

// Cámara con perspectiva. pixelSize es el tamaño de un píxel a distancia 1:
// lo usan los shaders para saber cuánto detalle vale la pena calcular.
struct Camera {
    glm::vec3 eye{0.0f, 0.0f, 10.0f};
    glm::vec3 target{0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovY = glm::radians(30.0f);
    float nearPlane = 0.05f;
    float farPlane = 500.0f;
};

// Un objeto para dibujar: geometría + shader + sus parámetros.
struct DrawCall {
    const Model* mesh = nullptr;
    glm::mat4 model{1.0f};
    Uniforms uniforms;
    FragmentShader shader = nullptr;
    bool transparent = false;  // true = blending, sin escribir en el z-buffer
    bool twoSided = false;     // true = sin back-face culling
};

class Pipeline {
public:
    // Corre el vertex shader de todos los objetos y dibuja el frame completo:
    // opacos -> cielo en lo que quedó vacío -> transparentes de atrás hacia adelante.
    // Los transparentes deben venir ya ordenados dentro de "draws".
    void render(Framebuffer& fb, const Camera& camera, const std::vector<DrawCall>& draws);

    // Línea 3D con z-test y alpha, para dibujar las órbitas.
    void drawLine(Framebuffer& fb, const Camera& camera, const glm::vec3& a, const glm::vec3& b,
                  const glm::vec3& color, float alpha, int thickness);

    // cuántos triángulos sobrevivieron al culling en el último frame (para medir)
    size_t lastTriangleCount() const { return opaque.size() + transparent.size(); }

private:
    std::vector<ScreenTriangle> opaque;
    std::vector<ScreenTriangle> transparent;
};
