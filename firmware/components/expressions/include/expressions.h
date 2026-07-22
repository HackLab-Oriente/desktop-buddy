#pragma once
// Expressions: outputs that consume actions from the bus.
// Actions are events too — "face.emotion", "led.mood" — so reflexes (C++ now,
// Berry later) drive them without linking against any driver.

namespace buddy {

// Proto-face on an SSD1306-class 128×64 I2C OLED (GME12864 et al).
// Two parametric eyes: blink loop, saccades, emotion states.
// Consumes: face.emotion (payload: neutral|happy|curious|sleepy|surprised)
void oled_face_start(int gpio_sda, int gpio_scl);

// Mood LED via PWM breathing (onboard LED or external + resistor).
// Consumes: led.mood (payload: calm|excited|thinking|off)
void led_mood_start(int gpio_led);

}  // namespace buddy
