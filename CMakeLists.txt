cmake_minimum_required(VERSION 3.16)
project(Lab5Graficas CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sin optimización los shaders corren unas 10 veces más lento, así que Release por defecto.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
endif()

find_package(SDL2 REQUIRED CONFIG)
find_package(glm REQUIRED CONFIG)
find_package(Threads REQUIRED)

add_executable(lab5
  src/main.cpp
  src/obj_loader.cpp
  src/mesh.cpp
  src/pipeline.cpp
  src/scene.cpp
  src/shaders/star.cpp
  src/shaders/rocky.cpp
  src/shaders/gas.cpp
  src/shaders/garden.cpp
  src/shaders/ocean.cpp
  src/shaders/crystal.cpp
  src/shaders/moon.cpp
  src/shaders/ring.cpp
  src/shaders/atmosphere.cpp
  src/shaders/sky.cpp)
target_link_libraries(lab5 PRIVATE SDL2::SDL2main SDL2::SDL2 glm::glm Threads::Threads)

if(MSVC)
  target_compile_options(lab5 PRIVATE /W4 /O2)
else()
  # -march=native usa AVX2 y FMA si el CPU los tiene: el noise corre bastante más rápido.
  target_compile_options(lab5 PRIVATE -Wall -Wextra -march=native)
endif()

# MinGW: enlazar el runtime de C++ estáticamente y copiar SDL2.dll junto al
# ejecutable, para poder ejecutarlo fuera de la terminal de MSYS2.
if(MINGW)
  target_link_options(lab5 PRIVATE -static-libgcc -static-libstdc++
    -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
endif()
if(WIN32)
  add_custom_command(TARGET lab5 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:lab5>)
endif()
