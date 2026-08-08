# Hardware: componentes y cableado (build de referencia v1)

Construimos desde cero: elegimos componentes, los cableamos, y diseñamos e
imprimimos nuestra propia carcasa. Esta es la BOM propuesta para v1 — la
elección final es decisión de grupo en la sesión 1, pero **los componentes
deben pedirse antes de la sesión 1**.

## Núcleo

| Pieza | Sugerencia | Notas |
|---|---|---|
| MCU | **Devkit ESP32-S3, N16R8** (16 MB flash / 8 MB PSRAM) | El S3 es necesario para el roadmap de voz (instrucciones vectoriales de ESP-SR); la PSRAM para gráficos + buffers de audio. Pilla el devkit con ambos puertos USB expuestos. *(El ESP32 clásico también está soportado como target secundario — sin PSRAM pierde la caché de ojos, el modelo local y la voz; ver `firmware/README.md`.)* |
| Pantalla | **A elección de cada uno:** GC9A01 redonda 1,28″ o ST7789 cuadrada 1,54″ — ambas 240×240, SPI | Misma resolución, mismo cableado, mismo stack gráfico — solo cambia la secuencia de init (un flag, no un fork). Regla: las caras se crean en un canvas de 240×240 con ojos/boca dentro del círculo inscrito ("zona segura redonda"). Evita los OLED I2C clase SSD1306: monocromos, 128×64, e I2C demasiado lento para animación. Rectangulares más grandes (ST7789 2″ / ILI9341 2,4–2,8″, 320×240) son post-v1 — rompen la paridad de resolución. |
| Tacto (caricias) | Pines capacitivos **nativos del ESP32** + cinta de cobre bajo la carcasa | Gratis (sin componente), y "acariciar la carcasa" gana a "tocar la pantalla" para una criatura. Una pantalla táctil es extra opcional. |
| Micrófono | **INMP441** (MEMS I2S) | Digital, sin líos analógicos. v1 lo usa para nivel de sonido (un Sense) y streaming STT del push-to-talk; el uso de sobremesa no necesita array de micros. |
| Salida de audio | **MAX98357A** (ampli I2S) + altavoz 4Ω 3W (~28 mm) | Chirps, efectos y reproducción TTS para el bucle PTT de v1. Sin canal de loopback → sin AEC → half-duplex por diseño. |
| Extras | LED RGB WS2812 (luz de ánimo), un botón de repuesto | Barato, expresivo, buen material de relleno para talleres. |

Coste estimado por buddy: **≈ 20–30 €** en cantidades unitarias. Pide al menos
un repuesto de todo; los micros MEMS y las pantallas baratas tienen bajas.

## Lista de compra y seguimiento de pedidos

Marca los items al pedirlos. **Decisión del lab (2026-07): construir 2–3
prototipos, no uno por miembro** — el pedido actual cubre exactamente eso (3
builds con repuestos). Cuando v1 esté sólida, el camino a un producto pulido
es un **lote de PCB portadora fabricada** (ver Alimentación y montaje), no más
protoboards.

### Componentes de núcleo (verificados 2026-07-13, pedidos)

