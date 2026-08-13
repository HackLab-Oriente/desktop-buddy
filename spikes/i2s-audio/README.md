# Spike: salida de audio I2S (MAX98357A)

Primer sonido del buddy. Proyecto aislado a propósito: si esto falla, solo
puede ser el cableado o la config de I2S. Metido en el firmware real, un fallo
sería ambiguo.

**Estado**: compila y corre en el S3 — `i2s up: bclk=15 ws=16 dout=18` sin
errores, y el bucle avanza. **Falta oírlo**: cuando esto se escribió el
amplificador aún no estaba cableado.

## Cableado

| Pin MAX98357A | A | Nota |
|---|---|---|
| Vin | **5V** | funciona a 3V3, pero suena bastante más flojo |
| GND | GND | compartido con el S3 |
| BCLK | GPIO **15** | |
| LRC | GPIO **16** | |
| DIN | GPIO **18** | |
| SD | GPIO **2** | el mute por firmware — la parte interesante |
| GAIN | al aire | 9 dB por defecto |
| + / − | altavoz | **ninguno de los dos va a GND** — ver abajo |

> ⚠️ La salida es **en puente (BTL)**: los dos terminales del altavoz están
> excitados, ninguno es masa. Poner a masa uno de los dos es cortocircuitar
> media etapa de salida. En un ampli normal el negativo es masa; aquí no.

El conector del altavoz **no hay que cortarlo**: un latiguillo JST-PH hembra
con cables sueltos vale, y así se pueden probar altavoces distintos.

## Qué hace

Cuatro pasos en bucle, cada uno anunciado por el log serie:

1. **Tono de 440 Hz** — ¿hay sonido, y es limpio?
2. **Ceros** — ¿se calla cuando le mandas silencio digital?
3. **Barrido 200→2000 Hz** — ¿está toda la banda o zumba?
4. **Mute por hardware** — tono, `SD` a nivel bajo 400 ms, tono.

El paso 4 es el motivo de este spike. Mandar ceros **no es silencio**: un
ampli clase D sigue funcionando y sigue siseando, y el buddy se oiría a sí
mismo. Bajar `SD` apaga la etapa de salida. Ese es el mecanismo half-duplex
del que depende todo el push-to-talk, así que se demuestra antes de construir
nada encima.

Durante el paso 4 el I2S **sigue relojando**: se comprueba que el mute es del
amplificador, no de dejar de enviar datos.

## Probarlo

```bash
cd spikes/i2s-audio
idf.py set-target esp32s3
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

El volumen está bajo a propósito (`AMPLITUDE 8000` de 32767). Súbelo cuando
sepas que suena bien; a ganancia máxima y 5 V, estos módulos son ruidosos.

## Qué hay que apuntar aquí después de oírlo

- [ ] ¿Se oye el tono, y limpio?
- [ ] En el paso 4, ¿el mute es **silencio de verdad** o solo más bajo?
- [ ] ¿Sisea con ceros? ¿Cuánto?
- [ ] ¿Sirve el volumen a 5 V, o hace falta tocar `GAIN`?
- [ ] ¿Los pines 15/16/18/2 se quedan, o chocan con algo al integrar?

Las conclusiones (y solo las conclusiones) se llevan a
[hardware/buddy-s3-audio.md](../../hardware/buddy-s3-audio.md) y al firmware.
