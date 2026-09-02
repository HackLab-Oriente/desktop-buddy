// Generación de frases por cadena de Markov, sobre los bancos por registro.
//
// Medido en placa (spikes/markov-s3): con los 7 bancos combinados son 18,9 KB
// de PSRAM, 14 ms de construcción al arrancar y ~46 us por frase. A ese coste
// no hace falta cachear nada ni pensarlo dos veces.
//
// El orden 2 es el punto de trabajo: conserva la concordancia casi siempre y
// lo que falla es el sentido, no la gramática. Ver spikes/markov-frases.
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Construye las cadenas desde los bancos embebidos. Una vez, al arrancar.
// Devuelve 0 si algo falló; el llamante debe seguir funcionando igual.
int markov_start(void);

// Escribe una frase del registro pedido en `out`. Si `reg` es NULL usa todos
// los bancos combinados (el equivalente a `pool: "registro"` del formato de
// packs). Devuelve la longitud, o 0 si no pudo generar.
//
// No repite ninguna de las últimas frases devueltas — la supresión vive aquí
// dentro porque es lo que hace que el bicho no parezca un loro, y cuesta 9
// reintentos por cada 300 frases.
int markov_say(const char* reg, char* out, size_t cap);

// Nombre del registro i, o NULL si i está fuera de rango. Para recorrerlos.
const char* markov_register(int i);

#ifdef __cplusplus
}
#endif
