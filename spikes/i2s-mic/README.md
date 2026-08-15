# Spike: entrada de audio I2S (micro INMP441)

¿Oye algo el micrófono, en los pines que planeamos? Proyecto aislado a
propósito: dentro del firmware real, un micro mudo podría ser el bus, la
prioridad de una tarea, DMA o el cableado. Aquí solo puede ser el cableado o
la config de I2S.

**Este spike no toca el amplificador.** «Grabar y reproducir» es la meta de la
sesión 1, pero son dos subsistemas: si no oyes nada, no has aprendido cuál de
los dos falló. Primero se demuestra el micro. Los pines ya son compatibles con
el spike de salida (comparten BCLK y WS), así que juntarlos después es poco
trabajo.

**Estado**: sin probar en hardware — recién escrito.

## Cableado

| Pin INMP441 | A | Nota |
|---|---|---|
| VDD | **3V3** | nunca 5 V |
| GND | GND | compartido con el S3 |
| SCK | GPIO **15** | reloj de bit — **el mismo que el ampli** |
| WS | GPIO **16** | selector de canal — **el mismo que el ampli** |
| SD | GPIO **17** | datos: habla el micro, escucha el ESP |
| L/R | **GND** | canal izquierdo — **no lo dejes al aire** |

Dos cosas que parecen detalles y no lo son:

**`L/R` a GND, siempre.** Es la causa número uno de «grabo y solo hay
silencio». Sin él, el micro no sabe en qué mitad de la trama hablar y el ESP
lee el hueco vacío. El firmware escucha el slot **izquierdo**, que es lo que
`L/R` a masa selecciona.

**`SCK` y `WS` son los mismos pines que el amplificador.** No es un ahorro
casual: el diseño es half-duplex a propósito (el buddy nunca escucha mientras
habla, si no se oye a sí mismo), así que micro y altavoz nunca transmiten a la
vez y pueden compartir relojes. Son 4 pines en total en vez de 6.

A 16 kHz el BCLK va a 1,024 MHz, que es exactamente el 64× que pide el
datasheet del INMP441. Si cambias el `RATE`, comprueba que sigue cuadrando.

## Probarlo

```bash
cd spikes/i2s-mic
idf.py set-target esp32s3
idf.py build flash monitor
```

Habla al micro. El medidor debería moverse:

```
I (2020) i2s-mic: [########################................]  -32 dBFS  peak=  210000  rms=   41000
```

## Qué mirar cuando no funciona

El spike imprime las **primeras 8 palabras en crudo** antes de interpretarlas,
porque casi todos los fallos se distinguen ahí:

| lo que ves | qué significa |
|---|---|
| `00000000 00000000 …` | no llegan datos: revisa `SD` en GPIO 17, y `L/R` a GND |
| `ffffffff ffffffff …` | la línea de datos está clavada arriba — mira si `SD` toca 3V3 |
| valores que cambian pero `peak` diminuto | suele ser el slot equivocado (`L/R` al aire o a 3V3) |
| `peak` clavado en el máximo | recorte: el desplazamiento de 24 en 32 bits no cuadra |
| ruido de fondo que sube al hablar | **funciona** |

El medidor sale una vez por segundo, no una por lectura: 31 líneas por segundo
no hay quien las lea.

## Lo siguiente

Cuando esto pase, el bucle half-duplex de la sesión 1 —grabar 3 s con el
micro, silenciar el ampli mientras se graba, y reproducirlo— es unir este
spike con [`../i2s-audio/`](../i2s-audio/README.md), que ya tiene la parte de
salida y el mute por firmware (`SD` del MAX98357A en GPIO 2). Comparten
relojes, así que se configuran como un único puerto I2S full-duplex y se usa
uno cada vez.

El plan de pines completo, con la tarjeta SD y los porqués, está en
[`../../hardware/buddy-s3-audio.md`](../../hardware/buddy-s3-audio.md).
