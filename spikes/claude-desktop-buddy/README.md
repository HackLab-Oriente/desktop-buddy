# Spike: qué nos llevamos de `anthropics/claude-desktop-buddy`

Un spike de **lectura**, no de soldadura: Anthropic publicó un buddy de
escritorio sobre ESP32 y, más importante, **el protocolo por el que Claude
Desktop habla con hardware casero**. La pregunta es qué de eso entra en
nuestro framework y a qué coste.

Leído en el commit `a280c64` (2026-04-16). Licencia MIT — se puede copiar
código citando la procedencia.

## Veredicto en 30 segundos

| | Qué | Por qué |
|---|---|---|
| ✅ **Nos llevamos** | **El puente BLE como Sense**: eventos `claude.*` en el bus, decisiones de permiso hacia fuera | Es la única pieza que nos da una capacidad que hoy no tenemos, y encaja en el bus sin tocar el núcleo |
| ✅ | El **chequeo de espacio antes de borrar** al instalar un pack | Su instalador calcula libre + reclamable *antes* de tocar el sistema de archivos; el nuestro todavía no |
| ✅ | La **disciplina de NVS**: guardar en hechos, nunca en un temporizador; y el *latch* de primera lectura para contadores acumulados | Dos trampas de desgaste y de contabilidad que ellos ya pisaron |
| 🔧 **Adaptamos** | **Estado base derivado + one-shot con fecha de caducidad** | Nuestras expresiones se quedan pegadas hasta que otro reflejo las cambie. Ellos resuelven "susto de 2 s" en cuatro líneas |
| 🔧 | La **tabla de compases** (`SEQ[]`): secuencia de poses a 5 fps, escrita a mano | Es el primo determinista de nuestra cadena de Markov, y ya lo tienen como *datos* en su manifiesto `mode:"text"` |
| 🔧 | Ideas de personalidad: nivel por tokens, humor por *velocidad de respuesta*, energía que baja, siesta boca abajo | Material para el equipo de personalidad. La nota arquitectónica: **un contador no es un evento** |
| ❌ **No nos llevamos** | Arduino + PlatformIO + `M5StickCPlus`, las 18 especies en C++, `stats.h` con estado `static` de un solo TU, `drawPixel` por píxel | Cada una choca con una decisión que ya tomamos y medimos |

**Lo que este spike NO responde:** si BLE cabe junto a WiFi + TLS en el S3.
Eso es la Puerta 1 y necesita hardware — ver abajo.

## La pregunta

Nuestro buddy habla con "un cerebro" por WiFi. El de Anthropic hace algo
distinto y más barato: se cuelga de la app de escritorio por BLE y **vive de
lo que tú estás haciendo con Claude** — duerme cuando no pasa nada, se pone
nervioso cuando hay un permiso esperando, y te deja aprobar o denegar desde
el propio bicho.

¿Podemos incorporar eso sin romper "primero el dispositivo, el hub es
opcional"? **Sí, y encaja mejor de lo esperado**: la app de escritorio es
exactamente un hub opcional. Cuando no está, los reflejos siguen corriendo y
el buddy no se entera. Nunca un ladrillo.

## Puerta 0 — ¿el protocolo entra en nuestro bus, con entrada hostil? ✅ PASADA

La regla que nos pusimos: si el decodificador no se puede hacer seguro contra
entrada hostil en el host en una tarde, se para.

| | |
|---|---|
| Decodificador | [`proto/claude_bridge.h`](proto/claude_bridge.h), 1 cabecera, sin dependencias de ESP |
| Pruebas | [`proto/test_claude_bridge.cpp`](proto/test_claude_bridge.cpp) |
| Resultado | **158 comprobaciones, limpio bajo ASan + UBSan**, sin warnings con `-Wall -Wextra` |
| Eventos propuestos | 8: `claude.hello · busy · idle · prompt · hint · resolved · turn · gone` |
| Salida hacia el escritorio | 1 comando: `permission` (`once` / `deny`) |

