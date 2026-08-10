# El buddy en ESP32-S3 — primer arranque: la cara redonda

Meta de este paso: la pantalla redonda GC9A01 mostrando la cara a color en el
ESP32-S3 (N16R8). No hace falta cablear nada más todavía — tacto, audio y
RC522 vienen deshabilitados por defecto en esta config.

## Cableado — GC9A01 (SPI) → ESP32-S3

Coincide con los defaults del firmware en `firmware/main/Kconfig.projbuild`.
Cablea por las **etiquetas de la serigrafía de la pantalla**, no por el orden
de los pines.

| Pin GC9A01 | ESP32-S3 | Notas |
|---|---|---|
| VCC | 3V3 | nunca 5 V |
| GND | GND | |
| SCL | GPIO 12 | reloj SPI (SCLK) |
| SDA | GPIO 11 | datos SPI (MOSI) |
| RES | GPIO 8 | reset |
| DC | GPIO 9 | dato/comando |
| CS | GPIO 10 | chip select |
| BLK | GPIO 7 | retroiluminación (o a 3V3 y BL = -1) |

**Minas de pines del ESP32-S3 N16R8** (la razón de esta elección):
- **GPIO 33–37 son la PSRAM octal** — no los uses jamás.
- GPIO 19/20 son USB, 0/3/45/46 son strapping, 26–32 son la flash.
- GPIO 7–12 (los elegidos) están todos libres.

## Flashearlo

```bash
cd firmware
idf.py set-target esp32s3      # primera vez en el S3 — regenera la config
idf.py menuconfig              # Buddy Zero → WiFi + clave de Anthropic (opcional)
idf.py build flash monitor
```

El ESP32-S3 + GC9A01 es el target de referencia. El **ESP32 clásico también
está soportado** — misma cara a color, renderizada por bandas al no tener
PSRAM, con otros pines por defecto: ver
[buddy-zero-wiring.md](buddy-zero-wiring.md).

## Checklist de primer arranque (idf.py monitor)

1. `face` / `buddy zero is alive` en el log, sin abortos de
   `ESP_ERROR_CHECK`.
2. La pantalla redonda muestra dos ojos cian sobre negro que **parpadean y
   derivan**.
3. Si la pantalla está en negro: revisa el cableado de RES/DC/CS y el 3V3.
4. Si los colores salen invertidos o mal (ej. ojos magenta): es un clon del
   GC9A01 con otro orden — invierte `cfg.invert` y/o `cfg.rgb_order` en
   `firmware/components/expressions/lgfx_buddy.h`. Los clones varían.

## Trampas que ya pisamos (para que no pierdas una tarde)

Todas están arregladas en el firmware; se documentan porque el siguiente
miembro del lab que cablee un S3 se topará con las mismas.

- **Stack overflow en la tarea `main` al arrancar.** `app_main` levanta la
  pantalla, el primer dibujado y la VM Berry en una sola tarea; el stack de
  3584 bytes por defecto se desborda (el pánico apunta a
  `vApplicationStackOverflowHook`). Arreglo:
  `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192` (en `sdkconfig.defaults`). La tarea
  de la cara a color corre además con 6144.
- **El tacto lee al revés en el S3.** En el ESP32 clásico un toque *baja* la
  lectura de capacitancia; en el **S3 la sube** — polaridad opuesta. El
  driver ramifica por versión de hardware (dispara por encima de la línea
  base en el S3, por debajo en el clásico). Si el tacto nunca dispara, mira
  la línea de debug `raw=… baseline=…`: tocar debería empujar `raw` ~30% por
  *encima* de la base en el S3.
- **Mismatch de Python `python` vs `python3`.** Si un build de CLI se queja
  de que el entorno "is not consistent" / "configured with python3", es que
  VS Code y una shell activada a mano eligieron symlinks de Python
  distintos. Ejecuta `idf.py fullclean` una vez desde el entorno que vayas a
  usar, y recompila.

## Anillo LED WS2812 (halo de ánimo)

El anillo de 12 LED brilla con el **color de ánimo de la emoción** (la misma
tabla que los ojos — cian neutral, rojo enfadado, azul triste…) y se anima
con `led.mood`: respirar (calm/excited), un cometa girando (thinking), o
apagado.