- [x] Devkit ESP32-S3 N16R8, doble USB-C, pack de 3 (Hosyond) — [B0F5QCK6X5](https://www.amazon.com/dp/B0F5QCK6X5)
- [x] Pantalla GC9A01 redonda 1,28″, 240×240 SPI, pack de 5 (D-FLIFE) — [B0DCBM8KV1](https://www.amazon.com/dp/B0DCBM8KV1)
- [x] Micrófono MEMS I2S INMP441, pack de 5 — [B0C1C64R8S](https://www.amazon.com/dp/B0C1C64R8S)
- [x] Breakout ampli I2S MAX98357A 3 W, pack de 6 — [B0FHWB5VFW](https://www.amazon.com/dp/B0FHWB5VFW)
- [x] Anillo WS2812 de 12 LED, pack de 5 — [B0C77WMM7B](https://www.amazon.com/dp/B0C77WMM7B)
  (capar el brillo en firmware: 12 LEDs a blanco pleno ≈ 700 mA a 5 V)
- [x] Altavoz 4Ω 3W ultrafino 35×25×6,8 mm, coleta JST 1.25, pack de 5 (DWEII) — [B0F3CY5ZD2](https://www.amazon.com/dp/B0F3CY5ZD2)
  (nota de montaje: cortar el JST a ras, pelar 5 mm, estañar, atornillar al terminal de salida del MAX98357A)

### Extras

- [ ] Kit de protoboards, 2×830 + 2×400 puntos + 126 jumpers (BOJACK) — [B08Y59P6D1](https://www.amazon.com/dp/B08Y59P6D1) — prototipado sesiones 1–3
- [ ] Cables Dupont 120 uds M-M/M-H/H-H (ELEGOO) — [B01EV70C78](https://www.amazon.com/dp/B01EV70C78) — los H-H conectan el módulo de pantalla directo a los pines del devkit
- [x] Cinta de cobre 2″×33 ft, adhesivo conductor (LOVIMAG) — [B07C6YLNYL](https://www.amazon.com/dp/B07C6YLNYL) — las almohadillas capacitivas
- [x] Pulsadores táctiles 6 mm, pack de 20, para protoboard — [B07WF76VHT](https://www.amazon.com/dp/B07WF76VHT) — entrada de repuesto / botón de boot
- [x] Protoboard soldable (ElectroCookie) — [B07ZYNWJ1S](https://www.amazon.com/dp/B07ZYNWJ1S) — el montaje final replica el layout de protoboard 1:1
- [x] Kit de insertos M3 + tornillos, 361 uds con puntas para soldador — [B0G8JLX1HR](https://www.amazon.com/dp/B0G8JLX1HR) — atornillar la carcasa; la vas a reabrir constantemente
- [x] Cable de silicona 22 AWG, 6 colores ×10 ft (Fermerry) — [B089CQHRDT](https://www.amazon.com/dp/B089CQHRDT) — almohadillas, tiradas de altavoz, piezas montadas en carcasa

### Hardware del hack port (por pedir)

El hack port del panel trasero necesita conectores de verdad, no solo un
recorte en la carcasa:

- [ ] Surtido de tiras de pines, macho + hembra, fila simple/doble 2,54 mm, 138 uds — [B0GLHJ3DXH](https://www.amazon.com/dp/B0GLHJ3DXH) (~$10) — el puerto en sí es un zócalo **hembra** 2×4 (compatible Dupont) pegado al recorte; el kit además cubre reparaciones de headers en todas partes
- [ ] Kit de cables Qwiic/Stemma QT, JST-SH 1,0 mm a Dupont — [B08HQ1VSVL](https://www.amazon.com/dp/B08HQ1VSVL) (~$8) — adapta el enorme ecosistema de sensores I2C Qwiic/Stemma directo al hack port, sin crimpar, sin conector SH de panel

**Mapa 2×4 propuesto** (congelar en la sesión 1):

```
3V3   SDA
5V    SCL
GND   GPIO A  (ADC + táctil)
GND   GPIO B  (ADC + táctil)
```

I2C es el bus de extensión (por direcciones, alimentado, y todo el catálogo
Qwiic entra con los cables adaptadores); los dos GPIO crudos cubren botones,
sensores analógicos o una almohadilla táctil extra. En firmware: los pines
del hack port son de la capa Berry — lectura/escritura digital/analógica y
tacto desde scripts con cero C++; los dispositivos I2C usan Senses con
driver.

### Sensores integrados v1 (por pedir)

Kit de consciencia, integrado en cada buddy (no son accesorios del hack
port). Todos alimentan el `sensor_snapshot` del Brain (consciencia
conversacional gratis) y emiten eventos del bus para los reflejos. Los
drivers/máquinas de estados los escriben agentes de IA; los costes humanos
son colocación, montaje y ajuste — anotados por sensor.

- [ ] Combo AHT20 + BMP280 temp/humedad/presión, I2C, pack de 5 — [B0G1RDY1Y8](https://www.amazon.com/dp/B0G1RDY1Y8) (~$8)
  — colocación: en la rejilla de entrada, bajo en la carcasa (el autocalentamiento del recinto lee 3–5 °C de más). La tendencia de presión = charla del tiempo.
- [ ] BH1750 (GY-302) luz ambiente, I2C, pack de 3 (HiLetgo) — [B00M0F29OS](https://www.amazon.com/dp/B00M0F29OS) (~$6)
  — colocación: ventanita de alfiler o asomando por las rejillas. Eventos: `sense.light.dark` → reflejo de dormir.
- [ ] MPU6050 (GY-521) acelerómetro + giroscopio, I2C, pack de 5 (AITRIP) — [B07RXQGGJX](https://www.amazon.com/dp/B07RXQGGJX) (~$10)
  — montaje: rígido, atornillado o bien pegado a la carcasa (no flotando de los cables). La capa de gestos (`motion.pickup`, `motion.shake`, `motion.tilt`) la escribe la IA pero necesita play-testing humano para ajustar umbrales — presupuesta una hora divertida.
- [ ] Radar de presencia mmWave LD2410C, UART/GPIO, pack de 3 — [B0FKBF3CT4](https://www.amazon.com/dp/B0FKBF3CT4) ($19)
  — el rey de la consciencia: detecta que llegas/te sientas/te vas *a través de la carcasa de PLA* — cero agujeros. Montar mirando al frente tras la pared. Usa UART, no I2C.
- [ ] VL53L0X distancia ToF (láser), I2C, pack de 5 (Starry) — [B0DZWS6WC5](https://www.amazon.com/dp/B0DZWS6WC5) ($15) — *opcional*
  — anticipación: mano acercándose → emocionado antes del contacto. Necesita apertura real o ventana transparente (el láser IR no atraviesa el PLA).

~$58 en total, cubre 3 builds con repuestos. Todos los sensores I2C comparten
el único bus (un par `Wire`, direcciones distintas — sin coste de pines por
sensor). Descartados por ahora: calidad de aire clase ENS160 (necesita
rodaje, deriva — buen experimento de lab más adelante), PIR (instrumento
romo; el radar lo supera en todo).

### Alimentación, prep de servos y NFC (por pedir)

Piezas de la arquitectura de alimentación (ver *Alimentación y montaje*) más
las dos adiciones aprobadas con vistas al futuro:

- [ ] Extensión USB-C de panel, macho→hembra, montaje a tornillo, pack de 2 — [B0G43JGJRX](https://www.amazon.com/dp/B0G43JGJRX) ($10; **pedir ×2** para 3 builds + repuesto)
  — la entrada de energía de la carcasa; alimenta el raíl de 5 V directo, el USB del devkit queda solo para depurar
- [ ] Micro servos MG90S de engranaje metálico, pack de 6 — [B0DRHX1L5Q](https://www.amazon.com/dp/B0DRHX1L5Q) ($18)
  — 2 por buddy para el cuello pan/tilt de v2; el prototipado en banco puede empezar justo tras la sesión 4. v1 solo *reserva* sus pines y margen de potencia
- [ ] Módulo lector NFC PN532 (modo I2C), pack de 3 — [B0DTHPL3GG](https://www.amazon.com/dp/B0DTHPL3GG) ($19)
  — un dispositivo más en el bus I2C compartido; lee a través de la carcasa de PLA, cero agujeros. Stretch de la sesión 4: tap a un tag para cambiar de personalidad
- [x] Pegatinas NFC NTAG215, pack de 50 — [B0CHVWTRGC](https://www.amazon.com/dp/B0CHVWTRGC) ($13)
  — cartuchos de pack, tokens de modo, tótems impresos. Regla: los tags disparan solo acciones de lista blanca — nunca auth, nunca entrada cruda al Brain
- [x] Módulo micro SD, SPI 3,3 V (sin conversor de nivel), pack de 6 (WWZMDiB) — [B0BV8ZQ81F](https://www.amazon.com/dp/B0BV8ZQ81F) ($7)
  — almacenamiento de media para packs de contenido (voz pre-generada, imágenes, sonidos — ej. el explicador de juegos de mesa). Comparte el bus SPI de la pantalla con su propio CS; montado solo-lectura en operación normal, escrituras solo en subidas desde la web. Evita las variantes con "conversor lógico 3.3V/5V" — el conversor causa los fallos clásicos de SD-en-ESP32

### Confirmar que alguien ya tiene (no pedir a ciegas)

- [ ] Soldador + estaño + flux (el soldador también instala los insertos)
- [ ] Pelacables, alicates de corte a ras
- [ ] Pistola de silicona caliente
- [ ] Multímetro
- [ ] Cables USB-C **de datos** (los de solo-carga son el clásico sumidero de tiempo en talleres)
- [ ] **Cargadores de pared USB-C, 15 W+ (5 V/3 A)** — uno por buddy; cualquier cargador de móvil moderno vale
- [ ] Tarjetas micro SD, 8–32 GB (clase A1 vale) — una por buddy; hay en cualquier cajón
- [ ] Polyfuse ~500 mA (protección del hack port) + condensadores electrolíticos de 1000 µF (raíl de 5 V) — piezas clásicas de cajón de lab
- [ ] Filamento PLA/PETG para la carcasa

## Alimentación y montaje

**Regla de arquitectura: las cargas nunca pasan por el devkit.** El consumo
pico de v1 es ~1,1–1,5 A a 5 V (ráfagas WiFi + altavoz + anillo capado +
radar), que excede el conector USB y las pistas de 5 V del devkit — la fuente
clásica de fantasmas tipo "se reinicia cuando suena el altavoz".

- **Entrada de energía**: USB-C de panel en la carcasa → **raíl de
  distribución de 5 V** en la protoboard (topología en estrella) con un
  electrolítico de 1000 µF. Devkit, ampli, anillo, radar y hack port beben
  todos del raíl. El USB del devkit queda solo para programar/depurar.
- **Fuente**: cargador USB-C de 5 V/3 A (15 W+) por buddy. El margen ya cubre
  los servos de v2 (~1–1,5 A de picos de bloqueo) sin rehacer nada.
- **El 5 V del hack port** pasa por un polyfuse de ~500 mA — un accesorio en
  corto apaga el experimento, no el buddy.
- **Gobernador de potencia en firmware**: cap de brillo del LED, volumen
  máximo del altavoz y (después) los movimientos de servo se coordinan en la
  capa Expression para que los peores casos no se apilen. ~20 líneas;
  política, no conocimiento tribal.
- **La batería = el módulo "base de potencia" de v2, no la cabeza.** Una
  18650 + placa de carga/boost (clase IP5306: carga, boost a 5 V y load
  sharing en un chip) vive en la misma base atornillable que el cuello servo:
  la masa de la batería hace de lastre para la cabeza móvil, el calor queda
  lejos del sensor de temperatura, y la base alimenta la cabeza por la
  costura USB-C existente — la cabeza no distingue pared, power bank o
  batería. Hack de portabilidad v1.5: cualquier power bank USB-C (el consumo
  en reposo de ~200 mA queda por encima del umbral de autoapagado de los
  power banks baratos).
- Protoboard para las sesiones 1–2; pasar a protoboard soldada o PCB
  portadora simple para el montaje final en la sesión 4. Una PCB portadora
  diseñada en paralelo por quien le interese es un side-track estupendo pero
  no puede bloquear el montaje.

## Carcasa

- Impresa en 3D, carcasa de dos piezas alrededor de la pantalla ("cabeza")
  con rejilla de altavoz, agujero de micro y una **entrada USB-C de panel**
  (desacoplada del puerto del devkit — se acabó alinear recortes con una
  placa). Almohadillas capacitivas pegadas por dentro donde la acariciarías
  naturalmente (encima de la cabeza).
- **Base preparada para cuello**: interfaz inferior plana con torres de
  tornillo M3 y canal de cables, para que la base de potencia v2 (servos +
  batería) se atornille sin reimprimir la cabeza.
- **Zona de tap NFC**: el PN532 se monta tras la carcasa (lee a través de
  2–3 mm de PLA); imprime un pequeño icono de tap en la superficie. El radar
  igualmente ve a través de la carcasa — montarlo mirando al frente.
- El diseño de la carcasa empieza en la sesión 1 (medir componentes reales),
  primera impresión de prueba en la sesión 2, impresión final entre las
  sesiones 3 y 4.

## Presupuesto de pines (comprobación, ESP32-S3)

Pantalla SPI (5–6 pines), micro I2S (3), ampli I2S (3), almohadillas táctiles
(2–4), LED (1), botón (1), bus I2C (2 — compartido por todos los sensores
integrados *y* el hack port), UART del radar (2) — cómodamente dentro del
número de GPIO del S3, dejando de sobra GPIO libres sacados a un conector en
la carcasa (el "hack port") para sensores de miembros y actuadores futuros.
Sacar GPIO sin usar + 3V3/GND a un conector externo es parte del diseño v1 de
la carcasa a propósito: es la promesa de extensibilidad hecha física.
