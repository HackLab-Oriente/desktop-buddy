# Plan de talleres — 4 sesiones × 4 h

Plan guiado por una restricción: cada sesión termina con algo que
visiblemente funciona. Tres tracks en paralelo para que nadie esté parado:
**núcleo** (firmware/framework), **voz** (1–2 responsables, sesiones 1–4 — el
push-to-talk es meta de v1) y **carcasa** (CAD/impresión). El desarrollo
continúa después de los talleres (wake word, hub, skills — ver arquitectura
v2+).

## Antes de la sesión 1 (prep crítica, responsable: Daniel)

- [x] Pedir todos los componentes (ver [hardware.md](hardware.md)) — los
  plazos de entrega matan talleres *(pedido 2026-07-13)*
- [ ] Este repo publicado, con un esqueleto de firmware que compila y
  parpadea/dibuja
- [ ] Instrucciones de instalación de ESP-IDF probadas en macOS/Linux/Windows
- [ ] Un prototipo montado en protoboard en casa, para que la sesión 1 tenga
  una referencia que se sabe buena
- [ ] Crear cuentas de proveedores + API keys según [services.md](services.md)
  (recomendado: Claude Haiku 4.5 + Groq Whisper + OpenAI mini-tts);
  probarlas primero con un script desde el portátil (POST de un WAV →
  transcripción → LLM → audio TTS de vuelta), para que el spike de voz de la
  sesión 2 depure solo el lado ESP32, nunca el lado cuentas/API

## Sesión 1 — Definir y despertar el hardware

**Meta: el grupo es dueño de la definición; cada subsistema probado en
protoboard.**

- 45 min: presentar esta definición, discutirla, enmendarla, decidir los
  puntos abiertos (forma de la pantalla, nombre del proyecto/buddy)
- Tracks en paralelo:
  - *Firmware:* despertar la placa — la pantalla dibuja, el pad táctil lee,
    los niveles del micro se imprimen, el altavoz pita (firmware de prueba
    aislado por subsistema)
  - *Voz:* grabar un clip de 3 s con el INMP441 y reproducirlo por el
    MAX98357A — demuestra I2S de entrada *y* de salida más el silenciado
    durante la grabación, que es todo el mecanismo half-duplex del que
    depende el PTT
  - *Hardware/carcasa:* medir componentes, bocetar la carcasa, empezar el CAD
- Cierre: demos por subsistema, asignar responsables por track (el de voz
  necesita 1–2 personas comprometidas hasta la sesión 4)

## Sesión 2 — El núcleo

**Meta: está vivo — cara + reacciones + WiFi + esqueleto de web UI (M0),
decisión sobre Berry tomada.**

- *Firmware:* aterriza el bus de eventos; cara con 2–3 animaciones; los
  eventos táctiles disparan reacciones; aprovisionamiento WiFi (modo AP) +
  servidor web con una UI mínima
- *Spike (1–2 personas):* integración de la VM Berry — disparar un evento a
  un script, el script lanza una animación. Punto de decisión: Berry vs Lua,
  por fricción
- *Voz:* spike de streaming en firmware de prueba aislado — micro → ring
  buffer → TLS/WebSocket → API de STT en streaming → transcripción por
  serie. Vigilar la RAM interna (los buffers DMA + WiFi no pueden vivir en
  PSRAM; TLS cuesta ~50 KB) y fijar audio al core 1, red al core 0. Listón:
  hablarle a la protoboard y ver tus palabras impresas
- *Carcasa:* primera impresión de prueba, ajuste, iterar

## Sesión 3 — La personalidad y el bucle de voz

**Meta: es hackeable, tiene alma, y te oye en la mesa (núcleo de M1 + M2 +
M3).**

- *Firmware:* reflejos Berry con recarga en caliente desde la web; carga del
  formato de packs (pack.json + reflejos + caras + sonidos)
- *Cerebro:* adaptador dispositivo-cloud — chatear con el buddy desde la
  consola de texto de la web; las respuestas del LLM mueven emoción →
  animación + chirp
- *Voz:* cablear el spike dentro del framework como bucle PTT completo —
  mantener el pad → silenciar audio + grabar; soltar → fin de frase → STT →
  Brain → TTS de vuelta por el amplificador. Al soltar, disparar
  `voice.thinking` en el bus para que el pack tape el viaje de 1,5–3 s con
  un gesto
- *Contenido:* un grupo pequeño crea el pack de personalidad `default`
  (prompt, animaciones, chirps, **caras de pensando/escuchando** para el
  bucle de voz) — trabajo divertido y sin C++ para todos los demás
- *Carcasa:* impresión final encolada

## Sesión 4 — El buddy

**Meta: montado, con carcasa, demoable — y habla (M3 + M4).**

- Pasar de protoboard a placa soldada; montar dentro de la carcasa
- Pulido de voz: ajustar niveles de silenciado y el timing del gesto de
  pensar dentro de la carcasa cerrada (la acústica cambia cuando altavoz y
  micro comparten caja — presupuesta una hora, y recuerda que half-duplex
  significa no escuchar nunca mientras suena un chirp)
- Pasada de pulido: experiencia de arranque, comportamientos de reposo,
  ajuste del pack por defecto
- Reservar la última hora para la demo + retro + votación de roadmap (¿wake
  word? ¿hub? ¿actuadores? ¿qué quiere el lab después?)

## Notas de riesgo

- **El único riesgo real es el scope creep.** Todo lo que no esté en M0–M4 va
  a la lista v2+, en público, en la sesión 1.
- **La voz es lo más arriesgado de v1 — tiene track, responsables y un plan
  B, no un alcance mayor.** Solo PTT (soltar = fin de frase, tocar =
  silencio; sin VAD, sin AEC, sin wake word — eso es v2). Si el bucle no
  funciona al final de la sesión 3, se entrega como stretch goal y la demo
  corre con chirps + chat web, que la arquitectura mantiene como
  primera-clase exactamente por esta razón.
- La instalación de ESP-IDF puede comerse una hora por portátil — hacerla
  como pre-trabajo, verificar en los primeros 30 min de la sesión 1.
- Si la integración Berry/Lua se atasca en la sesión 2, el plan B de M1 son
  reflejos declarados en JSON (tablas disparador → acción) con el scripting
  pospuesto; el formato de packs no cambia en ningún caso.
