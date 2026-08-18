# Formato de packs — propuesta borrador

Estado: **borrador para discusión** — se convierte en la spec `pack-format`
(`.kiro/specs/pack-format/`) cuando el equipo defina la dirección. Es una de
las cuatro piezas con contrato (dos tracks dependen de ella: el firmware carga
packs, la web los edita, los autores los escriben).

## Principios

1. **Convención antes que configuración.** Directorios reservados con
  significado fijo; lo demás que haya en el pack es asunto del autor.
2. **Las rutas son relativas al pack, siempre.** Los scripts nunca ven los
  montajes `/flash` o `/sd`; el resolvedor de assets del framework
   superpone ambos.
3. **Los bytes nunca entran a la VM de scripts.** Berry pasa rutas y
  decisiones; streaming, decodificación y buffers son trabajo de C++. Lo
   único que cruza a Berry son datos estructurados pequeños (`asset.json`,
   ≤ 4 KB).
4. **Los packs son compartibles por construcción**: sin secretos, sin estado
  específico del dispositivo, sin rutas absolutas. Comprime el directorio y
   ese es el cartucho.
5. **Media offline primero.** Todo lo que un pack necesita en *runtime* viaja
  en el pack; el LLM/TTS en vivo es para las partes sin guion.



## Estructura de directorios

```
<pack-id>/
  pack.json                 manifiesto obligatorio (esquema abajo)
  reflexes/
    main.be                 punto de entrada obligatorio; otros .be se importan desde él
  faces/
    *.anim.json             definiciones de animación
    sprites/                sprite sheets (PNG o RGB565 .bin)
  lines/
    <expresion>.txt         banco de frases, una por línea — ver «De dónde salen las frases»
  sounds/                   audio pequeño (chirps, efectos) — residente en flash
  media/                    biblioteca opcional de contenido grande — residente en SD
```



### Niveles de almacenamiento

- `pack.json`, `reflexes/`, `faces/`, `sounds/` → **flash** (LittleFS,
`/flash/packs/<id>/`). Siempre presentes; el pack arranca sin SD.
- `media/` → **tarjeta SD** (FAT, `/sd/packs/<id>/media/`). Opcional; si no
hay SD, las llamadas `asset.*` bajo `media/` devuelven nil y los reflejos
deciden el plan B.
- Orden de resolución: flash primero, luego SD. La misma ruta relativa en
ambos, así el contenido puede migrar de nivel sin tocar scripts.



## Manifiesto (`pack.json`)

```json
{
  "id": "boardgame-buddy",
  "name": "Board game explainer",
  "version": "0.2.0",
  "api_level": 1,
  "author": "cafe-member",
  "language": "es",
  "brain": {
    "system_prompt": "You are the warm, funny game-night host of this café…",
    "guardrails": "Only discuss the café's board games."
  },
  "voice": { "mode": "cached-first", "voice_id": "warm_host" },
  "expressions": { "map": "faces/expressions.json" },
  "senses": { "touch": { "pet_ms": 400 } }
}
```

- `id`: kebab-case, igual al nombre del directorio.
- `api_level`: la versión de la API de scripts del framework a la que apunta
el pack; el loader rechaza packs del futuro y corre los antiguos en modo
compatibilidad.
- `brain.system_prompt` + `guardrails`: los guardarraíles se concatenan de
forma no negociable después del prompt — los packs tipo kiosco (de cara al
público) viven o mueren por este campo.
- `voice.mode`: `"live"` (siempre TTS), `"cached-first"` (reproduce un
archivo de media si el reflejo lo nombra, TTS si no).



## Tres capas: expresión, registro, presentación

Antes del banco de frases conviene separar tres cosas que se confunden, porque
casi todas las decisiones abiertas de personalidad viven en esa confusión.

| capa | quién la define | cuántas hay |
|---|---|---|
| **expresión** | quien escribe el pack | **abiertas** — inventa las que quiera |
| **registro** | el proyecto | **cerradas y pocas** |
| **presentación** | firmware | animación de cara + `led.mood`, derivadas del registro |

Una **expresión** es un estado con nombre propio del pack: `huraño`, `festivo`,
`resacoso`. Un **registro** es una *manera de hablar*: seco, cálido, urgente.
Cada expresión declara a qué registro pertenece.

