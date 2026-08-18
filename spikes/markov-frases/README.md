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

## Prueba a escala: CESS-ESP (192.686 palabras)

El caveat de arriba —«60 frases es minúsculo, con más saldrá mejor»— ya no hace
falta creérselo. Corriendo lo mismo sobre [CESS-ESP](https://mailman.uib.no/public/corpora/2007-October/005448.html),
un corpus real de español de prensa (5.972 frases), con
`python3 cess_scale_test.py`:

**Novedad con orden 2, según el tamaño del corpus (solo frases de ≤12 palabras,
que es nuestro caso):**

| frases | novedad | nuevas distintas |
|---|---|---|
| 60 | 12,8 % | 32 |
| 200 | 22,7 % | 89 |
| 920 | **44,7 %** | 245 |

Tres cosas que esto zanja:

1. **Escribir más frases sí paga, y mucho.** De 60 a 920 frases la novedad se
   multiplica por 3,5. A ~1.000 frases, casi la mitad de lo que diga el buddy
   será una combinación que nadie escribió. Y cuesta 61 KB de RAM.
2. **Nuestras medidas del corpus de demo eran correctas.** 12,8 % con 60 frases
   cortas de CESS contra el 10–13 % que medimos con 60 frases nuestras: el
   número no era un artefacto del corpus de juguete, es lo que dan las frases
   cortas.
3. **El orden 3 está muerto para nuestro caso**: 0 % con 60 frases, 1,5 % con
   920. Orden 2 es el punto de trabajo, y por eso es el defecto en el formato de
   packs.

### La trampa que este experimento evita

Con las frases **completas** de CESS (30 palabras de media) el orden 2 da
**76,8 % de novedad ya con 60 frases**, y 96,3 % con el corpus entero. Si
hubiéramos medido solo eso, la conclusión habría sido «Markov es
generativísimo» — y sería falsa para nosotros. **A igualdad de corpus, la
longitud de frase importa más que el tamaño**: 76,8 % contra 12,8 % con las
mismas 60 frases. Frases largas comparten muchos bigramas de función («de la»,
«en el») y empalman por todas partes; las cortas casi no comparten nada.

### Y lo que se rompe no es la gramática

Muestras reales con orden 2 sobre frases cortas de CESS:

```
Pero Germán de Granda es un baño turco.
Acabar de una vez con el mar Negro.
Al lector de hoy elevó a Guga a otro nivel.
```

**La concordancia aguanta.** Lo que falla es el *sentido*: frases bien
construidas que dicen disparates. Eso cambia el riesgo que el grupo aceptó, y a
mejor: una frase agramatical parece un bug, pero una frase bien escrita que
dice algo absurdo parece **surrealismo**, que en un bicho de escritorio pasa por
personalidad.

Además, aquí el disparate viene de que CESS mezcla política, deportes y
finanzas, con nombres propios por todas partes. **Un corpus del buddy es
semánticamente estrecho** —todo son reacciones a una persona— así que los
empalmes se quedan dentro del mismo tema. Nuestro caso es más benigno que esta
prueba.

### Qué NO prueba esto

CESS es prensa, no diálogo: otro registro, otro vocabulario, y llena de nombres
propios que nosotros no tendremos. Filtrar a frases cortas acerca la forma, no
el contenido. Un proxy mejor sería diálogo real —subtítulos o Tatoeba en
español—, que se parece a cómo habla el buddy. Pendiente si alguien quiere
afinar el número.

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
| [cess_scale_test.py](cess_scale_test.py) | prueba a escala sobre CESS-ESP (necesita `nltk`) |
| [markov.py](markov.py) | la herramienta: `--measure`, `--memory` y generación de propuestas (código en inglés, como el resto del proyecto) |
| [corpus-demo.txt](corpus-demo.txt) | **datos de prueba** escritos solo para probar el generador — el corpus de verdad lo escribe el grupo |
