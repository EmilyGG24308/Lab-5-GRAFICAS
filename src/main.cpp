// Lab 5 - Gráficas por computadora: Flor de Helia, un sistema solar de fantasía pastel.
// Todos los cuerpos se pintan con fragment shaders corriendo en la CPU, sobre el
// framebuffer y el rasterizador del Lab 4. SDL2 solo abre la ventana y lee el teclado.
//
// Uso:
//   lab5                       abre la ventana interactiva
//   lab5 --vista N             empieza en la vista N (1 a 9, 0 = sistema completo)
//   lab5 --capturas [carpeta]  genera todas las capturas y termina (por defecto docs/capturas)
//   lab5 --medir [segundos]    recorre las vistas e imprime los FPS de cada una
//   lab5 --tamano ANCHOxALTO   cambia la resolución de la ventana

#include <SDL.h>

#include <glm/glm.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "color.h"
#include "framebuffer.h"
#include "pipeline.h"
#include "png_writer.h"
#include "scene.h"

namespace {

int windowWidth = 1280;
int windowHeight = 720;

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
std::unique_ptr<Framebuffer> framebuffer;
Pipeline pipeline;
const Color CLEAR_COLOR(18, 16, 34);

// La vista inicial de cada tecla (1..9 y 0). La vista "0" es la última de la lista.
int viewForKey(int digit, int viewCount) { return digit == 0 ? viewCount - 1 : digit - 1; }

// Estado de la cámara interactiva.
struct Interaction {
    int view = 9;           // arranca mostrando el sistema completo
    float yaw = 0.0f;       // lo que se giró con el mouse
    float pitch = 0.0f;
    float zoom = 1.0f;
    float time = 33.0f;
    bool paused = false;
    bool showOrbits = true;
    float timeScale = 1.0f;  // la cambian las teclas [ y ]
};

void printControls() {
    std::puts("=== Flor de Helia: controles ===");
    std::puts("  1-6            cuerpos: Helia, Malva, Peonia, Floralia, Nenufar, Hortensia");
    std::puts("  7 / 8 / 9      la luna Jazmin / Malva con su luna / anillos de Peonia");
    std::puts("  0              sistema completo");
    std::puts("  Tab / flechas  vista siguiente o anterior");
    std::puts("  Mouse (arrastrar)  girar la camara alrededor del cuerpo");
    std::puts("  Rueda o + / -  zoom");
    std::puts("  Espacio        pausar o seguir la animacion");
    std::puts("  [ / ]          animacion mas lenta o mas rapida");
    std::puts("  O              mostrar u ocultar las orbitas");
    std::puts("  R              regresar la camara a como estaba");
    std::puts("  C              guardar captura en capturas/");
    std::puts("  Esc            salir");
}

bool initWindow() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    window = SDL_CreateWindow("Flor de Helia", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth,
                              windowHeight, SDL_WINDOW_SHOWN);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    // sin vsync: acá el límite siempre es la CPU, no quiero que espere al monitor
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }
    framebuffer = std::make_unique<Framebuffer>(windowWidth, windowHeight);
    if (!framebuffer->init(renderer)) {
        std::fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

void cleanup() {
    framebuffer.reset();  // destruye la textura antes que el renderer
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

// Dibuja un frame completo en cualquier framebuffer. Se usa igual para la
// ventana y para las capturas, así la captura es lo mismo que se ve en pantalla.
void renderFrame(const Scene& scene, Framebuffer& fb, const ViewPreset& view, float time, float yaw, float pitch,
                 float zoom, bool showOrbits, int lineThickness) {
    const float aspect = static_cast<float>(fb.width) / fb.height;
    Camera camera = scene.cameraFor(view, time, aspect, yaw, pitch, zoom);
    std::vector<DrawCall> draws = scene.buildDraws(view, time, camera, fb.height);
    fb.clear(CLEAR_COLOR);
    pipeline.render(fb, camera, draws);
    if (showOrbits && view.body < 0) scene.drawOrbits(pipeline, fb, camera, lineThickness);
}

// Promedia cada grupo de ss x ss píxeles (supersampling). Sin esto, los bordes de los
// planetas y los pétalos finos se ven con serrucho en las capturas.
std::vector<Color> downsample(const Framebuffer& fb, int ss) {
    const int w = fb.width / ss, h = fb.height / ss;
    std::vector<Color> out(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int r = 0, g = 0, b = 0;
            for (int j = 0; j < ss; ++j) {
                for (int i = 0; i < ss; ++i) {
                    const Color& c = fb.pixels[fb.index(x * ss + i, y * ss + j)];
                    r += c.r;
                    g += c.g;
                    b += c.b;
                }
            }
            int n = ss * ss;
            // dithering al promediar: si solo trunco, el cielo oscuro se ve con escalones
            int d = static_cast<int>(n * std::fmod(52.9829189f * std::fmod(0.06711056f * x + 0.00583715f * y, 1.0f), 1.0f));
            out[static_cast<size_t>(y) * w + x] = Color((r + d) / n, (g + d) / n, (b + d) / n);
        }
    }
    return out;
}

bool saveCapture(const Scene& scene, const ViewPreset& view, const std::string& path, int width, int height, int ss,
                 float time, float yaw, float pitch, float zoom, bool showOrbits = true) {
    Framebuffer fb(width * ss, height * ss);
    auto start = std::chrono::steady_clock::now();
    renderFrame(scene, fb, view, time, yaw, pitch, zoom, showOrbits, ss);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::vector<Color> pixels = downsample(fb, ss);
    bool ok = png::write(path, width, height, pixels);
    std::printf("%s  %s (%.2f s)\n", ok ? "OK   " : "ERROR", path.c_str(), seconds);
    return ok;
}

// Modo --capturas: no abre ventana. Tiempo fijo + seeds fijas = siempre la misma imagen.
int generateCaptures(const Scene& scene, const std::string& folder) {
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    bool ok = true;
    for (const ViewPreset& view : scene.views()) {
        ok = saveCapture(scene, view, folder + "/" + view.file, 1280, 720, 2, view.captureTime, 0.0f, 0.0f, 1.0f) && ok;
    }
    // La captura del concurso: Floralia más de cerca, a más resolución y con 3x de supersampling.
    ViewPreset contest = scene.views()[3];
    ok = saveCapture(scene, contest, folder + "/concurso-floralia.png", 1920, 1080, 3, contest.captureTime,
                     glm::radians(-18.0f), glm::radians(4.0f), 0.92f) && ok;
    return ok ? 0 : 1;
}

// Modo --medir: abre la ventana y recorre todas las vistas para sacar FPS reales.
int measure(const Scene& scene, float secondsPerView) {
    if (!initWindow()) return 1;
    for (const ViewPreset& view : scene.views()) {
        int frames = 0;
        float time = view.captureTime;
        auto start = std::chrono::steady_clock::now();
        double elapsed = 0.0;
        while (elapsed < secondsPerView) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) elapsed = 1e9;
            }
            renderFrame(scene, *framebuffer, view, time, 0.0f, 0.0f, 1.0f, true, 1);
            framebuffer->renderBuffer(renderer);
            time += 0.033f;
            ++frames;
            elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }
        std::printf("%-24s %6.1f FPS\n", view.name.c_str(), frames / elapsed);
    }
    cleanup();
    return 0;
}

