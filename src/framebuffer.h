#pragma once

#include <SDL.h>

#include <algorithm>
#include <limits>
#include <vector>

#include "color.h"

// Framebuffer en memoria (CPU). Todo el dibujo se hace aquí; SDL solo se usa
// para mostrar el resultado en la ventana.
class Framebuffer {
public:
    Framebuffer(int width, int height)
        : width(width), height(height),
          pixels(static_cast<size_t>(width) * height),
          zbuffer(static_cast<size_t>(width) * height) {}

    ~Framebuffer() {
        if (texture) SDL_DestroyTexture(texture);
    }

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    // Crea la textura donde se copiará el framebuffer cada frame.
    // ojo: esta "texture" es solo el buffer que SDL usa para enseñar los píxeles,
    // no se usa para darle apariencia a ningún cuerpo.
    bool init(SDL_Renderer* renderer) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING, width, height);
        return texture != nullptr;
    }

    void clear(const Color& color) {
        std::fill(pixels.begin(), pixels.end(), color);
        std::fill(zbuffer.begin(), zbuffer.end(), std::numeric_limits<float>::infinity());
    }

    size_t index(int x, int y) const { return static_cast<size_t>(y) * width + x; }

    // Pinta un píxel si está dentro de la pantalla y más cerca que lo ya dibujado.
    // depth: distancia a la cámara (menor = más cerca).
    void point(int x, int y, float depth, const Color& color) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        size_t i = index(x, y);
        if (depth < zbuffer[i]) {
            zbuffer[i] = depth;
            pixels[i] = color;
        }
    }

    // Mezcla premultiplicada: destino = fuente + destino * (1 - alpha).
    // Con alpha = 0 queda una suma pura, que es justo lo que quiero para los glows.
    void blend(size_t i, float r, float g, float b, float alpha) {
        Color& d = pixels[i];
        float keep = 1.0f - alpha;
        d = Color(static_cast<int>(r * 255.0f + d.r * keep + 0.5f),
                  static_cast<int>(g * 255.0f + d.g * keep + 0.5f),
                  static_cast<int>(b * 255.0f + d.b * keep + 0.5f));
    }

    // Sube los píxeles a la textura y la presenta en la ventana.
    void renderBuffer(SDL_Renderer* renderer) {
        SDL_UpdateTexture(texture, nullptr, pixels.data(), width * static_cast<int>(sizeof(Color)));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    }

    const int width;
    const int height;
    std::vector<Color> pixels;   // fila 0 = parte superior de la pantalla
    std::vector<float> zbuffer;

private:
    SDL_Texture* texture = nullptr;
};
