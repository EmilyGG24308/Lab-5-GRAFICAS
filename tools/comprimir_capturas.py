#!/usr/bin/env python3
"""Comprime los PNG de docs/capturas sin cambiar ni un solo píxel.

El escritor PNG del renderer guarda los datos sin comprimir (no depende de ninguna
librería), así que cada captura pesa varios MB. Este paso es opcional y solo sirve
para que el repo pese menos. Requiere Pillow: pip install pillow
"""
import pathlib
import sys

from PIL import Image, ImageChops

folder = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "docs/capturas")
for path in sorted(folder.glob("*.png")):
    original = Image.open(path).convert("RGB")
    temp = path.with_suffix(".tmp.png")
    original.save(temp, optimize=True)
    # comprueba que la imagen quedó exactamente igual antes de reemplazarla
    if ImageChops.difference(original, Image.open(temp).convert("RGB")).getbbox() is not None:
        temp.unlink()
        sys.exit(f"{path.name}: los píxeles cambiaron, no se reemplaza")
    temp.replace(path)
    print(f"{path.name}: {path.stat().st_size / 1e6:.2f} MB")
