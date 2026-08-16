# Spike: micro INMP441 y bucle push-to-talk

Mantén el botón, habla, suéltalo, óyete. Es el listón de voz de la sesión 1, y
demuestra el mecanismo del que depende todo el diseño PTT: **el altavoz está
silenciado por hardware mientras el micro graba**. No «con el volumen bajo» —
la etapa de salida del amplificador apagada. Sin eso, el buddy se oye a sí
mismo y el bucle no vale nada.

Proyecto aislado a propósito: dentro del firmware real, una grabación muda
podría ser el bus, la prioridad de una tarea, DMA o el cableado. Aquí solo
puede ser el cableado o la config de I2S.

**Estado**: **funciona entero**. Micro, botón, grabación con el altavoz mudo y
reproducción: verificado en el S3 por Daniel. El bucle push-to-talk de la
sesión 1 está demostrado.

## Cableado

### Micro INMP441

| Pin | A | Nota |
|---|---|---|
| VDD | **3V3** | nunca 5 V |
| GND | GND | |
| SCK | GPIO **15** | reloj de bit — **el mismo que el ampli** |
| WS | GPIO **16** | selector de canal — **el mismo que el ampli** |
| SD | GPIO **17** | datos: habla el micro |
| L/R | **GND** | canal izquierdo — **no lo dejes al aire** |

### Botón push-to-talk

| | A |
|---|---|
| una pata | GPIO **5** |
| la otra | **GND** |

Nada más: sin resistencia. El firmware activa el pull-up interno, así que en
reposo el pin lee alto y pulsado lee bajo. Da igual la orientación del pulsador.

GPIO 5 porque es de los pocos que quedan: la pantalla ocupa 7–12, el tacto el
4, el anillo el 21, el RC522 38–42, el I2S 15–18, el mute del ampli el 2 y el
CS de la SD el 14. Libres quedan **1, 5, 6, 13, 47 y 48** — y de esos, 47/48
mueven el LED RGB de placa en algunos devkits S3, así que mejor no.

> En el buddy de verdad **el gesto PTT es la almohadilla táctil** (GPIO 4), como
> dice el plan de talleres: mantener el pad → grabar. Aquí se usa un botón
> porque en un spike quieres una variable menos: el tacto capacitivo tiene
> deriva de umbral, y un pulsador no.

### Amplificador MAX98357A

El del [spike de salida](../i2s-audio/README.md): BCLK 15 · LRC 16 · DIN 18 ·
SD (mute) 2 · Vin 5V. **Ninguno de los dos cables del altavoz va a GND** — la
salida es en puente.

## Probarlo

```bash
cd spikes/i2s-mic
idf.py set-target esp32s3
idf.py build flash monitor
```

**Ese `cd` importa más de lo que parece.** Si lanzas el flash desde `firmware/`
—o desde el botón de la extensión de ESP-IDF, que apunta al proyecto que tenga
configurado— flasheas el buddy y ves los ojos, no el spike. Pasó.

**Y si ya tenías un `sdkconfig` aquí de antes**, bórralo. ESP-IDF solo lee
`sdkconfig.defaults` cuando **no** existe `sdkconfig`, así que un
`sdkconfig` viejo se queda sin la PSRAM que el buffer de grabación necesita, y
el spike arranca solo para decir `no memory — is PSRAM enabled?`:

```bash
rm -f sdkconfig && rm -rf build && idf.py set-target esp32s3
```

En reposo sale un medidor de nivel. Mantén GPIO 5, habla, suelta: graba (con
el altavoz mudo), te dice los números de lo grabado y lo reproduce.

## Los números

Suelo de ruido medido en el S3, habitación normal en silencio, micro al aire
sin carcasa:

| | pico | RMS |
|---|---|---|
| ambiente, sin hablar | **−57 dBFS** (−66 … −53 según el momento) | ≈ **−68 dBFS** |

Es decir, **unos 57 dB de margen** hasta recortar. Voz normal a 30 cm cae
20–30 dB por encima de ese suelo. Cuando grabes con el botón, el spike imprime
pico y RMS en dBFS de cada toma: ahí salen los números de voz.

Dos cosas que se confunden y no son la misma:

- **Para el STT no hace falta ganancia**, ni digital ni de otro tipo. Lo que
  importa es la relación señal/ruido, y multiplicar las muestras no la mejora:
  sube la voz y el suelo por igual. Ya hay margen de sobra.
- **Para oírlo por el altavoz sí**, y es lo que hace la normalización de la
  reproducción (ver abajo). No cambia la SNR ni un dB; solo deja de pedirle al
  amplificador que trabaje al 3 % de su recorrido.

Qué mirar en esa línea:

- **pico entre −30 y −12 dBFS** — perfecto para STT.
- **pico por debajo de −45** — el spike te avisa: acércate o hará falta ganancia.
- **pico pegado a 0 y `clipped` > 0** — recorte; sonará roto.

## Subir el volumen

En orden de cuánto dan, no de lo obvios que parecen:

1. **Normalizar la grabación** — ya lo hace el spike. Una voz grabada a
   −30 dBFS reproducida tal cual mueve el ampli al 3 % de lo que puede: el
   altavoz suena flojo porque la **grabación** es floja, no porque el ampli lo
   sea. Ajustar el pico a −3 dBFS vale unos **25 dB**. La línea del log dice
   cuánta ganancia se aplicó, con tope de +24 dB (más allá se amplifica el
   ruido de sala, que está a −57 dBFS).
2. **Alimentar el ampli a 5 V, no a 3V3.** La potencia de salida va con la
   tensión de alimentación; a 3V3 el mismo módulo suena bastante más flojo.
3. **Pin `GAIN` del MAX98357A** — al aire son 9 dB. A GND directo, 12 dB; a
   GND por 100 kΩ, 15 dB. Es decir, **+6 dB como mucho**, mucho menos de lo
   que parece prometer.
4. **Filtrar los graves.** El spike de salida midió este altavoz: respecto a
   la banda de 1–1,4 kHz, da **−12,6 dB a 700–990 Hz, −20,4 dB a 500–700 y
   −30 dB a 350–490**. Es un pasa-banda estrecho alrededor de 1–2 kHz. Los
   graves no salen como sonido pero sí mueven el cono, y un cono al final de
   su recorrido distorsiona todo lo demás. Un pasa-altos a ~400 Hz sube el
   volumen **percibido** sin tocar la ganancia.
5. **Ponerle caja.** Un altavoz sin bafle cancela sus propios graves por el
   borde. Montarlo en la carcasa es gratis y ayuda — territorio de CAD.

## Una decisión de diseño que conviene conocer

El puerto I2S se **destruye y se reconstruye en cada cambio de sentido**, en
vez de dejar un par de canales full-duplex montado.

Cuesta unos milisegundos que no se perciben (pasa justo al soltar el botón), y
compra dos cosas. Cada sentido usa exactamente la configuración ya demostrada
por su propio spike —entrada mono/LEFT en slots de 32 bits, salida estéreo de
32— así que un fallo aquí no puede ser «el framing compartido estaba mal por
poco». Y hace que el half-duplex sea verdad en el hardware y no por convenio:
el periférico es físicamente incapaz de hacer las dos cosas a la vez.

## Qué mirar cuando no funciona

El spike imprime las primeras palabras en crudo antes de interpretarlas,
porque casi todos los fallos se distinguen ahí:

| lo que ves | qué significa |
|---|---|
| `00000000 00000000 …` | no llegan datos: revisa `SD` en GPIO 17 y `L/R` a GND |
| `ffffffff ffffffff …` | línea de datos clavada arriba |
| valores que cambian pero `peak` diminuto | slot equivocado (`L/R` al aire o a 3V3) |
| graba sin tocar el botón | el pulsador está al revés o el pin a masa fijo |
| se oye a sí mismo al grabar | el mute no llega: revisa `SD` del ampli en GPIO 2 |

## Lo siguiente

Con esto verde, lo que falta para el bucle de voz de verdad no es audio: son
los eventos [`voice.listening` y `voice.thinking`](https://github.com/HackLab-Oriente/desktop-buddy/issues/9),
que aún no existen, y que son lo que deja al pack tapar el viaje de 1,5–3 s
con un gesto.

Plan de pines completo, con la tarjeta SD y los porqués, en
[`../../hardware/buddy-s3-audio.md`](../../hardware/buddy-s3-audio.md).