| Pin del anillo | ESP32-S3 | Notas |
|---|---|---|
| VCC / 5V / VDD | **5V** (VBUS) | 12 LEDs quieren 5 V; a 3V3 se ve tenue y falla |
| GND | GND | compartido con el S3 — obligatorio |
| DIN / IN | GPIO **21** | datos; **usa el lado DIN**, no DOUT |

Notas:
- **Datos a 3,3 V hacia una tira de 5 V suele funcionar** a esta longitud
  tan corta. Si el primer LED va raro o con colores mal, añade un conversor
  de nivel, o baja el VCC del anillo a ~4,3 V (un diodo en serie) para bajar
  su umbral lógico.
- **El brillo está capado en firmware** (`kMaxBright = 0.35` en
  `led_ring.cpp`). 12 LEDs a blanco pleno piden ~700 mA — más de lo que le
  gusta al pin de 5 V del devkit. Mantén el cap salvo que el anillo tenga su
  propia fuente de 5 V (el raíl de la v1).
- Config: `menuconfig → Buddy Zero → WS2812 mood ring`, pin
  `BUDDY_WS2812_PIN` (21), cantidad `BUDDY_WS2812_COUNT` (12).
- Comprobación al arrancar: `ring: WS2812 ring: 12 LEDs on GPIO 21`, y luego
  una respiración cian suave. Colores mal (ej. rojo/azul intercambiados) →
  la tira no es GRB; cambia `LED_STRIP_COLOR_COMPONENT_FMT_GRB` a `_RGB` en
  `led_ring.cpp`.

## Qué se conecta después (las piezas ya están)

Mismo S3, incremental — cada una es un Sense/Expression más en el bus:
- **Almohadilla de caricias** — el tacto funciona en el S3 (GPIO 1–14).
  Configura `BUDDY_PIN_TOUCH`.
- **Micro (INMP441) + ampli (MAX98357A) + altavoz** — la ruta de audio I2S
  (chirps primero, luego el bucle de voz push-to-talk).
- **Botones** — Senses digitales extra.

## Lector NFC RC522 (verificado en el S3)

Su propio bus SPI3, así que no comparte con la pantalla. Estos son los
defaults de menuconfig; los valores viejos del ESP32 clásico eran
directamente peligrosos en este chip porque GPIO 19/20 son el USB.

| Pin RC522 | ESP32-S3 | Notas |
|---|---|---|
| 3.3V | **3V3** | 5 V mata el módulo |
| GND | GND | |
| SCK | GPIO **39** | |
| MISO | GPIO **40** | |
| MOSI | GPIO **41** | |
| SDA / CS | GPIO **42** | el módulo lo serigrafía **SDA**; en SPI es el chip select |
| RST | GPIO **38** | |
| IRQ | — **sin conectar** | el driver hace polling; no usa la interrupción |

Si buscas un pin marcado «CS» en el módulo, no lo hay: el MFRC522 multiplexa
SPI, I2C y UART sobre los mismos pines físicos y la serigrafía usa el nombre de
I2C. **`SDA` es el chip select.** Y el `IRQ` se queda al aire a propósito:
`rc522.cpp` lee `ComIrqReg` por SPI en vez de cablear la interrupción, así que
sobra un cable y un GPIO.

Se habilita en `menuconfig → Buddy Zero → Enable RC522 RFID reader`; los
pines son configurables en el submenú. Elegidos evitando cada mina del S3:
el bus de la pantalla (7–12), USB (19/20), flash (26–32), PSRAM octal
(33–37), strapping (0/3/45/46), tacto (4) y el anillo LED (21).

Comprobación al arrancar: acerca un llavero y busca `nfc.tag` con un UID hex
en la traza.

> **Verificado en hardware** (S3, primer intento): `rc522: MFRC522 version 0x92`
> y un llavero dispara `nfc.tag` con su UID, que la cara muestra en pantalla.
> Estos pines y este cableado son reales, no una propuesta leída del datasheet.
> En el **ESP32 clásico sigue sin probarse** — mismo módulo, otros pines.
