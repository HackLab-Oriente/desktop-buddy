# Desktop Buddy

Un compañero de escritorio abierto y hackeable sobre ESP32 — hecho por y para
nuestro HackLab.

## Qué es

Una criatura pequeña que vive en tu escritorio. Tiene cara, reacciona cuando
la tocas, emite sonidos y gestos, y tiene personalidad — impulsada por un LLM.
Es un **compañero primero**: la gracia, la expresividad y la personalidad son
el producto. La utilidad (recordatorios, calendario, integraciones) llega
después, como *skills* opcionales que cualquiera de la comunidad puede
construir.

Dos compromisos de diseño lo definen todo:

1. **El comportamiento es datos, no firmware.** El ESP32 ejecuta un núcleo C++
   estable (el framework). Todo lo que hace que un buddy sea *tu* buddy — su
   cara, sus estados de ánimo, sus reacciones, su voz, su system prompt — es un
   **pack de personalidad**: scripts y assets que editas desde una web y
   recargas en caliente sin recompilar. Las personalidades se comparten, se
   forkean y se intercambian — como cartuchos.

2. **Primero el dispositivo, el hub es opcional.** El buddy está completamente
   vivo por sí solo: los reflejos locales funcionan sin internet, y habla con
   un proveedor de LLM directamente por WiFi para conversar. El dispositivo
   habla con "un cerebro" a través de un protocolo pequeño — por defecto ese
   cerebro es un adaptador cloud en el propio dispositivo, pero el mismo
   endpoint puede apuntar a un servidor hub opcional para los trucos pesados
   (webhooks, integraciones, memoria a largo plazo). Sin hub no pasa nada: el
   buddy degrada con gracia, **nunca se rompe**.

## Conceptos

Dentro del framework todo es un evento en un bus, y hay cuatro primitivas de
extensión:

| Primitiva | Qué es | Ejemplos |
|---|---|---|
| **Senses** | Entradas que emiten eventos | tacto, micro, sensores, timers |
| **Expressions** | Salidas que consumen acciones | cara, sonidos, LEDs, motores |
| **Reflexes** | Comportamiento local scriptado (evento → acción), sin latencia, offline | lo acaricias → ronronea; sala a oscuras → se duerme |
| **Skills** | Capacidades mediadas por el LLM expuestas como herramientas | recordatorios, calendario (las construye la comunidad) |

Un **pack de personalidad** agrupa: system prompt, expresiones con nombre
(estado → cara/color/animación), reflejos y frases. El contrato completo está
en [docs/pack-format.md](docs/pack-format.md).

## Arranque en 5 minutos

Necesitas [ESP-IDF v6.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
(la v5 **no** vale) y una placa: **ESP32-S3** (la de referencia) o un
**ESP32 clásico** DevKit V1 (funciona casi todo — ver la tabla abajo).

```bash
# --recursive importa: sin los submódulos (Berry y LovyanGFX) no compila
git clone --recursive <url-del-repo>
cd desktop-buddy/firmware

idf.py set-target esp32s3   # o: esp32
idf.py menuconfig           # menú "Buddy Zero": WiFi, API key (opcional)
idf.py build flash monitor
```

Guía completa, cableado y la escalera de PoCs: [firmware/README.md](firmware/README.md).

### ¿Qué placa tengo y qué me da?

| | ESP32-S3 N16R8 | ESP32 clásico (DevKit V1) |
|---|---|---|
| Bus de eventos, reflejos Berry, web UI, cerebro cloud, tacto, NFC | ✓ | ✓ |
| Cara redonda a color | ✓ 30 fps (con caché en PSRAM) | ✓ por bandas, ~13 fps · **sin verificar en pantalla aún** |
| Modelo de IA local | ✓ medido | ✗ necesita PSRAM |
| Voz (v1) | ✓ planificado | ✗ sin RAM para audio+TLS |

## ¿Dónde me meto?

Según el equipo en el que estés (o quieras estar):

- **Firmware y arquitectura** → [firmware/README.md](firmware/README.md) y
  [docs/event-registry.md](docs/event-registry.md) — el contrato entre equipos.
- **Web UI / PWA** → el servidor vive en `firmware/components/webui/`; la API
  de configuración está por diseñar (es el bloqueo nº 1 — pregunta antes de
  empezar).
- **Voz** → [docs/services.md](docs/services.md) y los eventos `voice.*` que
  aún no existen ([docs/event-registry.md](docs/event-registry.md), sección
  «agujeros»).
- **Electrónica** → [docs/hardware.md](docs/hardware.md) (BOM y pedidos) y
  [hardware/](hardware/) (guías de cableado por placa).
- **CAD y carcasa** → [docs/hardware.md](docs/hardware.md), sección de
  alimentación y montaje.
- **Personalidad y contenido** → [docs/pack-format.md](docs/pack-format.md) y
  [packs/](packs/) — no hace falta saber programar para escribir frases.

## Estructura del repo

```
firmware/     núcleo ESP-IDF (C++): drivers, bus de eventos, VM Berry, web
packs/        packs de personalidad (scripts + assets); packs/zero es la semilla
hardware/     guías de cableado por placa, diagramas, máscaras imprimibles
docs/         arquitectura, decisiones, talleres — índice en docs/README.md
spikes/       experimentos desechables; solo sus conclusiones van a main
```

## Documentación

El índice completo está en [docs/README.md](docs/README.md). Si solo vas a
leer tres cosas: [la arquitectura](docs/architecture.md), [el plan de
talleres](docs/workshops.md) y [cómo contribuir](CONTRIBUTING.md).

> Idiomas: la documentación está en español; el código y sus comentarios, en
> inglés. `CLAUDE.md` y `.kiro/` son configuración de las herramientas de IA
> del proyecto y permanecen en inglés.

## Estado

En construcción activa antes de los talleres. Todo lo que hay aquí es una
propuesta — discútela. Licencia: [MIT](LICENSE).
