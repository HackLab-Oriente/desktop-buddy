# Spikes — experimentos desechables

Un spike es un proyecto pequeño y aislado para responder UNA pregunta con
números, barato de tirar si la respuesta es «no». La disciplina:

1. Rama `spike/<nombre>`, proyecto propio en `spikes/<nombre>/`.
2. El spike **no toca `firmware/`** — el buddy que funciona nunca está en
   riesgo.
3. A `main` solo se mergean las **conclusiones** (README con números medidos
   o un doc en `docs/`), no el código.
4. Un «no» bien argumentado es un resultado válido y valioso.

## Los que existen

- **`lovyangfx-gc9a01/`** (en `main`) — ¿qué librería gráfica? Respuesta:
  LovyanGFX para panel/sprites/fuentes + nuestro renderer SDF para los ojos,
  con caché. Registra las trampas (endianness de sprites, el coste real del
  renderer) que luego evitaron días de depuración.
- **`tinylm-s3/`** (rama `spike/tinylm-s3`) — ¿puede el S3 generar frases con
  un modelo propio? Respuesta: sí — 152 tok/s tras optimizar el kernel
  (4,85×), con cada paso medido y commiteado. Sus conclusiones están en
  [../docs/local-model-bringup.md](../docs/local-model-bringup.md) y
  [../docs/training-workshop.md](../docs/training-workshop.md).
- **`claude-desktop-buddy/`** (rama `claude/desktop-buddy-spike-kshx2i`) — ¿qué
  del buddy de Anthropic nos sirve? Respuesta: **el protocolo**, no el
  proyecto. Su puente BLE entra como un Sense (`claude.*`) y trae con él un
  decodificador probado — 158 comprobaciones bajo sanitizers — más media
  docena de lecciones sobre NVS, instalación de packs y animación barata.
  Falta el número que importa: si BLE cabe junto a WiFi + TLS en el S3.
