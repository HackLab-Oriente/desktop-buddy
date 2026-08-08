# Servicios de terceros: qué necesitamos y recomendaciones

Precios verificados el 2026-07-13. El contrato de Brain mantiene todo esto
intercambiable — son valores por defecto, no compromisos. Todas las claves
viven en la NVS del dispositivo, se meten por la web UI y nunca van en packs.

## Servicios que necesita el buddy

| Servicio | Para qué | Cuándo | ¿Cuenta? |
|---|---|---|---|
| **LLM** (el Brain) | Personalidad, conversación, elección de emoción | v1 (M2) | Sí |
| **STT** (voz a texto) | Entrada de voz push-to-talk | v1 (M3) | Sí |
| **TTS** (texto a voz) | Respuestas habladas | v1 (M3) | Sí |
| Hora NTP | Reloj, timers, comportamientos de reposo | v1 | No (pool.ntp.org, gratis) |
| Calendario/feeds por polling | Senses opcionales (ICS, RSS) | v2 | No (URLs públicas o con token) |
| Relay de webhooks / túnel | Eventos entrantes sin IP pública | v2 (era del hub) | Capas gratuitas (Cloudflare Tunnel, Tailscale) |

## LLM — el Brain

| Proveedor / modelo | Precio (in/out por MTok) | Por qué sí / por qué no |
|---|---|---|
| **Anthropic Claude Haiku 4.5** ⭐ | $1 / $5 | Rápido, barato y con carácter — para un compañero, la calidad de la personalidad ES el producto. Streaming + prompt caching (el system prompt = personalidad se cachea de maravilla). |
| OpenAI clase gpt-4o-mini | ~$0.60 / $2.40 | Algo más barato; su atractivo real es la comodidad de una sola cuenta (ver stacks abajo). |
| Anthropic Claude Sonnet 5 | $3 / $15 ($2/$10 intro) | Sobredimensionado para charla ligera; interesante después para Skills/herramientas en el hub. |

Una interacción típica ≈ 1.200 tokens de entrada + 150 de salida →
**~$0.002 con Haiku**, y menos con caché sobre el prompt de personalidad.

## STT — de voz a texto

La clave: **push-to-talk elimina la necesidad de STT en streaming en v1.**
Soltar el botón = fin de la frase, así que el dispositivo puede hacer un POST
HTTPS con el clip completo (unos segundos de WAV) y recibir el texto. Eso es
radicalmente más simple en el ESP32 que un WebSocket de streaming — un POST
multipart con `esp_http_client` y ya.

| Proveedor | Precio | Modo | Encaje en ESP32 |
|---|---|---|---|
| **Groq Whisper large-v3-turbo** ⭐ | $0.04/hora (~$0.0007/min), facturación mínima de 10 s | POST por lotes, ~216× tiempo real (un clip de 5 s se transcribe en mucho menos de un segundo) | Perfecto para PTT: un solo POST multipart. Capa gratuita generosa — ideal para un hacklab. |
| OpenAI gpt-4o-mini-transcribe | ~$0.003/min | POST por lotes (existe variante realtime) | Misma simplicidad; ~4× el precio de Groq, sinergia de una sola cuenta. |
| Deepgram Nova-3 | $0.0048/min streaming | Streaming por WebSocket, protocolo muy simple | La vía de mejora para v2: hará falta cuando llegue la wake word y los límites de frase se detecten en el servidor. |

## TTS — de texto a voz

Restricción: el ESP32 quiere **PCM o MP3 en streaming de vuelta** (decodificar
MP3 con libhelix/minimp3 es barato; PCM crudo a 16k/22k es aún más fácil).
Los tres de abajo pueden dar ambos.

| Proveedor | Precio | Por qué sí / por qué no |
|---|---|---|
| **OpenAI gpt-4o-mini-tts** ⭐ | ~$0.015/min de audio ($0.60/MTok texto de entrada, $12/MTok audio de salida) | Barato, buena calidad, ~13 voces, admite "instrucciones" de voz (tono/carácter — útil para packs de personalidad). |
| ElevenLabs Flash v2.5 | $0.05/1k caracteres (≈$0.0075 por respuesta de 150) | Las mejores voces con carácter + clonado — la opción "dale a tu buddy una voz única". ~3× el coste; vale la pena si la identidad de voz se vuelve central en los packs. |
| Deepgram Aura-2 | ~$0.03–0.05/1k caracteres | Baja latencia, API simple; atractivo de bundle si ya estás en Deepgram para STT. |

## Coste por interacción (el número que importa)

Un intercambio PTT ≈ 8 s de voz del usuario + respuesta hablada de ~150
caracteres:

| Pieza | Stack recomendado | Coste |
|---|---|---|
| STT (Groq) | 8 s a $0.04/h (mín. 10 s) | ~$0.0001 |
| LLM (Haiku 4.5) | ~1,2k in / 150 out | ~$0.002 |
| TTS (OpenAI mini-tts) | ~10 s de audio | ~$0.0025 |
| **Total** | | **≈ medio centavo** |

Con un uso intenso de 50 interacciones/día: **~$7/mes por buddy**. Las
interacciones solo-chirp (reflejos, gestos) no cuestan nada; el chat de texto
por web solo paga la parte del LLM.

## Stacks recomendados

- **Mejor calidad/precio (recomendado):** Claude Haiku 4.5 (cerebro) + Groq
  Whisper (STT) + OpenAI mini-tts (TTS). Tres cuentas con capa gratuita; cada
  pieza es la mejor de su columna.
- **Menos cuentas (para amigos):** solo OpenAI — gpt-4o-mini (cerebro) +
  gpt-4o-mini-transcribe + gpt-4o-mini-tts. Una sola clave que pegar en la
  web UI. La web debería soportar ambos presets; el contrato de Brain hace
  que el proveedor sea un desplegable, no un fork.
- **Mejora de voz con carácter:** cambia el TTS a ElevenLabs Flash v2.5
  cuando un pack quiera una voz distintiva.

## Notas de implementación

- **El streaming de la respuesta del LLM importa más que el del STT.**
  Streamea la respuesta de Haiku para que la cara/emoción reaccione según
  llega el texto, y el TTS pueda empezar con la primera frase — ahí se gana
  la latencia percibida.
- **Prompt caching:** mantén el system prompt de personalidad estable byte a
  byte para que la caché de Anthropic recorte ~90% del coste de entrada en
  interacciones repetidas.
- Todos los proveedores de aquí son TLS + token bearer sobre HTTPS/WebSocket
  — todo amigable para el ESP32; no hace falta baile OAuth en v1 (las
  integraciones OAuth-osas son exactamente para lo que existe el hub de v2).

### Fuentes

- [Precios de Deepgram](https://deepgram.com/pricing) · [Desglose de tarifas Nova-3](https://brasstranscripts.com/blog/deepgram-pricing-per-minute-2025-real-time-vs-batch)
- [Groq Whisper large-v3-turbo](https://groq.com/blog/whisper-large-v3-turbo-now-available-on-groq-combining-speed-quality-for-speech-recognition) · [Guía de precios de Groq](https://www.eesel.ai/blog/groq-pricing)
- [Precios de transcripción de OpenAI](https://costgoat.com/pricing/openai-transcription) · [Precios de gpt-4o-mini-tts](https://tokenmix.ai/blog/gpt-4o-mini-tts-cheapest-tts-api-2026)
- [Precios de la API de ElevenLabs](https://elevenlabs.io/pricing/api)
- Precios de modelos Anthropic: docs de la Claude API (Haiku 4.5 $1/$5, Sonnet 5 $3/$15 por MTok)
