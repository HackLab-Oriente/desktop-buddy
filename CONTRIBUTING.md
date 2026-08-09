# Cómo contribuir

Guía corta y honesta. Casi todo aquí existe porque alguna vez nos costó una
tarde no tenerlo.

## Cómo se organiza el trabajo

Seis equipos, cada uno con una persona que lo lidera: firmware y arquitectura,
voz, web UI, electrónica, CAD y carcasa, personalidad y contenido. Liderar
significa saber cómo va tu parte y traer las decisiones al grupo — no hacer
todo el trabajo ni decidir en solitario. Nadie está casado con un equipo.

Las decisiones de producto se discuten en las sesiones; lo que se propone en
este repo es un punto de partida, no un decreto.

## Ramas y spikes

- `main` siempre compila (los **dos** targets: `esp32s3` y `esp32`) y sus
  docs cuentan la verdad.
- Trabajo normal: rama corta → PR → merge. Sin ceremonias.
- **Experimentos: rama `spike/<nombre>` con proyecto propio en `spikes/`.**
  Un spike es desechable por diseño; lo único que se mergea a `main` son sus
  *conclusiones*, como documento. Ejemplo vivo: `spike/tinylm-s3` (el modelo
  de IA local) — su README registra números medidos, y el código se queda en
  la rama.

## Commits

- El mensaje explica el **porqué**, no solo el qué. Si mediste algo, los
  números van en el mensaje ("31.4 → 56.8 tok/s"); si algo falló por el
  camino, también — el siguiente que pase por ahí se ahorra tu tarde.
- **Nunca se commitea salida de build.** `build/`, `sdkconfig`,
  `managed_components/` y `*.bin` están en `.gitignore` porque una vez
  entraron 1.556 artefactos de build en un commit y hubo que reescribir la
  historia. Un `git status` antes de commitear es gratis.
- Los binarios grandes (pesos de modelos, media) se descargan, no se
  vendorizan.

## Idiomas

- **Documentación: español.** Markdown y HTML.
- **Código y comentarios en el código: inglés.** También los mensajes de
  commit y los nombres de eventos del bus.
- `CLAUDE.md` y `.kiro/` son configuración de las herramientas de IA y se
  quedan en inglés.

## Añadir un evento al bus

El bus es el contrato entre equipos, y tiene registro:
[docs/event-registry.md](docs/event-registry.md) (con vista interactiva en
[docs/event-registry.html](docs/event-registry.html)).

La regla: **cada equipo es dueño de su prefijo** (`voice.*` es de voz,
`config.*` de web UI…). Crear un evento en tu espacio no pide permiso — un PR
que cambia el código **y el registro a la vez**. Cambiar o borrar el de otro
equipo necesita a su responsable. Los nombres son para siempre: añadir es
barato, renombrar rompe todos los packs existentes.

Al escribir un handler: nunca bloquees (la entrega es monohilo — un handler
lento retrasa a todos), y el payload es un string pequeño — una ruta o un id,
nunca bytes.

## Firmware: reglas de la casa

- **ESP-IDF v6.0.2.** La v5 no está soportada; no pierdas el día intentándolo.
- Antes de un PR que toque `firmware/`, compila los dos targets:

```bash
cd firmware && idf.py set-target esp32s3 && idf.py build
```

```bash
cd firmware && idf.py set-target esp32 && idf.py build
```

- Los tests de host del bus corren sin placa: `firmware/host_test/`.
- Pines: se cambian en `menuconfig` (Kconfig), nunca hardcodeados. Ojo con
  las minas por chip — en el S3: 33–37 (PSRAM), 19/20 (USB), 26–32 (flash),
  0/3/45/46 (strapping); en el clásico: 6–11 (flash), 12/15 (strapping),
  34–39 (solo entrada).
- Si el build se queja de que el entorno "is not consistent": tienes dos
  Pythons cruzados. `idf.py fullclean` una vez y a seguir.

## Docs

- Los documentos HTML de `docs/` son autocontenidos a propósito (CSS y JS
  inline, sin CDNs): tienen que funcionar en el hackerspace sin internet.
- Si un doc describe algo que el código aún no hace, que lo diga — un doc que
  promete eventos inexistentes le cuesta una tarde a alguien (nos pasó; está
  en la lista de agujeros del registro).

## PRs de Dependabot

Dependabot vigila dos cosas: las actions del CI (semanal, agrupadas en un solo
PR) y los submódulos Berry y LovyanGFX (mensual, uno por PR).

Los de actions se mergean con el CI en verde y ya está.

Los de submódulos **no**. Llevan la etiqueta `needs-hardware-check` por un
motivo concreto: Dependabot sigue la rama por defecto del submódulo, no sus
releases — así que el PR cambia un pin deliberado (LovyanGFX está parado en el
tag 1.2.26) por lo que haya hoy en master, sin changelog. Y el CI compila para
los dos targets, pero **el CI no ve la pantalla**. LovyanGFX es la capa
gráfica; ya nos mordió una vez con el endianness de los sprites de 16 bpp, que
compila perfecto y pinta franjas arcoíris. Verde significa "compila", no "la
cara está bien". Flashea una placa antes de mergear.

Lo que Dependabot **no** cubre: los componentes de ESP-IDF
(`idf_component.yml` — littlefs, cJSON, led_strip) y la versión de ESP-IDF del
CI. Esos se suben a mano y a conciencia. El razonamiento completo está
comentado en [`.github/dependabot.yml`](.github/dependabot.yml).

## Hardware

- El RC522 muere a 5 V. La pantalla va a 3V3. El anillo LED va a 5 V con el
  brillo capado en firmware — no le quites el cap sin leer la sección de
  alimentación de [docs/hardware.md](docs/hardware.md).
- Regla de potencia: las cargas nunca pasan por el devkit.
