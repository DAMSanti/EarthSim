# Estructura de Datos Espaciales

## El Problema de la Esfera

La representación de una esfera en memoria presenta un problema topológico fundamental: **no es posible mapear una esfera a un plano rectangular sin distorsión** (Teorema Egregium de Gauss).

### Problema de la Esfera UV Tradicional

Los polos crean celdas infinitamente estrechas, requiriendo pasos de tiempo minúsculos para estabilidad (condición CFL), destruyendo el rendimiento.

## Comparativa de Estructuras Geodésicas

| Estructura | Uniformidad | Adyacencia | GPU | Veredicto |
|------------|-------------|------------|-----|-----------|
| Esfera UV | Pésima (Singularidad Polar) | Variable | Alta | ❌ Descartada |
| Icosfera | Excelente | 12 Pentágonos | Baja | ⚠️ Solo Agentes |
| HEALPix | Perfecta (Iso-Area) | Compleja | Media | ⚠️ Solo científico |
| **Cubo Esférico** | Buena (<50% distorsión) | Regular (4 vecinos) | Óptima | ✅ **Seleccionado** |

## Cubo Esférico Normalizado

### Arquitectura

Proyecta las 6 caras de un cubo sobre la esfera, normalizando los vectores de posición. Aprovecha la arquitectura de **Cube Maps** de GPUs modernas.

### Corrección de Distorsión

- Precalcular mapa de "factores de escala métrica"
- Multiplicar ecuaciones de flujo por este factor
- Garantiza conservación de masa

## Estrategia de Datos en GPU

Almacenamiento en **Texture Arrays** de alta precisión (`R32G32B32A32_FLOAT`):

### Buffer Geológico
- Altura del lecho rocoso
- Densidad de la corteza (tipo de roca)
- Espesor de la corteza

### Buffer Hidrológico
- Nivel del agua
- Velocidad del flujo (vector 2D)
- Sedimentos suspendidos
- Salinidad

### Buffer Ecológico
- Nivel de Nitrógeno/Fósforo en suelo
- Biomasa vegetal
- Humedad del suelo
- Temperatura superficial