La clave se llama `register` y no `registro` porque **las claves del esquema son
código** y el código va en inglés ([CONTRIBUTING](../CONTRIBUTING.md#idiomas)).
Los **valores** los escribe quien hace el pack, en su idioma.

```json
{
  "huraño":  { "register": "seco",     "use_model": true  },
  "festivo": { "register": "juguetón"                     },
  "alerta":  { "register": "urgente",  "use_model": true  }
}
```

Esto es lo que deshace el vocabulario duplicado de
[#19](https://github.com/HackLab-Oriente/desktop-buddy/issues/19): en vez de dos
listas paralelas (ocho `face.emotion` contra cuatro `led.mood`), hay **un solo
vocabulario cerrado —el de registros— del que todo lo demás deriva**. Las
expresiones son infinitas porque son del autor; los registros son pocos porque
son el contrato.

## De dónde salen las frases

**Decidido: las dos fuentes, y se elige por expresión.**
([#17](https://github.com/HackLab-Oriente/desktop-buddy/issues/17))

- El **banco** vive en `lines/<expresión>.txt`, una frase por línea. Siempre.
- **`use_model: true`** añade el modelo local, condicionado por el **registro**
  de esa expresión.

### `use_model` es un añadido, no una alternativa

`use_model: true` **no exime de tener `lines/<expresión>.txt`.** Si no hay
modelo, si falla o si tarda de más, se cae al banco. Es la única lectura
compatible con «nunca un ladrillo»: una expresión sin frase deja al bicho mudo.

Por eso es un booleano y no un `source: "bank" | "model"`. Un enum diría que la
fuente *es* el modelo, y sería mentira — el banco sigue ahí y sigue haciendo
falta. Un booleano dice lo que de verdad pasa: «además, prueba el modelo». Y
como el valor por defecto es `false`, la expresión normal no escribe nada.

**Pero el nombre no es la defensa: la defensa es el validador.** El validador
de packs exige `lines/<expresión>.txt` para **toda** expresión, use modelo o
no. Un pack sin banco está roto de una forma que no se nota hasta que falla el
modelo, que es el peor momento para enterarse.

### El modelo es *el* del dispositivo. No hay opciones

`use_model` no lleva parámetro y no lo va a llevar: **hay un solo modelo, el
que está flasheado en la partición `model` del chip**, y un pack lo
**referencia**, nunca lo trae. Un pack pesa kilobytes; un modelo, megas.

No es un hueco por rellenar, es una decisión: **no se soportan varios
modelos.** Si algún día se soportaran, aparecería un campo entonces — no ahora,
y no «por si acaso». Si no hay modelo flasheado, `use_model: true` es
silenciosamente equivalente a `false`, que es justo lo que la caída al banco
garantiza.

### Nombres de archivo con acentos

El nombre del archivo sigue a **la expresión**, que la inventa quien escribe el
pack — no al vocabulario de registros. `huraño` → `lines/huraño.txt`. Que #16 y
#19 sigan abiertas **no bloquea** escribir bancos de frases.

Lo que sí hay que saber:

- **La longitud no es problema.** `CONFIG_LITTLEFS_OBJ_NAME_LEN=64` incluyendo
  el terminador, y `huraño.txt` son 11 bytes en UTF-8. Harían falta ~30
  caracteres acentuados para acercarse.
- **`fopen` pasa los bytes tal cual**, así que UTF-8 funciona sin tocar nada.
- **La trampa es la normalización Unicode, y es silenciosa.** `ñ` se puede
  codificar como `U+00F1` (precompuesta, NFC) o como `n` + `U+0303`
  (descompuesta, NFD). macOS tiende a NFD; LittleFS no normaliza nada y compara
  bytes. Un pack escrito en un Mac puede acabar con el archivo en NFD y el JSON
  en NFC: **son dos nombres distintos para LittleFS**, el archivo «no existe» y
  no hay ningún error que lo explique. **El validador tiene que normalizar a NFC
  y rechazar lo que no lo esté.**
- **Para mostrarlo en pantalla, Latin-1 y ya**: la fuente cubre `U+0000`–`U+00FF`
  (acentos, ñ, ¿, ¡). Un nombre de expresión en japonés o con emoji se
  imprimiría vacío.

## Los registros — juego inicial propuesto

**Propuesta, no decisión**: el juego lo cierra el equipo de personalidad
(#16, #19). Siete, porque son el vocabulario cerrado y deben caber en la cabeza.

| registro | la manera | `led.mood` sugerido |
|---|---|---|
| `cálido` | cercano, sin prisa | `calm` |
| `juguetón` | pica, se burla, no va en serio | `excited` |
| `curioso` | pregunta, se fija, no da nada por hecho | `thinking` |
| `urgente` | corto, reclama atención ya | `excited` |
| `seco` | mínimo, retiene, no colabora | `calm` |
| `somnoliento` | se apaga, se le va la frase | `off` |
| `llano` | informa y punto — el neutro | `calm` |

### Qué es escribir «en registro» y no «en situación»

Es la parte que se atraganta, así que aquí está la regla: **el registro es el
*cómo*, la expresión es el *cuándo*.** Los tres ejemplos de cada registro
contestan a propósito a situaciones distintas —un saludo, una reacción, una
negativa— para que lo único constante sea la manera.

**La prueba**: intercambia dos frases de registros distintos. Si el significado
sobrevive pero cambia la personalidad, has escrito registros. Si el significado
se rompe, has escrito expresiones.

**`cálido`**
> Ahí estás.
> Tómate tu tiempo, que no me voy a ningún lado.
> Te extrañé, aunque no lo diga.

**`juguetón`**
> ¿Otra vez tú? Qué pesado.
> Hazlo otra vez, a ver si te sale.
> Yo no vi nada. Yo no estaba.

**`curioso`**
> ¿Y eso qué es?
> Espera… ¿eso siempre estuvo ahí?
> Cuéntame más, que me da curiosidad.

**`urgente`**
> Eh. EH.
> Ahora, en serio.
> No, no, no — mira esto.

**`seco`**
> Ya.
> Si tú lo dices.
> HMPH.

**`somnoliento`**
> Mmm… ¿qué?
> Cinco minutos más.
> Sigo aquí… más o menos.

**`llano`**
> Listo.
> Conectado a la red.
> No encuentro esa tarjeta.

### Para qué sirven exactamente estos ejemplos

**No son bancos de frases.** Los bancos van por expresión (`lines/huraño.txt`) y
pueden ser todo lo específicos que quieran. Estos ejemplos definen **a qué suena
cada registro**, y sirven para dos cosas: escribir el prompt que condiciona al
modelo, y tener contra qué juzgar lo que el modelo devuelve.

Dicho de otro modo: nadie tiene que escribir un banco por registro. El registro
solo es el parámetro con el que se le pide al modelo.

### Frases cortas, y no por estilo

Tres razones medidas: la pantalla es redonda y de 240 px; el TTS se cobra por
carácter; y el buddy es **half-duplex**, así que mientras habla está sordo — una
frase larga es un rato largo sin poder escucharte.

## Los moods también son del pack

**Implementado.** Hasta ahora `led.mood` aceptaba cuatro valores fijos en C++
(`calm|excited|thinking|off`), que es una segunda lista cerrada compitiendo con
las ocho emociones — la mitad del problema de #19. Si los moods pasan a ser
datos del pack, esa lista deja de existir como vocabulario rival y se convierte
en un espacio de nombres abierto que cada pack llena.

```json
"moods": {
  "fuego":  { "anim": "breathe", "colors": ["#ff3300", "#ff8800"], "period_ms": 2400 },
  "chispa": { "anim": "spin",    "colors": ["#ffcc00"], "period_ms": 600, "dir": "cw" },
  "duerme": { "anim": "pulse",   "colors": ["#101030"], "period_ms": 5000 }
}
```

Y la cadena completa queda coherente con las tres capas:

**registro** (cerrado, del proyecto) → **mood** (nombre, del pack) → **animación** (parámetros, del pack)

### Primitivas cerradas, no un lenguaje

`anim` sale de una lista corta y fija: **`solid`, `breathe`, `spin`, `pulse`,
`off`**. Un `anim` que no esté en la lista cae a `breathe` — algo visible y
lento, nunca algo rápido ni a oscuras. La expresividad la ponen los parámetros —colores, periodo, sentido,
brillo—, no la gramática.

Es deliberado. Son **12 LEDs** actualizándose junto a una cara que renderiza a
30,4 ms/frame; un mini-lenguaje de animación aquí es una madriguera con muy
poco premio. Si alguien necesita algo que las primitivas no dan, ese es el
momento de añadir **una** primitiva más, no un intérprete.

### El contrato del bus no cambia

`led.mood` sigue siendo el mismo evento con el mismo payload: un nombre. Lo
único que cambia es **de dónde se resuelve ese nombre** — de un enum compilado
a datos del pack. Ningún cambio en [event-registry.md](event-registry.md), y
los reflejos existentes siguen valiendo tal cual.

### Y otra vez el suelo

- Un pack **sin** `moods` se queda con los cuatro de siempre. `packs/zero`
  sigue funcionando sin tocar nada.
- Un mood que se nombre y no exista **deja el que estuviera puesto** y lo
  avisa por log, en vez de apagar el anillo. Mismo principio que la caída al
  banco: lo peor que puede pasar no es quedarse a oscuras sin explicación.

### Y una cosa que el `mood` de cada expresión sí cambia

Una expresión puede nombrar su mood (`"angry": { "mood": "fuego" }`). Al llegar
`face.emotion` se aplica **como valor por defecto**: un `led.mood` publicado
después sigue ganando. Por eso los reflejos que ya existen —que publican los
dos— se comportan exactamente igual que antes, y los nuevos pueden publicar
solo `face.emotion` y dejar de repetirse.

## Los sentidos: umbrales en el pack, decisiones en los reflejos

**Propuesta.** El bus lleva eventos discretos; los sensores dan valores
continuos. Alguien tiene que decidir que 12 lux «es de noche», y hoy ese
número estaría compilado en el firmware — el mismo error que tenía
`kEmotions`.

Ya está pasando: [`touch_sense.cpp`](../firmware/components/senses/touch_sense.cpp)
parte `touch.poke` de `touch.pet` en `< 400` ms. **Un buddy al que hay que
acariciar un segundo entero para que cuente como caricia es otro bicho.** Eso
es personalidad viviendo en un número mágico.

El reparto:

> **El pack pone los umbrales que convierten lecturas en eventos. Los reflejos
> deciden qué significan esos eventos.**

Lo que *no* se propone es un sistema de condiciones declarativas en el pack
(«cuando esté oscuro, ponte somnoliento»). Eso ya lo hacen los reflejos, en
caliente y con toda la potencia de un lenguaje; una segunda capa de
comportamiento en JSON sería la duplicación de #19 otra vez, ahora en el
comportamiento. Y las condiciones crecen: «cuando esté oscuro» acaba siendo
«cuando esté oscuro Y nadie me toque hace 30 s Y sean más de las diez», que es
un lenguaje de scripting — y ya tenemos uno mejor.

```json
"senses": {
  "touch":    { "pet_ms": 400 },
  "idle":     { "after_s": 300 },
  "light":    { "dark_below_lux": 15, "bright_above_lux": 40 },
  "motion":   { "shake_g": 1.8, "pickup_g": 0.4 },
  "presence": { "arrive_after_s": 2, "leave_after_s": 30 }
}
```

### Valores por defecto

Como con las expresiones y los moods: **el pack sobrescribe, lo ausente usa el
built-in.** Un pack sin `senses` se comporta igual que hoy, y un pack puede
tocar un solo umbral sin declarar los demás.

| umbral | por defecto | de dónde sale |
|---|---|---|
| `touch.pet_ms` | **400** | el valor que ya está compilado; así nada cambia al adoptarlo |
| `idle.after_s` | **300** | los 5 min del `timer.idle_5m` propuesto |
| `light.dark_below_lux` | **15** | una habitación a oscuras da <10 lux; en penumbra, ~50 |
| `light.bright_above_lux` | **40** | |
| `motion.shake_g` | **1,8** | **provisional** — la capa de gestos necesita play-testing |
| `motion.pickup_g` | **0,4** | **provisional**, ídem |
| `presence.arrive_after_s` | **2** | el radar ve el movimiento antes de que te sientes |
| `presence.leave_after_s` | **30** | irse cuesta más que llegar, a propósito |

**Los umbrales van en pares, y ese hueco *es* la histéresis.** Un umbral único
tartamudea en la frontera: a 15,0 lux exactos emitiría `dark`/`bright` en
bucle. Por eso oscuro es «<15» y claro es «>40», con tierra de nadie en medio.
Lo mismo en el tiempo con `presence`: llegar tarda 2 s y marcharse 30, para que
asomarte fuera del alcance del radar no te «vaya» del escritorio. El driver del
RC522 ya lleva histéresis de 3 fallos por esta misma razón.

Y una razón práctica que ya está escrita en
[hardware.md](hardware.md): la capa de gestos del MPU6050 «necesita
play-testing humano para ajustar umbrales — presupuesta una hora divertida».
Con los umbrales compilados, esa hora es un bucle de recompilar y reflashear.
Con los umbrales en el pack, es editar y recargar. Es la diferencia entre una
hora divertida y una hora miserable.

### Leer sensores desde Berry

```berry
buddy.sense(name)                 # un valor, o nil si no hay sensor / la lectura está vieja
buddy.sense([n1, n2, n3])         # varios de golpe -> map; una sola llamada nativa
```

**Sí, y con una regla que no es negociable: `buddy.sense()` lee una caché,
nunca el dispositivo.** Los sensores los sondea su propia tarea a su propio
ritmo y dejan el resultado en una instantánea; `buddy.sense()` lee memoria, en
microsegundos.

El motivo es el de siempre en este bus: **la entrega de eventos es monohilo y
un handler no puede bloquear.** Una lectura I2C del AHT20 tarda ~80 ms; hacerla
dentro de un handler congelaría la entrega de eventos para todo el mundo.

No es mecanismo nuevo: [hardware.md](hardware.md) ya dice que todos los
sensores alimentan el `sensor_snapshot` del Brain. Esto es un segundo
consumidor de algo que ya estaba diseñado.

**Por qué hace falta, y no basta con los eventos.** Los eventos dicen *cuándo
cambió algo*; la instantánea dice *cómo están las cosas ahora*. Un reflejo que
reacciona a `touch.pet` puede querer saber si está oscuro — y eso no es un
evento, es estado ambiental. Sin API de lectura, cada pack acaba reconstruyendo
ese estado a mano guardando cada evento en variables globales… **y esa
reconstrucción está mal hasta que llega el primer evento.** Un buddy que
arranca en una habitación a oscuras no sabría que está oscuro hasta que la luz
*cambie*.

**Pide varios de una vez cuando los necesites juntos.** Un handler que quiere
luz, temperatura y presencia hace *una* llamada nativa y recibe un map con
exactamente esos tres, en lugar de tres llamadas:

```berry
var s = buddy.sense(["light", "temp", "presence"])
if s["light"] != nil && s["light"] < 15 ... end
```

Esto es a propósito **lo contrario** de meter la instantánea entera en el
evento. La lista se paga solo cuando alguien la pide y solo por lo que pide;
la instantánea en el evento se pagaría en cada evento, la use alguien o no —
y el coste crecería con cada sensor que se añada, que es justo la dirección en
la que va el proyecto. La mayoría de los handlers (`touch.pet`, `nfc.tag` del
pack `zero`) no miran ningún sensor.

**`nil` cuando no hay dato, y esto importa.** Si el BH1750 no está montado, o
murió, o la lectura es vieja, `buddy.sense("light")` devuelve `nil` — no el
último valor para siempre, y no cero. Un cero silencioso es «oscuridad total»
para un reflejo, que es exactamente la clase de fallo que nadie depura.

**Unidades reales, las mismas que los umbrales**: lux, °C, g. Así un pack puede
razonar a la vez sobre `dark_below_lux: 15` y sobre lo que devuelve
`buddy.sense("light")`, sin conversiones mentales.

```berry
buddy.on("touch.pet", def (ev)
  var lux = buddy.sense("light")
  if lux != nil && lux < 15
    buddy.led.mood("off")         # caricia de noche: no le des un flashazo
  else
    buddy.face.emotion("happy")
  end
end)
```

### Una nota de privacidad que conviene no descubrir tarde

El radar mmWave sabe **cuándo estás en tu escritorio**, y a través de la
carcasa. Un pack puede leer eso y metérselo a `buddy.ask`, que lo manda a la
nube. No es un canal nuevo —un pack ya puede mandar texto arbitrario al
cerebro— pero el dato es bastante más personal que la mayoría. Va en la misma
conversación que la seguridad de instalar packs (#21, #24), no en esta.

## Superficie de la API de scripts (Berry)

```berry
import buddy

buddy.on(event, handler)          # subscribe: "nfc.tag", "touch.pet", "timer.*", "brain.reply", …
buddy.emit(event, payload)        # custom events between reflexes

buddy.asset.json(rel)             # small JSON → Berry map (≤4 KB), nil if missing
buddy.asset.exists(rel)           # cheap existence probe

buddy.face.play(anim)             # by name from faces/
buddy.screen.show(rel)            # image from pack, C++ decodes/blits
buddy.sound.play(rel, done_cb)    # streamed by C++; callback on sound.done
buddy.led.mood(name)
buddy.say(text)                   # estas palabras exactas salen (pantalla; + TTS cuando llegue la voz)
buddy.ask(prompt)                 # le preguntas al Brain; ÉL decide qué dice el buddy
buddy.hint(text)                  # solo pantalla, nunca se pronuncia

buddy.sense(name)                 # lectura cacheada de un sensor; nil si falta o está vieja
buddy.sense([n1, n2])             # varios de golpe -> map, una sola llamada
buddy.lang                        # active language code, from pack + device config
buddy.pack.meta                   # own manifest as a map
```

Ejemplo — el flujo central completo del explicador de juegos de mesa:

```berry
buddy.on("nfc.tag", def (ev)
  if !ev.payload || !ev.payload.startswith("game:") return end
  var game = ev.payload[5..]
  var meta = buddy.asset.json(f"media/games/{game}/meta.json")
  if meta == nil
    buddy.face.play("confused")
    buddy.say("Hmm, I don't know that one yet!")
    return
  end
  buddy.face.play("storyteller")
  buddy.screen.show(f"media/games/{game}/cover.png")
  buddy.sound.play(f"media/games/{game}/narration_{buddy.lang}.mp3", def ()
    buddy.face.play("idle_happy")
    buddy.hint("Ask me anything about " + meta["title"])
  end)
end)
```



## Responsabilidades del framework (C++)

- **Resolvedor de assets**: búsqueda superpuesta (flash → SD), acotada al
pack, sin escapes de ruta (`..` rechazado).
- **Audio**: `sound.play` publica una acción al bus; la tarea de audio
(core 1) abre el archivo, decodifica (WAV/MP3 vía helix) y streamea a I2S
en trozos de ~8 KB bajo el mutex del bus SPI compartido (los flushes de
pantalla se intercalan). Emite `sound.done` / `sound.error`. Alimenta el
RMS de cada trozo al módulo de lip-sync — la narración cacheada y el TTS
en vivo animan la boca exactamente igual.
- **Imágenes**: `screen.show` decodifica PNG al canvas; las imágenes grandes
se encajan con bandas negras en el canvas lógico de 240×240.
- **Higiene de SD**: montada solo-lectura en operación normal; se remonta rw
solo durante subidas desde la web. La ausencia de tarjeta es un evento
(`storage.sd.gone`), no un crash.



## Mapeo NFC: dos capas

1. **Payloads semánticos** (preferido): el texto NDEF de la pegatina lleva el
  significado (`game:catan`, `pack:pirate`, `mode:focus`). Se escribe una
   vez con cualquier móvil; funciona en todo buddy cuyo pack entienda el
   prefijo.
2. **Registro del dispositivo** (para tags vírgenes/solo-UID): la web mapea
  UID → payload sintético. Los reflejos solo ven payloads, siempre.

Reglas de seguridad (reiteradas del doc de hardware): los tags disparan solo
acciones de lista blanca — nunca autenticación, nunca texto crudo al Brain.

## Validación y autoría

- La web (y un CLI para CI) valida al subir: esquema del manifiesto,
`main.be` parsea, cada literal de `asset.*`/`sound.play`/`screen.show`
resuelve a un archivo, el tamaño total del nivel flash dentro de
presupuesto.
- **Flujo de autoría de narraciones** (packs de contenido): escribir/editar
el guion → una llamada TTS por archivo al crear (voz buena, revisada por
humanos) → soltar en `media/`. La web puede poner cara a esto ("generar
narración de este texto") para que el dueño del café nunca toque una
terminal.



### Dos reglas que el validador tiene que aplicar

1. **Toda expresión necesita su `lines/<expresión>.txt`**, use modelo o no. Un
   pack sin banco no falla al validarlo ni al arrancar: falla el día que falla
   el modelo.
2. **Los nombres de archivo, normalizados a NFC**, y rechazar lo que no lo
   esté. Es la única forma de que un pack escrito en un Mac funcione en el
   chip — ver «Nombres de archivo con acentos».

## Preguntas abiertas para la spec

- Formato de sprites: PNG (coste de decodificación) vs RGB565 pre-convertido
(coste de tooling).
- Esquema de definición de animaciones (`*.anim.json`) — paramétrico (estilo
m5stack-avatar) vs sprite-sheet, o ambos.
- Semántica del cambio de pack: qué estado sobrevive (¿volumen? ¿idioma?) y
qué se resetea.
- Multi-pack: una personalidad activa + "packs de contenido" pasivos, ¿o
estrictamente un pack a la vez? (El buddy del café sugiere que la división
personalidad + contenido puede valer la pena.)

