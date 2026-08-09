# Máscara del OLED — histórico

**Esto no es parte del buddy actual.** Es la máscara imprimible para la
pantalla SSD1306 monocroma de 128×64 de la prueba de concepto original sobre
ESP32 clásico. Ese backend se retiró del firmware: hoy ambos targets
(ESP32-S3 y ESP32 clásico) usan la misma pantalla redonda GC9A01 a color.

Se conserva porque la máscara imprime bien y alguien podría querer el PoC
mono para otra cosa. Para el buddy de verdad, usa
[`../round-mask/`](../round-mask/).

El firmware de aquella PoC vive en el historial de git; el commit que retiró
el backend explica por qué.
