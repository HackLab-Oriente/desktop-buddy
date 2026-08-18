# Taller de entrenamiento: enseña al buddy a escribir sus propias frases

Una receta práctica para entrenar un modelo de lenguaje diminuto — de cero
experiencia en ML a un modelo tuyo corriendo en el buddy. Es una **sesión
suelta**, sin fecha todavía — ver [workshops.md](workshops.md#la-sesión-de-entrenamiento-suelta);
no depende de ninguna sesión del buddy y ninguna sesión del buddy depende de
ella. Se apoya en los
resultados de kernel de `spikes/tinylm-s3` (rama `spike/tinylm-s3`), donde el
lado de inferencia ya está resuelto: **152 tok/s** para un modelo de 260K
parámetros en el ESP32-S3, con la salida streameando a la cara.

Nadie necesita experiencia previa en IA/ML para asistir. Si sabes ejecutar
comandos en una terminal, sabes entrenar un modelo.

---

## 1. Entrenar, en cinco frases

Un modelo de lenguaje es una pila grande de números ("parámetros" o "pesos")
que convierte *las palabras hasta ahora* en *una apuesta por la siguiente
pieza de palabra*. Entrenar es enseñarle millones de fragmentos de texto;
tras cada apuesta, un algoritmo empuja cada número para que esa apuesta
hubiera estado un poco menos equivocada. Repite unos millones de veces y las
apuestas se vuelven buenas — ese es todo el truco. Tu trabajo como
entrenador es solo elegir el **tamaño y forma** del modelo (los flags de
abajo), apuntarlo a **datos**, y **mirar cómo baja un número** hasta que deja
de bajar. El control de calidad es leer lo que escribe.

Dos números importan mientras corre:

- **train loss** — cuánto se equivoca con el texto del que está aprendiendo.
- **val loss** — cuánto se equivoca con texto que *nunca ha visto*. Este es
  el honesto. Cuando el val loss deja de bajar, entrenar más no compra nada.

## 2. Qué necesitas

- Un Mac (GPU Apple Silicon vía el device `mps` de PyTorch), una máquina
  Linux (GPU NVIDIA ideal; CPU pelada vale para los tamaños XS/S), o Google
  Colab gratis. Sin nube de pago, sin claves de cuenta.
- **Python 3.10–3.12 — NO 3.13/3.14.** PyTorch y el tooling de build van por
  detrás del Python más nuevo; con 3.14 falla antes de empezar a entrenar
  (ver resolución de problemas, §8).
- ~3 GB de disco, una tarde.
- El código de entrenamiento es el mismo repo del que salió nuestro kernel —
  [karpathy/llama2.c](https://github.com/karpathy/llama2.c). Escribe el
  formato `.bin` exacto que el buddy ya ejecuta. Entrenar → cuantizar →
  flashear.
- Compañero para el proyector de la sesión:
  [param-explorer.html](param-explorer.html) — la versión interactiva de §4 y
  §5 (mueve los mandos, mira cómo cambian tamaño y velocidad en el buddy).

## 3. Primer modelo: datos conocidos antes que los nuestros

Entrena con **TinyStories** (un dataset público de cuentos infantiles
simples) antes de tocar un corpus del buddy. Esto separa "aprender el
pipeline" de "construir nuestro dataset" — y te da comparación directa en
dispositivo contra el modelo de referencia `stories260K`.

```bash
git clone https://github.com/karpathy/llama2.c.git && cd llama2.c
python3.12 -m venv .venv && source .venv/bin/activate
pip install torch numpy sentencepiece requests tqdm
python tinystories.py download                    # ~1.5 GB, una vez
python tinystories.py train_vocab --vocab_size=512
python tinystories.py pretokenize --vocab_size=512
```

(Dos desviaciones deliberadas del README del propio repo: el venv va fijado
a Python 3.12, y los paquetes se instalan **sin pinear** en vez de
`pip install -r requirements.txt` — el repo pinea `numpy==1.23.5`, tan viejo
que ya no compila en setups actuales. Las versiones recientes de los cinco
paquetes funcionan bien. En Mac, antes `brew install python@3.12` si no lo
tienes; en Debian/Ubuntu, `sudo apt install python3.12-venv`.)

(`train_vocab` construye el vocabulario de 512 piezas; `pretokenize`
pre-mastica el dataset a ids de token. Ambos son de una sola vez y tardan
unos minutos.)

Luego el entrenamiento — esta forma son ~150K parámetros:

```bash
python train.py \
  --vocab_source=custom --vocab_size=512 \
  --dim=48 --n_layers=4 --n_heads=4 --n_kv_heads=4 \
  --max_seq_len=256 --batch_size=128 --gradient_accumulation_steps=1 \
  --device=mps --dtype=float32 --compile=False \
  --eval_interval=200 --max_iters=20000 --always_save_checkpoint=True
```

Tres de esos flags **no** son los defaults del repo, y cada uno importa:

- **`--max_iters=20000`.** El default es 100.000, y el learning rate decae
  con una curva calibrada a ese número, sea el que sea. Para una corrida de
  100k a las 8k y el rate nunca llega a bajar, así que el modelo nunca
  recibe su fase de pulido a rate bajo — acaba peor que una corrida *más
  corta* que terminó como es debido. Fija el presupuesto que de verdad vas a
  gastar.
- **`--max_seq_len=128`.** Es **la palanca de velocidad más grande que
  hay**, porque el coste de la atención crece con el cuadrado del contexto.
  Medido en un Mac M-series a tamaño XS: 251 ms/paso a 256, **89 ms a 128,
  43 ms a 64.** 128 es contexto de sobra para cuentos cortos. Ver §5.5.
- **`--always_save_checkpoint=True`** para que un Ctrl-C nunca pierda más
  que el último intervalo de evaluación.

`--batch_size` y `--gradient_accumulation_steps`, en cambio, son casi
irrelevantes aquí: cada reparto del mismo batch efectivo midió dentro de un
7% de los demás. No gastes tiempo en ajustarlos.

Imprime una línea de loss cada pocos segundos. Déjalo llegar al final, o
hasta que el val loss se aplane (a ojo, 20–40 min en un M-series a este
tamaño). Lee sus cuentos con:

```bash
python sample.py --checkpoint=out/ckpt.pt --num_samples=5 --seed=42
```

**Pasa `--num_samples` y `--seed` o te va a engañar:** `sample.py` lleva
`seed = 1337` y `num_samples = 1` hardcodeados, así que relanzarlo da una
salida idéntica byte a byte cada vez. Parece alarmantemente un modelo
atascado y es solo una semilla fija.

El trío `--device/--dtype/--compile` depende de tu máquina — cambian solo la
velocidad, nunca lo que el modelo aprende:

| máquina | flags |
|---|---|
| Mac (Apple Silicon) | `--device=mps --dtype=float32 --compile=False` (`torch.compile` y las GPU de Apple no se llevan) |
| Linux + GPU NVIDIA, o Colab | `--device=cuda --dtype=bfloat16 --compile=True` |
| cualquier máquina solo-CPU | `--device=cpu --dtype=float32 --compile=False` (bien para XS/S; doloroso más arriba) |

## 4. Los mandos, en lenguaje llano

Todo lo que le pasas a `train.py` cae en dos familias: flags que dan forma
**al modelo en sí** (cambian lo que acaba en el buddy) y flags que dan forma
**al proceso de entrenamiento** (cambian cuánto tarda en llegar, no lo que
se entrega).

### Flags que dan forma al modelo

| flag | significado llano | subirlo significa |
|---|---|---|
| `--dim` | anchura: el tamaño del "pensamiento" que el modelo lleva por pieza de palabra | más matiz por palabra, mejor elección léxica — y el coste crece más o menos con dim², así que es el mando más caro |
| `--n_layers` | profundidad: cuántas veces se refina el pensamiento antes de predecir | mejor gramática y coherencia de frase; el coste crece linealmente |
| `--n_heads` | cuántas cosas puede "buscar" la atención a la vez en las palabras anteriores (una cabeza puede seguir al sujeto, otra al ánimo) | rendimientos decrecientes a nuestra escala; mantén dim/n_heads (el tamaño por cabeza) entre 8 y 16 |
| `--n_kv_heads` | una variante de ahorro de memoria: las cabezas pueden compartir sus tablas de búsqueda | menos = KV cache más pequeña en el buddy; `stories260K` usa la mitad de kv-heads que heads y no cuesta nada apreciable |
| `--vocab_size` | cuántas piezas de palabra existen. 512 significa que las palabras comunes son 1 pieza y las raras se deletrean con fragmentos | vocabulario mayor = menos piezas por frase (lectura más rápida) pero tabla de embedding más grande; **mantén 512 para todo el taller** para que todos los modelos compartan tooling |
| `--max_seq_len` | hasta dónde ve hacia atrás, en piezas | generamos frases de 15 palabras; 256 ya es generoso. En el buddy fija la RAM de la KV cache (por eso el firmware la capa) |
| `--dropout` | ignorar al azar una fracción de la red en cada paso, para desalentar la memorización | vale la pena subirlo (0.1–0.2) en la Parte 2, cuando nuestro corpus es pequeño y memorizar es el riesgo principal |

**Cómo los mandos se vuelven un número de parámetros** (≈ velocidad en el
buddy): cada capa cuesta aproximadamente `4·dim²` (atención) `+ 3·dim·hidden`
(feed-forward, con hidden ≈ 2.7·dim), y la tabla de embedding cuesta
`vocab × dim` encima. Nunca necesitas calcularlo — la primera línea que
imprime `train.py` es el número exacto. Menos parámetros = buddy más rápido,
modelo más tonto. Ese intercambio es todo el juego.

### Flags que solo dan forma al entrenamiento

| flag | significado llano | qué saber |
|---|---|---|
| `--batch_size` | fragmentos digeridos por empujón | mayor = mejor uso del chip, más RAM; apenas cambia la calidad final. Bájalo solo si te quedas sin memoria |
| `--gradient_accumulation_steps` | repartir un empujón en N pasadas | un apaño de memoria para modelos que no caben con un batch real. **Ponlo a 1 a nuestros tamaños** — el default 4 cuadruplica el overhead por paso para un aprendizaje idéntico |
| `--learning_rate` | tamaño del empujón | demasiado alto: el loss explota o tiembla. Demasiado bajo: glacial. El default está ajustado para este repo — déjalo hasta tener una razón |
| `--max_iters` | empujones totales antes de parar | **también fija la curva de decaimiento del learning rate.** No planees hacer Ctrl-C antes de hora: una corrida truncada nunca recibe su pulido a rate bajo. Elige el presupuesto real desde el principio |
| `--eval_interval` | cada cuánto se mide el val loss | 200 mantiene el bucle de feedback corto |
| `--device` / `--dtype` / `--compile` | qué chip hace las cuentas y cómo | solo mecánica — cero efecto en lo que aprende. Mac: `mps/float32/False`. GPU de Colab: `cuda/bfloat16/True` |

### Cómo se ve el fracaso (para que nadie entre en pánico)

- **El loss SUBE o da `nan`** → learning rate demasiado alto, o mala
  combinación de flags. Mátalo y relanza; no se ha roto nada.
- **Las muestras son ensalada de palabras** → poco entrenado (el val loss
  sigue bajando — sigue) o el modelo es demasiado pequeño para la variedad
  de los datos.
- **Las muestras repiten una frase para siempre** → el fallo clásico de
  modelo diminuto; más variedad de datos, algo más de tamaño o más
  entrenamiento suele arreglarlo.
- **El val loss sube mientras el train loss sigue bajando** → memorización
  (overfitting). Para TinyStories es señal de parar. Para nuestro corpus
  diminuto del buddy en la Parte 2 es *lo esperado* — ver §7.

## 5. La escalera de tamaños

El experimento central de la sesión: mismos datos, cuatro tamaños, y el
grupo lee la salida y elige el más pequeño que suene bien. Las velocidades
en el buddy derivan del resultado int8-PSRAM **medido** en el spike (88
tok/s a 228K parámetros no-embedding; la velocidad escala inversamente con
ese número — es la cifra realista dentro del firmware, no el titular del
benchmark en RAM interna):

| escalón | flags | ≈ params | velocidad en el buddy (int8, PSRAM) | frase de 15 palabras |
|---|---|---|---|---|
| XS | `--dim=48 --n_layers=4 --n_heads=4 --n_kv_heads=4` | ~140K | ~180 tok/s | instantánea |
| S *(la forma de stories260K)* | `--dim=64 --n_layers=5 --n_heads=8 --n_kv_heads=4` | ~280K | ~81 tok/s | ~0,3 s |
| M | `--dim=64 --n_layers=6 --n_heads=8 --n_kv_heads=4` | ~305K | ~74 tok/s | ~0,35 s |
| L | `--dim=80 --n_layers=6 --n_heads=8 --n_kv_heads=8` | ~520K | ~42 tok/s | ~0,55 s |
| XL *(≈ "Config S")* | `--dim=128 --n_layers=6 --n_heads=8 --n_kv_heads=4` | ~1.3M | ~17 tok/s | ~1,3 s |

Los tokens streamean a la cara según se generan, así que la latencia
percibida es el primer token, no la frase entera — hasta XL se siente ágil.

(Por qué S dice ~280K cuando stories260K son 260K: el default de `train.py`
redondea la capa oculta a 192 donde el original usó 172. Misma forma, ~7%
más parámetros — los 88 tok/s medidos son del original; la S entrenada con
la receta cae en ~81.)

## 5.5. Hacer las corridas rápidas (mide, no adivines)

`bench_train_config.py` (en esta carpeta — cópialo a tu clon de llama2.c)
cronometra un forward+backward real en cada combinación de `(device, batch,
accumulation)` para una forma de modelo dada. Predijo 258 ms/paso donde el
entrenamiento en vivo midió luego 260, así que sus números se transfieren.

```bash
cp path/to/desktop-buddy/docs/bench_train_config.py .
python bench_train_config.py                          # tamaño XS, seq 256
python bench_train_config.py --max_seq_len=64         # lo que usará la ronda 2
python bench_train_config.py --dim=64 --n_layers=5    # otro escalón de la escalera
```

Medido en un Mac M-series, tamaño XS, batch efectivo 128:

| qué cambias | efecto |
|---|---|
| `--max_seq_len` 256 → 128 → 64 | 251 → 89 → **43 ms/paso** (la atención es cuadrática en el contexto) |
| reparto batch/accumulation | **menos de 7% entre todos los repartos** — no vale la pena ajustarlo |
| `mps` → `cpu` | ~1,4× más lento a este tamaño (más cerca de lo que esperarías; vale probarlo en tu máquina) |

**Para la ronda 2 este es el titular:** nuestras frases son ~20–30 piezas,
así que `--max_seq_len=64` no es un compromiso, es el ajuste correcto — y
hace la corrida completa ~6× más rápida. Es la diferencia entre un
experimento por tarde y uno por pausa de café.

Dos hábitos que vale la pena copiar del resto de este proyecto: **un tiempo
de paso impreso suelto es ruido** (la deriva térmica y el jitter del loader
lo mueven un 30% fácil), así que compara corridas del benchmark, no líneas
de log individuales; y el `mfu` del log se calcula contra los FLOPS pico de
una A100, así que en cualquier otra máquina no significa nada — ignóralo.

## 6. Poner un modelo en el buddy

`train.py` escribe `out/model.bin` (fp32, mismo formato que stories260K).
Desde el directorio `spikes/tinylm-s3` en la rama `spike/tinylm-s3`:

```bash
esptool.py --chip esp32s3 -p PORT write-flash 0x710000 model.bin
python3 tools/quantize_rowq8.py model.bin model-rowq8.bin
esptool.py --chip esp32s3 -p PORT write-flash 0x910000 model-rowq8.bin
idf.py -p PORT flash monitor
```

El benchmark lee la forma del modelo de las cabeceras del archivo, así que
se adapta a cualquier escalón sin modificar nada. Dos trampas:

- **El archivo fp32 debe quedar por debajo de 2 MB** o pisa el blob int8 en
  el offset `+0x200000` de la partición. XS/S/M caben; **L (~2,1 MB) y XL
  (~5 MB) no — para esos, sáltate el flasheo fp32 del todo** y flashea solo
  el blob int8 (las dos pasadas fp32 del benchmark imprimirán basura;
  ignóralas, las pasadas int8 son autocontenidas).
- **Un tokenizer nuevo hay que re-embeberlo.** Cada corrida de `train_vocab`
  produce un `tok512.bin` distinto, y el firmware lo lleva dentro de
  `tok512.h`. Si te lo saltas, un modelo perfectamente sano decodifica a
  galimatías. Regenéralo con:

```bash
python3 -c "
data = open('data/tok512.bin','rb').read()
with open('tok512.h','w') as f:
    f.write('// llama2.c tokenizer, embedded. %d bytes.\n' % len(data))
    f.write('#pragma once\n#include <stdint.h>\n')
    f.write('static const uint32_t kTokBytes = %d;\n' % len(data))
    f.write('static const uint8_t kTok[%d] = {\n' % len(data))
    for i in range(0, len(data), 16):
        f.write('    ' + ''.join('0x%02X, ' % b for b in data[i:i+16]) + '\n')
    f.write('};\n')
"
```

y sustituye `spikes/tinylm-s3/main/tok512.h` y recompila.

## 7. Parte 2: el corpus del buddy

**El corpus de entrenamiento y el banco de frases ya no son el mismo archivo.**
Conviene tenerlo claro antes de escribir una línea:

| | va por | para qué |
|---|---|---|
| **banco** (`lines/<expresión>.txt`) | expresión (`huraño`) | se lee tal cual; puede ser todo lo específico que quiera |
| **corpus** (este) | **registro** (`seco`) | entrena al modelo a *hablar así* sobre cualquier cosa |

El formato sigue siendo una línea por frase con el prefijo en texto plano —
pero el prefijo es el **registro**, no la emoción:

```
cálido: Ahí estás. Te estaba esperando.
seco: Ah. Eres tú.
somnoliento: Mmm… ¿ya es de día?
```

En generación el firmware alimenta `seco: ` como prompt y el modelo lo
completa. Prefijos en texto plano y no `<tokens>` especiales: el BPE los
aprende como piezas frecuentes y el pipeline del tokenizer se queda intacto.

### Escríbelo como una rejilla, no como una lista

Es la parte que decide si esto funciona. **No escribas frases sueltas por
registro: escribe la misma situación en los siete.**

El motivo es concreto. En un corpus pequeño, registro y tema van juntos sin
querer — si todas las frases `seco` hablan de tarjetas y todas las `cálido` de
saludos, el modelo aprende *el tema*, porque no tiene forma de saber cuál de
las dos variables querías. Manteniendo la situación fija y variando solo el
registro, **el registro es lo único que explica la diferencia**. Eso es todo el
truco, y es la razón de que 200 líneas en rejilla enseñen más que 2.000 sueltas.

**Situación: llegas y te sientas**

```
cálido: Ahí estás. Te estaba esperando.
juguetón: Mira quién se dignó a aparecer.
curioso: ¿Y hoy qué traes?
urgente: Por fin. Ven, ven.
seco: Ah. Eres tú.
somnoliento: Mmm… ¿ya es de día?
llano: Hola.
```

**Situación: te acaban de acariciar**

```
cálido: Otra vez, por favor. Se siente bien.
juguetón: Uy, qué cariñoso amaneciste.
curioso: ¿Y eso por qué lo haces? No me quejo.
urgente: Más. Ahora. No pares.
seco: Bueno. Ya.
somnoliento: Aaah… así… sigue…
llano: Gracias.
```

**Situación: no has entendido lo que te han dicho**

```
cálido: Perdona, no te entendí. ¿Me repites?
juguetón: Ni idea de qué dijiste, pero sonó importante.
curioso: ¿Cómo? Dilo otra vez, que ahora me da curiosidad.
urgente: No te escucho. Otra vez.
seco: No.
somnoliento: ¿Mmm? Se me fue.
llano: No entendí.
```

**Situación: te han quitado la tarjeta**

```
cálido: Listo, la dejamos ahí. Cuando quieras.
juguetón: Ya no está. Magia.
curioso: ¿Ya? Si apenas empezábamos.
urgente: Eh, ¿y eso? Tráela.
seco: Ya.
somnoliento: ¿Se fue? Bueno.
llano: Tarjeta retirada.
```

### La rejilla también dice si faltan registros

Ventaja de escribirlo así: **rellenar la rejilla es la prueba de completitud
del juego de registros.** Si una casilla cuesta mucho o sale forzada, o la
situación es rara o falta un registro.

Con las situaciones del buddy, los dos huecos que más se notan son:

- **`contrito`** — cuando la culpa es suya. `llano` lo dice plano y `cálido` lo
  dice con cariño, pero ninguno pide perdón.
- **`preocupado`** — `urgente` reclama atención; preocuparse es otra cosa.

Ambos pasan la prueba de «¿puedo decir *cualquier* frase en esta manera?», así
que son candidatos legítimos y no situaciones disfrazadas. Serían nueve. Lo
decide el equipo de personalidad (#16, #19).

### Dos expectativas que fijar antes de entrenar

- **La rejilla es la plantilla, no el corpus.** Siete registros por diez
  situaciones son setenta líneas, y con eso no se entrena nada. La rejilla dice
  *qué* recoger; el volumen sale de estirarla — más situaciones, y varias
  formas de decir cada casilla.
- **Un modelo de 150K con unos miles de líneas va a memorizar fuerte** — se
  comporta como un banco de frases difuso que recombina. No es fracaso: es
  justo el A/B que el grupo tiene que juzgar **de oído** contra el banco a
  secas. A este tamaño de datos el val loss deja de significar gran cosa; lee
  la salida.

Y una consecuencia de gobernanza que no conviene descubrir tarde: **los
prefijos quedan grabados al entrenar.** El juego de registros tiene que estar
cerrado antes de la primera corrida buena, o hay que reentrenar. Es la misma
dependencia que sacar `kEmotions` a datos de pack (#18).

Prep de la sesión aún por construir (pequeño, ~30 líneas cada uno, siguiendo el
patrón de `tinystories.py`): un script `pretokenize` para nuestro archivo de
corpus, y un script de muestreo que pregunte con cada registro.

## 8. Resolución de problemas

- **`pip install` explota con `AttributeError: module 'pkgutil' has no
  attribute 'ImpImporter'`** — estás en Python 3.13/3.14. El
  `numpy==1.23.5` pineado del repo no tiene wheel para esas versiones, así
  que pip intenta compilarlo desde fuente con tooling que llama a una API
  eliminada en Python 3.12. Arreglo: recrea el venv con
  `python3.12 -m venv .venv` e instala sin pinear como en §3. (Borra el
  `.venv` viejo primero.)
- **`torch` no existe para tu Python** — misma causa, mismo arreglo: los
  wheels de PyTorch van meses por detrás del Python más nuevo.
- **Flashear desde Linux:** la placa aparece como `/dev/ttyACM0` (USB
  nativo) o `/dev/ttyUSB0` (el puente CH343) en vez del
  `/dev/cu.usbmodem*` de macOS. Si esptool no puede abrirlo, añádete al
  grupo serie y re-loguéate: `sudo usermod -aG dialout $USER` (el grupo es
  `uucp` en Arch). Todo lo demás — offsets, comandos — es idéntico.
- **El entrenamiento parece congelado a 0 it/s en `mps`** — el primer paso
  compila kernels de GPU; dale un minuto antes de juzgar.
- **`sample.py` imprime exactamente el mismo texto cada vez** — no es un
  modelo atascado. `seed = 1337` fijo; pasa `--seed=N --num_samples=5`.
- **Los pasos van lentos (200+ ms a tamaño XS)** — comprueba
  `--gradient_accumulation_steps=1` y un `--batch_size` real. Prueba
  también `--device=cpu`: a estos tamaños el overhead de despachar a la GPU
  puede superar a las cuentas, y la CPU a veces gana. Ignora el porcentaje
  `mfu` por completo — se calcula contra los FLOPS pico de una A100
  (`model.py`), así que en cualquier otra máquina no significa nada.
- **Reanudar**: `--init_from=resume` continúa desde `out/ckpt.pt` y
  conserva el estado del optimizador. Cambiar `--max_iters` al reanudar
  re-ajusta la curva de decaimiento al nuevo presupuesto, que es la forma
  correcta de rescatar una corrida lanzada contra el default de 100k.

## 9. Dónde encaja esto

- El trabajo de kernel/velocidad: hecho — `spikes/tinylm-s3/README.md`
  (Pasos 0 y 0.5).
- Esta sesión: ¿puede un modelo pequeño *sonar bien*? Se decide escuchando.
- La decisión que alimenta: modelo local vs banco de frases vs híbrido —
  los tres van detrás del mismo evento `brain.ask`, así que lo que el grupo
  elija es una decisión de pack/config, no de arquitectura.
