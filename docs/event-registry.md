# Registro de eventos — el contrato del bus

Cada evento que existe, qué lleva, quién lo emite y de quién es el nombre —
y, en una tabla aparte, los que **todavía no existen pero ya están
diseñados**, para que nadie construya contra un nombre creyendo que es real.

Esto es el **contrato entre equipos**. Es la razón de que los tracks puedan ir
en paralelo: el equipo de voz puede construir sobre `voice.*` mientras
firmware sigue con la API de configuración, siempre que ambos lados acuerden
los nombres. Mantén este archivo al día — un registro equivocado es peor que
ninguno.

Dueño: **firmware y arquitectura** (el contrato). Cada equipo es dueño de los
eventos dentro de su espacio de nombres — ver [Espacios](#espacios-de-nombres).

Vista interactiva de este archivo — con buscador y los sitios reales de
publicación y suscripción: [event-registry.html](event-registry.html).

## Cómo funciona el bus

- `bus().publish(name, payload)` encola; la entrega ocurre en `pump()`,
  ejecutado por la tarea del bus. **Los handlers por tanto nunca corren en
  paralelo** y no necesitan locks propios.
- `bus().subscribe(pattern, fn)` donde `pattern` es un nombre exacto, un
  comodín de prefijo (`touch.*`) o `*` para todo.
- `Event` es `{ std::string name; std::string payload; }` —
  [bus.h](../firmware/components/bus/include/bus.h). Un string. Los datos
  grandes nunca viajan en el bus; pasa una ruta o un id.
- Los reflejos Berry ven los mismos eventos vía `buddy.on(pattern, fn)`, con
  `ev['name']` y `ev['payload']`.

## Convenciones

1. **`espacio.cosa`**, minúsculas, separado por puntos. El espacio es el
   subsistema dueño, no el destinatario.
2. **Para entradas, nombra hechos, no órdenes.** `touch.pet` es algo que
   pasó. Las salidas pueden ser imperativas (`face.emotion`, `led.mood`)
   porque son peticiones a un subsistema.
3. **El payload es un string.** Texto plano donde se pueda; JSON solo donde
   la estructura sea inevitable (hoy: `brain.reply`). Si necesitas un segundo
   campo, eso es señal de consultar antes con el dueño del contrato.
4. **Nunca bloquees en un handler.** El pump es monohilo; un handler lento
   atasca a todos los demás suscriptores.
5. **Aditivo por defecto.** Añadir un evento es barato; renombrarlo rompe
   todos los packs que existan. Elige el nombre como si no pudieras
   cambiarlo.

## Eventos actuales

### Senses — cosas que pasaron

| evento | payload | lo emite | cuándo |
|---|---|---|---|
| `touch.down` | `"pad0"` | [touch_sense.cpp](../firmware/components/senses/touch_sense.cpp) | el dedo hace contacto |
| `touch.poke` | `"pad0"` | touch_sense | soltado en **< 400 ms** |
| `touch.pet` | `"pad0"` | touch_sense | soltado en **≥ 400 ms** |
| `nfc.tag` | UID hex, 8 o 14 dígitos | rc522 | tarjeta presentada |
| `nfc.text` | texto NDEF decodificado (máx. 128) | rc522 | **solo si** la tarjeta lleva contenido legible |
| `nfc.gone` | — | rc522 | la tarjeta salió del campo |
| `time.synced` | — | wifi/SNTP | reloj puesto en hora tras conectar |

Tres garantías de los eventos `nfc.*` que conviene tener por escrito, porque no
se pueden adivinar leyendo el código:

1. **`nfc.tag` siempre llega antes que `nfc.text`** en la misma presentación.
   Un reflejo que necesite identidad *y* contenido guarda el UID al recibir
   `nfc.tag` y lo usa cuando llega el texto.
2. **Una tarjeta en blanco solo emite `nfc.tag`.** La ausencia de `nfc.text` es
   la señal; no hay caso especial de cadena vacía.
3. **Mantener la tarjeta puesta no repite eventos.** Se emite al llegar y
   `nfc.gone` al irse (con histéresis de ~600 ms, porque una lectura fallida
   suelta es ruido de RF, no una retirada). Eso convierte «dejar la tarjeta
   encima» en un gesto sostenido, no en un interruptor.

**El firmware no interpreta el contenido.** `nfc.text` lleva el string tal cual;
qué significa `mood:happy` lo decide un reflejo Berry del pack, no C++. Si la
gramática viviera en el firmware, cada verbo nuevo pediría reflashear — justo
lo contrario del compromiso «el comportamiento es datos». La gramática de
cartuchos la define el equipo de personalidad.

### Brain — el viaje de pensar

| evento | payload | lo emite | cuándo |
|---|---|---|---|
| `brain.ask` | texto del prompt | reflejos | algo quiere una respuesta |
| `brain.reply` | JSON `{"utterance": "...", "emotion": "..."}` | brain_cloud | el modelo contestó |
| `brain.error` | `"no_reply"` | brain_cloud | la petición falló |

`brain.reply` se parsea centralmente en [main.cpp](../firmware/main/main.cpp)
y se reparte en `face.emotion` + `face.say`; los packs normalmente reaccionan
a esos dos, no a la respuesta cruda.

### Expressions — peticiones a subsistemas de salida

| evento | payload | lo consume | notas |
|---|---|---|---|
| `face.emotion` | nombre de emoción (`happy`, `sad`…) | round_face, led_ring | el anillo copia el color de la cara |
| `face.say` | texto | round_face | palabras en pantalla |
| `face.look` | objetivo de mirada | round_face | **suscrito, nunca publicado** — ver agujeros |
| `led.mood` | `calm` \| `excited` \| `thinking` \| `off` | led_ring | estilo de animación, no color |

### Sistema

| evento | payload | lo emite | cuándo |
|---|---|---|---|
| `boot.status` | texto corto de estado | main | cada paso del arranque; mueve la línea del splash |
| `boot.ready` | — | main | arranque terminado; la cara sale del splash con glitch |
| `system.reload` | — | web UI | recargar la VM Berry (recarga en caliente) |

## Eventos propuestos

**Nada de esta tabla existe en código.** Están aquí porque ya estaban
escritos en otros documentos, y un evento prometido en un doc y ausente del
registro le cuesta una tarde a quien se lo cree. Al menos ahora están todos
en el mismo sitio, con dueño y con el estado dicho en voz alta.

Tres reglas para esta sección:

- **Un nombre de aquí no es un compromiso.** Puede cambiar o desaparecer
  mientras siga en esta tabla. Los nombres solo son para siempre cuando
  cruzan a «Eventos actuales».
- **Se sube a «Eventos actuales» en el mismo PR que lo implementa**, nunca
  antes. Ese PR es el que fija el nombre.
- Si algo lleva aquí mucho tiempo, la pregunta correcta no es «¿cuándo se
  implementa?» sino «¿esto lo sigue queriendo alguien?».

| evento | payload propuesto | dueño | especificado en | para qué |
|---|---|---|---|---|
| `voice.listening` | — | Voz | [#9](https://github.com/HackLab-Oriente/desktop-buddy/issues/9) | el pad está mantenido y el micro graba |
| `voice.thinking` | — | Voz | [#9](https://github.com/HackLab-Oriente/desktop-buddy/issues/9) | soltaste el pad: empieza el viaje de 1,5–3 s de STT + cerebro + TTS. **Es el evento que deja al pack tapar la espera**, y sin él la espera se ve como un cuelgue |
| `sound.done` | ruta del sonido | Voz | [pack-format.md](pack-format.md) | terminó de sonar un `buddy.sound.play` |
| `sound.error` | razón | Voz | [pack-format.md](pack-format.md) | no se pudo reproducir |
| `config.changed` | nombres de sección separados por coma (`"wifi,brain"`) | Web UI | [config-api.md](config-api.md) | cambió la configuración. **Nunca valores**: los packs Berry están suscritos al bus, así que un payload con la config dentro es una clave de API legible desde un pack |
| `config.setup` | SSID del AP (`"buddy-a3f2"`) | Web UI | [config-api.md](config-api.md) | el buddy entró en modo aprovisionamiento; la cara puede enseñar el QR |
| `timer.idle_5m` | — | Firmware | [architecture.md](architecture.md) | cinco minutos sin interacción |
| `sense.light.dark` | — | Electrónica | [architecture.md](architecture.md), [hardware.md](hardware.md) | el sensor de luz dice que la sala está a oscuras → reflejo de dormir |
| `storage.sd.gone` | — | Firmware | [pack-format.md](pack-format.md) | se quitó la tarjeta SD y los assets de `media/` dejan de resolver. Un evento, no un crash |
| `webhook.*` | — | Firmware | [architecture.md](architecture.md) | **solo hub, v2+.** No es un evento del buddy; se lista para que nadie lo confunda con uno |

## Espacios de nombres

Un equipo es dueño de los nombres bajo su prefijo. **Añadir un evento en tu
propio espacio no pide permiso** — un PR que cambia el código *y este archivo*
a la vez. Cambiar o quitar un evento del espacio de otro equipo necesita al
responsable de ese equipo.

| prefijo | dueño | estado |
|---|---|---|
| `touch.*`, `nfc.*`, `sense.*` | Electrónica | `touch.*` y `nfc.*` vivos; `sense.light.dark` propuesto |
| `voice.*`, `sound.*` | Voz | **ninguno vivo**; 4 propuestos — los fija el equipo de voz |
| `face.*`, `led.*` | Personalidad + Firmware | vivos |
| `config.*` | Web UI | **ninguno vivo**; 2 propuestos en [config-api.md](config-api.md) |
| `brain.*`, `boot.*`, `system.*`, `timer.*`, `storage.*` | Firmware y arquitectura | `brain.*`, `boot.*`, `system.*` vivos; `timer.*` y `storage.*` solo propuestos |

Por qué esta división: si firmware tiene que nombrar cada evento, los demás
tracks se bloquean esperando a una persona. Si cada uno inventa libremente,
la misma idea aparece tres veces con tres nombres. La propiedad por espacios
da autonomía dentro de un límite, que es la única versión que sobrevive a
cuatro tracks en paralelo.

## Agujeros conocidos

Inconsistencias reales, listadas para que nadie las redescubra.

*Los eventos prometidos en un doc y ausentes del código ya no son un agujero
suelto: viven arriba, en [Eventos propuestos](#eventos-propuestos), con dueño
y estado.*

1. **`face.look` está muerto.** `round_face.cpp` se suscribe; nadie lo
   publica. Resto del experimento del logo flotante. O se conecta a la
   mirada o se borra el suscriptor.
2. **El payload no tiene esquema.** `brain.reply` ya lleva JSON dentro del
   string. Tolerable hoy; dolerá cuando la web consuma eventos. Si aparece
   un segundo payload estructurado, revisar antes de que haya un tercero.
3. **El vocabulario de emociones está duplicado.** `face.emotion` acepta los
   ocho nombres de `face_model.cpp`, mientras `led.mood` acepta solo
   `calm|excited|thinking|off`. Dos vocabularios solapados para un concepto —
   lo que el grupo decida sobre expresiones tiene que reconciliarlos.
4. **`brain.error` no tiene suscriptor.** El cerebro cloud lo publica al
   fallar y nada reacciona, así que una petición fallida es silenciosa: el
   buddy simplemente no contesta nunca. Es el único agujero que contradice
   un principio declarado ("nunca un ladrillo") — como mínimo debería mover
   una cara.
5. **`time.synced` no tiene suscriptor.** Inofensivo hoy, pero significa que
   nada espera al reloj; cualquier cosa basada en la hora lo necesitará.

## Añadir un evento — checklist

1. ¿Es un **hecho** (un sense) o una **petición** (una salida)? Eso elige el
   espacio de nombres.
2. ¿El espacio es tuyo? Si no, habla antes con su responsable.
3. Añade la fila a **«Eventos actuales»** en el mismo PR que el código. Si
   aún lo estás diseñando y no hay código, la fila va a **«Eventos
   propuestos»** — un evento no cruza de una tabla a la otra sin
   implementación.
4. Payload: el string más pequeño que funcione. Una ruta o un id, nunca
   bytes.
5. Si un pack debería reaccionar a él, dilo también en
   [pack-format.md](pack-format.md) — los packs son API pública.
