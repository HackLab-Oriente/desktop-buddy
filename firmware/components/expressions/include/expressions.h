#pragma once
// Expressions: outputs that consume actions from the bus.
// Actions are events too — "face.emotion", "led.mood" — so reflexes (C++ now,
// Berry later) drive them without linking against any driver.

namespace buddy {

// The face — parametric eyes with blink, saccades, emotions and a text mode.
// The active display backend (SSD1306 OLED or GC9A01 round color) is chosen
// in menuconfig; each backend reads its own pins from Kconfig. Both share the
// emotion model in face_model.h.
// Consumes: face.emotion (any expression NAME in the pack's table — see
//                         face_model.h; the built-ins are neutral, happy,
//                         curious, sleepy, surprised, angry, sad, suspicious)
//           face.say     (short text shown on screen)
//
// Freezes the expression table on the way up: from here the render task reads
// it every frame, so pack_load() has to have run already.
void face_start();

// Mood indicator — the backend (single PWM LED or WS2812 ring) is chosen in
// menuconfig.
// Consumes: led.mood     (any mood NAME in the pack's table — see mood_model.h;
//                         built-ins are calm|excited|thinking|off)
//           face.emotion (ring colour, and the expression's default mood;
//                         a led.mood published afterwards still wins)
//
// Freezes the mood table on the way up, for the same reason as face_start().
void led_start();

}  // namespace buddy
