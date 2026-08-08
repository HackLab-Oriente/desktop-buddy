# Spike: LovyanGFX en ESP-IDF v6.0.2 + GC9A01

Un proyecto desechable en un **segundo** montaje S3 + GC9A01, para que el
buddy PoC que funciona nunca tenga que desmontarse. Desechable por diseño —
si el veredicto es "no", se borra el directorio.

## La pregunta

¿Es LovyanGFX mejor sustrato para la capa gráfica del buddy que nuestro
código a mano de `esp_lcd` + framebuffer en
`firmware/components/expressions/round_face.cpp`?

**No** es una alternativa a LVGL — LovyanGFX son primitivas de dibujo y un
driver de panel, LVGL es un toolkit de widgets. Se componen. Lo que LovyanGFX
sustituiría es *nuestro* código: setup del panel, la fontanería de
`put`/`blend_at`/`clear`/`push`, y la fuente 5×7 `kFont57` escrita a mano.

## Puerta 0 — ¿siquiera compila en ESP-IDF v6.0.2? ✅ PASADA

Este era el riesgo real: la v6 eliminó las APIs legacy de driver que forzaron
nuestra propia reescritura, y LovyanGFX mete la mano hondo en los periféricos
SPI/LCD.

| | |
|---|---|
| Commit de LovyanGFX | `3f78b70`, 2026-07-22 (mantenido activamente) |
| Resultado del build | **limpio, exit 0** |
| Coste en flash | **37.642 B** (27.722 text + 9.484 rodata) |
| RAM estática | 436 B |
| Warnings | 3, todos el mismo — ver abajo |

37 KB es mucho más barato de lo esperado. Contra los 1,86 MB de margen de la
partición de app de 3 MB, el coste es un no-tema.

El único warning es `ledc_channel_config_t::intr_type is deprecated` en
`Light_PWM.cpp` — el helper PWM de retroiluminación de LovyanGFX. Cosmético,
y evitable del todo atando BL a 3V3 y poniendo `pin_bl = -1`.

## Setup

LovyanGFX **no** está vendorizado (40 MB de historia git, y quizá no lo
conservemos):

```bash
git clone --depth 1 https://github.com/lovyan03/LovyanGFX.git components/LovyanGFX
```

El cableado es idéntico al buddy real (ver
`../../hardware/buddy-s3-display.md`) para que cada medición se transfiera
sin paso de traducción de pines.

```bash
source ~/.espressif/tools/activate_idf_v6.0.2.sh
idf.py set-target esp32s3
idf.py -p /dev/cu.usbmodemXXXXXXX build flash monitor
```

**Pasa `-p` siempre.** Con dos placas en la mesa, un `idf.py flash` a secas
sobrescribe alegremente el buddy que funciona.

## Qué prueba el firmware, en orden

1. **Orden de color** — cuatro muestras etiquetadas. La palabra debe
   coincidir con el color. ROJO mostrando azul → invierte `cfg.rgb_order`.
   Negativo fotográfico → invierte `cfg.invert`. Nuestra ruta `esp_lcd`
   necesitó `invert=true` + BGR; esto comprueba si LovyanGFX acierta el
   GC9A01 por sí solo.
2. **Orientación** — "BUDDY" debe leerse de izquierda a derecha. Nuestra
   ruta `esp_lcd` salía en espejo y necesitó
   `esp_lcd_panel_mirror(panel, true, false)`. Si esto sale bien de fábrica,
   ese riesgo de regresión queda retirado.
3. **Calidad de texto** — una muestra de texto pequeño. Es el listón de
   legibilidad que nuestra fuente 5×7 tiene que superar, y la respuesta
   directa a "fuente más pequeña, más texto" sin escribir a mano otra fuente
   de bitmap (la última traía un desbordamiento de buffer que reiniciaba el
   dispositivo con respuestas largas de Claude).
4. **Tiempo de frame** — 60 pushes de pantalla completa, reportado como
   ms/frame y fps.
5. **Coste de sprite** — un sprite PSRAM de 240×240 (112 KB, igual que
   nuestro framebuffer actual) dibujado y empujado 60×. Es a la vez la
   primitiva de "volar alrededor del buddy" y la superficie donde
   renderizaríamos los ojos SDF.

## Resultados

Rellenados desde el log serie. Puerta de adopción: tiempo de frame no peor
que la ruta `esp_lcd` actual, y colores/orientación correctos sin pelea.

