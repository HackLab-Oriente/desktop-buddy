# Documentación — índice

**Publicado en → [hacklaboriente.org/desktop-buddy](https://hacklaboriente.org/desktop-buddy/)**

Los `.html` son interactivos y autocontenidos: funcionan en la web publicada y
también abriéndolos desde el disco sin internet. Lo que **no** funciona es
verlos desde el navegador de archivos de GitHub — ahí sale el código fuente.
Los `.md` se leen bien en GitHub.

## Para empezar

| Doc | Qué cuenta |
|---|---|
| [../README.md](../README.md) | Qué es el buddy y arranque en 5 minutos |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | Ramas, spikes, commits, idiomas, reglas del bus |
| [architecture.md](architecture.md) | La definición de producto y arquitectura — el documento fundacional |
| [workshops.md](workshops.md) | El plan de las 4 sesiones, con tracks y riesgos |

## Decisiones y reuniones

| Doc | Qué cuenta |
|---|---|
| [reunion-equipos.html](reunion-equipos.html) | **La reunión de equipos**: frases locales (banco vs modelo), las 8 emociones y las capas, equipos y líderes, BOM y cableado |
| [brief-presentacion.md](brief-presentacion.md) | El brief original de presentación al lab |
| [ideas-exploration.html](ideas-exploration.html) | Registro de ideas exploradas: qué se descartó y por qué |

## Referencia técnica

| Doc | Qué cuenta |
|---|---|
| [event-registry.md](event-registry.md) · [.html](event-registry.html) | **El contrato entre equipos**: los 15 eventos del bus, dueños por prefijo, agujeros conocidos |
| [pack-format.md](pack-format.md) | Formato de packs de personalidad (borrador para discusión) |
| [config-api.md](config-api.md) | Esquema de configuración, secretos, aprovisionamiento SoftAP/NFC/QR (propuesta) |
| [hardware.md](hardware.md) | BOM completa, pedidos, sensores, alimentación y montaje |
| [../hardware/](../hardware/) | Guías de cableado por placa (S3 y clásico) |
| [services.md](services.md) | Proveedores externos: LLM, STT, TTS — cuentas y costes |
| [firmware-architecture.html](firmware-architecture.html) | Arquitectura del firmware, interactiva |
| [arquitectura.html](arquitectura.html) | Arquitectura general, interactiva |

## Modelo de IA local

| Doc | Qué cuenta |
|---|---|
| [local-model-bringup.md](local-model-bringup.md) | Paso 0: ¿puede el chip correr un modelo? (sí — medido) |
| [training-workshop.md](training-workshop.md) | **La sesión de entrenamiento**: de cero a un modelo propio, con la escalera de tamaños |
| [param-explorer.html](param-explorer.html) | Explorador interactivo de parámetros para el proyector |
| `spikes/tinylm-s3` (rama `spike/tinylm-s3`) | El kernel optimizado: 31 → 152 tok/s, con cada paso medido |
