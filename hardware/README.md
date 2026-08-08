# Hardware — guías de cableado

La BOM completa, los pedidos y la arquitectura de alimentación viven en
[../docs/hardware.md](../docs/hardware.md). Aquí están las guías prácticas
por placa:

| Guía | Placa | Estado |
|---|---|---|
| [buddy-s3-display.md](buddy-s3-display.md) | **ESP32-S3 N16R8** — la de referencia | verificada en hardware |
| [buddy-zero-wiring.md](buddy-zero-wiring.md) | **ESP32 clásico** DevKit V1 | arranque verificado; píxeles pendientes |

También: `buddy-zero.wireviz.yml` (diagrama-como-código del cableado clásico,
se renderiza con [WireViz](https://github.com/wireviz/WireViz)), y las
máscaras imprimibles `oled-mask/` y `round-mask/`.

Regla que lo explica casi todo: **cada chip tiene minas distintas**. En el S3,
GPIO 33–37 son la PSRAM, 19/20 el USB, 26–32 la flash y 0/3/45/46 strapping.
En el clásico, 6–11 son la flash, 12 y 15 strapping y 34–39 solo entrada. Por
eso los pines por defecto difieren por target (están en `menuconfig`, nunca
hardcodeados).