| Medición | Resultado |
|---|---|
| Orden de color correcto de fábrica | **sí** — sin pelea de `rgb_order` |
| Orientación correcta de fábrica | **sí** — el texto se lee del derecho, sin mirror |
| `fillScreen` | **24,01 ms/frame (41,6 fps)** |
| Dibujo+push de sprite | **30,78 ms/frame (32,5 fps)** |
| PSRAM para un sprite 240×240 | **116.740 B** |
| RAM interna de `lcd.init` | 2.352 B |

### La importante: estamos limitados por el cable, no por la CPU

240×240×2 B a SPI de 40 MHz son **23,04 ms de puro tiempo de cable**. Medimos
24,01 ms — LovyanGFX logra ~96% del ancho de banda teórico del bus. No queda
nada que optimizar dentro de la librería, y nuestra propia ruta `esp_lcd`
paga exactamente los mismos 23 ms, así que adoptar esto no puede costarnos
frame rate.

Las únicas dos palancas sobre el frame rate son por tanto:
- **subir el reloj SPI** (muchos clones del GC9A01 van a 80 MHz → ~12 ms), y
- **empujar rectángulos sucios en vez de frames enteros** (los ojos son una
  fracción pequeña de 240×240).

Ambas aplican igual a nuestro renderer actual. Conviene saberlo antes de que
alguien gaste una sesión "optimizando el código de dibujo" — el dibujo nunca
fue el problema.

## Test 2 — el escaparate de ojos, y una corrección

Cinco renderizados de la misma emoción, rotando en pantalla. **Son opciones
para que el grupo elija, no una propuesta de que una sea mejor.**

| | |
|---|---|
| A SHIPPED | exactamente lo que `round_face.cpp` dibuja hoy: color plano + glow |
| B DITHERED | + gradiente vertical, con dithering ordenado para que RGB565 deje de hacer bandas |
| C CATCHLIGHT | + un brillo especular — la mayor señal de "vivo" en animación de personajes |
| D DEPTH | + sombreado de borde interior, para que el ojo se lea como lente y no como pegatina |
| E LGFX NATIVE | `fillSmoothRoundRect`; **no puede** hacer la ceja ni el guiño |

Medido, ms por frame, sprite en RAM **interna** (cabe — 115 KB de ~380 KB):

| emoción | A draw | B draw | C draw | D draw | E draw | push |
|---|---|---|---|---|---|---|
| sleepy | 30,6 | 35,2 | 37,9 | 39,5 | 1,8 | 23,1 |
| happy | 62,9 | 69,6 | 73,6 | 76,0 | 2,5 | 23,1 |
| neutral | 64,3 | 76,2 | 82,6 | 87,2 | 2,6 | 23,1 |
| angry | 69,1 | 78,4 | — | — | — | 23,1 |
| curious | 80,4 | 96,2 | 104,4 | — | — | 23,1 |
| **surprised** | **102,4** | 123,8 | 134,7 | **142,7** | 3,3 | 23,1 |

### Corrección: estamos limitados por CPU, no por el cable

El benchmark anterior de `fillScreen` dijo "limitado por el cable" — pero
`fillScreen` es un memset y un push, y nunca toca las cuentas del ojo. Con el
renderer real, **nuestro dibujo SDF del ojo cuesta 1,3×–4,4× el tiempo de
cable.** Una cara completa son 54 ms (sleepy) a 125 ms (surprised) →
aproximadamente **8–19 fps**, y la CPU es el cuello de botella.

Es casi seguro por esto que la cara se sentía lenta. No es el panel, no es el
reloj SPI y no es LovyanGFX — es nuestro bucle por píxel, que llama a `sqrtf`
en cada píxel de la caja del ojo más un margen de glow de 10 px.

## Test 3 — optimizar el renderer

Cinco cambios, todos en el bucle interior, ninguno ingenioso:

1. **`-O2` para este componente** (el proyecto es `-Os` global — correcto
   para el firmware, incorrecto para un bucle por píxel).
2. **Saltarse el `sqrtf`** — `outside` solo es distinto de cero en las
   cuatro esquinas redondeadas. En el resto estábamos calculando `sqrtf(0)`
   o `sqrtf(v*v)`.
3. **Izar el término por fila** — `qy` depende solo de `y`, y se recalculaba
   para cada píxel de la fila. Más un `continue` temprano para píxeles más
   allá del radio del glow.
4. **Izar los colores por ojo** — `rgb(em.r/4, ...)` se recalculaba por
   píxel.
5. **Saltarse el leer-modificar-escribir donde el ojo es opaco** — que es la
   mayoría de sus píxeles, y no hay nada detrás contra lo que mezclar.

