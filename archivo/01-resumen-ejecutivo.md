> 🗃️ **Documento archivado.** Visión de diseño original, anterior al código. Destilado y corregido en [`docs/REQUISITOS.md`](../docs/REQUISITOS.md) — ver [`LEEME.md`](LEEME.md) para saber dónde acabó cada parte y en qué puntos el destilado lo contradice.

---

# Resumen Ejecutivo - Simulación Planetaria

## Visión del Proyecto

**Objetivo:** Crear un "Digital Twin" (Gemelo Digital) de escala planetaria capaz de simular la evolución geológica, atmosférica y biológica de la Tierra desde el eón Hadeico hasta el Antropoceno.

## Características Principales

- **Simulación en tiempo real** a 60 FPS con representación gráfica fotorrealista
- **Alta fidelidad científica** como sustrato para experimentos de evolución biológica
- **Escalas de tiempo integradas:**
  - Deriva continental: millones de años
  - Dinámica atmosférica: segundos
  - Evolución biológica: generaciones

## Enfoque Técnico

| Aspecto | Solución |
|---------|----------|
| Arquitectura | Híbrida basada en física fundamental |
| Procesamiento | Compute Shaders (GPGPU) |
| Estructura de datos | Cubo Esférico Normalizado |
| Motor gráfico | Unreal Engine 5 modificado |
| Geometría | Nanite (geometría virtualizada) |
| Agentes biológicos | Sistema Niagara |

## Diferenciador Clave

Se descarta el enfoque tradicional de "trucaje visual" de videojuegos en favor de **resolución de ecuaciones diferenciales parciales** sobre la estructura del Cubo Esférico, eliminando distorsiones polares.
