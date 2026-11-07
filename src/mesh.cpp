#include "mesh.h"

#include <glm/gtc/constants.hpp>

#include <cmath>

Model generateSphere(int segments, int rings) {
    Model m;
    const float pi = glm::pi<float>();
    for (int r = 0; r <= rings; ++r) {
        float phi = pi * r / rings;  // 0 en el polo norte, pi en el polo sur
        for (int s = 0; s <= segments; ++s) {
            float theta = 2.0f * pi * s / segments;
            glm::vec3 p(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
            m.vertices.push_back(p);
            m.normals.push_back(p);
        }
    }

    const int stride = segments + 1;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            int a = r * stride + s;
            int b = a + 1;
            int c = a + stride;
            int d = c + 1;
            // el orden importa: es lo que el culling usa para saber qué cara mira a la cámara
            if (r > 0) m.faces.push_back({{a, c, b}, -1});
            if (r < rings - 1) m.faces.push_back({{b, c, d}, -1});
        }
    }
    return m;
}

Model generateRing(float innerRadius, float outerRadius, int segments) {
    Model m;
    const float pi = glm::pi<float>();
    for (int s = 0; s <= segments; ++s) {
        float theta = 2.0f * pi * s / segments;
        glm::vec3 dir(std::cos(theta), 0.0f, std::sin(theta));
        m.vertices.push_back(dir * innerRadius);
        m.vertices.push_back(dir * outerRadius);
        m.normals.push_back({0.0f, 1.0f, 0.0f});
        m.normals.push_back({0.0f, 1.0f, 0.0f});
    }
    for (int s = 0; s < segments; ++s) {
        int a = s * 2;
        m.faces.push_back({{a, a + 2, a + 1}, -1});
        m.faces.push_back({{a + 1, a + 2, a + 3}, -1});
    }
    return m;
}