| emoción · variante | antes | después | ganancia |
|---|---|---|---|
| neutral · A SHIPPED | 64,29 | **42,74** | −34% |
| happy · A SHIPPED | 62,93 | **45,81** | −27% |
| curious · A SHIPPED | 80,38 | **53,47** | −33% |
| neutral · D DEPTH | 87,15 | **66,74** | −23% |

Una cara neutral completa son ahora **65,9 ms** (42,7 dibujo + 23,2 push)
contra 87,5 ms antes — cerca de **15 fps, desde 11**. El push sigue intacto
en 23,1 ms, como se esperaba; es el cable.

**El intercambio, ya cuantificado:** incluso optimizado, nuestro renderer SDF
es ~16× el coste del `fillSmoothRoundRect` de LovyanGFX (2,6 ms). Lo que eso
compra es la ceja inclinada y el guiño de felicidad, que la primitiva de la
librería no puede expresar en absoluto. Esa es la decisión para el grupo, y
ya no es cuestión de opinión.

**La mayor palanca restante, aún sin probar:** cachear el ojo renderizado. El
bitmap del ojo solo cambia en un parpadeo o cambio de emoción — una sacada en
reposo es una *traslación* de una forma que no cambió. Blitear un ojo
cacheado con offset de mirada dejaría la mayoría de frames a coste de dibujo
casi cero, quedando solo el push de 23 ms. Aritmética de punto fijo y pushes
de rectángulos sucios son las dos siguientes.

### El bug del gradiente (por qué B se veía plano en el panel)

El primer intento usó `k = 1.18 - 0.62t`, aclarando la parte alta del ojo.
Pero el azul de neutral ya es **255**, así que escalar por encima de 1.0 solo
recorta: el tercio superior del ojo quedaba clavado en azul máximo y luego
caía en escalones de ~6 px. Región plana seguida de escalones visibles se lee
exactamente como "sin gradiente, cambios duros de color", que es lo que
parecía.

Encontrado volcando el buffer del sprite por serie en vez de entrecerrar los
ojos frente al panel — los números mostraron `b5=31,31,31,31,30,29,...` de
inmediato.

Arreglo: `k = 1.0 - 0.55t` (nunca pasa de 1.0), y la amplitud del dithering
subida de un paso de cuantización a 1,5, porque un paso apenas rompe una
banda a esta distancia de visión. La columna ahora baja `g6 46→22, b5
30→14`, monótona desde el píxel de arriba.

Para contraste, la primitiva propia de LovyanGFX dibuja en **1,8–3,3 ms**,
25–40× más rápido — pero no puede expresar la ceja ni el guiño, porque esos
se recortan de la forma como *cobertura*, no se dibujan como formas. Ese es
el intercambio real a discutir: expresividad vs un orden de magnitud de CPU.

### La trampa del orden de bytes (nos costó una tarde — lee esta)

**LovyanGFX guarda los sprites de 16 bpp en BIG-ENDIAN**, porque ese es el
orden de bytes que consume el bus SPI. `getBuffer()` te da ese buffer crudo.
Escribir `uint16_t` RGB565 little-endian nativo en él — lo obvio, y lo que
`round_face.cpp` hace con su propio framebuffer — está mal, y mal de una
forma fácil de diagnosticar erróneamente:

- un color **plano** byte-swapeado da un color sólido pero *equivocado* (el
  rojo sale azul-morado), que se lee como bug de paleta;
- un **gradiente** byte-swapeado da *franjas arcoíris horizontales*, porque
  el byte bajo que lleva el azul se convierte en el canal rojo y cicla
  rápidamente rampa abajo.

Demostrado con una carta de prueba que dibuja los mismos tres colores de
tres maneras. La fila 1 (API de LovyanGFX) y la 3 (crudo, byte-swapeado)
salieron correctas; la fila 2 (crudo, little-endian) renderizó R/G/B como
Azul/Rojo/Verde. Las filas 1 y 3 se confirmaron byte-idénticas en el
dispositivo (`0x00F8` / `0xE007` / `0x1F00`), así que escribir el buffer
directamente no cuesta nada en calidad frente a la API de la librería — lo
que importa, porque el renderer SDF necesita acceso por píxel y no puede
usar `fillRect`.

Arreglo: convertir solo en la frontera del buffer (`to_store` /
`from_store`), y pasar `spr.color565(r,g,b)` — nunca un `uint16_t` 565
crudo — a cualquier llamada de LovyanGFX.

**Nota de metodología.** La primera ronda de capturas del framebuffer se
decodificó con la misma suposición little-endian con que se escribió, así
que estaban de acuerdo consigo mismas y no mostraban nada. Lo que cazó esto
fueron fotografías del panel real. Un volcado solo es evidencia cuando se ha
contrastado contra el cristal al menos una vez.

