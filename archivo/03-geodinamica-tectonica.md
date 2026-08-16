> 🗃️ **Documento archivado.** Visión de diseño original, anterior al código. Destilado y corregido en [`docs/REQUISITOS.md`](../docs/REQUISITOS.md) — ver [`LEEME.md`](LEEME.md) para saber dónde acabó cada parte y en qué puntos el destilado lo contradice.

---

# Geodinámica: Motor de Tectónica de Placas

La topografía debe ser una **propiedad emergente** de la interacción dinámica de placas tectónicas.

## Algoritmo de Tectónica Procedimental

### Génesis de la Corteza (Hadeico)

1. **Fracturación inicial:** Algoritmo de Voronoi Esférico o Poisson Disc Sampling
2. **Asignación de placas:** Jump Flooding Algorithm (JFA) paralelo en GPU
3. **Resultado:** Mosaico de placas cubriendo el planeta

### Propiedades de cada Placa

```
- ID de Placa: Identificador único (Integer)
- Polo de Euler: Vector 3D del eje de rotación
- Velocidad Angular: Rotación alrededor del polo
- Tipo de Corteza: Oceánica (basalto) o Continental (granito)
```

## Dinámica de Movimiento

### Actualización por Paso de Tiempo

1. Aplicar rotación de cuaterniones basada en Polo de Euler
2. Calcular vectores de velocidad para celdas de borde
3. Detectar interacciones comparando IDs de placas adyacentes

## Interacciones de Frontera

### Límites Convergentes (Choque)

| Tipo | Resultado |
|------|-----------|
| **Subducción** (Oceánica vs Continental) | Vulcanismo, cordillera tipo Andes |
| **Orogenia** (Continental vs Continental) | Elevación drástica (Himalayas) |

### Límites Divergentes (Separación)

- Creación de nueva corteza oceánica
- Altura base baja (Dorsales Oceánicas)
- Temperatura alta inicial
- Expansión del fondo oceánico

### Límites Transformantes

- Generan fricción y terremotos
- No crean ni destruyen corteza significativamente

## Ciclo de Wilson

Formación y ruptura cíclica de supercontinentes (como Pangea). Las placas deben cambiar velocidad y dirección periódicamente simulando corrientes de convección del manto.

## Implementación: Tectónica Rasterizada

### Mapas en GPU

1. **Mapa de IDs de Placa:** Qué placa ocupa cada píxel
2. **Mapa de Elevación Local:** Altura relativa a la placa

### Algoritmo por Frame

```
Compute Shader:
  - Mover píxeles aplicando rotación de placa
  - Solapamiento (colisión) → Sumar elevación (montañas)
  - Huecos (divergencia) → Asignar ID "Corteza Nueva"
```

**Ventaja:** Maneja topología cambiante de forma implícita y eficiente en memoria.
