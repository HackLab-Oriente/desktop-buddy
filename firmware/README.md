# Firmware — Buddy Zero

El núcleo del framework: bus de eventos, host de Berry, drivers de Senses y
Expressions, el adaptador de cerebro cloud y la web de recarga en caliente.

## Dos targets

| | `esp32s3` (referencia) | `esp32` (clásico, DevKit V1) |
|---|---|---|
| Bus, Berry, web UI, cerebro cloud, tacto, NFC | ✓ | ✓ |
| Cara GC9A01 a color | ✓ 1 banda + caché PSRAM, ~30 fps | ✓ 5 bandas de 23 KB, ~13 fps |
| Modelo local / voz | ✓ / planificado | ✗ (sin PSRAM) |
| Verificado | en hardware, completo | **arranque sí; píxeles pendientes** |

El truco que hace posible el clásico: los 460 KB de PSRAM que la cara parece
necesitar son la **caché** de niveles de ojo, no el renderizado. Dibujar
necesita un frame, y un frame se puede construir por bandas horizontales
(240×48 = 23 KB). En el S3 `kBandH == H` — una sola banda, exactamente el
código de siempre. Todo esto vive en `components/expressions/round_face.cpp`.

## La capa gráfica

Los ojos los dibuja **nuestro** renderer de campos de distancia (SDF), porque
la ceja y el guiño de felicidad se recortan de la forma como *cobertura* —
ninguna primitiva de librería expresa eso. Todo lo demás (driver del panel,
sprites, fuentes, blitting) es **LovyanGFX**.

En el S3 los ojos van **cacheados**: el SDF cuesta 40–100 ms por frame, pero
la imagen solo cambia al parpadear o cambiar de emoción — una sacada es una
*traslación* de una imagen que no cambió. Renderizar una vez por (emoción,
apertura) en tres sprites PSRAM y blitear con offset de mirada lo lleva de 13
a ~30 fps, píxel por píxel idéntico. Todo se midió primero en
[../spikes/lovyangfx-gc9a01/](../spikes/lovyangfx-gc9a01/README.md), que
registra las trampas — la principal: **LovyanGFX guarda los sprites de 16 bpp
en big-endian**; escribir little-endian nativo en `getBuffer()` convierte los
degradados en franjas arcoíris.

## El arranque

`face_start()` corre primero en `app_main`, así el splash está en pantalla
mientras lo lento pasa por detrás. La retroiluminación no se enciende hasta
después del init del panel — el arranque se lee como oscuro → estática → logo.
El logo del HackLab «sintoniza» como una tele estropeada (~1,6 s) y la línea
de estado la publican los pasos reales de `app_main` vía `boot.status`.
`boot.ready` hace glitch del splash a la cara. En un arranque típico la línea
se queda ~4 s en "connecting wifi": ese es el cuello de botella honesto.

## Setup

```bash
# 1. ESP-IDF v6.x (la v5 NO está soportada). El instalador EIM deja el script
#    de activación en ~/.espressif/tools/activate_idf_v6.0.2.sh — haz source.
# 2. Submódulos: Berry (VM de reflejos) y LovyanGFX (capa gráfica).
git submodule update --init
cd firmware/components/berry_host/berry
mkdir -p generate && python3 tools/coc/coc -o generate src default -c default/berry_conf.h
#    (Sin el submódulo/codegen el build compila igual; los reflejos caen al
#     fallback en C — main.cpp replica packs/zero/reflexes/main.be.)

cd ../../..            # de vuelta a firmware/
idf.py set-target esp32s3    # o: esp32 — elige tu placa
idf.py menuconfig            # menú "Buddy Zero": WiFi, API key, pines
idf.py build flash monitor
```

`set-target` regenera `sdkconfig` desde `sdkconfig.defaults` +
`sdkconfig.defaults.<target>`, y elige la tabla de particiones (16 MB con
partición de modelo en el S3; 4 MB sin ella en el clásico).

