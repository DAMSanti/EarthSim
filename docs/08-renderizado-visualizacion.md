# Estrategia de Visualización y Renderizado (60 FPS)

Para lograr representación "atractiva y fluida con texturas llamativas".

## Principio Clave

**Desacoplar** la resolución de simulación de la resolución de renderizado.

## Geometría Virtualizada (Nanite)

### Tecnología: Unreal Engine 5 Nanite

Renderiza geometría con detalle de polígonos casi infinito.

### Implementación

1. Generar malla base de Cubo Esférico de alta resolución
2. Usar **World Position Offset (WPO)** en shader
3. Textura de altura de simulación geológica → desplaza vértices Nanite
4. UE 5.3+ soporta desplazamiento de Nanite eficiente

### Resultado

Ver montañas crecer y valles erosionarse con **detalle geométrico real**, no solo mapas normales.

## Texturizado Procedimental

### Técnicas

| Técnica | Propósito |
|---------|-----------|
| **Triplanar Mapping** | Evitar distorsiones en pendientes verticales |
| **Splat Maps Dinámicos** | Mezcla reactiva de materiales |

### Lógica de Splat Maps

```hlsl
// El shader de píxeles lee datos de simulación
float humedad = SampleTexture(HumedadMap);
float pendiente = CalcularPendiente();

// Mezcla de materiales PBR
Material = lerp(Arena, Hierba, humedad);
Material = lerp(Material, Roca, pendiente);
```

### Materiales Recomendados

**Quixel Megascans** - Materiales PBR de alta calidad

## Atmósfera Volumétrica

### Componente: Sky Atmosphere (Unreal Engine)

Configurar con parámetros de:
- **Dispersión de Rayleigh:** Cielo azul, atardeceres rojos
- **Dispersión de Mie:** Neblina, bruma

### Sincronización

Parámetros deben coincidir con la **densidad atmosférica simulada** para lograr cielos físicamente correctos.

## Pipeline de Renderizado

```
Simulación (GPU Compute)
    ↓
Texturas de Estado del Planeta
    ↓
Deformación de Malla Nanite (WPO)
    ↓
Splat Maps → Materiales PBR
    ↓
Iluminación Global (Lumen)
    ↓
Atmósfera Volumétrica
    ↓
Output a 60 FPS
```
