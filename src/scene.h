#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

#include "obj_loader.h"
#include "pipeline.h"

// El sistema Flor de Helia: una estrella, cinco planetas, una luna y un anillo.
struct Body {
    std::string name;
    std::string kind;
    FragmentShader shader;
    float radius;
    float orbitRadius;   // 0 = no orbita
    float orbitSpeed;    // radianes por segundo
    float orbitPhase;    // ángulo inicial, para que no salgan todos en línea
    float orbitTilt;     // inclinación de la órbita (radianes)
    float spinSpeed;     // rotación sobre su propio eje
    float axialTilt;     // inclinación del eje
    int parent;          // índice del cuerpo que orbita (-1 = el centro)
    glm::vec3 haloColor; // color del halo de atmósfera
    float haloRadius;    // tamaño del halo en radios (0 = sin halo)
    float ringInner;     // anillo en radios del planeta (0 = sin anillo)
    float ringOuter;
    uint32_t seed;
};

// Una vista de la cámara: a qué mira y cómo lo encuadra.
struct ViewPreset {
    std::string name;
    std::string file;      // nombre del PNG en modo captura
    int body;              // cuerpo al que sigue (-1 = sistema completo)
    int companion;         // otro cuerpo que debe entrar en el cuadro (-1 = ninguno)
    float frameRadius;     // qué tanto hay que encuadrar, en radios del cuerpo
    float azimuth;         // grados, medidos desde la dirección del sol
    float elevation;       // grados sobre el plano orbital
    float captureTime;     // el tiempo fijo que se usa al capturar
};

class Scene {
public:
    Scene();

    const std::vector<Body>& bodies() const { return bodyList; }
    const std::vector<ViewPreset>& views() const { return viewList; }

    glm::vec3 bodyPosition(int index, float time) const;

    // Calcula a dónde debe mirar la cámara en una vista. orbitYaw, orbitPitch y zoom
    // son lo que el usuario movió con el mouse encima de la vista base.
    Camera cameraFor(const ViewPreset& view, float time, float aspect, float orbitYaw, float orbitPitch, float zoom) const;

    // Arma la lista de draw calls de un frame (ya en el orden correcto). En las vistas de
    // un cuerpo solo se dibuja ese y su compañero, para que nada estorbe en el cuadro.
    std::vector<DrawCall> buildDraws(const ViewPreset& view, float time, const Camera& camera, int framebufferHeight) const;

    // Dibuja las líneas de las órbitas, solo para la vista del sistema.
    void drawOrbits(Pipeline& pipeline, Framebuffer& fb, const Camera& camera, int thickness) const;

private:
    std::vector<Body> bodyList;
    std::vector<ViewPreset> viewList;
    Model sphere;
    Model shell;
    std::vector<Model> ringMeshes;  // uno por cada cuerpo (vacío si no tiene anillo)
};
