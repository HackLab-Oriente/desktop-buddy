# Definición de producto y arquitectura

Estado: propuesta (pre-HackLab). Las decisiones marcadas **[decidido]**
reflejan la posición de trabajo actual; todo lo demás está abierto al grupo.

## 1. Identidad del producto **[decidido]**

Compañero primero. El buddy es una criatura de escritorio con personalidad —
piensa en Tamagotchi / Anki Vector, no en Alexa-con-cara. El framework tiene
que hacer excelente el bucle *expresivo* (reaccionar, gesticular, piar,
conversar). La utilidad es una superficie de extensión, no un entregable
fundacional: el framework trae el enchufe (Skills), la comunidad trae los
electrodomésticos.

Consecuencia para el alcance: los chirps y los gestos en pantalla (estilo
Animal Crossing) son el canal expresivo *nativo* del buddy — funcionan sin
internet, son gratis y llevan la personalidad. **La voz push-to-talk es meta
de v1 encima de eso** (ver §7 sobre por qué PTT en concreto es abordable en 4
sesiones), y la wake word es explícitamente v2. Cuando la voz no está
disponible (offline, sin API key, pipeline roto) el buddy degrada a chirp +
texto — nunca un ladrillo, y la demo del taller nunca depende del subsistema
más arriesgado.

## 2. Dónde vive el cerebro: primero el dispositivo, hub opcional **[decidido]**

La regla del protocolo único: el firmware nunca habla con "OpenAI" ni
"Anthropic" ni "el hub" — habla con **un endpoint de Brain** con un contrato
pequeño:

```
device → brain:  { event, personality_context, sensor_snapshot, user_input? }
brain → device:  { utterance?, emotion, actions[] }        (streamed)
```

Tres implementaciones intercambiables de ese contrato:

1. **Adaptador cloud en el dispositivo (por defecto).** Un adaptador C++ fino
   formatea la petición para la API de un proveedor LLM directamente sobre
   TLS. Funciona solo con WiFi + una API key. Esto es v1.
2. **Cerebro en hub (opcional, después).** El mismo contrato servido por un
   servidor compañero (cualquier Pi/portátil). Apuntar el dispositivo a un
   hub desbloquea lo que un ESP32 pelado genuinamente no puede: recibir
   webhooks (el dispositivo está tras NAT), integraciones OAuth
   (correo/calendario), memoria a largo plazo, modelos más baratos o locales.
3. **Sin cerebro (offline).** Los reflejos siguen corriendo. El buddy está
   vivo, solo que no conversa. Nunca un ladrillo.

Para "consultar datos externos" sin hub, el dispositivo puede hacer
**polling** (feeds de calendario, RSS, un relay serverless) — el polling es
un Sense, no una Skill, y no necesita conectividad entrante.

Los secretos (API keys, WiFi) viven en la NVS del dispositivo, se meten por
la web UI y nunca van en packs — los packs deben poder compartirse sin filtrar
claves.

## 3. Stack de firmware **[decidido]**

- El **núcleo ESP-IDF (C++)** es dueño de la capa dura/tiempo-real: driver de
  pantalla + renderizado, audio I2S entrada/salida, tacto, WiFi + TLS, el bus
  de eventos, OTA, el servidor web embebido y los adaptadores de Brain.
- La **VM de scripting Berry** (el patrón de Tasmota) es dueña de la capa de
  comportamiento: reflejos, mapeos de expresión, comportamientos de reposo.
  Los scripts se suben y recargan en caliente desde la web — cambiar el
  comportamiento nunca significa reflashear. (Lua es el plan B si la
  integración de Berry se nos resiste; el criterio de decisión es la fricción
  con ESP-IDF, evaluada en la sesión 2.)
- **Por qué no MicroPython para todo:** baja la barrera de contribución, pero
  pelea con el pipeline de audio y se apodera del firmware entero; la
  división núcleo C++ + VM pequeña mantiene un framework estable que los
  miembros del lab raramente tocan, con toda la superficie divertida en
  scripts.

## 4. Bus de eventos y primitivas

Todo es un evento: `touch.pet`, `touch.poke`, `timer.idle_5m`,
`sense.light.dark`, `brain.reply`, `webhook.*` (solo hub). Los comportamientos
se suscriben a eventos y emiten acciones: `face.play(anim)`,
`sound.chirp(mood)`, `say(text)`, `gpio.set(...)`, `brain.ask(...)`.

El registro completo de eventos, con dueños por prefijo y agujeros conocidos,
es [event-registry.md](event-registry.md).

- Los **Senses** son drivers C++ registrados en el bus (tacto, nivel de
  micro, GPIO, timers, pollers). Añadir un *nuevo tipo* de sensor es una
  contribución C++; usar uno existente es nivel script.
- Las **Expressions** igual: renderer de cara, reproductor de sonido, LED, y
  después motores. La interfaz está diseñada para que los actuadores encajen
  sin cambios del núcleo.
