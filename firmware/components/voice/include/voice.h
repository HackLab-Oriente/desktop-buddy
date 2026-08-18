// Leer frases en voz alta con la API de OpenAI, por el MAX98357A.
//
// La clave se pone en menuconfig -> Buddy Zero -> OpenAI API key. Sin clave el
// componente no arranca y el resto del firmware sigue igual: hablar es un
// extra, nunca un requisito ("nunca un ladrillo").
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Reserva el buffer, abre el ampli en mudo y lanza la tarea de voz.
// Devuelve 0 si no hay clave o no hay memoria; el llamante sigue como si nada.
int voice_start(void);

// Encola una frase. NO bloquea: se puede llamar desde un manejador del bus,
// que es justo lo que exige la convención 4 del registro de eventos. Si la
// cola está llena la frase se descarta, para que lo que se oye no se separe
// de lo que se ve.
void voice_say(const char* text);

#ifdef __cplusplus
}
#endif
