# Cableado — ESP32 clásico (DevKit V1, 30 pines)

La misma cara a color que el S3, en la placa que muchos ya tienen en el cajón.
Coincide con los defaults de `firmware/main/Kconfig.projbuild` para el target
`esp32`. **Sigue las etiquetas de la serigrafía, no la posición del pin** —
los clones del DevKit V1 barajan el orden entre fabricantes.

> **Estado**: el arranque está verificado en placa real (5 bandas de 23 KB,
> LittleFS, tacto V1). **Nadie ha conectado aún la pantalla a un clásico**:
> si eres el primero, comprueba que no haya costuras entre bandas y cuéntalo.

## Primero los raíles

- `3V3` del devkit → raíl **rojo** · `GND` → raíl **azul**.
- La pantalla y el RC522 van a **3V3** (el RC522 muere a 5 V). Solo el anillo
  LED usa el pin `VIN`/5V.

## Pantalla GC9A01 (SPI, redonda 1,28″)

| Pin GC9A01 | A | Color sugerido |
|---|---|---|
| VCC | raíl 3V3 | rojo |
| GND | raíl GND | negro |
| SCL | GPIO **18** | amarillo |
| SDA | GPIO **23** | naranja |
| RES | GPIO **26** | blanco |
| DC | GPIO **27** | verde |
| CS | GPIO **5** | morado |
| BLK | GPIO **25** | gris (o a 3V3 y BL=-1 en menuconfig) |

Son los pines VSPI de toda la vida (18/23/5) más tres GPIOs libres. Los pines
del S3 (7–12) **no sirven aquí**: en el clásico 6–11 son la flash del chip.

## Almohadilla táctil

- Un jumper macho pelado en GPIO **4** (T0). El extremo de metal expuesto es
  la almohadilla; luego, pégalo a cinta de cobre para una de verdad.
- En el clásico, tocar **baja** la lectura (en el S3 la sube). El firmware ya
  lo sabe; se menciona porque es la trampa clásica al depurar.

## Anillo WS2812 (12 LED)

| Pin del anillo | A |
|---|---|
| VCC / 5V | **VIN (5V)** — a 3V3 se ve tenue y falla |
| GND | raíl GND (compartido con la placa, obligatorio) |
| DIN | GPIO **21** — el lado DIN, no DOUT |

El brillo está capado en firmware (~35%): 12 LEDs a blanco pleno piden
~700 mA, más de lo que le gusta al 5 V del devkit.

## RC522 NFC (opcional, deshabilitado por defecto)

Actívalo en `menuconfig → Buddy Zero → Enable RC522 RFID reader`.

| Pin RC522 | A | Nota |
|---|---|---|
| 3.3V | raíl 3V3 | **nunca 5 V** |
| GND | raíl GND | |
| SCK | GPIO **14** | |
| MISO | GPIO **34** | solo-entrada: perfecto para MISO y seguro en el arranque |
| MOSI | GPIO **13** | |
| SDA (=CS) | GPIO **15** | |
| RST | GPIO **32** | |
| IRQ | — sin conectar | |

MISO va en 34 y no en el clásico 12 a propósito: GPIO 12 es strapping (MTDI),
y un módulo que lo deje alto en el arranque selecciona el voltaje de flash
equivocado. Estos pines están **sin probar en hardware** — mismo aviso que la
pantalla.

## Consejos de protoboard

- El DevKit V1 es ancho: ponlo a caballo entre **dos protoboards** (o deja
  una fila de pines colgando del borde) para tener puntos libres a ambos
  lados.
- Los cuatro cables SPI de la pantalla, cortos y de largo parecido.
- El cable del GPIO 4 (tacto) capta ruido: rútalo lejos del mazo SPI.

## Checklist de primer arranque (`idf.py monitor`)

1. `face: frame 240x48 in 5 bands (23040 B), no cache (no PSRAM)` — el
   renderizado por bandas está activo.
2. `touch: baseline=NNN threshold=NNN (touch lowers)` — polaridad V1
   correcta; al tocar el cable caen `touch.down` / `touch.pet` en el log.
3. Ojos parpadeando en la pantalla. Sin imagen: revisa RES/DC/CS y 3V3;
   colores raros: es un clon con otro orden de color (ver
   [buddy-s3-display.md](buddy-s3-display.md), la sección de gotchas vale
   igual aquí).
4. Con RC522: `rc522: MFRC522 version 0x91` (o `0x92`). `0x00`/`0xFF` =
   revisa CS/SCK/MISO/MOSI y alimentación.

## Diagrama como código

`buddy-zero.wireviz.yml` junto a este archivo genera el diagrama del mazo:

```bash
pip install wireviz && wireviz hardware/buddy-zero.wireviz.yml
```

## Qué NO da esta placa

Sin PSRAM no hay caché de ojos (≈13 fps en vez de 30), ni modelo de IA local,
ni el bucle de voz v1. Todo lo demás — bus, reflejos Berry, web con recarga
en caliente, cerebro cloud, NFC — funciona igual que en el S3. La tabla
comparativa está en [../firmware/README.md](../firmware/README.md).