```bash
cd spikes/claude-desktop-buddy/proto
c++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined \
    -fno-sanitize-recover=all -I/usr/include/cjson \
    test_claude_bridge.cpp -lcjson -o test_claude_bridge && ./test_claude_bridge
```

Por qué tanta paranoia para "leer un JSON": **el emisor es un peer de radio**.
Mientras no hagamos *bonding*, "la app de Claude" es cualquiera que esté a
unos metros con un dongle de 20 €. Así que el puente se trata igual que un
pack o una etiqueta NFC: tipo equivocado se ignora, número absurdo se acota,
cadena se corta **en frontera UTF-8**, y la profundidad del documento se
cuenta *antes* de dárselo a cJSON — la misma bomba de anidamiento que el
parser de packs ya conocía, pero aquí le cuesta al atacante un solo paquete.

El caso que de verdad importa está en `test_line_framing`: **una línea
demasiado larga se descarta entera**, no se trunca. Truncar deja un documento
más corto que quizá siga siendo válido — el emisor elige qué sobrevive al
corte — y además arranca la trama siguiente a mitad, con lo que el flujo no
se resincroniza nunca más.

## Puerta 1 — BLE junto a WiFi y TLS en el S3 ⏸️ SIN MEDIR

Todo lo anterior es teoría hasta que esto tenga un número, y hace falta una
placa. Lo que hay que medir, en este orden:

1. **RAM.** El controlador BLE + host + NUS, *conviviendo* con WiFi y una
   conexión TLS (≈ 50 KB por conexión, según `tech.md`). Medir con **NimBLE**,
   no con el Bluedroid que arrastra el ejemplo de Arduino — se espera bastante
   menos RAM interna, pero cuánto exactamente es precisamente el número que
   falta, y es RAM interna, que es la que no nos sobra.
2. **Coexistencia de radio.** El S3 tiene una sola antena: WiFi y BLE se
   turnan. La medición honesta es **los fps de la cara** con el puente
   conectado y una petición al Brain en vuelo, contra los 33 fps que ya
   medimos.
3. **Flash.** Coste de NimBLE + el servicio, contra el margen de la
   partición de app.

**Regla de parada:** si con BLE conectado no queda RAM para una conexión TLS,
el puente y el Brain cloud son excluyentes y esto pasa de ser una función a
ser un *modo* — decisión de producto, no de firmware, y hay que llevarla al
grupo antes de escribir una línea más.

## Qué es su buddy, en una pantalla

Un M5StickC Plus (pantalla 135×240) con Arduino + PlatformIO. 2.861 líneas de
núcleo y 3.924 más de "especies" ASCII. Siete estados —
`sleep · idle · busy · attention · celebrate · dizzy · heart` — y dos formas
de dibujarlos:

- **ASCII en C++**: 18 especies, cada una un `.cpp` con siete funciones de
  animación.
- **Packs GIF**: una carpeta con `manifest.json` + GIFs de 96 px que se
  arrastra a una ventana del escritorio y viaja por BLE. El ejemplo `bufo`
  pesa 588 KB en 15 GIFs.

Lo interesante no es el bicho: es el cable. La app de escritorio manda un
*snapshot* cada vez que algo cambia (y un keepalive cada 10 s) con sesiones
totales / corriendo / esperando, las últimas líneas de transcripción,
contadores de tokens y — cuando toca — un **prompt de permiso con un `id`**.
El dispositivo contesta `once` o `deny` con ese `id`. Todo son objetos JSON
separados por `\n` sobre Nordic UART Service.

## 1. El puente, como Sense

La lectura arquitectónica que ordena todo lo demás: **el puente no es un
Brain**. No contesta preguntas. Empuja hechos y acepta un sí/no. Así que en
nuestras cuatro primitivas es un **Sense** (hechos hacia dentro) más un canal
de mando muy estrecho (una decisión hacia fuera). Nada de esto toca
`brain.ask`.

Su firmware consume un *snapshot* de nivel — el mismo estado repetido cada
10 s. Un reflejo no quiere eso; quiere flancos. El `Watcher` del spike traduce
uno en otro:

| evento propuesto | payload | cuándo |
|---|---|---|
| `claude.hello` | — | primer tráfico del puente |
| `claude.busy` | nº de sesiones corriendo | pasó de 0 a >0 |
| `claude.idle` | — | volvió a 0 |
| `claude.prompt` | nombre de la herramienta (`Bash`) | hay un permiso esperando |
| `claude.hint` | la línea de comando propuesta | contenido, y va **siempre después** de `claude.prompt` |
| `claude.resolved` | — | el permiso se contestó (aquí o en el escritorio) |
| `claude.turn` | texto del turno del modelo, recortado | terminó un turno |
| `claude.gone` | — | 30 s sin noticias |

Los dos últimos merecen comentario.

`claude.prompt` + `claude.hint` es **el mismo par que `nfc.tag` + `nfc.text`**,
y por la misma razón: una cosa es *qué* es (nombre corto que elige el
escritorio) y otra el *contenido* que un reflejo tiene que tratar como datos.
Reusamos el patrón, la garantía de orden y la nota de "esto lo escribió otro"
que el pack `zero` ya modela en su manejador de NFC.

`claude.turn` es la joya escondida: el protocolo documenta un evento con el
texto real del modelo y **su propio firmware no lo lee nunca**. Es la única
fuente de palabras que un buddy podría decir sin gastar un token propio.

Lo que deliberadamente *no* proponemos como evento son los contadores
(`tokens`, `tokens_today`). Un contador no es un hecho que pasó; publicarlo en
el bus sería ruido cada 10 s. Su sitio es el `sensor_snapshot` del contrato de
Brain, consultable, no empujado.

### La decisión de seguridad, que es nuestra y no suya

**Un pack no puede aprobar una llamada a herramienta.** Nuestros packs se
comparten como cartuchos; el suyo no existe como concepto. Un reflejo Berry
con acceso a `claude.allow` es un `rm -rf` a un `git clone` de distancia, y el
pack se recarga en caliente desde una web.

La regla, si esto se construye: **el firmware es dueño del salto de gesto
físico a decisión**, y el pack solo decide cómo se ve la cara mientras tanto.
Concretamente, `claude.*` entra en la lista de espacios que un reflejo no
puede publicar, junto a `system.*`, `config.*`, `boot.*` y `pack.*`.

Y el *bonding* deja de ser opcional: por ese enlace viajan líneas de
transcripción y comandos de la máquina de alguien. Su propio REFERENCE lo dice
sin rodeos ("sniffable por cualquiera con un dongle barato"), y su firmware sí
lo hace bien — características cifradas, `DisplayOnly`, passkey de 6 dígitos
en pantalla, y un `unpair` que borra los LTK.

## 2. Instalación de packs: el chequeo antes del borrado

En `xfer.h`, antes de aceptar una carpeta, calculan **libre + reclamable** y
comparan contra el total anunciado, con 4 KB de holgura para los metadatos de
LittleFS. Solo si cabe borran el personaje anterior. El fallo deja el bicho
exactamente como estaba.

Es una línea de razonamiento que nuestra API de configuración todavía no tiene
escrita, y el modo de fallo que evita es el peor posible: **quedarte sin pack
y sin espacio para el nuevo**, que en nuestro caso es un buddy sin cara.

El transporte en sí (`char_begin` → `file` → `chunk` → `file_end` → `char_end`,
con ack por chunk) no nos sirve tal cual — nosotros subimos por HTTP y TCP ya
hace control de flujo. Pero el número derivado es útil para no repetirlo: con
300 bytes decodificados por chunk, un pack de 1,8 MB son **≥ 6.000 idas y
vueltas con ack**; al intervalo de conexión que anuncian (7,5–22,5 ms) eso son
minutos, no segundos. **BLE no es un canal de instalación de packs para
nosotros.** Para eso ya tenemos WiFi.

## 3. Disciplina de NVS

Dos cosas que su `stats.h` explica mejor de lo que las teníamos escritas:

- **Se guarda en hechos, nunca en un temporizador.** Los tokens se acumulan en
  RAM y solo tocan la NVS al cruzar un hito. El peor caso de un corte es
  perder progreso, no un sector.
- **El *latch* de primera lectura.** El escritorio manda un acumulado *desde
  que él arrancó*. Si al reiniciar el dispositivo se toma el primer paquete
  como delta, se re-acredita la sesión entera. La solución es de tres líneas:
  la primera lectura solo sincroniza, no suma. Un número que baja significa
  que el otro lado reinició, y también solo sincroniza.

Cualquier contador nuestro que venga de fuera (tokens, minutos de uso, lo que
sea) tiene exactamente esta forma.

## 4. Estado base derivado + one-shot con caducidad

Su bucle son cinco líneas que valen la pena:

```cpp
baseState = derive(tama);                                   // del dato
if ((int32_t)(now - oneShotUntil) >= 0) activeState = baseState;   // el one-shot expira solo
...
if (checkShake()) triggerOneShot(P_DIZZY, 2000);            // 2 s de mareo y vuelve
```

Nosotros no tenemos esto. Un reflejo hace `buddy.face.emotion("angry")` y el
buddy se queda enfadado hasta que otro reflejo se acuerde de arreglarlo — por
eso el pack `zero` tiene que devolver la cara a `neutral` a mano en
`nfc.gone`. Un segundo argumento opcional de duración
(`buddy.face.emotion("surprised", 2000)`) hace que las reacciones sean
reacciones y no cambios de estado.

Es una propuesta para el equipo de personalidad, no una decisión de firmware.

## 5. Animación: la tabla de compases

Cada estado de cada especie es un puñado de poses y **una secuencia de índices
escrita a mano**, muestreada a 5 fps:

```cpp
static const uint8_t SEQ[] = { 0,0,0,3,0,1,0,2,0,  7,8,7,8,7,  0,5,0,6,0, ... };
uint8_t beat = (t / 5) % sizeof(SEQ);
```

Ese array es el ritmo: tres reposos, un parpadeo, mirar a un lado, dos
coletazos. Cuesta un byte por compás y da mucha más sensación de vida que un
bucle de dos frames. Es el primo determinista de nuestro Markov, y las dos
cosas se combinan: la cadena elige *qué* estado, la tabla de compases da el
*ritmo* dentro de él.

Y ya lo tienen como datos: su manifiesto admite `"mode": "text"` con
`{frames: [...], delay: N}` por estado. Eso es, casi literalmente, lo que
nuestro `faces/*.anim.json` tiene que ser — con la ventaja de que el formato
ya existe y funciona en un dispositivo real.

Dos detalles de rendimiento que confirman, desde otro ángulo, lo que ya
medimos en el spike de LovyanGFX:

- **Redibujan por *tick*, no por frame.** El bucle corre a 60 fps, la
  animación a 5, y el redibujado se salta si el compás y el estado no han
  cambiado: ~12× de trabajo ahorrado. Es nuestra misma regla — "cachea lo
  caro, mueve lo barato" — aplicada con otra palanca.
- **Un estado de un solo GIF se congela en el último frame** en vez de
  reabrirse: abrir el fichero y decodificar la cabecera es una ráfaga
  bloqueante de milisegundos y les estaba matando de hambre al controlador de
  BT durante el estado `sleep`. Nota mental para cuando nuestra cara y la
  radio compartan tarea.

## 6. Ideas de personalidad (para el equipo de contenido)

Su bicho no vive de un LLM: **vive de tu forma de trabajar**.

- **Nivel** cada 50.000 tokens de salida.
- **Humor** derivado de la *mediana de tus últimas 8 velocidades de respuesta*
  a un permiso: contestas rápido, está contento; tardas dos minutos, está de
  morros. Con un correctivo si deniegas más de lo que apruebas.
- **Energía** que baja un escalón cada 2 horas y se recarga con una **siesta**:
  pones el bicho boca abajo y duerme.
- **Corazones** si apruebas en menos de 5 segundos.