int interactive(const Scene& scene, int startView) {
    if (!initWindow()) return 1;
    printControls();

    const std::vector<ViewPreset>& views = scene.views();
    const int viewCount = static_cast<int>(views.size());
    Interaction s;
    if (startView >= 0) s.view = startView;
    s.time = views[s.view].captureTime;

    auto resetCamera = [&]() {
        s.yaw = 0.0f;
        s.pitch = 0.0f;
        s.zoom = 1.0f;
    };

    int captureCounter = 1;
    bool dragging = false;
    bool running = true;
    auto last = std::chrono::steady_clock::now();
    float fpsTimer = 0.0f;
    int fpsFrames = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) dragging = true;
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) dragging = false;
                    break;
                case SDL_MOUSEMOTION:
                    if (dragging) {
                        s.yaw -= event.motion.xrel * 0.006f;
                        s.pitch += event.motion.yrel * 0.006f;
                    }
                    break;
                case SDL_MOUSEWHEEL:
                    s.zoom *= event.wheel.y > 0 ? 0.92f : 1.08f;
                    break;
                case SDL_KEYDOWN: {
                    const SDL_Keycode key = event.key.keysym.sym;
                    if (key >= SDLK_0 && key <= SDLK_9) {
                        s.view = viewForKey(key - SDLK_0, viewCount);
                        resetCamera();
                    } else if (key == SDLK_TAB || key == SDLK_RIGHT) {
                        s.view = (s.view + 1) % viewCount;
                        resetCamera();
                    } else if (key == SDLK_LEFT) {
                        s.view = (s.view + viewCount - 1) % viewCount;
                        resetCamera();
                    } else if (key == SDLK_SPACE) {
                        s.paused = !s.paused;
                    } else if (key == SDLK_LEFTBRACKET) {
                        s.timeScale *= 0.5f;
                    } else if (key == SDLK_RIGHTBRACKET) {
                        s.timeScale *= 2.0f;
                    } else if (key == SDLK_PLUS || key == SDLK_EQUALS || key == SDLK_KP_PLUS) {
                        s.zoom *= 0.9f;
                    } else if (key == SDLK_MINUS || key == SDLK_KP_MINUS) {
                        s.zoom *= 1.1f;
                    } else if (key == SDLK_o) {
                        s.showOrbits = !s.showOrbits;
                    } else if (key == SDLK_r) {
                        resetCamera();
                    } else if (key == SDLK_c) {
                        std::error_code ec;
                        std::filesystem::create_directories("capturas", ec);
                        std::string path;
                        // busco un nombre libre para no pisar capturas anteriores
                        do {
                            path = "capturas/captura-" + std::to_string(captureCounter++) + ".png";
                        } while (std::filesystem::exists(path));
                        saveCapture(scene, views[s.view], path, windowWidth, windowHeight, 2, s.time, s.yaw, s.pitch, s.zoom, s.showOrbits);
                    } else if (key == SDLK_ESCAPE) {
                        running = false;
                    }
                    break;
                }
                default:
                    break;
            }
        }
        // no dejo que el zoom se meta dentro del planeta ni que se vaya demasiado lejos
        s.zoom = glm::clamp(s.zoom, 0.35f, 3.0f);

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (!s.paused) s.time += dt * s.timeScale;

        renderFrame(scene, *framebuffer, views[s.view], s.time, s.yaw, s.pitch, s.zoom, s.showOrbits, 1);
        framebuffer->renderBuffer(renderer);

        // el título de la ventana hace de HUD: no hace falta dibujar texto
        fpsTimer += dt;
        ++fpsFrames;
        if (fpsTimer >= 0.5f) {
            std::string title = "Flor de Helia | " + views[s.view].name + " | " +
                                std::to_string(static_cast<int>(fpsFrames / fpsTimer)) + " FPS" +
                                (s.paused ? " | PAUSA" : "") +
                                (s.timeScale != 1.0f ? " | velocidad x" + std::to_string(s.timeScale).substr(0, 5) : "");
            SDL_SetWindowTitle(window, title.c_str());
            fpsTimer = 0.0f;
            fpsFrames = 0;
        }
    }
    cleanup();
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    int startView = -1;
    bool capture = false;
    bool measureMode = false;
    float secondsPerView = 3.0f;
    std::string captureFolder = "docs/capturas";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--capturas") {
            capture = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') captureFolder = argv[++i];
        } else if (arg == "--medir") {
            measureMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') secondsPerView = static_cast<float>(std::atof(argv[++i]));
        } else if (arg == "--vista" && i + 1 < argc) {
            startView = std::atoi(argv[++i]);
        } else if (arg == "--tamano" && i + 1 < argc) {
            int w = 0, h = 0;
            if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
                windowWidth = w;
                windowHeight = h;
            }
        } else {
            std::fprintf(stderr, "Argumento desconocido: %s\n", arg.c_str());
            return 1;
        }
    }

    Scene scene;
    if (capture) return generateCaptures(scene, captureFolder);
    if (measureMode) return measure(scene, secondsPerView);
    const int viewCount = static_cast<int>(scene.views().size());
    int start = startView >= 0 && startView <= 9 ? viewForKey(startView, viewCount) : -1;
    return interactive(scene, start);
}
