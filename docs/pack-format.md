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
  "expressions": { "map": "faces/expressions.json" }
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



## Dos capas: expresión y presentación

**Decidido en la sesión 1 (2026-08-29):** hay dos capas, no tres. El
**registro** queda **pospuesto**, no descartado — ver más abajo.

| capa | quién la define | cuántas hay |
|---|---|---|
| **expresión** | quien escribe el pack | **abiertas** — inventa las que quiera |
| **presentación** | el pack, sobre primitivas del firmware | una por expresión |

Una **expresión** es un estado con nombre propio del pack: `huraño`, `festivo`,
`resacoso`. Trae consigo su banco de frases y su presentación — cara, color y
mood del anillo.

```json
{
  "huraño":  { "mood": "fuego" ,  "eye": { "width": 28, "openness": 55 } },
  "festivo": { "mood": "excited", "eye": { "width": 30, "lift": 14 } }
}
```

Esto es lo que deshace el vocabulario duplicado de
[#19](https://github.com/HackLab-Oriente/desktop-buddy/issues/19), y por una vía
más simple que la que se había propuesto: en vez de un vocabulario cerrado del
que todo deriva, **no queda ningún vocabulario cerrado**. Los moods los define
el pack a partir de primitivas de animación, y la presentación también. El
firmware deja de tener opinión sobre cómo se llaman las cosas.

### El registro: por qué se pospone

El registro —una *manera de hablar*: seco, cálido, urgente— tenía un solo
trabajo real: **ser el parámetro del modelo local.** Se le alimenta `seco: ` y
completa en ese tono.

El proyecto arranca con **banco de frases y Markov**, y ninguno de los dos lo
necesita: los dos van por expresión. Definir hoy un vocabulario cerrado sería
comprar un compromiso caro de cambiar para resolver un problema aplazado.

**La condición que lo reabre:** cuando se entrene el modelo, hará falta un eje
por el que organizar el corpus, y ahí el registro vuelve con toda su fuerza. El
juego propuesto y el método de rejilla siguen escritos más abajo, intactos,
para esa sesión. Lo que no hay es un vocabulario que el firmware o los packs
tengan que respetar hoy.

Lo único que el firmware exige de una tabla de expresiones es que **exista
`neutral`**. Ese es todo el contrato.

Un detalle de vocabulario, porque muerde: en este proyecto **«registro»
significa ya dos cosas** — esta capa lingüística y el *registro de eventos*
(`event-registry.md`). Son cosas distintas; el segundo sigue muy vivo.

## De dónde salen las frases

**Decidido: todas las fuentes son añadidos sobre el banco, y se eligen por
expresión.** ([#17](https://github.com/HackLab-Oriente/desktop-buddy/issues/17))

- El **banco** vive en `lines/<expresión>.txt`, una frase por línea. Siempre.
- **`use_markov: true`** recombina ese mismo banco. Activo desde la v1.
- **`use_model: true`** añade el modelo local. **Pospuesto junto con el
  registro**: la sección sigue especificada porque no cambia, pero no hay nada
  que implementar hasta que exista el modelo entrenado.

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
pack. `huraño` → `lines/huraño.txt`. Con #16 pospuesta y #19 resuelta, **el
nombre del banco no depende de ninguna decisión pendiente**: se escribe ya.

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

## Los registros — material para cuando se entrene el modelo

**Pospuesto, no descartado** (#16, sesión 1). Nada de esta sección aplica hoy:
ni el firmware ni los packs conocen los registros. Se conserva entera porque es
el punto de partida de la sesión de entrenamiento, y porque el método de
rejilla de más abajo es la parte que de verdad cuesta descubrir.

Siete, porque serían el vocabulario cerrado y deben caber en la cabeza.

| registro | la manera | `led.mood` sugerido |
|---|---|---|
| `cálido` | cercano, sin prisa | `calm` |
| `juguetón` | pica, se burla, no va en serio | `excited` |
| `curioso` | pregunta, se fija, no da nada por hecho | `thinking` |
| `urgente` | corto, reclama atención ya | `excited` |
| `seco` | mínimo, retiene, no colabora | `calm` |
| `soñoliento` | se apaga, se le va la frase | `off` |
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
> Tómate tu tiempo, que no me voy a ninguna parte.
> Te he echado de menos, aunque no lo diga.

**`juguetón`**
> ¿Otra vez tú? Qué pesadito.
> Hazlo otra vez, a ver si te sale.
> Yo no he visto nada. Yo no estaba.

**`curioso`**
> ¿Y eso qué es?
> Espera… ¿eso siempre ha estado ahí?
> Cuéntame más, que me interesa.

**`urgente`**
> Eh. EH.
> Ahora, en serio.
> No, no, no — mira esto.

**`seco`**
> Ya.
> Si tú lo dices.
> HMPH.

**`soñoliento`**
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

**Decidido en la sesión 1 (#19).** Hoy `led.mood` acepta cuatro valores fijos en C++
(`calm|excited|thinking|off`), que es una segunda lista cerrada compitiendo con
las ocho emociones — la mitad del problema de #19. Si los moods pasan a ser
datos del pack, esa lista deja de existir como vocabulario rival y se convierte
en un espacio de nombres abierto que cada pack llena.

```json
"moods": {
  "fuego":  { "anim": "pulse",   "colors": ["#ff3300", "#ff8800"], "period_ms": 1800 },
  "chispa": { "anim": "spin",    "colors": ["#ffcc00"], "period_ms": 600, "dir": "cw" },
  "duerme": { "anim": "pulse",   "colors": ["#101030"], "period_ms": 5000 }
}
```

Y la cadena completa queda, sin ningún vocabulario cerrado en medio:

**expresión** (nombre, del pack) → **mood** (nombre, del pack) → **animación** (primitiva del firmware + parámetros del pack)

### Primitivas cerradas, no un lenguaje

`anim` sale de una lista corta y fija: `solid`, `breathe`, `spin`, `pulse`,
`off`. La expresividad la ponen los parámetros —colores, periodo, sentido,
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
- Un mood que se nombre y no exista **degrada al built-in**, no apaga el anillo
  ni revienta. Mismo principio que la caída al banco: lo peor que puede pasar
  no es quedarse a oscuras sin explicación.

## Markov como tercera fuente de frases (propuesta)

El banco ya estaba decidido (#17), y con el modelo pospuesto **Markov es la
única fuente generativa de la v1**. Encaja como iba a encajar el modelo, no
como un sistema aparte: un booleano por expresión, que *añade* sobre el banco y
nunca lo sustituye.

```json
{
  "huraño":  { "use_markov": true, "markov": { "order": 1, "pool": "ariscas" } },
  "festivo": { "use_markov": true },
  "alerta":  { "use_markov": true, "markov": { "max_words": 8 } }
}
```

**No trae corpus propio.** Markov recombina `lines/<expresión>.txt`, el banco
que la expresión ya está obligada a tener. Sin banco no hay cadena, igual que
sin banco no hay nada.

### Mandos, con sus valores por defecto en `pack.json`

```json
"markov": { "order": 2, "mix": 0.5, "max_words": 18, "no_repeat_last": 12 },
"pools":  { "ariscas": ["huraño", "alerta"] }
```

| campo | por defecto | qué hace |
|---|---|---|
| `order` | `2` | palabras de contexto. **El mando de riesgo**: 2 conserva la concordancia casi siempre; 1 inventa mucho más y la rompe |
| `mix` | `0.5` | probabilidad de usar una frase generada en vez de una escrita. `0.0` = nunca generar |
| `max_words` | `20` | corta el fallo típico de Markov: la frase que se enrolla y no termina |
| `no_repeat_last` | `0` | no repetir las últimas N frases dichas. Mucha variedad percibida por muy poca memoria |
| `pool` | *(ninguno)* | nombre de un grupo declarado en `pools`. Sin él, recombina solo el banco de su propia expresión |
| `speak` | hereda de `voice.mode` | si la frase generada va también al TTS; se puede apagar por expresión |

Cualquiera de ellos puede sobrescribirse por expresión, como el `order: 1` del
ejemplo.

### Por qué `pool` y por qué `order` por expresión

Los dos salen de la misma medida. Con orden 2, en el corpus de prueba, las
expresiones de tono tierno o juguetón recombinaron bien —comparten trozos como
«yo te»—, pero las secas y las dramáticas produjeron **cero** frases nuevas:
sus frases son cortas y muy distintas entre sí, así que no hay ningún par de
palabras donde empalmar. Míralo en unas frases secas típicas —«Ya.», «Si tú lo
dices.», «HMPH.»— y se ve por qué: no comparten nada.

De ahí los dos mandos. `order: 1` deja generar a una expresión seca, aceptando
más frases raras. Y **`pool` junta los bancos de varias expresiones**, que es
la otra forma de tener material suficiente: más frases = más puntos de empalme.
Un `order` único global obligaría a elegir mal para alguien.

**Los grupos los declara el pack, no el proyecto** (decidido en la sesión 1).
En la propuesta anterior el pool era el registro, con la lista cerrada que eso
traía. Al posponerse el registro se conserva el mecanismo y se tira el
vocabulario: en `pools` el autor agrupa las expresiones que quiera y les pone
el nombre que quiera. Es lo mismo que ya se hizo con los moods — quien escribe
el pack nombra sus propias cosas.

Y es un mando que **importa más de lo que parece con pools pequeños**: la
variedad medida fue de 12,8 % de frases nuevas con 60 líneas de corpus y 44,7 %
con 920. Una expresión sola, con quince frases, vive en la peor parte de esa
curva.

### Lo que esto deliberadamente NO hace

- **No reintroduce sus frases en el banco.** Medido: el bucle se agota **en una
  sola vuelta**. La cadena solo recorre caminos que el banco ya tenía, así que
  devolver su salida no añade ni un estado ni una frase alcanzable — solo
  refuerza los caminos trillados y baja la variedad. Si alguien quiere más
  frases nuevas, se escriben a mano; es la única fuente de material nuevo.
- **No elige expresión.** Eso lo decide el reflejo o el cerebro, no la cadena.

### Reglas que el validador impone

- `order` fuera de 1–3 o `mix` fuera de 0–1: pack rechazado al cargar, con el
  error en el log y en la web UI.
- **Nunca mudo.** Si la generación sale vacía o pasa de `max_words`, se cae al
  banco — la misma caída que ya protege a `use_model`.
- `use_markov` sin `lines/<expresión>.txt` es un pack roto, y el validador ya
  exige ese fichero para toda expresión.

## Animaciones de entrada y salida (propuesta)

**Idea de la sesión 1.** Cada pack trae una animación de **salida** y una de
**entrada**, y el cambio de pack se cuenta con ellas. Nace como una cuestión de
presentación —tapar el tiempo que tarda la carga— pero resuelve además el único
problema técnico serio del cambio en caliente.

### El orden importa, y es lo que hace que funcione

```
salida (pack viejo)  →  logo del HackLab (firmware)  →  entrada (pack nuevo)
                              ↑
                        aquí ocurre pack_load()
```

La clave es **qué se está dibujando durante el intercambio**. Si la transición
se pinta con datos del firmware —el logo, compilado dentro— entonces mientras
dura, la tarea de render **no lee ni una tabla del pack**. Ya no hay que
detenerla ni hacer doble buffer: el peligro desaparece porque nadie está
mirando lo que se reemplaza.

De ahí el reparto:

1. **Salida** — la pone el pack viejo, que en ese momento sigue siendo válido
   por completo.
2. **Logo** — firmware puro. Es la ventana segura, y es donde ocurre el
   `pack_load()`. El logo del HackLab es el valor por defecto, no un relleno.
3. **Entrada** — la pone el pack nuevo, ya cargado.

El requisito de presentación y el de seguridad resultan ser el mismo requisito.

### Reglas

- **Salida del fallo.** Si el pack nuevo no carga, la entrada nunca llega: se
  vuelve a la cara del pack viejo y se dice que algo pasó. Sin esto el logo se
  queda en pantalla para siempre.
- **Tope de duración.** Un pack no puede secuestrar la pantalla con una salida
  de treinta segundos. El firmware corta.
- **Ambas son opcionales.** Sin ellas se ve solo el logo, que es exactamente el
  comportamiento correcto.
- Reutiliza la máquina del splash de arranque, que ya existe con su glitch.

### Formato: recomendación sin medir

**No está medido en nuestra placa, y conviene decirlo antes que la
recomendación.** Lo que se descarta con confianza:

- **Lottie**: vectorial, necesita un rasterizador en tiempo real. Dependencia
  cara para lo que da en un ESP32.
- **Canvas HTML**: en el dispositivo no hay navegador. Sirve como herramienta
  de autoría, no como formato.

Lo que se pelea de verdad:

| | autoría | coste en la placa |
|---|---|---|
| **Secuencia de frames** (RGB565 crudo) | exportar desde cualquier cosa | **115 KB por frame** a 240×240. 30 frames = 3,4 MB. Solo viable en una zona pequeña o con muy pocos frames |
| **GIF** | **cualquiera puede hacer uno** — el móvil, ffmpeg, herramientas de diseño | paleta + compresión; hay decodificadores pequeños y probados para ESP32 |

La recomendación es **GIF**, y el criterio no es cuál se dibuja mejor: es
**cuál puede crear alguien del lab que no programa**. Un solo archivo,
exportable desde cualquier herramienta, y la compresión por paleta va bien con
arte tipo logo.

Es una afirmación sin medir con la forma exacta de un spike: *¿cuánto cuesta
decodificar un GIF de 240×240 en el S3, y a cuántos fps?* Una pregunta, un
número, desechable.

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

buddy.lang                        # active language code, from pack + device config
buddy.pack.meta                   # own manifest as a map
buddy.pack_load(name)             # pide cambiar de pack; ocurre DESPUÉS del dispatch
```

**`buddy.pack_load` es un método y no un evento, a propósito** (sesión 1). Dos
razones, y la segunda es la que decide:

- Nadie puede suscribirse a una llamada de función. Si fuera un evento del bus,
  un reflejo podría escucharlo y volver a publicarlo: bucle de recarga.
- **La bandera colapsa duplicados gratis.** Si tres handlers del mismo evento
  piden cargar, la bandera se sobrescribe y se carga una vez. Una cola de
  eventos habría encolado tres cargas.

Encaja con la distinción que ya hace el registro de eventos: **el bus lleva
hechos, no órdenes.** Cargar un pack es una orden, así que es un método. Que el
pack cambió es un hecho, así que sale como evento `pack.changed` cuando el
nuevo ya está en pie — y ahí es donde los reflejos del pack nuevo pueden
reaccionar.

Tres reglas que hacen que esto no muerda:

1. **Se difiere hasta el final del dispatch completo, no del handler.** Si no,
   los demás handlers del mismo evento seguirían corriendo contra las tablas
   del pack nuevo. Los eventos en cola del pack viejo se descartan.
2. **Fallo = no pasa nada.** Si el pack no existe o está malformado, se queda el
   actual y sale un evento de error. Es «nunca un ladrillo»: un cartucho mal
   grabado no puede dejar el bicho tieso.
3. **Guarda contra el bucle.** El pack A carga el B, cuyo reflejo de arranque
   carga el A. Es un error que un autor bien intencionado escribe sin querer, y
   desde fuera se ve como un cuelgue. Un cambio por dispatch.

Por qué hace falta diferir, y no es estilo: `dispatch()` ejecuta el handler
**dentro** de la VM, así que recargar el pack desde el reflejo sería liberar el
intérprete que está corriendo. Y `set_emotions()` hace `table() = std::move(v)`
—reemplaza el vector entero— mientras la tarea de render lo lee cada frame. Por
eso en `main.cpp` el `pack_load()` va **antes** de `face_start()`.

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



## Mapeo NFC: toda la gramática vive en el pack

**Decidido en la sesión 1 (#24): el firmware no define ninguna gramática.** No
hay verbos reservados, ni prefijos con significado, ni lista de usos
permitidos. El firmware entrega tres hechos y se aparta:

| evento | qué trae |
|---|---|
| `nfc.tag` | el UID, siempre |
| `nfc.text` | el texto NDEF, tal cual, **solo si** la etiqueta lleva alguno |
| `nfc.gone` | la etiqueta salió del campo |

Qué significa `game:catan` o `pack:pirata` lo decide un reflejo del pack. Si la
gramática viviera en C++, cada verbo nuevo pediría reflashear — justo lo
contrario del compromiso «el comportamiento es datos».

Cuatro usos que el punto de partida contemplaba, y que ahora son simplemente
cosas que un pack *puede* hacer, no una lista cerrada: identidad por UID, una
frase que el buddy dice, cambiar de pack, y un estado sostenido mientras la
etiqueta esté encima.

Tres cosas que hay que saber al escribir esos reflejos:

- **`nfc.tag` llega antes que `nfc.text`** en la misma presentación. Si
  necesitas identidad *y* contenido, guarda el UID al recibir el primero.
- **Una etiqueta en blanco solo emite `nfc.tag`.** La ausencia de `nfc.text` es
  la señal; no hay caso especial de cadena vacía.
- **Dejarla puesta no repite eventos** — uno al llegar, `nfc.gone` al irse. Eso
  convierte «dejar la etiqueta encima» en un gesto sostenido, no en un
  interruptor. Y un gesto sostenido **no sobrevive a un cambio de pack**: el
  `nfc.gone` le llegaría al pack nuevo, que nunca vio la llegada.

Reglas de seguridad (reiteradas del doc de hardware): una etiqueta **nunca
autentica** y su texto **nunca va crudo al Brain**. Que la gramática sea del
pack no cambia esto — son límites del firmware, no del vocabulario.

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
qué se resetea. El *mecanismo* ya está decidido (`buddy.pack_load` diferido);
lo que queda abierto es qué se conserva.
- Formato de las animaciones de entrada/salida: GIF es la recomendación, sin
medir. Pendiente del spike de decodificación en el S3.
- Multi-pack: una personalidad activa + "packs de contenido" pasivos, ¿o
estrictamente un pack a la vez? (El buddy del café sugiere que la división
personalidad + contenido puede valer la pena.)

