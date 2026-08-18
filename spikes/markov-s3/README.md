# Spike: Markov corriendo de verdad en el ESP32-S3

Convierte en medidas lo que hasta ahora era una estimación analítica: cuánto
ocupa la cadena, cuánto tarda en construirse, y cuánto cuesta devolver una
frase — **combinando los bancos de siete registros** y con supresión de frases
recientes.

```bash
idf.py set-target esp32s3 && idf.py -p PORT flash monitor
```

El corpus va **embebido en el binario** (`EMBED_TXTFILES`), un fichero por
registro, así que el spike no necesita LittleFS ni tarjeta: mide la cadena, no
el sistema de ficheros.

## Resultados en placa (ESP32-S3 N16R8, 160 MHz, PSRAM octal)

| | un banco (`cálido`) | los 7 combinados |
|---|---|---|
| frases | 67 | 371 |
| palabras / únicas | 395 / 169 | 1.701 / 633 |
| transiciones | 462 | 2.072 |
| **huella en RAM** | **4,4 KB** | **18,9 KB** |
| construir (parse + ordenar) | 3,3 ms | 13,8 ms |
| **por frase, p50** | **23 µs** | **46 µs** |
| por frase, p99 | 71 µs | 87 µs |
| frases vacías / repetidas agotadas | 0 / 0 | 0 / 0 |

Corpus en flash: **9.456 B** los siete ficheros. Binario: 183 KB.

### Qué dicen estos números

- **Combinar los siete bancos cuesta el doble por frase que uno solo** (46 vs
  23 µs) y ~4× la memoria. Ambas cosas siguen siendo ruido: 46 µs es
  **20.000 veces menos** que los ~1 s del modelo neuronal.
- **Construir la tabla es lo más caro, y aun así son 14 ms.** Se puede hacer al
  arrancar sin que se note, o precalcular y flashear ya ordenado.
- **La supresión de repetidas casi no cuesta**: 9 reintentos en 300 frases con
  los 7 bancos (76 con uno solo, que tiene menos donde elegir). Ninguna agotó
  los 8 intentos.
- El p99 es ~2× el p50 y el máximo 99 µs. Sin sorpresas: no hay nada que pueda
  bloquear la cara.

### La estimación analítica se quedó corta

`markov.py --memory` predecía **14,6 KB y ~18 µs**. Medido: **18,9 KB y 46 µs**.

- **Memoria: 30 % optimista.** El modelo contaba filas y texto del vocabulario,
  pero no la tabla de offsets completa.
- **Tiempo: 2,5× optimista.** El modelo contaba solo los saltos de la búsqueda
  binaria; en la práctica también se recorre la corrida de filas iguales, se
  copian las palabras y se tira del RNG.

El orden de magnitud era correcto y la conclusión no cambia, pero conviene
saber que el modelo tira a optimista: **si algún día el margen importa, mídelo,
no lo estimes.**

## Un bug que costó encontrar y merece estar escrito

El 2 % de las generaciones salía **vacío**. La instrumentación descartó las tres
salidas obvias del bucle (estado sin continuaciones, fin de frase inmediato,
buffer lleno) y ninguna se disparaba, lo que dejaba una sola posibilidad: el
bucle terminaba entero **sin añadir nada**, es decir, alguna «palabra» era la
cadena vacía.

Causa: **`EMBED_TXTFILES` añade un `\0` al final de cada blob.** El tokenizador
lo tomaba por palabra, la internaba, y cuando la cadena la elegía la frase salía
sin nada. Encajaba con la evidencia: los ids afectados eran siempre los últimos
de cada fichero.

Arreglo: cortar también en `\0` y `\r`. Quien embeba texto en un binario de
ESP-IDF y lo tokenice se va a encontrar esto mismo.

## Lo que este spike NO mide

- **Carga desde LittleFS.** Aquí el corpus está embebido; en el firmware real
  vendrá de `/flash/packs/<id>/lines/*.txt`, que añade el coste de abrir y leer
  ficheros (no el de la cadena, que es lo medido aquí).
- **Convivencia con la cara y el WiFi.** Corre solo. En el firmware compartirá
  PSRAM con la caché de ojos y core 0 con la red; a 46 µs por frase hay margen
  de sobra, pero no está medido.
