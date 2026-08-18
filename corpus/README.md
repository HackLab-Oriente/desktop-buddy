# Corpus semilla — material para cortar, no la voz del bicho

**Estado: borrador sin curar.** Lo escribió Claude como punto de partida para
que el equipo de personalidad tenga algo concreto que romper en la sesión, no
como la personalidad del buddy. El corpus *es* el carácter: eso lo decide el
grupo (#16, #19), no un generador.

- [`semilla-registros.txt`](semilla-registros.txt) — 371 frases en los 7
  registros propuestos en [`../docs/pack-format.md`](../docs/pack-format.md).
- Formato `registro: frase`, que es lo que leen `markov.py` y la
  [página interactiva](../docs/markov.html).

Cuando el grupo cierre las **expresiones**, estas frases se reparten en
`lines/<expresión>.txt` dentro del pack. El registro es el vocabulario cerrado;
la expresión la inventa quien escribe el pack.

## Lo que midió escribirlo

Se escribió en dos tandas, y la segunda existe porque la primera falló de una
forma que conviene no repetir.

**Primera tanda (300 frases):** arranques repetidos dentro de cada registro
(«Ahí estás.» / «Ahí estás, por fin.»), pensando que eso daría a Markov por
dónde empalmar. **Dio 30 frases nuevas en total.** El error: para empalmar hace
falta compartir el **medio** de la frase, no el principio. Con arranques
compartidos la cadena solo elige entre las continuaciones que ya existían.

**Segunda tanda (+71 frases largas):** trozos compartidos a mitad de frase
(«que no me voy a ninguna parte», «se me ha ido», «que conste en acta»),
insertados en frases con principios y finales distintos. **75 frases nuevas,
2,5× más, con solo un 24 % más de corpus.**

### El umbral de longitud es real

| registro | palabras/frase | nuevas con orden 2 |
|---|---|---|
| juguetón | 6,4 | 36 |
| cálido | 6,3 | 17 |
| curioso | 5,5 | 16 |
| soñoliento | 5,0 | 6 |
| urgente | 3,1 | **0** |
| seco | 2,4 | **0** |
| llano | 2,0 | **0** |

**Por debajo de ~4 palabras por frase, Markov con orden 2 da exactamente cero.**
No hay medio de frase donde empalmar.

**Y eso no es un fallo que arreglar.** `seco` es «mínimo, retiene», `urgente` es
«corto, reclama atención ya», `llano` es «informa y punto». Alargarlos para
alimentar al algoritmo sería doblar el carácter para que encaje en la
herramienta. Esos tres van como **banco de frases puro** (`mix: 0.0`), y es la
razón concreta de que el formato de packs permita configurar `order` y `mix`
por expresión en vez de tener un valor global.

### Coste en la placa

371 frases → **14,6 KB de RAM** y ~18 µs por frase. Nada.

### Muestras de lo que genera hoy

```
Me gusta cuando te quedas un rato más, que no me voy a ninguna parte.
Estaba pensando en algo importante y se me ha ido el santo al cielo.
No lo entiendo del todo, y mira que miro cosas.
Venga, hazlo otra vez que me estaba aburriendo.
```

Gramaticales y en registro, como predijo la prueba con CESS-ESP: con orden 2 lo
que se rompe es el sentido, no la concordancia — y en un corpus estrecho como
este, ni siquiera eso.

## Cómo seguir

```bash
python3 spikes/markov-frases/markov.py corpus/semilla-registros.txt --measure
python3 spikes/markov-frases/markov.py corpus/semilla-registros.txt --memory
```

O arrastra el fichero a la [página interactiva](../docs/markov.html), que hace
lo mismo sin instalar nada y dibuja la curva de novedad frente al tamaño.

**Escribir más frases es lo que más rinde.** Medido sobre CESS-ESP: 60 frases
cortas dan 12,8 % de novedad, 200 dan 22,7 %, 920 dan 44,7 %. Estamos en 371.

## Decisiones abiertas que este fichero da por supuestas

- **Los siete registros** son la propuesta de `pack-format.md`, no una decisión
  (#16, #19). Si el juego cambia, este fichero se reorganiza.
- **El español es peninsular**, siguiendo los ejemplos del propio
  `pack-format.md` («Te he echado de menos», «¿Otra vez tú?»). Si el grupo
  quiere marca regional propia, es una decisión de identidad y toca reescribir
  bastante — mejor decidirlo antes de escribir las siguientes 500.
- **Nada de emoji ni caracteres fuera de Latin-1**: la fuente de la pantalla
  cubre `U+0000`–`U+00FF`.
