#pragma once

#include "obj_loader.h"

// Las mallas se generan por código y usan el mismo struct Model del cargador OBJ.
// Solo aportan geometría: ninguna trae color, material ni textura.

// Esfera UV de radio 1. La posición de cada vértice también es su normal.
Model generateSphere(int segments, int rings);

// Anillo plano en el plano XZ, de innerRadius a outerRadius (en radios del planeta).
// Es una malla aparte del planeta, como pide la rúbrica.
Model generateRing(float innerRadius, float outerRadius, int segments);
