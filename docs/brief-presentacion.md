# Brief para presentación: Desktop Buddy (proyecto HackLab)

> **Instrucciones para el agente que genere la presentación:** audiencia =
> miembros del HackLab (técnicos, makers, niveles mixtos) en una reunión de
> planeación/ideación. Tono: entusiasta pero honesto, hacker-friendly, sin
> humo corporativo. Idioma: español. Duración objetivo: 15–20 min + discusión.
> Sugerencia: 12–14 slides (estructura propuesta al final). Las decisiones
> marcadas como **abiertas** deben presentarse como preguntas al equipo, no
> como hechos. Cifras y detalles técnicos: usar los de este documento, no
> inventar.

---

## 1. La idea en una frase

Una criatura de escritorio abierta y hackeable: tiene cara, reacciona cuando
la acaricias, chirrea y gesticula, conversa con personalidad propia (LLM), y
todo lo que la hace única es **data compartible, no firmware** — construida
desde cero por el HackLab (electrónica + carcasa impresa + framework propio).

**Identidad: compañero primero.** La gracia (reaccionar, gesticular,
conversar) es el producto. La utilidad tipo asistente (recordatorios,
calendario) es superficie de extensión para la comunidad, no fundación.

## 2. Inspiración / estado del arte

| Proyecto | Qué es | Qué tomamos |
|---|---|---|
| **Stack-chan / M5StackChan** (open source, ESP32-S3) | Robot kawaii de escritorio, enorme comunidad japonesa; M5Stack vende kit oficial con servos, touch, NFC | Validación total del concepto; su librería de cara (m5stack-avatar, MIT): parpadeo, sacadas, **lip-sync por amplitud**; geometría del cuello con servos |
| **Anki Vector / wire-pod** | Robot companion comercial revivido por la comunidad tras morir su nube | La lección: si depende de una nube ajena, muere con ella |
| **EMO, Eilik** | Companions comerciales cerrados | Referencia de encanto/expresividad; anti-referencia de apertura |
| **Tasmota** | Firmware ESP con scripting Berry embebido | El patrón exacto de nuestro layer de comportamiento hot-reload |
| **ESPHome / HA Voice PE** | Firmware config-driven; pipeline de voz abierto en ESP32-S3 | UX de provisioning web; referencia de voz y wake word (microWakeWord) |

**Nuestro diferencial (el pitch):** *"Stack-chan sin casero."* M5StackChan se
configura con app del fabricante + cuenta + nube XiaoZhi. El nuestro: web UI
servida por el propio dispositivo, sin cuentas, tus propias API keys,
proveedor de IA intercambiable, y reflejos que funcionan offline. Regla de
oro: **nunca brick** — sin red sigue vivo, sin nube sigue siendo tuyo.

## 3. Conceptos clave del framework

- **El comportamiento es data:** core estable en C++ (ESP-IDF) que casi nadie
  toca + scripts Berry hot-reload desde la web UI. Recompilar para cambiar
  comportamiento se considera un bug.
- **Todo es un evento** en un bus, con 4 primitivas: **Senses** (entradas),
  **Expressions** (salidas), **Reflexes** (scripts locales evento→acción),
  **Skills** (herramientas LLM, v2+).
- **Packs de personalidad = cartuchos:** prompt + caras + chirridos + reflejos
  en un zip. Compartibles, forkeables, intercambiables entre buddies.
- **Brain contract:** el firmware nunca habla con "OpenAI" o "Anthropic";
  habla con "un cerebro" (contrato único). Implementaciones intercambiables:
  adaptador cloud en el dispositivo (default v1), hub opcional (v2, para
  webhooks/integraciones), o ninguno (offline, solo reflejos).
- **Soberanía:** claves en NVS vía web UI, nunca en packs.

## 4. Hardware v1 (ya pedido para 3 builds de prueba)

- ESP32-S3 N16R8 · pantalla redonda GC9A01 240×240 (o cuadrada ST7789, misma
  resolución) · mic I2S INMP441 · amp MAX98357A + parlante plano · anillo 12×
  WS2812 como halo difuso tras el bisel · pads capacitivos de cobre bajo la
  carcasa ("acariciar", no tocar pantalla) · botón.
- **Kit de consciencia (sensores integrados):** temp/humedad/presión
  (AHT20+BMP280), luz (BH1750), IMU (MPU6050: `motion.pickup`, `motion.shake`),
  **radar de presencia mmWave LD2410C** (te detecta llegar/sentarte/irte *a
  través del plástico*, cero agujeros), ToF opcional (mano acercándose).
  Todos alimentan el `sensor_snapshot` del Brain → consciencia conversacional
  gratis ("está oscuro y la presión cae, ¿lluvia?").
- **NFC PN532** tras la carcasa + stickers NTAG215: tocar una tarjeta = cambiar
  de personalidad (el cartucho hecho físico). Solo acciones whitelisted, nunca
  auth.
- **Arquitectura de poder:** USB-C panel-mount en la carcasa → riel 5 V/3 A en
  estrella (las cargas nunca pasan por el devkit) + polyfuse en el hack port.
- **Hack port** trasero 2×4 (I2C + 2 GPIO): la promesa de extensibilidad hecha
  física; adaptadores Qwiic/Stemma QT → ecosistema completo de sensores plug &
  play.
- **Costo:** ~€60–70 por buddy con todos los sensores (~€25 el core mínimo).

## 5. Voz: push-to-talk en v1, wake word en v2

- Realidad técnica: STT de vocabulario abierto **no puede** correr en un
  ESP32-S3 (~100× la memoria/cómputo disponible). El dispositivo captura y
  transmite; el reconocimiento es cloud.
