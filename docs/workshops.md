# Plan de talleres — 4 sesiones × 4 h

Plan guiado por una restricción que no cambia: **cada sesión termina con algo
que visiblemente funciona.**

Lo que sí ha cambiado desde la primera versión de este plan: el firmware llegó
antes de tiempo. Las sesiones ya no van de construir el núcleo —está
construido y verificado en placa— sino de que los otros cinco equipos lo
alcancen y de que el buddy pase de «funciona» a «es nuestro».

## Fechas

| | fecha | hora | dónde |
|---|---|---|---|
| Sesión 1 | **sábado 29 de agosto de 2026** | 14:00–18:00 | Gógoblu, El Carmen de Viboral |
| Sesión 2 | sábado 5 de septiembre | 14:00–18:00 | ídem |
| Sesión 3 | sábado 12 de septiembre | 14:00–18:00 | ídem |
| Sesión 4 | sábado 19 de septiembre | 14:00–18:00 | ídem |

Según la [agenda del lab](https://hacklaboriente.org/agenda/): **semanales,
todos los sábados**. Siete días entre sesiones — y eso es lo que decide el
plan: **entre sesión y sesión casi no hay tiempo de construir.** Lo que no esté
hecho el sábado se hace en la sala o no se hace.

## Los seis equipos

| equipo | responsable | prefijo de eventos |
|---|---|---|
| Firmware | Daniel Pérez (@dapanas) | `brain.*`, `boot.*`, `system.*`, `timer.*`, `storage.*` |
| Voz | Elkin Botero (@cybux) | `voice.*`, `sound.*` |
| Web | Juan Esteban Londoño (@jelondoca) | `config.*` |
| Electrónica | Diego Isaza (@Dsaoro) | `touch.*`, `nfc.*`, `sense.*` |
| CAD | Juan Sebastián Carrasquilla (@Juansess) | — |
| Personalidad | Luis Palacios (@LuisGuillemoPalaciosGiraldo) | `face.*`, `led.*` (con firmware) |

El plan original tenía tres tracks (núcleo, voz, carcasa) y se escribió antes
de que existieran los equipos. Ahora cada sesión lleva **una línea por
equipo**: si un equipo no aparece en una sesión, esa tarde tiene gente parada.

**Se construyen 2–3 prototipos, no uno por persona**
([decisión del lab, julio](hardware.md)). Es decir: el cuello de botella no es
el código ni las placas, son las manos disponibles por placa. Por eso los
equipos que no tocan hardware tienen trabajo propio y paralelo desde la
sesión 1.

## Lo que ya funciona (estado a 2026-08-17)

Verificado en placa, no «debería andar». **Nadie tiene que volver a construir
esto**, y quien llegue el día 29 se encuentra una referencia conocida-buena
contra la que comparar cuando lo suyo no arranque:

| subsistema | estado | evidencia |
|---|---|---|
| Pantalla y caras | 8 emociones, splash con glitch | 30,4 ms/frame (32,9 fps) en S3 |
| Pad táctil | `touch.down` · `touch.pet` · `touch.poke` | reflejos disparando en vivo |
| Anillo LED | `led.mood`, 4 estados | |
| Bus de eventos | 15 eventos, dueños por prefijo | [event-registry.md](event-registry.md) |
| VM Berry | integrada, **recarga en caliente** desde la web | `packs/zero/reflexes/main.be` |
| Web UI | editor de reflejos | 3 endpoints, **sin autenticación** — #27 |
| Cerebro | Anthropic, `buddy.ask` | conversación real |
| NFC (RC522) | UID de 7 bytes + texto NDEF | etiqueta real leída en S3 |
| Tipografía | acentos y ñ en pantalla | `¡Ñoño! ¿Qué tal? áéíóú` |
| Audio de salida | MAX98357A + silenciado por hardware | [spike i2s-audio](https://github.com/HackLab-Oriente/desktop-buddy/blob/spike/i2s-audio/spikes/i2s-audio/README.md) |
| Audio de entrada | INMP441, suelo −57 dBFS de pico | [spike i2s-mic](https://github.com/HackLab-Oriente/desktop-buddy/blob/spike/i2s-mic/spikes/i2s-mic/README.md) |
| Bucle push-to-talk | mantener → grabar mudo → soltar → oírse | ídem, verificado |

Y lo que **no** existe, que es de lo que van estas cuatro tardes:

- **Configuración**: WiFi y claves siguen compiladas — nadie puede llevarse una
  placa que funcione (#5, #27; contrato en [config-api.md](config-api.md))
- **Voz dentro del framework**: el bucle existe como spike aislado, no como
  parte del buddy (#9, #26)
- **Cargador de packs**: `pack.json` está especificado y el firmware no lo lee (#21)
- **Carcasa**: sin empezar (#15)
- **Tarjeta SD**: sin tocar

## Antes de la sesión 1 (12 días, responsable: Daniel)

- [x] Pedir todos los componentes *(pedido 2026-07-13)*
- [x] Repo publicado, con firmware que compila y hace bastante más que dibujar
- [x] Prototipo montado en protoboard, como referencia conocida-buena
- [ ] Instrucciones de ESP-IDF **probadas en Linux y Windows** — macOS y
      **Linux verificados** (2026-08-29: activación del entorno v6.0.2,
      submódulos, build y flash a un ESP32-S3 real de punta a punta); **falta
      Windows**, que sigue siendo el riesgo de calendario más caro que queda
- [ ] Cuentas y claves de proveedor según [services.md](services.md):
      **Claude Haiku 4.5 (cerebro) + OpenAI para STT y TTS** — una sola clave
      para las dos mitades de voz. *(El plan viejo recomendaba Groq Whisper; se
      cambió al medir su mínimo de facturación de 10 s.)* Probarlas con un
      script desde el portátil antes del día 29, para que el equipo de voz
      depure el lado ESP32 y nunca el lado cuentas.
- [ ] Núcleo de la API de configuración: esquema, NVS y validación con pruebas
      de host — **sin la parte que depende de la decisión de autenticación**
      (#27), que se toma en la sesión 1.

## Sesión 1 — Definir, decidir y repartir

**Meta: el grupo es dueño de la definición, las decisiones abiertas están
cerradas, y cada equipo termina la tarde con algo suyo en el repo.**

**Primera hora: cerrar las decisiones abiertas.** Es el bloque más valioso de
las cuatro sesiones, y ahora tiene material real — cinco issues etiquetadas
`needs-decision`, cada una con su contexto ya escrito:

| # | decisión | bloquea a |
|---|---|---|
| #20 | ¿seguimos redondos? | **CAD — no puede empezar sin esto** |
| #19 | `face.emotion` (8) vs `led.mood` (4): vocabulario duplicado | personalidad, firmware |
| #16 | cómo se llama el set de estados | personalidad |
| #24 | qué puede poner una pegatina NFC | personalidad |
| #27 | autenticación del web UI: hoy no hay ninguna | firmware, web |

#20 va primero y sin discusión larga: es la única que deja a un equipo entero
sin poder trabajar el resto de la tarde.

Después, en paralelo:

- **Firmware** — dos papeles a la vez, y el segundo pesa más de lo que parece:
  *(a)* adelantar la API de configuración, que es el bloqueo de «llevarse una
  placa a casa»; *(b)* hacer de facilitador — que los otros cinco equipos
  tengan entorno montado y su primer PR mergeado hoy. Con 2–3 placas y seis
  equipos, el cuello de botella son las personas, no el código.
- **Voz** — #10 (grabar 3 s y reproducir) ya está demostrado en el spike, así
  que la tarde empieza directamente en **#25: medir el coste de un handshake
  TLS en el S3**. Es lo que decide el diseño de STT y es el bloqueo de la
  sesión 2. Reproducir el spike en su placa es el calentamiento, no la meta.
- **Web** — #12: inventario del editor de reflejos, qué hace hoy y qué falta.
  Es la puerta de entrada al código y la base de #11.
- **Electrónica** — #13 (cerrar la BOM) y #14 (RC522 en el ESP32 clásico; en
  S3 ya está verificado).
- **CAD** — #15: medir componentes y bocetar, en cuanto caiga #20.
- **Personalidad** — dueños de tres de las cinco decisiones de arriba;
  después, empezar el contenido del pack `default`.

**Cierre**: demo por equipo y confirmar responsables. El equipo de voz necesita
1–2 personas comprometidas hasta la sesión 4.

## Sesión 2 — Que se pueda llevar a casa

**Meta: una placa se configura sin recompilar, y la voz entra en el framework.**

- **Firmware** — portal SoftAP + QR de WiFi en la cara
  ([config-api.md](config-api.md)). Al final de la tarde, un buddy se conecta a
  una red nueva sin tocar `menuconfig`. Ese es el listón visible de la sesión.
- **Voz** — sacar el bucle PTT del spike y meterlo en el framework, con los
  eventos `voice.listening` / `voice.thinking` (#9). Adelantado desde la
  sesión 3 porque el spike ya está verde.
- **Web** — #11: pantalla de ajustes contra el esquema del contrato. Ojo:
  **los secretos no se leen de vuelta**; la UI muestra «configurada» y una
  acción de reemplazar, no un campo relleno.
- **Electrónica** — raíl de 5 V, condensador de desacoplo, y preparar el paso
  de protoboard a soldadura.
- **CAD** — primera impresión de prueba, ajustar, iterar.
- **Personalidad** — prompt del pack `default` y las caras que faltan para el
  bucle de voz: **pensando y escuchando**. Sin ellas, el viaje de 1,5–3 s de
  la sesión 3 se ve como un cuelgue.

## Sesión 3 — Personalidad y bucle de voz completo

**Meta: es hackeable, tiene alma, y te contesta hablando.**

- **Firmware** — cargador de packs (#21) y sacar `kEmotions` de C++ a datos del
  pack (#18). A partir de aquí, cambiar la personalidad no recompila nada.
- **Voz** — el bucle entero: mantener el pad → silenciar + grabar; soltar →
  STT → cerebro → TTS por el amplificador. `voice.thinking` en el bus para que
  el pack tape la espera con un gesto.
- **Web** — gestión de packs: elegir el activo, subirlo, editarlo.
- **Electrónica** — soldar sobre placa perforada.
- **CAD** — impresión final, encolada.
- **Personalidad** — el pack `default` terminado. Es la tarde en la que el
  trabajo divertido y sin C++ decide cómo es el bicho.

## Sesión 4 — El buddy

**Meta: montado, con carcasa, demoable — y habla.**

- Montaje dentro de la carcasa; las cargas van al raíl, nunca por el devkit.
- **Pulido de voz dentro de la caja cerrada**: la acústica cambia cuando
  altavoz y micro comparten carcasa. Presupuesta una hora entera, y recuerda
  que half-duplex significa no escuchar nunca mientras suena un chirp.
- Experiencia de arranque, comportamientos de reposo, ajuste del pack.
- **Última hora reservada**: demo, retro y votación de roadmap (¿wake word?
  ¿hub? ¿actuadores? ¿qué quiere el lab después?).

## La sesión de entrenamiento (suelta)

[Taller de entrenamiento](training-workshop.md): entrenar un modelo de lenguaje
diminuto, de cero experiencia en ML a un modelo propio escribiendo frases en el
buddy. **No hace falta saber nada de IA**; si sabes ejecutar comandos en una
terminal, sabes entrenar un modelo.

Es la única sesión del programa que **flota**, y por dos razones concretas:

- **No necesita hardware.** Portátiles y terminal. No compite por los 2–3
  prototipos, que es el recurso escaso de las otras cuatro tardes.
- **El lado de inferencia ya está resuelto**: el spike `tinylm-s3` mide
  **152 tok/s** para un modelo de 260K parámetros en el S3, con la salida
  streameando a la cara ([local-model-bringup.md](local-model-bringup.md)).
  La sesión va de entrenar, no de arrancar.

**Recomendación: después de la serie, no antes.** El primer sábado libre es el
26 de septiembre. Necesita fecha propia: las sesiones de *IA y hardware
hacking* de noviembre que hay en la agenda **son otra cosa**, no esta.

Por qué después:

1. **No cabe antes.** La serie ocupa cuatro sábados consecutivos desde el 29
   de agosto, y el sábado anterior está a menos de una semana vista.
2. **Añadir un quinto sábado consecutivo antes de empezar arriesga la
   asistencia a la serie**, que es lo que tiene fecha y presupuesto.
3. **Nada del buddy la bloquea.** Ninguna de las cuatro sesiones necesita que
   alguien haya entrenado un modelo.

Lo único que podía haber obligado a adelantarla era **#17** (frases locales:
¿banco o modelo?). **Ya está resuelta, y con las dos**: cada expresión declara
su `source`, `bank` o `model`, y el banco es siempre el suelo del que se cae
([pack-format.md](pack-format.md#de-dónde-salen-las-frases)). Con eso, el
modelo local pasa a ser una mejora opcional y no un prerrequisito de nada —
que es lo que deja a esta sesión flotar de verdad.

## Notas de riesgo

- **El único riesgo real sigue siendo el scope creep.** Que el firmware vaya
  adelantado invita a subir el techo; la respuesta es no. Todo lo que no esté
  en M0–M4 va a la lista v2+, en público, en la sesión 1. El margen ganado se
  gasta en carcasa, contenido y pulido, que es donde este plan siempre ha
  tenido menos colchón.
- **Siete días entre sesiones no dan para construir.** El plan asume que el
  trabajo pasa en la sala. Cualquier tarea que dependa de «lo termino entre
  semana» hay que tratarla como opcional, no como planificada.
- **ESP-IDF puede comerse una hora por portátil**, y es el riesgo de calendario
  más caro que queda abierto porque escala con el número de asistentes.
  Instalación como pre-trabajo, verificación en los primeros 30 minutos del
  día 29, y firmware haciendo de facilitador esa primera hora.
- **La voz es lo más arriesgado de v1 — tiene equipo, responsable y plan B, no
  un alcance mayor.** Solo PTT: soltar = fin de frase, tocar = silencio. Sin
  VAD, sin AEC, sin wake word; eso es v2. Si el bucle no está listo en la
  sesión 3, se entrega como stretch goal y la demo corre con chirps + chat web,
  que la arquitectura mantiene como primera-clase exactamente por esto.
- **Que el núcleo esté hecho no significa que la sesión 1 sea una demo.** Si
  las cuatro horas se van en mirar una pantalla que ya funciona, se pierde lo
  único que estos talleres no pueden recuperar después: que la gente sienta que
  el bicho es suyo. Por eso la primera hora son decisiones del grupo y no una
  presentación.
