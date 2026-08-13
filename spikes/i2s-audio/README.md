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

Cinco pasos en bucle, cada uno anunciado por el log serie:

1. **hola** — una tercera ascendente
2. **feliz** — un arpegio corto (do–mi–sol)
3. **curioso** — una nota que se dobla hacia arriba
4. **calma** — cae y se apaga
5. **Mute por hardware** — nota, `SD` a nivel bajo, nota.

Son *chirps*, no tonos de prueba: es lo que el buddy necesita de verdad (su
voz es chirps primero y habla después), así que esta secuencia vale como
primer boceto de ese vocabulario.

### Por qué la primera versión sonaba a alarma

Dos motivos, los dos arreglados — y valen para cualquier sonido que hagamos:

- **Sin envolvente.** Una nota que empieza y acaba a amplitud plena es un
  escalón, y un altavoz reproduce un escalón como un *clic*. Ahora cada nota
  entra en 8 ms y sale en 25 ms con un coseno alzado. Medido: antes la última
  muestra valía −1375 y caía a cero de golpe; ahora empieza y acaba en 0
  exacto, también en las notas cortas.
- **Barrido lineal.** El tono se percibe en escala logarítmica, así que una
  rampa lineal corre por los graves y se arrastra por los agudos. El glissando
  ahora es exponencial y suena a nota que se dobla, no a sirena.

El paso 5 es el motivo de este spike. Mandar ceros **no es silencio**: un
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

El volumen está bajo a propósito (`PEAK 7000` de 32767). Súbelo cuando
sepas que suena bien; a ganancia máxima y 5 V, estos módulos son ruidosos.

## Qué hay que apuntar aquí después de oírlo

- [ ] ¿Se oyen las notas limpias, sin clic al empezar ni al acabar?
- [ ] En el paso 5, ¿el mute es **silencio de verdad** o solo más bajo?
- [ ] ¿Sisea con ceros? ¿Cuánto?
- [ ] ¿Sirve el volumen a 5 V, o hace falta tocar `GAIN`?
- [ ] ¿Los pines 15/16/18/2 se quedan, o chocan con algo al integrar?

Las conclusiones (y solo las conclusiones) se llevan a
[hardware/buddy-s3-audio.md](../../hardware/buddy-s3-audio.md) y al firmware.