Nada de esto necesita red ni modelo, y todo sale de sensores que ya tenemos o
tendremos. Es exactamente la clase de bucle expresivo que nuestro `product.md`
llama "el producto".

## 7. Lo que no nos llevamos, y por qué

| | Por qué no |
|---|---|
| Arduino + PlatformIO + `M5StickCPlus` | Somos ESP-IDF v6 por decisión medida. Su firmware *depende* de los drivers de esa placa; el propio README lo admite. Lo portable es el protocolo, no el proyecto |
| **18 especies compiladas en C++** | Es literalmente lo que nuestro `product.md` critica de nuestro `face_model.h`: "el sitio más dictatorial posible". Su repo enseña las dos opciones una al lado de otra — y la de datos (packs GIF) es la que ellos ponen en la portada. Nosotros ya movimos expresiones y moods al pack; esto lo confirma, no lo cuestiona |
| `stats.h` con estado `static` a nivel de fichero, "incluir desde exactamente un `.cpp`" | Nuestra regla es que todo lo que tiene lógica compile en el host y tenga pruebas. Ese patrón lo impide por construcción |
| `drawPixel` por píxel en el callback de scanline del GIF | Ya sabemos, del spike de LovyanGFX, que la forma correcta es empujar la línea entera |
| El ajuste "bluetooth" que no apaga el bluetooth | Guardan la preferencia y dejan la radio encendida, con un comentario honesto explicando por qué. Un ajuste que miente es peor que un ajuste que falta |

## 8. Trampas que ellos ya documentaron (y una que no cumplieron)

Esto es lo que este repo llama memoria institucional. Vale para nosotros
aunque no toquemos su código:

1. **Su REFERENCE dice "valida `file.path` antes de escribir"… y `xfer.h` no
   lo hace.** Mete el string del peer directo en
   `"/characters/%s/%s"` con `snprintf`. No hay comprobación de `..` en ningún
   sitio del fichero. La lección no es "qué mal": es que **el documento
   correcto y el código correcto son dos trabajos distintos**. Nuestro
   `safe_name()` está en el spike, con las diez pruebas que lo demuestran.
2. **El buffer de línea es de 1.024 bytes y el protocolo permite eventos de
   4 KB.** Todo turno entre 1 y 4 KB desaparece en silencio, partido en dos
   fragmentos que ninguno parsea. Nuestro cap es 4.352 y las líneas largas se
   descartan enteras y contadas.
3. **`hasClient()` miente** — su comentario, no el mío: rastrean bytes reales
   en vez de fiarse del estado del enlace. La misma trampa nos espera con el
   estado de WiFi.
4. **El evento `turn` está documentado y nadie lo lee.** Un canal en el
   protocolo no es un canal en el producto.

## Qué sigue

Este spike es una lectura y un decodificador probado; no cambia el firmware ni
propone que lo cambie todavía. Si el grupo dice que sí:

1. Llevar la Puerta 1 a una placa. Sin ese número no hay nada que decidir.
2. Si pasa: los ocho `claude.*` entran en la tabla de **eventos propuestos**
   de [`docs/event-registry.md`](../../docs/event-registry.md) — propuestos,
   no reales — y la regla de "un pack no aprueba nada" entra en la lista de
   espacios prohibidos para reflejos.
3. `claude_bridge.h` se promociona a `firmware/components/senses/` con su
   prueba en `firmware/host_test/` y su paso en CI, que es donde vive el resto
   de código que parsea entrada hostil. El `Event` local del spike es un
   `typedef` de distancia respecto a `buddy::Event`; se hizo así a propósito.
4. Lo demás — one-shot con caducidad, tabla de compases, la economía del bicho
   — son propuestas para el equipo de personalidad y no dependen de la
   Puerta 1.

Y una advertencia que conviene repetir en la sesión: **el puente solo existe
en modo desarrollador y Anthropic dice explícitamente que no es una función
soportada**. Es material de taller y de demo, no cimiento. Si un día lo
quitan, nos quedamos exactamente donde estamos hoy — que es la definición de
un buen cartucho.
