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

Un **la de 440 Hz continuo**, y nada más. Es la prueba más dura que se le
puede hacer al audio: una senoidal pura no tiene transitorios donde esconderse,
así que un zumbido, un raspado o un chisporroteo se oyen enseguida — y
cualquiera de los tres apunta al hardware, no al código.

El detalle que lo hace exacto: a 16 kHz, 440 Hz son 36,3636… muestras por
ciclo, que no cae redondo. Pero **once ciclos son exactamente 400 muestras**
(440 × 400 = 11 × 16000). Así que un buffer de 400 tramas contiene un número
entero de periodos y se puede repetir para siempre: sin acumulador de fase, y
por tanto sin deriva a las horas, y sin discontinuidad en la costura del
bucle. Medido: el salto entre la última muestra y la primera es 1203, y el
salto máximo *dentro* del buffer es 1207 — la costura es indistinguible de
cualquier otro par de muestras.

Reconstruir la onda en cada buffer desde una fase en coma flotante dejaría un
error diminuto en cada costura, y un error diminuto y periódico es
exactamente lo que un oído reconoce como zumbido.

Arranca con una rampa de ~250 ms para que no dé un golpe, y a partir de ahí no
para. Cada 10 s escribe un latido en el log, para poder distinguir «el
altavoz no suena» de «la placa se colgó».

### Qué escuchar

| lo que oyes | qué es |
|---|---|
| un la limpio, tipo diapasón | correcto |
| zumbido o raspado | probablemente BCLK y LRC cambiados (15 ↔ 16) |
| flojo y delgado | `Vin` en 3V3 en vez de 5V |
| un zumbido *por debajo* de la nota | alimentación, no datos |

### Lo que ya no está

La prueba de mute por hardware (nota → `SD` a nivel bajo → nota) se quitó
porque interrumpía el tono. Sigue siendo el único resultado de este spike que
puede cambiar el diseño, así que hay que recuperarla antes de dar el audio por
bueno: está en el historial de `spike/i2s-audio`.

## Probarlo

```bash
cd spikes/i2s-audio
idf.py set-target esp32s3
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

El volumen está bajo a propósito (`PEAK 7000` de 32767). Súbelo cuando
sepas que suena bien; a ganancia máxima y 5 V, estos módulos son ruidosos.

## Qué hay que apuntar aquí después de oírlo

- [ ] ¿El tono es limpio, sin zumbido ni raspado?
- [ ] ¿Se oye algún clic periódico? (no debería: la costura está medida)
- [ ] Con el mute recuperado, ¿es **silencio de verdad** o solo más bajo?
- [ ] ¿Sisea con ceros? ¿Cuánto?
- [ ] ¿Sirve el volumen a 5 V, o hace falta tocar `GAIN`?
- [ ] ¿Los pines 15/16/18/2 se quedan, o chocan con algo al integrar?

Las conclusiones (y solo las conclusiones) se llevan a
[hardware/buddy-s3-audio.md](../../hardware/buddy-s3-audio.md) y al firmware.
