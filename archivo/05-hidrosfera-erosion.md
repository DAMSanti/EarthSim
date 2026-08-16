> 🗃️ **Documento archivado.** Visión de diseño original, anterior al código. Destilado y corregido en [`docs/REQUISITOS.md`](../docs/REQUISITOS.md) — ver [`LEEME.md`](LEEME.md) para saber dónde acabó cada parte y en qué puntos el destilado lo contradice.

---

# Hidrosfera y Erosión

La erosión crea **suelo fértil** para la vida y **regula el ciclo del carbono** a largo plazo.

## Modelo de Tuberías Virtuales (Pipe Model)

Ideal para GPU por su naturaleza local (basada en celdas vecinas).

### Algoritmo por Paso

```
1. LLUVIA
   → Incrementar capa de agua según precipitación climática

2. CÁLCULO DE FLUJO
   → Cada celda calcula agua saliente hacia vecinas
   → Basado en diferencia de altura hidrostática (terreno + agua)
   → Tuberías virtuales conectan celdas del Cubo Esférico

3. TRANSPORTE DE SEDIMENTOS
   → Calcular Capacidad de Transporte (C)
   → C = f(velocidad del agua, pendiente)

4. EROSIÓN/DEPOSICIÓN
   → Si sedimento < C: Erosionar suelo → Profundizar cauce
   → Si sedimento > C: Depositar → Elevar terreno (deltas, llanuras)

5. EVAPORACIÓN
   → Reducir agua gradualmente
```

## Resultados Emergentes

- Redes fluviales dendríticas naturales
- Cuencas sedimentarias ricas en nutrientes
- Deltas y llanuras aluviales

## Estratigrafía y Composición del Suelo

### Modelo de Capas (Vóxeles 2.5D)

| Capa | Composición | Origen |
|------|-------------|--------|
| **Base (Roca Madre)** | Granito, Basalto | Tectónica |
| **Regolito/Sedimento** | Arena, Limo, Arcilla | Erosión física |
| **Capa Orgánica (Humus)** | Materia orgánica | Descomposición biológica |

### Firma Química de Sedimentos

Cada paquete de sedimento transportado lleva concentraciones de:
- **N** - Nitrógeno
- **P** - Fósforo  
- **K** - Potasio (NPK)

**Resultado:** Los valles de ríos son agrícolamente productivos en la simulación biológica.
