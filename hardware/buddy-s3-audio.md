# Audio (INMP441 + MAX98357A) y tarjeta SD — ESP32-S3

> **PROPUESTA, sin verificar.** No hay firmware todavía: ni driver I2S, ni
> driver de SD, ni entradas en `Kconfig.projbuild`. Estos pines están elegidos
> esquivando las minas del S3 y las asignaciones que ya existen, pero **nadie
> ha encendido esto aún**. El RC522 nos acaba de recordar por qué eso importa:
> el módulo no tenía el pin que la tabla decía. Trata este documento como un
> plan de cableado, y cámbialo en cuanto la placa diga otra cosa.
>
> El primer arranque real es la [#10](https://github.com/HackLab-Oriente/desktop-buddy/issues/10)
> (grabar 3 s y reproducirlos), que es el listón de la sesión 1 para voz.

## La decisión que ahorra seis pines

Dos decisiones de arquitectura antes de la tabla, porque explican casi todos
los números que hay en ella.

**1. Un solo bus I2S, compartido entre micro y altavoz.** El INMP441 y el
MAX98357A podrían tener cada uno su reloj, pero el diseño del buddy es
**half-duplex a propósito**: nunca escucha mientras habla (si no, se oye a sí
mismo). Como jamás van a transmitir a la vez, pueden compartir `BCLK` y `WS`
y quedarse cada uno con su línea de datos. **4 pines en vez de 6**, y el
firmware usa un único puerto I2S en modo full-duplex.

**2. La SD comparte el bus SPI del RC522.** Esto no es ahorro, es obligación:
el ESP32-S3 solo tiene **dos** controladores SPI de propósito general (SPI2 y
SPI3 — SPI1 es la flash), y ya están cogidos, la pantalla en SPI2 y el RC522
en SPI3. La tercera tarjeta tiene que compartir con alguien.

Comparte con el **RC522**, no con la pantalla. La pantalla se reescribe 30
veces por segundo y LovyanGFX es dueña de ese bus; meter ahí una SD es pelearse
por el bus en cada frame. El RC522 se consulta de vez en cuando y tolera
compartir sin despeinarse. Comparten `SCK`/`MISO`/`MOSI` y cada uno tiene su
propio `CS` — que es exactamente para lo que existe el chip select.

## Los pines

| Señal | GPIO | Va a |
|---|---|---|
| I2S `BCLK` | **15** | INMP441 `SCK` **y** MAX98357A `BCLK` |
| I2S `WS` / `LRCL` | **16** | INMP441 `WS` **y** MAX98357A `LRC` |
| I2S datos de entrada | **17** | INMP441 `SD` (el micro habla) |
| I2S datos de salida | **18** | MAX98357A `DIN` (el buddy habla) |
| Mute del ampli | **2** | MAX98357A `SD` — ver abajo, no es solo apagado |
| SD `CS` | **14** | módulo SD `CS` |
| SD `SCK` / `MISO` / `MOSI` | **39 / 40 / 41** | compartidos con el RC522 |

Quedan libres **1, 5, 6, 13, 47, 48** para sensores I2C, botones y el puerto
de hackeo.

## Micro INMP441

| Pin | A | Nota |
|---|---|---|
| VDD | **3V3** | |
| GND | GND | |
| SCK | GPIO **15** | |
| WS | GPIO **16** | |
| SD | GPIO **17** | salida del micro → entrada del ESP |
| L/R | **GND** | canal izquierdo; **no lo dejes al aire** |

`L/R` al aire es la causa nº 1 de «grabo y solo hay silencio»: sin él, el
micro no sabe en qué mitad de la trama hablar y el ESP lee el hueco vacío.

## Amplificador MAX98357A

| Pin | A | Nota |
|---|---|---|
| Vin | **5V** | funciona a 3V3, pero suena bastante más flojo |
| GND | GND | |
| BCLK | GPIO **15** | compartido con el micro |
| LRC | GPIO **16** | compartido con el micro |
| DIN | GPIO **18** | |
| SD | GPIO **2** | mute por firmware — ver abajo |
| GAIN | al aire | 9 dB por defecto; suficiente para empezar |
| + / − | altavoz | **lee la sección siguiente antes de cablear esto** |

### El pin `SD` no es solo un apagado

Se llama *shutdown*, pero el nivel de tensión selecciona además el canal
(izquierdo, derecho o la media de ambos). Lo interesante para nosotros es el
extremo: **llevarlo a nivel bajo silencia el ampli de verdad**, en hardware.

Ese es exactamente el mecanismo half-duplex que la sesión 1 pide demostrar:
mientras el micro graba, el firmware baja este pin y el altavoz queda mudo de
forma física, no «bajando el volumen». Por eso gasta un GPIO en vez de dejarse
al aire.

### El altavoz: **no cortes el conector**

La salida del MAX98357A es un **puente (BTL)**: los dos terminales del altavoz
están activamente excitados, ninguno es masa.

> ⚠️ **Nunca conectes uno de los cables del altavoz a GND.** En un ampli normal
> el negativo es masa; aquí no. Poner a masa un lado del puente es cortocircuitar
> media etapa de salida, y el chip lo paga.

Con eso claro: **no hace falta cortar nada.** Ese conector de plástico es casi
seguro un **JST-PH de 2 mm** (el paso estándar de los altavoces pequeños).

1. **Lo mejor** — un latiguillo JST-PH hembra con cables sueltos (se vende en
   bolsas de diez por muy poco). El altavoz se enchufa como venía, y los dos
   cables pelados van al ampli. Reversible, y puedes cambiar de altavoz sin
   volver a soldar.
2. **Si el ampli trae bornes de tornillo**: pelas 5 mm de los cables del
   latiguillo y los aprietas. Cero soldadura.
3. **Si el ampli trae pads para soldar**: sueldas ahí los cables del
   latiguillo, y el conector sigue siendo el punto de desconexión.

Cortar el conector funciona, pero es irreversible, y en un taller donde varias
personas van a probar altavoces distintos, el conector es justo lo que quieres
conservar. Guarda el corte para cuando ya sepas qué altavoz es el definitivo.

La **polaridad no importa** con un solo altavoz: al revés solo invierte la
fase, y no hay con qué compararla. Empezará a importar el día que haya dos.

## Lector de tarjeta SD

| Pin | A | Nota |
|---|---|---|
| VCC | **depende del módulo** — ver abajo | |
| GND | GND | |
| SCK | GPIO **39** | compartido con el RC522 |
| MISO | GPIO **40** | compartido |
| MOSI | GPIO **41** | compartido |
| CS | GPIO **14** | suyo propio, no compartido |

**El detalle del VCC.** El módulo azul clásico de microSD lleva un regulador
de 3,3 V y un buffer de nivel, y está pensado para alimentarse a **5 V** con
lógica de 3,3 V — dale 3V3 al VCC y muchos funcionan a ratos, que es peor que
no funcionar. Si en cambio es una placa pequeña sin regulador (solo el
portatarjetas y unas resistencias), es de **3V3 directo** y 5 V la mata. Mira
si tiene un chip de tres patas junto al pin de VCC: si lo tiene, 5 V.

Y una advertencia de bus: **si la SD se porta mal, sospecha del reparto antes
que del módulo.** Es lo primero que hemos colgado del mismo bus que otra cosa,
y las tarjetas SD son quisquillosas con la señal — cables cortos, y si hay que
elegir quién se queda con SPI3 en exclusiva, la SD tiene más derecho que el
RC522.

## Qué falta para que esto sea real

1. Entradas en `firmware/main/Kconfig.projbuild` para estos pines (hoy no
   existen: se cambiarían recompilando, que va contra las reglas de la casa).
2. Un firmware de prueba aislado por subsistema, como pide el plan de la
   sesión 1 — no meterlo en la app principal, donde un fallo es ambiguo.
3. Los eventos `voice.*`, que tampoco existen todavía
   ([#9](https://github.com/HackLab-Oriente/desktop-buddy/issues/9)).
