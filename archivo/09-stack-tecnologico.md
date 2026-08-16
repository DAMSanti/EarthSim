> 🗃️ **Documento archivado.** Visión de diseño original, anterior al código. Destilado y corregido en [`docs/REQUISITOS.md`](../docs/REQUISITOS.md) — ver [`LEEME.md`](LEEME.md) para saber dónde acabó cada parte y en qué puntos el destilado lo contradice.

---

# Stack Tecnológico

## Motor de Juego Principal: Unreal Engine 5

### ✅ Ventajas

| Tecnología | Beneficio |
|------------|-----------|
| **Nanite** | Geometría virtualizada sin coste de CPU |
| **Lumen** | Iluminación global dinámica |
| **Niagara** | Simulación masiva de agentes biológicos en GPU |
| **Calidad visual** | Mejor "out of the box" |

### ⚠️ Consideraciones

- Requiere conocimientos sólidos de **C++**
- Integración de bibliotecas de simulación de fluidos requiere trabajo

### Plugins Recomendados

| Plugin | Uso |
|--------|-----|
| **Voxel Plugin** | Terreno volumétrico (cuevas) |
| **Unreal Engine Python** | Scripts de lógica científica |

## Alternativa: Unity 6 (HDRP + Compute Shaders)

### ✅ Ventajas

- Mayor facilidad para Compute Shaders personalizados (HLSL)
- Mejor estructura de datos científicos
- **DOTS** ideal para simulaciones masivas CPU/GPU

### ❌ Desventajas

- Sistema de terreno planetario desde cero
- Sin equivalente nativo a Nanite para esferas deformables

## Lenguajes y Librerías

### Núcleo de Simulación

| Componente | Tecnología |
|------------|------------|
| **Física principal** | C++ / HLSL |
| **Tectónica** | Compute Shaders GPU |
| **Fluidos** | Compute Shaders GPU |
| **Erosión** | Compute Shaders GPU |

### Estructuras de Datos Clave

```cpp
class CubeSphereGrid {
    // Manejo de adyacencia entre caras del cubo
    // Correcciones de distorsión métrica
    // Acceso a vecinos en bordes de caras
};
```

## Requisitos de Hardware (Estimados)

| Componente | Mínimo | Recomendado |
|------------|--------|-------------|
| **GPU** | RTX 3070 / RX 6800 | RTX 4080+ / RX 7900 XT |
| **VRAM** | 8 GB | 16+ GB |
| **RAM** | 32 GB | 64 GB |
| **CPU** | 8 cores | 16+ cores |
