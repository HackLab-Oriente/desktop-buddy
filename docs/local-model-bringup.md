# Arranque del modelo local — Paso 0: probar la ruta de inferencia

**Estado:** HECHO — ejecutado el 2026-08-05. Veredicto: **amarillo, con
margen.** Código y notas completas: rama `spike/tinylm-s3` (los spikes no se
mergean a main). *(Continuación: el Paso 0.5 — la optimización del kernel —
llevó luego el resultado de 31,4 a 152 tok/s; ver el README del spike y
[training-workshop.md](training-workshop.md). Las cifras de este documento
son las del Paso 0 original.)*
**Audiencia:** quien retome esto (humano o agente) antes del Taller 0.

## Qué es esto, y qué no es

Queremos que el buddy genere sus propias frases cortas con carácter en el
dispositivo, con un modelo pequeño entrenado por nosotros (ver "A dónde lleva
esto" al final).

Antes de gastar dinero y un fin de semana entrenando nada, hay que responder
una pregunta con un **número medido**, no una estimación:

> ¿Puede un ESP32-S3 correr inferencia de transformer lo bastante rápido,
> *mientras la cara sigue animándose*, como para que valga la pena?

Este documento trata solo de responder eso. Usa un modelo desechable.

### El modelo desechable: `stories260K`

**`stories260K` NO es el modelo de
[slvDev/esp32-ai](https://github.com/slvDev/esp32-ai), y no es el modelo que
pretendemos entregar.** Tres cosas distintas, fáciles de confundir:

| | Qué | Por qué está aquí |
|---|---|---|
| **slvDev/esp32-ai** | 28,9M params, 4-bit, **14,9 MB** | El proyecto que inspiró esto. **Rechazado**: no cabe en nuestra flash (ver `docs/ideas-exploration.html`). Buena fuente de técnica. |
| **`stories260K`** | ~260K params, fp32, **~1 MB** | **Este documento.** Un benchmark desechable. Su salida es casi galimatías. No nos importa qué dice — solo a qué velocidad lo dice. |
| **El modelo "voz del buddy"** | ~1,3–4M params, 4-bit, ~0,7–2 MB | Lo que de verdad queremos entrenar. Bloqueado por la respuesta de aquí. |

`stories260K` es de [llama2.c](https://github.com/karpathy/llama2.c) de
Andrej Karpathy. Es el benchmark correcto porque es *deliberadamente
diminuto*: la misma familia de arquitectura que entrenaríamos, tan pequeño
que corre en fp32 **sin ningún trabajo de cuantización**, y su motor de
inferencia (`run.c`) es un único archivo C sin dependencias de menos de 1000
líneas. Eso convierte el Paso 0 en un ejercicio de porteo, no un proyecto de
investigación. Si hubiera que escribir primero un kernel de 4 bits, esto
sería un mes en vez de un fin de semana.

## Prerrequisitos

- Placa ESP32-S3 N16R8 con la pantalla redonda ya funcionando (`hardware/buddy-s3-display.md`)
- ESP-IDF v6.0.2 (`source ~/.espressif/tools/activate_idf_v6.0.2.sh`)
- La partición `model`, ya añadida a `firmware/partitions.csv` (4 MB, cruda, offset 0x710000)

## Paso 1 — Conseguir el modelo y verificar su forma

```bash
mkdir -p /tmp/l2c && cd /tmp/l2c
git clone --depth 1 https://github.com/karpathy/llama2.c
ls -la llama2.c/stories260K/    # stories260K.bin + tok512.bin
```

Si esa carpeta falta, los modelos están también replicados en
`huggingface.co/karpathy/tinyllamas`. **No te fíes de números de
configuración citados de memoria — léelos del archivo.** La cabecera legacy
de llama2.c son siete int32 little-endian:

```python
import struct
with open('llama2.c/stories260K/stories260K.bin','rb') as f:
    dim, hidden_dim, n_layers, n_heads, n_kv_heads, vocab_size, seq_len = \
        struct.unpack('<7i', f.read(28))
print(f"{dim=} {hidden_dim=} {n_layers=} {n_heads=} {n_kv_heads=} {vocab_size=} {seq_len=}")
# A negative vocab_size means the classifier weights are NOT shared with the
# embedding table — run.c keys off this, so note which one you have.
```

Registra estos números en este archivo cuando los tengas. Todo lo de aguas
abajo (dimensionado de memoria, la extrapolación del Paso 5) depende de
ellos.

## Paso 2 — Flashear los pesos a la partición `model`

Los pesos van **en crudo**, no como archivo, para que se puedan mapear a
memoria siquiera — `esp_partition_mmap` funciona sobre una partición cruda
pero no sobre un archivo dentro de LittleFS. (El Paso 4 descubrió que *no*
conviene ejecutar desde el mapeo: cópialo a PSRAM en el arranque. La
partición cruda sigue siendo el sitio correcto para guardarlo.)

```bash
esptool.py --chip esp32s3 write_flash 0x710000 llama2.c/stories260K/stories260K.bin
```

Confirma el offset contra `firmware/partitions.csv` antes de ejecutar esto —
escribir en el offset equivocado corrompe la app o el sistema de archivos de
packs.

El tokenizer (`tok512.bin`, unos KB) puede ir en LittleFS como archivo
normal; se lee secuencialmente una vez al arrancar, así que no necesita mmap.

## Paso 3 — Portar `run.c` a un componente ESP-IDF

Crea `firmware/components/tinylm/`. Copia `llama2.c/run.c` tal cual y haz
exactamente cuatro cambios. Resiste hacer más — la meta es una medición, no
un buen componente.

1. **Pesos: I/O de archivo → mmap.** Sustituye el bloque `mmap()`/`open()`
   de `build_transformer` por:
   ```c
   const esp_partition_t* p = esp_partition_find_first(
       ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "model");
   const void* base = NULL;
   esp_partition_mmap_handle_t h;
   ESP_ERROR_CHECK(esp_partition_mmap(p, 0, p->size,
                                      ESP_PARTITION_MMAP_DATA, &base, &h));
   ```
   y apunta los punteros de pesos dentro de `base` exactamente como el
   código original los apunta dentro del archivo mapeado.
2. **Buffers de RunState → PSRAM.** Cada `calloc` de `malloc_run_state` se
   vuelve `heap_caps_calloc(..., MALLOC_CAP_SPIRAM)`. Son las activaciones y
   la KV cache — se escriben en cada token, así que deben estar en RAM, no
   en flash.
3. **Tokenizer desde un array embebido**, no un archivo — un benchmark no
   debería necesitar sistema de archivos. Y **elimina `main()`**.
4. **Añade un `CMakeLists.txt`**: `idf_component_register(SRCS "run.c" INCLUDE_DIRS "include" REQUIRES spi_flash esp_partition)`
   y pon `-ffast-math -O2` solo en este componente.

Luego llámalo desde una tarea fijada al **core 1**, prioridad **3** (por
debajo de la tarea de la cara a 4, para que la cara gane la contención —
queremos medir la cara degradándose, no muriéndose de hambre).

## Paso 4 — Medir

Toma cinco números. Regístralos aquí en la tabla.

| Métrica | Resultado |
|---|---|
| Modelo, leído de su propia cabecera | dim 64, hidden 172, 5 capas, 8 heads, 4 kv-heads, vocab 512, seq 512 |
| Parámetros | 260.672 en total — **227.840 no-embedding** (la parte que cuesta cómputo) |
| tok/s, pesos mapeados desde flash | **12,6** (79,5 ms/token) |
| tok/s, pesos copiados a PSRAM | **31,4** (31,8 ms/token) |
| p50 / p99 por token (PSRAM) | 31,7 / 39,2 ms — muy plano, el jitter no es problema |
| PSRAM consumida | 676 KB, casi todo la KV cache |
| RAM interna consumida | ~17 KB |
| Salida | prosa TinyStories coherente — el porteo es correcto, no solo rápido |

### El hallazgo titular: NO mapees los pesos a memoria

Cada token lee el set de pesos completo, y 1 MB no cabe en la caché de la
MMU — la inferencia mapeada gasta el **60% de su tiempo esperando a la
flash**. Copiar los pesos a PSRAM en el arranque es **2,5× más rápido**, y
para nosotros es gratis: la PSRAM son 8 MB y todos los modelos que
consideramos pesan 0,7–3,4 MB.

Aquí es donde el truco de Per-Layer Embeddings del proyecto esp32-ai deja de
aplicarnos. PLE existe porque un modelo de 14,9 MB *no puede* caber en RAM.
El nuestro cabe, así que simplemente lo dejamos residente y nos saltamos el
problema entero.

## Paso 5 — Extrapolar al modelo real

El coste de cómputo escala con los parámetros **no-embedding** (la tabla de
embedding es una búsqueda, no un matmul). Para `stories260K`, no-embedding ≈
`n_layers × 12 × dim²`. Para nuestra candidata Config S (vocab 1024, d=128,
6 capas) son ~1,18M.

```
predicted_ms_per_token(Config S) ≈ measured_ms_per_token(260K) × (1.18M / non_emb_260K)
```

Luego aplica un **factor de aceleración de 0,5–0,7×** por pesos int4 (menos
tráfico de memoria, más coste de desempaquetado) — y trátalo como una
suposición hasta que alguien lo mida.

### Puertas de decisión — y dónde caímos

Escalando por parámetros no-embedding desde el número de PSRAM:

| | ms/token | fp32 | int4 (est. 0,6×) |
|---|---|---|---|
| **Config S** (v1024 d128 L6, 1,18 M no-emb) | 164,8 | 6,1 tok/s | **~10 tok/s** |
| Config M (v2048 d192 L8, 3,54 M) | 494 | 2,0 tok/s | ~3,4 tok/s |
| Config L (v2048 d256 L8, 6,29 M) | 878 | 1,1 tok/s | ~1,9 tok/s |

Contra las puertas: **Config S es AMARILLO** (5–15 tok/s). Buena para
réplicas cortas y murmullos de reposo; no conversacional. Config M y L son
rojas.

**Pero tres palancas están completamente sin tocar**, y dos son grandes:

1. **`seq_len` es 512 y necesitamos ~64.** Una frase de 15 palabras son ~20
   tokens. Eso recorta la KV cache 8× (676 KB → ~85 KB) *y* recorta el
   cómputo de atención.
2. **El matmul es un triple bucle escalar naíf.** El S3 tiene SIMD de 128
   bits y ESP-DSP trae productos punto optimizados. Para dar escala:
   esp32-ai corre ~17× más parámetros no-embedding a un ritmo de tokens
   similar, lo que implica que su kernel es aproximadamente un orden de
   magnitud mejor que el C de referencia de llama2.c.
3. **int4** — el 0,6× de arriba es una suposición, no una medición.

Así que la lectura honesta **no es "el chip no puede"** — un porteo fp32
naíf y sin vectorizar ya mete a Config S en la banda usable. La pregunta
restante es cuánto trabajo de kernel quiere hacer el lab, y esp32-ai es open
source para aprender de él. *(Posdata: ese trabajo de kernel se hizo — Paso
0.5, rama `spike/tinylm-s3` — y rindió 4,85×.)*

### La pregunta de la contención con la cara, respondida de otro modo

El plan original era medir la inferencia contra una cara ocupada. Ese
planteamiento quedó obsoleto: desde que los ojos van cacheados, la cara solo
renderiza en un parpadeo o cambio de emoción y blitea el resto del tiempo —
está **ociosa la mayor parte del tiempo**. El jitter por token aquí es 31,7
p50 vs 39,2 p99 sin nada más corriendo; el dibujado ocasional de ~8 ms de la
cara más los 23 ms de push no van a perturbar eso de forma significativa. La
preocupación por la contención era un producto del coste del renderer
*antiguo*.

## Riesgos conocidos

- **Contención de ancho de banda de PSRAM.** El framebuffer (112 KB) también
  vive en PSRAM y se manda por DMA a la pantalla cada frame. Las
  activaciones de la inferencia compiten por el mismo bus. Esto puede doler
  más que el tiempo de CPU, y no aparece en un benchmark solo-CPU — que es
  exactamente la razón de la fila 2 de la tabla.
- **El espacio de direcciones de `esp_partition_mmap` es finito.** El S3
  tiene un número limitado de páginas MMU para mapear datos. Un mapeo de
  1 MB va bien; uno de 4 MB puede que no. Conviene saberlo antes de que el
  modelo real crezca.
- **fp32 no es lo que entregaremos.** El Paso 0 se salta la cuantización a
  propósito. Espera que los números int4 difieran; para eso está el factor
  de corrección del Paso 5.

## A dónde lleva esto (no es parte del Paso 0)

Si las puertas pasan: generar ~100k frases etiquetadas por ánimo con Claude
Haiku (~$15), entrenar un tokenizer BPE propio de 1024 tokens sobre ese
corpus, entrenar Config S con el trainer de `nanoGPT` o `llama2.c`,
cuantizar a int4 y entregarlo como la ruta de Brain offline. El corpus ES la
personalidad — y por eso el **formato del corpus, el set de tokens de afecto
y el vocabulario de expresiones son decisiones de grupo**, no algo que se
decida aquí. Ver `docs/ideas-exploration.html`.