## Cableado

Los pines por defecto **difieren por chip** — los rangos libres no coinciden
(en el clásico, los pines de display del S3 son la flash). Siempre en
`menuconfig`, nunca hardcodeados.

| Periférico | ESP32-S3 | ESP32 clásico |
|---|---|---|
| GC9A01 (SPI, **3V3**) | SCL 12 · SDA 11 · CS 10 · DC 9 · RST 8 · BL 7 | SCL 18 · SDA 23 · CS 5 · DC 27 · RST 26 · BL 25 |
| Almohadilla táctil | GPIO 4 | GPIO 4 |
| Anillo WS2812 (**5V**) | DIN 21 | DIN 21 |
| RC522 (opcional, **3V3**) | SCK 39 · MISO 40 · MOSI 41 · CS 42 · RST 38 | SCK 14 · MISO 34 · MOSI 13 · CS 15 · RST 32 |

Guías paso a paso con checklist de primer arranque:
[../hardware/buddy-s3-display.md](../hardware/buddy-s3-display.md) (S3) y
[../hardware/buddy-zero-wiring.md](../hardware/buddy-zero-wiring.md) (clásico).

Minas por chip — **S3**: 33–37 PSRAM octal, 19/20 USB, 26–32 flash, 0/3/45/46
strapping. **Clásico**: 6–11 flash, 12 y 15 strapping, 34–39 solo entrada.

## La escalera de PoCs

1. **Latido** — flashea, mira el log serie, toca el jumper: `touch.pet` → el
   anillo respira. El bus funciona.
2. **Proto-cara** — la pantalla muestra dos ojos paramétricos: parpadeos,
   sacadas, emociones. Payloads de `face.emotion`: neutral, happy, curious,
   sleepy, surprised, angry, sad, suspicious.
3. **Cartuchos** — acerca un llavero RFID: `nfc.tag` + UID en el log. Pon tus
   UIDs en `packs/zero/reflexes/main.be` vía web y repite.
4. **Cerebro** — con WiFi + API key: acaricia el cable, el buddy pregunta a
   Claude y la emoción de la respuesta mueve la cara.
5. **Recarga en caliente** — abre `http://<ip-del-buddy>/`, edita reflejos en
   el navegador, sube — el comportamiento cambia sin reflashear. La tesis
   entera del framework en un botón.

## Estructura

```
components/bus/          bus de eventos (testeable en host — ver host_test/)
components/senses/       tacto, RC522 → eventos
components/expressions/  cara a color, anillo WS2812 ← acciones
components/brain/        contrato de cerebro: adaptador cloud (Claude)
components/webui/        WiFi STA + editor de reflejos + recarga en caliente
components/berry_host/   VM Berry, API buddy.*, despacho de eventos
main/                    arranque + el único reflejo del framework (brain.reply)
../packs/zero/           los reflejos Berry de Buddy Zero
```

El contrato de eventos completo (con dueños por prefijo y agujeros conocidos):
[../docs/event-registry.md](../docs/event-registry.md).

## Tests de host

```bash
cd host_test
c++ -std=c++17 -Wall -I../components/bus/include test_bus.cpp ../components/bus/bus.cpp -o test_bus
./test_bus   # se espera: "bus: all tests passed"
```

## Estado

- **Verificado**: ambos targets compilan en ESP-IDF v6.0.2; el bus pasa sus
  tests de host; el S3 completo funciona en hardware (cara, anillo, tacto,
  WiFi, cerebro Claude).
- **Verificado en el clásico**: arranque completo en placa real — 5 bandas,
  LittleFS, polaridad táctil V1 correcta. **Pendiente**: nadie ha conectado
  aún una pantalla a un clásico — costuras entre bandas y fps reales sin
  confirmar.
- **Sin cablear todavía**: audio (INMP441/MAX98357A), tarjeta SD, sensores
  I2C; RC522 sin probar en hardware.