### Notas de arranque (nos costaron 20 minutos, te costarán lo mismo)

- La placa llegó corriendo un firmware que presentaba un puerto **TinyUSB
  CDC** (`0x303A:0x4001`). El auto-reset de esptool no tiene con quién
  hablar en esa interfaz, así que falla con *"No serial data received"*
  aunque el puerto abra bien. El modo download manual (mantener BOOT, tocar
  RESET, soltar BOOT) hace que la ROM presente `0x303A:0x1001` "USB
  JTAG_serial debug unit" en su lugar.
- Tras flashear, **suelta BOOT antes de resetear** o la ROM vuelve directa a
  `boot:0x0 (DOWNLOAD)` y la app no corre nunca.
- El USB-C de esta placa va a los pines USB nativos, así que la consola debe
  ser `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` — la consola UART0 por defecto
  no sale por ningún sitio visible.
- El devkit tiene un **segundo** USB-C tras un puente CH343. Es mejor para
  flashear (el auto-reset siempre funciona) pero **inútil para volcados de
  framebuffer**: 153 KB a 115200 sin control de flujo pierde bytes, y un
  byte perdido desalinea todo el stream base64 en basura rasgada. Usa el USB
  nativo para todo lo que mueva datos a granel. Cada volcado lleva ahora un
  checksum para que la corrupción se detecte en vez de renderizarse en
  silencio.

## Lo que NO entregaríamos

El renderer de ojos. `sd_round_rect` más multiplicación de cobertura es lo
que hace funcionar la ceja inclinada y el guiño — se recortan del ojo como
*cobertura*, no se dibujan como formas, y `fillSmoothRoundRect` no puede
expresar eso. El plan si esto se adopta: conservar la matemática SDF del
ojo y renderizarla en un `LGFX_Sprite` en vez de nuestro `s_fb` crudo.

Nota también que LovyanGFX **no** arregla el problema del coloreado del ojo.
Eso es banding de profundidad RGB565; el panel sigue siendo de 16 bits. El
dithering o el trabajo de paleta es nuestro en cualquier caso.

## Test 4 — puedes tener las formas Y el frame rate

El veredicto del lab sobre el test 2: conservar la expresividad de A/B,
conservar la fluidez de E, descartar el catchlight. Y un desafío directo a
mi afirmación de que las primitivas de la librería no podían hacer la ceja —
*"no me creo que no podamos tener esas formas (aunque sea con otra
técnica)"*.

**Ese desafío tenía razón y mi afirmación estaba equivocada.** Dos enfoques,
ambos funcionan:

| variante | qué es | fps | aspecto |
|---|---|---|---|
| A SHIPPED | el renderer de hoy, color plano | 15,4 | línea base |
| B GRADIENT | gradiente con dithering + glow | 13,0 | el aspecto preferido |
| **C CACHED** | **los píxeles exactos de B, el SDF corre solo al cambiar emoción/parpadeo** | **32,1** | **idéntico a B** |
| D PRIMITIVE | `fillSmoothRoundRect` + oclusión con triángulo negro | 39,4 | plano, sin glow, formas correctas |

**C es la respuesta.** Una sacada es una *traslación* de una imagen que no
cambió, así que cuesta un blit y no un re-render. Cachear los tres niveles
de apertura por emoción (3 × 115 KB en PSRAM) lleva a B de 13 fps a 32 — una
ganancia de 2,5× con salida píxel-idéntica. El coste es una reconstrucción
de ~110 ms al cambiar la emoción, que es una vez por reacción y se lee como
un compás natural, no un tirón.

**D demuestra que las formas no son exclusivas del SDF.** Dibuja el ojo
entero con la primitiva de la librería y pinta un triángulo negro encima de
la región que la ceja quita — sustracción en vez de multiplicación de
cobertura. La inclinación de enfado sale correcta. Pierde el gradiente y el
glow, pero la geometría está, así que "la primitiva no puede expresar la
ceja" era simplemente falso.

Tanto C como D están cerca del techo físico: los 23,1 ms de cable limitan el
panel a ~43 fps a SPI de 40 MHz, así que 32 vs 39 fps es una brecha
perceptiva mucho menor que 13 vs 39. Subir el reloj SPI levanta ambos.

**Sin resolver entonces e independiente de la técnica:** `browAmt = open *
0.5` rebanaba la mitad de la altura del ojo, y cada variante reproducía las
mismas cuñas. Era un bug de geometría, no de técnica de renderizado.
*(Posdata: arreglado después en el firmware — `kBrowDepth = 0.24` en
`round_face.cpp`; las cejas de angry/sad/suspicious se ven bien desde
entonces.)*
