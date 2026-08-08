# Registro de eventos — el contrato del bus

Cada evento que existe, qué lleva, quién lo emite y de quién es el nombre.

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
| `nfc.tag` | UID hex, ej. `04A2B3C4` | rc522 | tarjeta presentada |
| `time.synced` | — | wifi/SNTP | reloj puesto en hora tras conectar |

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

## Espacios de nombres

Un equipo es dueño de los nombres bajo su prefijo. **Añadir un evento en tu
propio espacio no pide permiso** — un PR que cambia el código *y este archivo*
a la vez. Cambiar o quitar un evento del espacio de otro equipo necesita al
responsable de ese equipo.

| prefijo | dueño | estado |
|---|---|---|
| `touch.*`, `nfc.*`, `sense.*` | Electrónica | `touch.*`, `nfc.tag` vivos |
| `voice.*`, `sound.*` | Voz | ninguno aún — los define el track |
| `face.*`, `led.*` | Personalidad + Firmware | vivos |
| `config.*` | Web UI | ninguno aún |
| `brain.*`, `boot.*`, `system.*`, `timer.*`, `storage.*` | Firmware y arquitectura | parcialmente vivos |

Por qué esta división: si firmware tiene que nombrar cada evento, los demás
tracks se bloquean esperando a una persona. Si cada uno inventa libremente,
la misma idea aparece tres veces con tres nombres. La propiedad por espacios
da autonomía dentro de un límite, que es la única versión que sobrevive a
cuatro tracks en paralelo.

## Agujeros conocidos

Inconsistencias reales, listadas para que nadie las redescubra:

1. **`face.look` está muerto.** `round_face.cpp` se suscribe; nadie lo
   publica. Resto del experimento del logo flotante. O se conecta a la
   mirada o se borra el suscriptor.
2. **Documentado pero no implementado.** `architecture.md` menciona
   `timer.idle_5m`, `sense.light.dark` y `webhook.*`; `pack-format.md`
   promete `sound.done` y `storage.sd.gone`. Ninguno existe en código. Son
   diseños razonables — solo que aún no son reales, y un doc que describe
   eventos ausentes le cuesta una tarde a alguien.
3. **El bucle de voz necesita eventos que no existen**: como mínimo
   `voice.listening` y `voice.thinking`, para que un pack tape el viaje de
   1,5–3 s con un gesto. Primer encargo del equipo de voz.
4. **El payload no tiene esquema.** `brain.reply` ya lleva JSON dentro del
   string. Tolerable hoy; dolerá cuando la web consuma eventos. Si aparece
   un segundo payload estructurado, revisar antes de que haya un tercero.
5. **El vocabulario de emociones está duplicado.** `face.emotion` acepta los
   ocho nombres de `face_model.cpp`, mientras `led.mood` acepta solo
   `calm|excited|thinking|off`. Dos vocabularios solapados para un concepto —
   lo que el grupo decida sobre expresiones tiene que reconciliarlos.
6. **`brain.error` no tiene suscriptor.** El cerebro cloud lo publica al
   fallar y nada reacciona, así que una petición fallida es silenciosa: el
   buddy simplemente no contesta nunca. Es el único agujero que contradice
   un principio declarado ("nunca un ladrillo") — como mínimo debería mover
   una cara.
7. **`time.synced` no tiene suscriptor.** Inofensivo hoy, pero significa que
   nada espera al reloj; cualquier cosa basada en la hora lo necesitará.

## Añadir un evento — checklist

1. ¿Es un **hecho** (un sense) o una **petición** (una salida)? Eso elige el
   espacio de nombres.
2. ¿El espacio es tuyo? Si no, habla antes con su responsable.
3. Añade la fila a este archivo **en el mismo PR** que el código.
4. Payload: el string más pequeño que funcione. Una ruta o un id, nunca
   bytes.
5. Si un pack debería reaccionar a él, dilo también en
   [pack-format.md](pack-format.md) — los packs son API pública.
