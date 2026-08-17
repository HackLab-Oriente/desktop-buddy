# API de configuración — propuesta de contrato

> **PROPUESTA. Nada de esto existe todavía en código.** Es el contrato que
> firmware y web necesitan acordar *antes* de escribir cada uno su mitad, para
> que las dos mitades se encuentren. Una decisión sigue abierta y está marcada
> como tal: la autenticación.
>
> Relacionado: [#5](https://github.com/HackLab-Oriente/desktop-buddy/issues/5)
> (UI de configuración), [#26](https://github.com/HackLab-Oriente/desktop-buddy/issues/26)
> (adaptadores de voz), [#21](https://github.com/HackLab-Oriente/desktop-buddy/issues/21)
> (selección de pack).

## Qué hay hoy

Tres hechos, porque explican por qué este documento existe:

1. **Las credenciales están compiladas.** `BUDDY_WIFI_SSID`, `BUDDY_WIFI_PASS`
   y `BUDDY_ANTHROPIC_API_KEY` viven en `Kconfig.projbuild`. Cambiar de red
   significa recompilar, que va contra las reglas de la casa. El propio
   Kconfig ya lo admite: *«PoC only — v1 stores this in NVS via web UI»*.
2. **La partición NVS existe y nadie la usa.** 24 KB en `0x9000`. El proyecto
   solo llama a `nvs_flash_init()` para el driver de WiFi; no hay namespace
   propio, ni una lectura, ni una escritura. Terreno limpio: no hay nada que
   migrar.
3. **El web UI no tiene autenticación.** Tres endpoints (`GET /`,
   `GET /reflex`, `POST /reflex`) y ninguno pide nada. Hoy eso significa
   «cualquiera en tu WiFi puede cambiar el comportamiento de tu buddy». Con
   config significa otra cosa. Ver [Lo que sigue abierto](#lo-que-sigue-abierto).

## El límite: qué es config y qué es pack

La regla, para no discutirla caso por caso:

> **Si es un secreto, o es sobre la red de *este* buddy, es config. Si es
> sobre *quién es* el buddy, es pack.**

| config (NVS) | pack (LittleFS / SD) |
|---|---|
| WiFi, claves de API, proveedores | prompt de sistema, expresiones |
| qué pack está activo | reflejos Berry, frases, sonidos |
| nombre del dispositivo | la gramática de las tarjetas NFC |

Este límite se rompe callado: el día que el prompt de sistema acabe en NVS
«porque es más fácil de editar», el pack deja de ser portable y regalar un
buddy configurado deja de funcionar.

## El esquema

Versionado desde el primer día: el campo `schema` no cuesta nada ahora y es
imposible de retrofitear después.

```json
{
  "schema": 1,
  "device": { "name": "buddy-a3f2" },
  "wifi":   { "ssid": "HackLab", "psk": "(solo escritura)" },
  "brain":  {
    "provider": "anthropic",
    "model": "claude-haiku-4-5",
    "api_key": "(solo escritura)"
  },
  "voice": {
    "stt": { "provider": "openai", "model": "whisper-1", "api_key": "(solo escritura)" },
    "tts": { "provider": "openai", "voice": "nova",      "api_key": "(solo escritura)" }
  },
  "pack": { "active": "zero" }
}
```

Tres decisiones dentro de ese JSON que no son obvias:

**STT y TTS tienen claves separadas aunque hoy sean la misma de OpenAI.**
Es justo el punto de [#26](https://github.com/HackLab-Oriente/desktop-buddy/issues/26):
los proveedores son intercambiables, y Groq para STT + OpenAI para TTS es una
combinación razonable. Un solo campo `api_key` compartido codifica la
suposición de que un proveedor hace las dos cosas.

**Los proveedores se validan contra una lista, no son texto libre.** Una errata
en `"opanai"` no debe descubrirse como un 401 raro tres capas más abajo; se
rechaza al escribir, con un mensaje que diga cuáles valen. Esto es lógica pura
y se prueba en el host, igual que `ndef.h` y `latin1.h`.

**El formato de la API y el de almacenamiento no son el mismo**, y la razón es
concreta: **las claves de NVS no pueden pasar de 15 caracteres.**
`voice.stt.api_key` son 17. Así que el JSON va anidado y el almacenamiento es
plano, con una tabla de traducción:

| ruta JSON | clave NVS | | ruta JSON | clave NVS |
|---|---|---|---|---|
| `schema` | `schema` | | `voice.stt.provider` | `stt.prov` |
| `device.name` | `dev.name` | | `voice.stt.model` | `stt.model` |
| `wifi.ssid` | `wifi.ssid` | | `voice.stt.api_key` | `stt.key` |
| `wifi.psk` | `wifi.psk` | | `voice.tts.provider` | `tts.prov` |
| `brain.provider` | `brain.prov` | | `voice.tts.voice` | `tts.voice` |
| `brain.model` | `brain.model` | | `voice.tts.api_key` | `tts.key` |
| `brain.api_key` | `brain.key` | | `pack.active` | `pack.active` |

Namespace único: `buddy`. Claves planas en vez de un blob JSON entero por dos
razones: NVS da atomicidad por clave (cambiar el modelo no reescribe la clave
de API), y un blob corrupto se lleva por delante hasta el WiFi.

## Los secretos: una regla, no una lista

> **Todo campo que se llame `api_key` o `psk` es de solo escritura.**

`GET /config` nunca los devuelve. Devuelve el booleano correspondiente:

```json
{ "brain": { "provider": "anthropic", "model": "claude-haiku-4-5", "api_key_set": true } }
```

Regla y no lista enumerada, porque así el proveedor de voz que alguien añada
en marzo lo hereda sin que nadie se acuerde de actualizar nada.

**Consecuencia directa en el bus** — ver la sección siguiente, porque es donde
esta regla se puede perder sin darse cuenta.

Y una advertencia que hay que escribir aunque no la arreglemos: **NVS no está
cifrada por defecto.** Quien tenga la placa en la mano y `esptool` puede sacar
la clave de la flash. ESP-IDF soporta cifrado de NVS sobre flash encryption,
pero complica bastante el flasheo. Para un taller la decisión razonable es no
hacerlo — y dejar constancia de que fue una decisión, no un olvido.

## Los eventos

```
config.changed   payload: nombres de sección, separados por coma → "wifi,brain"
config.setup     payload: el SSID del AP de aprovisionamiento    → "buddy-a3f2"
```

**El payload de `config.changed` son nombres de sección, nunca valores, y esto
no es estilo.** Los packs Berry se suscriben al bus. Si el evento llevara la
config, cualquier pack podría leerte la clave de API en tres líneas de Berry.
Con nombres de sección, el cerebro sabe que tiene que releer su clave y nadie
más se entera de cuál es.

Qué necesita reinicio y qué no, para que la UI pueda decirlo con honestidad:

| sección | efecto |
|---|---|
| `brain`, `voice` | en caliente: el consumidor relee y sigue |
| `pack.active` | en caliente: recarga la VM Berry (el camino ya existe para `POST /reflex`) |
| `wifi` | reconecta — disruptivo, pero sin reiniciar |
| `device.name` | al siguiente arranque (afecta al SSID del AP y a mDNS) |

**Estos eventos no están en [`event-registry.md`](event-registry.md) todavía, a
propósito.** El registro se queja explícitamente de los eventos «documentados
pero no implementados» porque le cuestan una tarde a alguien; entran ahí en el
PR que los implemente. Y `config.*` es espacio del equipo de Web, así que los
nombres finales los firma Juan Esteban.

## Precedencia: Kconfig sigue existiendo

**NVS gana si tiene valor; Kconfig es el valor de fábrica.**

Así los flasheos actuales siguen funcionando (hay `sdkconfig` con credenciales
reales por ahí), NVS pasa a ser una capa de override, y «reset de fábrica»
tiene una definición de una línea: borrar el namespace `buddy`.

Esa regla es además la que resuelve el taller **sin código nuevo**: la red de
la sala entra por `menuconfig` al flashear, los buddies arrancan conectados, y
el día que alguien se lleve el suyo a casa el portal escribe la red nueva en
NVS y gana. La red del taller es el valor de fábrica, no una atadura. Si se
hace, que sea en un `sdkconfig` local sin commitear: la contraseña de la sala
no tiene por qué vivir en un repositorio público.

## Las dos puertas y el núcleo único

La pregunta no es «¿cuántas puertas?» sino «¿cuántas implementaciones?». Una:

```
config_apply(json) → validar → escribir NVS → emitir config.changed
```

Con eso, cada transporte es fino:

| puerta | cuándo | tamaño |
|---|---|---|
| **Portal SoftAP** | primer arranque, o la red de casa ya no existe | la que hay que construir |
| **Web UI normal** | ya está en la red | el `webui` que ya existe |

Que el núcleo esté separado no se justifica por tener muchas puertas: se
justifica porque la validación, la escritura y el evento ocurren en un solo
sitio. Una consola serie de depuración, o lo que venga después, son veinte
líneas en vez de una segunda implementación con sus propios fallos.

### Portal SoftAP

El buddy levanta `buddy-a3f2`, el teléfono se une, el portal pide la red. Es
la única opción que funciona **sin nada más que un teléfono**, y por eso es la
que tiene que existir.

Hay una bifurcación real ahí: usar el componente `wifi_provisioning` de
Espressif (cifrado, probado, pero **necesita su app**) o montar un portal
cautivo propio sobre el `esp_http_server` que ya tenemos (cualquier navegador,
sin instalar nada, pero la HTML y el secuestro de DNS los escribimos
nosotros). Para un taller donde la gente se lleva el buddy a casa, **no
instalar una app vale mucho** — y si el AP va con WPA2, las credenciales no
viajan en claro por el aire aunque el portal sea HTTP.

El portal cautivo se abre solo si el DNS responde a todo con la IP del
dispositivo: iOS sondea `captive.apple.com` y saca la hoja de login, Android
sondea `connectivitycheck.gstatic.com` y saca la notificación. Es un camino
muy trillado, con dos avisos: la hoja de iOS es un WebView limitado —**el
portal tiene que ser HTML simple y pequeño**— y un teléfono con DNS privado
configurado puede no morder el sondeo.

### El QR en la cara

El payload es texto estándar, sin app:

```
WIFI:T:WPA;S:buddy-a3f2;P:8fk29dla;;
```

Son 36 bytes → **QR versión 3** (29×29 módulos), que a ECC-M admite 42. La
pantalla es redonda, así que el cuadrado útil no es 240×240 sino el inscrito:
**169 px**. A 4 px por módulo, con la zona tranquila de 4 módulos que pide la
norma: 37 × 4 = **148 px**, y las esquinas quedan a 105 px del centro contra
un radio de 120. Entra.

**El límite no son los píxeles, son los milímetros.** En un panel de 1,28″
(~32,5 mm) un módulo de 4 px mide 0,54 mm y el código entero unos **20 mm**:
se escanea, pero la ventana útil está sobre los **10–20 cm** y un teléfono con
mal enfoque cercano lo va a buscar un rato. La palanca es el número de
caracteres, y es más empinada de lo que parece — cada carácter puede subir la
versión, y subir la versión encoge *todos* los módulos. **SSID corto.**

Segundo uso, que en el día a día vale más que el primero: ya en la red de
casa, un QR de `http://192.168.40.96/` en la cara mata para siempre la
búsqueda de «cuál era la IP del buddy».

Espressif publica un componente `qrcode` en el registro (lo usan sus ejemplos
de aprovisionamiento) que expone la rejilla de módulos en vez de solo
imprimirla por consola, así que LovyanGFX puede dibujarla. Confirmar la API
exacta al integrar.

### Por qué no hay tarjeta NFC de configuración

Se consideró y se descartó. Queda escrito porque la idea es buena y va a
volver.

La propuesta era una tarjeta con la red del taller: un toque, quince buddies
configurados. Al acotarla a **solo la puesta en marcha** —no uso diario— se
quedó sin caso propio:

- **En la puesta en marcha la gente está delante de un portátil.** Las
  sesiones se montan en protoboard y cada uno flashea desde su clon; la red se
  pone una vez en `menuconfig` y ya. La tarjeta no acelera nada ahí.
- **Lejos del portátil ya es uso diario**, que es justo lo que la tarjeta no
  iba a cubrir. Y ese caso es el del portal SoftAP, una vez por persona: no
  hay problema de caudal que resolver.

Los costes, en cambio, eran permanentes:

- **La contraseña del WiFi en claro sobre una pegatina**, sin caducidad y sin
  forma de revocarla. Las tarjetas se quedan en las mesas, se van en mochilas
  y se reescriben luego como cartuchos.
- **El firmware tendría que reclamar un prefijo** (`buddy:config:`) de la
  gramática de las tarjetas, que es del equipo de personalidad. Ese prefijo
  queda reservado para siempre aunque la función se use una tarde.
- **NTAG215 obligatorio** (504 B de usuario) en vez de NTAG213 (144 B): una
  consecuencia de compra para toda la tanda.
- Una superficie de ataque más, y pasiva: nadie confirma nada, se acerca una
  tarjeta y el buddy obedece. Aceptarlas solo en modo setup habría estrechado
  esa ventana, pero la ventana **es** el taller — el rato exacto en que hay
  tarjetas sueltas por todas las mesas.

**Lo que sí queda**: NFC sigue haciendo lo que ya hace bien, cartuchos de
personalidad por `nfc.text`, con la gramática entera en manos del pack. El
firmware no le quita ningún espacio de nombres a nadie.

## Lo que sigue abierto

**La autenticación.** Hoy no hay ninguna, y añadir config sube la apuesta: se
pasa de «cualquiera en tu WiFi puede cambiar el comportamiento de mi buddy» a
«cualquiera en tu WiFi puede gastarse mi clave de API». Las opciones:

1. **Ninguna** (lo de hoy) — defendible en una red de casa, malo en el WiFi de
   un hacklab, una cafetería o un congreso.
2. **PIN en la pantalla** para cualquier escritura. La pantalla es un canal que
   solo puede leer quien está en la habitación: presencia física como factor.
3. **Sesión con token** — lo correcto de manual, y bastante más trabajo.

Y una observación que reduce el problema: **si el AP de aprovisionamiento va
con WPA2 y la contraseña se enseña como QR, el aprovisionamiento ya está
autenticado por presencia física**, en el enlace y no en la aplicación. La
pregunta del PIN se queda entonces reducida al web UI en funcionamiento
normal, que es una pregunta más pequeña y más separable.

Sin decidir. Va a discusión de equipo.