- **PTT borra los dos problemas más duros:** mantener presionado = micrófono
  activo con audio propio silenciado (half-duplex, sin cancelación de eco);
  soltar = fin de la frase (sin VAD). Un POST HTTPS del clip completo, sin
  streaming.
- Proveedores recomendados (intercambiables): Claude Haiku 4.5 (cerebro),
  OpenAI mini-transcribe (STT) y mini-tts (TTS). **~medio centavo de
  dólar por interacción de voz** (~$7/mes con uso intenso). Preset alternativo
  de una sola cuenta (todo OpenAI).
- Latencia real 1.5–3 s: no se elimina, se **enmascara** (cara de "pensando" +
  chirrido instantáneo al soltar — vive en el pack, no en el firmware).
- Wake word ("¡Hey Buddy!") = v2 con microWakeWord (entrenable por nosotros).

## 6. Roadmap

- **M0 — Está vivo:** cara animada, reacciona al tacto, WiFi + web UI básica.
- **M1 — Es hackeable:** bus de eventos + reflejos Berry hot-reload.
- **M2 — Tiene alma:** Brain contract + chat por web UI, emociones LLM, packs.
- **M3 — Escucha:** loop de voz push-to-talk completo.
- **M4 — Es tuyo:** ensamblado en carcasa propia, pack default pulido, demo.
- **v2+ (post-talleres):** wake word · **power base** (módulo atornillable:
  batería 18650 + cuello con 2 servos MG90S — el peso de la batería es lastre
  para la cabeza móvil) · cartuchos NFC · hub opcional (webhooks, calendario,
  integraciones) · más formatos de carcasa (¡Macintosh 1984 con pantalla
  cuadrada ya es viable en v1 como variante!).

## 7. Plan: 4 talleres × 4 h (+ trabajo continuo después)

Tres tracks paralelos para que nadie mire: **core** (firmware), **voz** (1–2
personas fijas), **carcasa** (CAD/impresión). Cada sesión termina con algo que
funciona: S1 definición + bring-up por subsistema · S2 cara+bus+web UI + spike
de voz · S3 personalidad + loop PTT en banco · S4 ensamblaje + demo + retro.
Pre-trabajo crítico: componentes pedidos ✅, toolchain ESP-IDF instalado antes,
API keys probadas desde laptop.

## 8. Pros y contras de meter tanto en v1 (discusión honesta)

**A favor de la ambición:**
- El bus I2C hace que cada sensor extra cueste 4 cables y ~horas de código —
  y **los agentes de IA escriben los drivers**; el costo humano real es
  colocación física y tuning, no código.
- El `sensor_snapshot` convierte sensores en encanto conversacional sin
  escribir comportamientos (el LLM reacciona solo).
- Radar y NFC leen a través del plástico: cero costo de carcasa.
- Todo el hardware ya está pedido y es barato; el riel de 5 V ya tiene margen
  para los servos de v2.

**En contra (los riesgos reales):**
- Las 4 sesiones son fijas; cada subsistema extra compite por el mismo tiempo
  de debugging. La voz ya es el ítem más arriesgado del v1.
- ~50 conexiones de cableado: intermedio, no trivial (mitigado: cada
  subsistema se prueba aislado y en paralelo).
- Riesgo cultural: el scope creep es el único riesgo capaz de matar el
  proyecto entero.

**Mitigaciones ya diseñadas (mostrar como decisiones, no esperanzas):**
- La columna de sensores es **opcional al ensamblar**: un buddy solo con cara,
  voz, tacto y halo está completo; los sensores se sueldan después sin
  reabrir nada.
- Fallback por hito: si la voz no está al final de S3, la demo corre con
  chirridos + chat web (primera clase, no plan B vergonzoso).
- Servos: v1 es *servo-ready* (base atornillable + pines reservados + margen
  de poder), no *servo-equipped*.
- Regla de specs: solo se especifica formalmente lo que dos tracks comparten
  (Brain contract, bus, formato de pack, API web).

## 9. Decisiones abiertas para el equipo (presentar como preguntas)

1. ¿Construimos un **hub mínimo** durante los talleres? (Es el track perfecto
   para los web devs; ~200 líneas; pero hay que proteger la promesa
   device-first.)
2. **Nombre** del proyecto y de la criatura.
3. Pantalla redonda vs cuadrada (misma resolución; elección por miembro).
4. ¿Cuántos buddies / quiénes construyen? (Define el segundo pedido de
   componentes.)
5. Berry vs Lua para scripting (se decide por fricción en S2 — solo informar).

## 10. Estructura de slides sugerida

1. Portada: la criatura + una frase.
2. ¿Qué es? (identidad: compañero primero, demo mental de 30 segundos).
3. Inspiración: fotos de Stack-chan, Vector, EMO — "esto existe y encanta".
4. El diferencial: "Stack-chan sin casero" (soberanía, nunca brick).
5. Cómo funciona: las 4 primitivas + packs como cartuchos (diagrama).
6. El cuerpo: hardware v1 + costo (~€60–70).
7. La consciencia: sensores + radar + NFC (los trucos "a través del plástico").
8. La voz: por qué PTT primero (y medio centavo por conversación).
9. Roadmap M0→M4 → v2+ (power base con servos como zanahoria).
10. Los 4 talleres: tracks paralelos, cada sesión termina con demo.
11. Pros/contras del scope v1 + mitigaciones.
12. Decisiones que tomamos HOY (las 5 preguntas abiertas).
13. Cierre: qué se lleva cada miembro (su propio buddy + un framework para
    hackear años).

---
_Fuente de verdad técnica: `docs/architecture.md`, `docs/hardware.md`,
`docs/services.md`, `docs/workshops.md` (en inglés). Generado 2026-07-15._
