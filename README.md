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
(la v5 **no** sirve para este proyecto) y una placa: **ESP32-S3** (la de referencia) o un
**ESP32 clásico** DevKit V1 (funciona casi todo — ver la tabla abajo).

```bash
# --recursive importa: sin los submódulos (Berry y LovyanGFX) no compila
git clone --recursive git@github.com:HackLab-Oriente/desktop-buddy.git
cd desktop-buddy/firmware

idf.py set-target esp32s3   # o: esp32
idf.py menuconfig           # menú "Buddy Zero": WiFi, API key (opcional)
idf.py build flash monitor
```

Guía completa, cableado y la escalera de PoCs: [firmware/README.md](firmware/README.md).

## Preparar el editor

Todo compila desde la terminal con `idf.py` — el editor es comodidad, no
requisito. Pero con el IDE bien puesto tienes autocompletado real sobre
ESP-IDF, y un botón de flash/monitor.

### VS Code, Cursor, Antigravity, Windsurf (todos son forks de VS Code)

Abre `desktop-buddy.code-workspace` y acepta las extensiones recomendadas, o
instálalas a mano:

| Extensión | ID | Para qué |
|---|---|---|
| **ESP-IDF** | `espressif.esp-idf-extension` | Build, flash, monitor, menuconfig y depuración desde el editor. Su asistente instala también el toolchain si aún no lo tienes. |
| **clangd** | `llvm-vs-code-extensions.vscode-clangd` | Autocompletado, ir-a-definición y errores en vivo, leyendo el `compile_commands.json` real del build. |
| *(opcional)* Berry | `skiars.berry` | Resaltado de los reflejos `.be`. Si no la instalas, el repo los muestra como Python, que se parece bastante. |

**No instales `ms-vscode.cpptools`** (la extensión C/C++ de Microsoft). Pelea
con clangd por las mismas funciones y da resultados peores en proyectos
ESP-IDF; además es propietaria y no está en Open VSX, así que en Cursor,
Antigravity o Windsurf directamente no se instala. El workspace la marca como
no recomendada por eso.

Dos detalles que ahorran una tarde:

- **Abre el workspace, no la carpeta suelta.** clangd necesita el
  `compile_commands.json`, que lo genera el build en `firmware/build/` — no
  en la raíz del repo. Apuntado a un sitio sin él, clangd *no falla*: adivina
  los flags y parece que funciona hasta que discrepa del compilador.
- **Compila una vez antes de esperar autocompletado.** Sin build no hay
  `compile_commands.json` que leer.

Los ajustes propios de cada máquina (ruta de ESP-IDF, puerto serie, binario
de clangd) **no van en el repo**: la extensión de ESP-IDF reescribe
`settings.json` con rutas absolutas de quien la ejecutó, así que esos
archivos están en `.gitignore`. Copia las plantillas:

```bash
cp .vscode/settings.example.json .vscode/settings.json
cp firmware/.vscode/settings.example.json firmware/.vscode/settings.json
```

### CLion (alternativa)

Funciona bien y a algunos les resulta más cómodo para C++. El proyecto es
CMake normal, así que se abre directamente — pero hay que darle el toolchain
de ESP-IDF, no el del sistema:

1. Abre la carpeta **`firmware/`** (no la raíz del repo): ahí está el
   `CMakeLists.txt` de nivel superior.
2. *Settings → Build, Execution, Deployment → Toolchains* → añade uno con el
   compilador del toolchain de Xtensa
   (`~/.espressif/tools/xtensa-esp-elf/…/bin/xtensa-esp32s3-elf-gcc`).
3. En *CMake profiles*, añade a las opciones:
   `-DCMAKE_TOOLCHAIN_FILE=$IDF_PATH/tools/cmake/toolchain-esp32s3.cmake
   -DTARGET=esp32s3` (cambia `esp32s3` por `esp32` para la placa clásica), y
   arranca CLion desde una terminal donde hayas hecho `source` del script de
   activación de ESP-IDF, para que herede `IDF_PATH`.
4. Flash y monitor siguen siendo `idf.py -p PUERTO flash monitor` en la
   terminal integrada — el plugin oficial de Espressif es solo para VS Code.

La ganancia real de CLion aquí es el indexador y el depurador; el ciclo de
flasheo se queda en la terminal en cualquier caso.

### Solo terminal

Perfectamente válido, y es como se hicieron todas las mediciones de este
repo:

```bash
source ~/.espressif/tools/activate_idf_v6.0.2.sh
cd firmware && idf.py build flash monitor
```

### ¿Qué placa tengo y qué me da?

| | ESP32-S3 N16R8 | ESP32 clásico (DevKit V1) |
|---|---|---|
| Bus de eventos, reflejos Berry, web UI, cerebro cloud, tacto, NFC | ✓ | ✓ |
| Cara redonda a color | ✓ **33 fps medidos** (caché en PSRAM) | ✓ por bandas, **14,4 fps medidos** |
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

### 📖 **[hacklaboriente.org/desktop-buddy](https://hacklaboriente.org/desktop-buddy/)**

Ahí está todo, con los documentos interactivos funcionando de verdad: la
reunión de equipos, el registro de eventos, el explorador de parámetros y las
dos arquitecturas.

Si solo vas a leer tres cosas: [la arquitectura](docs/architecture.md), [el
plan de talleres](docs/workshops.md) y [cómo contribuir](CONTRIBUTING.md).

> **Ojo:** los `.html` de `docs/` son páginas interactivas. Si los abres desde
> el navegador de archivos de GitHub verás el código fuente, no la página —
> usa el enlace de arriba. El índice en texto plano está en
> [docs/README.md](docs/README.md).

> Idiomas: la documentación está en español; el código y sus comentarios, en
> inglés. `CLAUDE.md` y `.kiro/` son configuración de las herramientas de IA
> del proyecto y permanecen en inglés.

## Estado

En construcción activa antes de los talleres. Todo lo que hay aquí es una
propuesta — discútela. Licencia: [MIT](LICENSE).