- Los **Reflexes** son handlers Berry — aquí ocurre el 90% del hackeo.
- Las **Skills** son herramientas expuestas al LLM por el Brain que esté
  activo. El cerebro dispositivo-cloud no trae ninguna (v1); el del hub aloja
  las de la comunidad.

## 5. Packs de personalidad

Un pack es un directorio (subido como zip por la web, guardado en flash/SD):

```
pack.json        nombre, autor, versión, system_prompt, config de voz/chirp
reflexes/*.be    scripts Berry
faces/*          sprite sheets / definiciones de animación
sounds/*         chirps, efectos (PCM/MP3)
```

El framework trae un pack `default` bien hecho que a la vez es el tutorial:
cada mecanismo que ofrece el framework, demostrado en el pack.

**Packs de contenido (capa de almacenamiento SD).** Los packs pueden
referenciar una biblioteca `media/` en la tarjeta micro SD (el VFS hace
transparente flash vs SD — es solo una ruta). Esto habilita packs pesados en
media y *offline-first*; el ejemplo canónico es el **explicador de juegos de
mesa** (la cafetería de un miembro): una pegatina NFC en cada caja → tap → el
buddy reproduce una narración pre-generada con imágenes y animaciones,
enteramente desde SD. El LLM/TTS en vivo queda solo para las preguntas de
seguimiento. Pre-generar la voz (una llamada TTS al crear el pack, cacheada
para siempre) significa coste cero por reproducción, respuesta instantánea y
ninguna dependencia del WiFi del café — la demo funciona con la red caída,
honrando el "nunca un ladrillo". Los packs tipo kiosco como este deberían
además fijar el system prompt del Brain a la tarea (es un dispositivo de cara
al público — los guardarraíles de tema son parte del pack, no una ocurrencia
tardía).

## 6. Web UI

Servida por el propio dispositivo (sin app, sin cuenta cloud):

- aprovisionamiento WiFi de primer arranque — **modo AP → portal cautivo**
  (la base garantizada: funciona sin tag, sin app, sin PN532)
- ajustes: endpoint del brain, API key, volumen, nombre
- gestor de packs: subir/cambiar/editar packs, recarga en vivo de Berry
- una pestaña de chat/consola: hablar con el buddy por texto y ver el bus de
  eventos en directo (a la vez es la herramienta de depuración — disfrutable
  para hackers)

### Aprovisionamiento WiFi por NFC (mejora, no reemplazo)

Un tag NFC puede llevar credenciales WiFi en el registro NDEF estándar
**Wi-Fi Simple Config** (`application/vnd.wfa.wsc`, el formato de credencial
WPS) — escribible nativamente en Android o con la app "NFC Tools" (iOS vía
app; verificar la ruta exacta antes de prometerla). Flujo: tap del tag → la
pantalla muestra `Connect to <SSID>? pet to confirm` → caricia → conecta y
guarda en NVS. Desplegar varios buddies en una red (el café) se convierte en:
escribir un tag, tap a cada uno.

El portal cautivo sigue siendo el suelo; esto es una capa de comodidad
encima.

**Modelo de seguridad — los tags WiFi son la única excepción gobernada a
"los tags nunca hacen auth":**

1. Las credenciales WiFi se parsean en la **capa framework y nunca entran al
   evento Berry `nfc.tag`** — los packs reciben como mucho una señal sin
   contenido `provision.wifi_seen`, nunca el SSID/contraseña. Esto cierra un
   agujero de exfiltración de credenciales (un pack malicioso esperando un
   tap de WiFi).
2. **La confirmación en pantalla es obligatoria** — cambiar de red es una
   acción privilegiada disparada por datos observados (un tag), así que nunca
   se aplica en silencio. Modo estricto opcional: aceptar tags WiFi solo
   durante una ventana de emparejamiento (primer arranque / mantener para
   emparejar).
3. Advertencia que hay que decirle a la gente: la contraseña está en el tag
   en claro (legible con posesión física + un móvil). Bien para casa/café;
   el tag, fuera del exterior del buddy.

Regla general: exactamente un tipo de tag privilegiado (aprovisionamiento
WiFi), privilegiado *porque* lo maneja el framework con confirmación y cero
exposición de credenciales a los scripts. Todos los demás tags siguen bajo
"solo acciones de lista blanca".

## 7. Voz: push-to-talk en v1, wake word en v2 **[decidido]**

**Primero la verdad de base: STT de vocabulario abierto no puede correr en un
ESP32-S3.** Los modelos clase Whisper necesitan ~100× la memoria y cómputo
del S3. Lo que ESP-SR corre localmente es detección de wake word (WakeNet) y
sets fijos de ~200 comandos (MultiNet) — no dictado. Así que el trabajo del
dispositivo es capturar y streamear audio limpio; el reconocimiento pasa en
la nube (o el hub). El contrato de Brain ya lo asume — la voz añade una ruta
de audio, no una arquitectura nueva.

