# Spike: cadenas de Markov para las frases del buddy

Idea propuesta por un miembro del grupo: en vez de entrenar un modelo, ¿podemos
generar frases recombinando el corpus con una cadena de Markov? Sale
microsegundos en vez de segundos, sin GPU, sin entrenar, y sigue siendo un
fichero de texto que cualquiera edita — que es justo el compromiso
«el comportamiento son datos» del proyecto.

Este spike la mide en vez de opinar.

**Explicación interactiva: [`docs/markov.html`](https://github.com/HackLab-Oriente/desktop-buddy/blob/main/docs/markov.html)**
— el mecanismo paso a paso (estado, candidatas, probabilidades), el grafo del
vecindario, generación por lotes con recuento de copias, y cómo usarlo. Pensada
para proyector.

Vive en `main`, no en esta rama, por dos razones: GitHub Pages solo publica
desde `main`, y con una sola copia no hay dos ficheros que se separen. Está
enlazada desde la portada de la documentación.

## Cómo funciona, en una frase

El **estado** son las últimas *N* palabras (*N* = el orden). El corpus dice qué
palabras siguieron a ese estado y cuántas veces; se elige una al azar ponderada
por esas veces y el estado avanza. No hay más.

## Resultado medido

Corpus de demo: 60 frases, 4 registros, 330 palabras. 200 generaciones por
registro y orden (`python3 markov.py corpus-demo.txt --measure`):

| orden | copias literales | frases nuevas | calidad de las nuevas |
|---|---|---|---|
| 1 | 41–78 % | 126 | rotas — se pierde la concordancia |
| 2 | 78–100 % | **8** | todas gramaticales y en registro |
| 3 | 94–100 % | 2 | buenas, pero casi no hay |

**La predicción falló en la parte interesante.** Se esperaba que el español se
rompiera por concordancia (*la perro contenta*), y con orden 1 pasa exactamente
eso. Pero con **orden 2 los empalmes caen en fronteras de sintagma** y las
frases salen limpias:

```
¿Me trajiste algo o seguimos mirándonos?
Descansa un poquito, yo te espero.
Cuídate mucho, yo te acompaño.
```

El límite real no es la gramática, es el **rendimiento**: 60 frases escritas
dieron 8 nuevas, un 13 %. Y depende del registro de forma instructiva —
`tierno` y `juguetón` recombinan porque comparten trozos («yo te», «con toda
la»); `seco` y `dramático` dieron **cero**, porque frases cortas y muy
distintas no comparten ningún par de palabras donde empalmar.

**Caveat que corta al otro lado:** 60 frases es minúsculo. Los pares
compartidos crecen rápido con el tamaño del corpus, así que 1.000 frases
reales darán bastante más del 13 %. Este spike no lo zanja; `--measure` sobre el
corpus de verdad sí, en segundos.

## La decisión

**Tomada: variedad por encima de corrección.** El grupo acepta que salga una
frase rara de vez en cuando a cambio de que el buddy no repita nunca, así que
**sí se genera dentro del dispositivo**. Este spike aporta los números, no el
veredicto.

Lo que las medidas dicen sobre esa decisión: técnicamente no hay obstáculo
—5.000 frases son ~282 KB de PSRAM y ~26 µs por frase— y el orden 2 ya sale
gramatical casi siempre, así que la «cola» de frases rotas es más fina de lo
que parecía al empezar.

Mandos a la hora de implementarlo, para el pack, no para el firmware:

- **el orden es el mando de riesgo** — 2 conserva la concordancia, 1 inventa
  mucho más y la rompe; configurable por pack para quien quiera caos
- **tope de longitud**, que corta el fallo típico de Markov: la frase que se
  enrolla y no termina
- **supresión de frases recientes**, que sube mucho la variedad percibida con
  muy poca memoria
- **mezcla**: un pack puede tirar de frases aprobadas la mayor parte del tiempo
  y de generación en vivo el resto; el porcentaje es un número en un fichero

**Expandir el corpus fuera de línea sigue mereciendo la pena**, precisamente
porque un corpus más rico también mejora la generación en vivo. Es el mismo
patrón de «generar y curar» que usamos con Claude, salvo que Markov escribe con
la voz *del propio grupo*, porque solo recombina lo que el grupo ya escribió.

```bash
python3 markov.py corpus.txt --measure               # ¿da para recombinar?
python3 markov.py corpus.txt --memory                # coste en el ESP32-S3
python3 markov.py corpus.txt --order 2 --new 50 > propuestas.txt
```

Solo stdlib de Python — sin dependencias, sin entorno virtual.

## Qué cuesta generar dentro del buddy

Coste estimado con `python3 markov.py corpus.txt --memory`. La tabla que un
puerto en C usaría: array **ordenado** de filas `(w1, w2, siguiente)` de 3
uint16 = 6 B, más los offsets del vocabulario; el texto del vocabulario puede
quedarse en flash. Buscar el estado es una búsqueda binaria — sin punteros,
que triplicarían el tamaño y dispersarían los accesos a PSRAM.

| líneas de corpus | RAM | por palabra | frase de 15 palabras |
|---|---|---|---|
| 1.000 | 61 KB | 1,09 µs | **22 µs** |
| 5.000 | 282 KB | 1,28 µs | **26 µs** |
| 20.000 | 1,1 MB | 1,45 µs | **29 µs** |
| 100.000 | 5,2 MB | 1,64 µs | **33 µs** |

Dos cosas que saltan a la vista:

1. **El tiempo es prácticamente constante.** Multiplicar el corpus por 100 sube
   la frase de 22 a 33 µs, porque la búsqueda binaria crece con el logaritmo.
   Comparado con el modelo neuronal en la misma placa (~800–1200 ms por frase),
   Markov es unas **40.000 veces más rápido**. El coste de CPU no es un
   argumento contra esto: es ruido.
2. **La memoria crece lineal con el corpus, no con un modelo.** Un corpus
   realista de 5.000 frases cabe en 282 KB de PSRAM — de los 8 MB de la placa —
   frente a los ~1,2 MB de pesos del modelo. Dicho de otro modo: la cadena
   cuesta más o menos **el doble que guardar el texto**, y a cambio genera.

El tiempo de arranque sí hay que contarlo: construir la tabla es una pasada
sobre el corpus (decenas de ms para miles de líneas), o se precalcula fuera y
se flashea ya ordenada.

**Cómo leer esto:** el coste nunca fue el problema — con un corpus grande la
generación en el dispositivo es trivial, y además la calidad *mejora* con el
tamaño (más pares compartidos = más novedad, y los empalmes siguen cayendo en
fronteras de sintagma). Lo único que quedaba en la balanza era la cola de
frases raras, que es una cuestión de gusto y no de ingeniería. El grupo la ha
resuelto a favor de la variedad.

Nota honesta: estos números son analíticos, no medidos en placa — todavía no
hay puerto en C. Lo que **sí** está medido es la latencia de una lectura
aleatoria de PSRAM (~83 ns, del perfil de `spikes/tinylm-s3`), que es de donde
sale el tiempo por palabra.

## Para la sesión de entrenamiento

Es la rampa de entrada al modelo neuronal: se entiende entero en una hora, la
salida da risa, y deja claro qué añade el modelo. Un modelo de lenguaje hace
*esto mismo* — predecir la siguiente pieza — pero con memoria de toda la frase
y probabilidades aprendidas en vez de contadas. Ver
[training-workshop.md](../../docs/training-workshop.md).

## Ficheros

| fichero | qué es |
|---|---|
| [markov.py](markov.py) | la herramienta: `--measure`, `--memory` y generación de propuestas (código en inglés, como el resto del proyecto) |
| [corpus-demo.txt](corpus-demo.txt) | **datos de prueba** escritos solo para probar el generador — el corpus de verdad lo escribe el grupo |
