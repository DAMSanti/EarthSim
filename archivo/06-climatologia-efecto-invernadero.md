> 🗃️ **Documento archivado.** Visión de diseño original, anterior al código. Destilado y corregido en [`docs/REQUISITOS.md`](../docs/REQUISITOS.md) — ver [`LEEME.md`](LEEME.md) para saber dónde acabó cada parte y en qué puntos el destilado lo contradice.

---

# Climatología Profunda: Efecto Invernadero

El planeta debe reaccionar a cambios a largo plazo de forma científicamente correcta.

## Ciclo del Carbono Geológico

El termostato de la Tierra a largo plazo es el **ciclo de silicatos-carbonatos**.

### Fuentes y Sumideros

| Proceso | Efecto | Mecanismo |
|---------|--------|-----------|
| **Volcanes** | Calentamiento | Emiten CO₂ a la atmósfera |
| **Erosión de silicatos** | Enfriamiento | Lluvia + granito → secuestra CO₂ |

### Lógica de Simulación

```
SI orogenia masiva (muchas montañas):
   → Erosión aumenta
   → CO₂ baja
   → Planeta se enfría

SI actividad volcánica alta:
   → CO₂ sube
   → Planeta se calienta
```

## Albedo Dinámico

### Fórmula de Albedo Total

$$\text{Albedo Total} = \text{Albedo}_{\text{agua}} \times \%_{\text{agua}} + \text{Albedo}_{\text{hielo}} \times \%_{\text{hielo}} + \text{Albedo}_{\text{tierra}} \times \%_{\text{tierra}}$$

### Valores de Albedo

| Superficie | Albedo |
|------------|--------|
| Hielo | ~0.8 (alto) |
| Agua | ~0.06 (bajo) |
| Tierra | ~0.1-0.4 (variable) |

## Feedback Ice-Albedo (Retroalimentación Positiva)

```
Planeta se enfría
    ↓
Hielo avanza desde polos
    ↓
Albedo aumenta (más reflexión)
    ↓
Menos energía solar absorbida
    ↓
Planeta se enfría más
    ↓
... (ciclo continúa)
```

### Eventos Simulables

- **Glaciaciones globales**
- **"Snowball Earth"** (Tierra bola de nieve)
- **Periodos interglaciares**
