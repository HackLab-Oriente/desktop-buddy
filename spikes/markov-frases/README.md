# Spike: cadenas de Markov para las frases del buddy

Idea propuesta por un miembro del grupo: en vez de entrenar un modelo, ¿podemos
generar frases recombinando el corpus con una cadena de Markov? Sale
microsegundos en vez de segundos, sin GPU, sin entrenar, y sigue siendo un
fichero de texto que cualquiera edita — que es justo el compromiso
«el comportamiento son datos» del proyecto.

Este spike la mide en vez de opinar.

**Explicación interactiva: [markov.html](markov.html)** — el mecanismo paso a
paso (estado, candidatas, probabilidades), generación por lotes con recuento
de copias, y cómo usarlo. Pensada para proyector.

## Cómo funciona, en una frase

El **estado** son las últimas *N* palabras (*N* = el orden). El corpus dice qué
palabras siguieron a ese estado y cuántas veces; se elige una al azar ponderada
por esas veces y el estado avanza. No hay más.

## Resultado medido

Corpus de demo: 60 frases, 4 registros, 330 palabras. 200 generaciones por
registro y orden (`python3 markov.py corpus-demo.txt --medir`):

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
reales darán bastante más del 13 %. Este spike no lo zanja; `--medir` sobre el
corpus de verdad sí, en segundos.

## Recomendación

**No generar dentro del buddy.** Un orden bajo puede soltar una frase rota, y
una frase rota rompe el personaje mucho más que una repetida: un buddy
repetitivo tiene muletillas, uno agramatical parece un bug.

**Sí usarlo fuera de línea para expandir el corpus, con revisión humana.**
Nada llega al dispositivo sin que una persona lo haya leído, y la variedad sale
gratis — sin modelo, sin entrenar, sin coste en ejecución. Es el mismo patrón
de «generar y curar» que usamos con Claude, salvo que Markov escribe con la voz
*del propio grupo*, porque solo recombina lo que el grupo ya escribió.

```bash
python3 markov.py corpus.txt --medir                 # ¿da para recombinar?
python3 markov.py corpus.txt --orden 2 --nuevas 50 > propuestas.txt
```

Solo stdlib de Python — sin dependencias, sin entorno virtual.

## Para la sesión de entrenamiento

Es la rampa de entrada al modelo neuronal: se entiende entero en una hora, la
salida da risa, y deja claro qué añade el modelo. Un modelo de lenguaje hace
*esto mismo* — predecir la siguiente pieza — pero con memoria de toda la frase
y probabilidades aprendidas en vez de contadas. Ver
[training-workshop.md](../../docs/training-workshop.md).

## Ficheros

| fichero | qué es |
|---|---|
| [markov.html](markov.html) | explicación interactiva, autocontenida, para proyector |
| [markov.py](markov.py) | la herramienta: `--medir` y generación de propuestas |
| [corpus-demo.txt](corpus-demo.txt) | **datos de prueba** escritos solo para probar el generador — el corpus de verdad lo escribe el grupo |
