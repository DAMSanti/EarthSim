> 🗃️ **Índice archivado** del conjunto de documentos de diseño original. La documentación viva está en [`../docs/`](../docs/); ver [`LEEME.md`](LEEME.md).

---

# 📚 Documentación del Proyecto - Simulación Planetaria

## Índice de Documentos

| # | Documento | Descripción |
|---|-----------|-------------|
| 01 | [Resumen Ejecutivo](./01-resumen-ejecutivo.md) | Visión general y objetivos del proyecto |
| 02 | [Estructura de Datos Espaciales](./02-estructura-datos-espaciales.md) | Cubo Esférico Normalizado y buffers GPU |
| 03 | [Geodinámica y Tectónica](./03-geodinamica-tectonica.md) | Motor de placas tectónicas |
| 04 | [Atmósfera y Clima](./04-atmosfera-clima.md) | Ecuaciones de Aguas Someras y termodinámica |
| 05 | [Hidrosfera y Erosión](./05-hidrosfera-erosion.md) | Modelo de erosión hidráulica |
| 06 | [Climatología y Efecto Invernadero](./06-climatologia-efecto-invernadero.md) | Ciclo del carbono y albedo |
| 07 | [Biosfera y Evolución](./07-biosfera-evolucion.md) | Sistema de agentes evolutivos |
| 08 | [Renderizado y Visualización](./08-renderizado-visualizacion.md) | Estrategia para 60 FPS |
| 09 | [Stack Tecnológico](./09-stack-tecnologico.md) | Herramientas y lenguajes |
| 10 | [Hoja de Ruta](./10-hoja-de-ruta.md) | Cronograma de desarrollo |

---

## 🎯 Objetivo del Proyecto

Crear un **Gemelo Digital planetario** capaz de simular la evolución geológica, atmosférica y biológica de la Tierra desde el eón Hadeico hasta el Antropoceno, con representación gráfica fotorrealista a 60 FPS.

## 🔑 Decisiones Arquitectónicas Clave

1. **Cubo Esférico Normalizado** como estructura de datos principal
2. **Compute Shaders (GPGPU)** para toda la física
3. **Unreal Engine 5** con Nanite para visualización
4. **Tectónica Rasterizada** para eficiencia
5. **Ecuaciones de Aguas Someras** para atmósfera

## ⏱️ Duración Estimada

**12+ meses** divididos en 5 fases principales.

---

## 📖 Documento Original

El documento original completo está disponible en:
- [`../Instrucciones.md`](../Instrucciones.md)

Este incluye todas las referencias académicas y detalles técnicos extendidos.