### v1: push-to-talk (en los talleres)

Mantén la almohadilla (o un botón) para hablar. PTT no es solo prudencia de
alcance — *borra* los dos problemas más difíciles de la voz:

- **Eco/interrupción** → half-duplex. El altavoz está a ~2 cm del micro; la
  cancelación de eco real necesita un canal de referencia loopback que el
  MAX98357A no da. Con PTT, "tocando = escuchando" silencia naturalmente el
  audio del propio buddy. Sin AEC.
- **Fin de frase** → soltar la almohadilla marca el fin. Sin ajustar VAD, sin
  cortar a nadie a mitad de frase.

Pipeline: mantener → silenciar audio + grabar → streamear PCM mono 16 kHz/16
bits (32 KB/s crudo — no hace falta códec en WiFi) a una API de STT →
transcripción al Brain → audio TTS de vuelta por el amplificador.

Notas de ingeniería (donde se van las horas de depuración):
- Los buffers DMA y WiFi deben vivir en RAM *interna* (la PSRAM no vale); una
  conexión TLS cuesta ~50 KB; los framebuffers van a PSRAM.
- Audio fijado al core 1, red/TLS al core 0.
- La latencia (subida → STT → LLM → TTS → bajada) es realistamente de 1,5–3 s
  y no se puede eliminar, solo *enmascarar*: al soltar, el buddy gesticula
  "pensando" al instante (cara + chirp). Ese enmascarado vive en el pack de
  personalidad.

### v2: wake word (post-talleres)

Wake word en el dispositivo vía las instrucciones vectoriales del S3.
Advertencia que el lab debe saber de entrada: WakeNet de Espressif solo trae
palabras pre-entrenadas — un "Hey Buddy" a medida significa pagarle a
Espressif el entrenamiento. La ruta abierta es **microWakeWord** (lo que usa
Home Assistant Voice PE): entrenable por nosotros, corre en el S3. La v2
también reabre los problemas que PTT borró: VAD siempre-encendido para el fin
de frase, y el compromiso half-duplex vs interrupción mientras el buddy
habla. Las APIs realtime/speech-to-speech son un posible atajo que merece un
spike. Chirp+texto sigue siendo primera clase todo el tiempo — la voz es
aditiva.

## 8. Hitos

- **M0 — Está vivo:** placa despierta, cara animada, reacciones al tacto,
  aprovisionamiento WiFi, esqueleto de web UI.
- **M1 — Es hackeable:** bus de eventos + reflejos Berry recargados en
  caliente desde la web.
- **M2 — Es una personalidad:** contrato de Brain + adaptador
  dispositivo-cloud, chat vía web, emociones/chirps movidos por el LLM,
  packs de personalidad.
- **M3 — Escucha:** bucle de voz push-to-talk — mantener para hablar → STT
  cloud → Brain → respuesta TTS, latencia enmascarada con gestos de pensar.
- **M4 — Es tuyo:** carcasa propia montada, pack por defecto pulido, demo.
- **v2+ (post-talleres):** wake word (microWakeWord), cerebro en hub, Skills,
  el módulo **base de potencia** (batería 18650 + carga/boost clase IP5306 +
  cuello con 2× servos MG90S, acoplándose a la carcasa v1 preparada para
  cuello), cartuchos de pack por NFC (stretch de la sesión 4 si da tiempo),
  más placas.

## 9. Referentes que vale la pena estudiar

- **Tasmota** — scripting Berry embebido en ESP32 (el patrón para M1)
- **ESPHome** — firmware guiado por configuración, UX de aprovisionamiento web
- **wire-pod** (Anki Vector) — arquitectura de hub para un robot compañero
- **Willow / Home Assistant Voice PE** — pipelines de voz en ESP32-S3
- **ElatoAI, demos ESP32 de OpenAI realtime** — voz directa dispositivo↔API
  realtime
- **Stack-chan** (meganetaaan) + el oficial **M5StackChan** (ESP32-S3) — el
  primo más cercano: carcasa/placa/firmware abiertos, host
  TypeScript-en-Moddable + "MODs" rápidos (valida nuestra división núcleo
  C++ + scripts con recarga). Su cuello servo pan/tilt y sus soportes
  impresos son la referencia para nuestra base de potencia v2. Contraste en
  soberanía: M5StackChan se configura vía app del fabricante + cuenta + nube
  XiaoZhi; nosotros vía web servida por el dispositivo + claves propias
  ("un Stack-chan sin casero").
- **m5stack-avatar** (MIT) — cara paramétrica: estados de expresión, timing
  de parpadeo, sacadas y **lip-sync por amplitud** (la boca se abre según el
  volumen del TTS). No es drop-in (M5GFX vs nuestro stack) pero el modelo de
  cara se porta; nuestro renderer debería adoptar el lip-sync por amplitud
  desde el día uno — el encanto más barato por línea del género.
