# Hardware — guías de cableado

La BOM completa, los pedidos y la arquitectura de alimentación viven en
[../docs/hardware.md](../docs/hardware.md). Aquí están las guías prácticas
por placa:

| Guía | Placa | Estado |
|---|---|---|
| [buddy-s3-display.md](buddy-s3-display.md) | **ESP32-S3 N16R8** — la de referencia | verificada en hardware |
| [buddy-zero-wiring.md](buddy-zero-wiring.md) | **ESP32 clásico** DevKit V1 | **histórica** — target retirado |
| [buddy-s3-audio.md](buddy-s3-audio.md) | **S3** — micro, ampli y tarjeta SD | **propuesta, sin firmware todavía** |

También: la máscara imprimible [`round-mask/`](round-mask/).

**Históricas**, se conservan porque documentan trabajo hecho y verificado:
[`oled-mask/`](oled-mask/), la máscara de la PoC monocroma cuyo backend ya no
existe; y el cableado del ESP32 clásico —`buddy-zero-wiring.md` más su
`buddy-zero.wireviz.yml`—, para un target que el firmware ya no compila.

Regla que lo explica casi todo: **cada chip tiene minas distintas**, y por eso
los pines viven en `menuconfig` y nunca hardcodeados. En el S3 —el único
target— GPIO 33–37 son la PSRAM, 19/20 el USB, 26–32 la flash y 0/3/45/46
strapping. En el clásico eran 6–11 flash, 12 y 15 strapping y 34–39 solo
entrada: eso ya solo hace falta para leer el cableado histórico.
