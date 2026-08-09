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
  "expressions": { "emotion_map": "faces/emotions.json" }
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
buddy.say(text)                   # → Brain/TTS pipeline (live)
buddy.hint(text)                  # on-screen text, no TTS

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

