> 🗃️ **Documento archivado.** Visión de diseño original, anterior al código. Destilado y corregido en [`docs/REQUISITOS.md`](../docs/REQUISITOS.md) — ver [`LEEME.md`](LEEME.md) para saber dónde acabó cada parte y en qué puntos el destilado lo contradice.

---

# Biosfera: Experimentos de Evolución Biológica

El objetivo final es usar este mundo como laboratorio de evolución.

## Arquitectura de Agentes en GPU

Para escala planetaria: **Compute Shaders** o frameworks como **FLAME GPU** para millones de agentes simultáneos.

### Genoma Vectorial de Agentes

Cada agente (o "super-individuo" representando una población) tiene:

| Categoría | Atributos |
|-----------|-----------|
| **Tolerancias** | Rango de temperatura óptima, necesidad de agua |
| **Morfología** | Tamaño, velocidad, tipo de dieta |
| **Comportamiento** | Agresividad, gregarismo |

### Tipos de Dieta

- Herbívoro
- Carnívoro
- Fotosíntesis

## Bucle Evolutivo

### Ciclo de Simulación Biológica

```
1. EVALUACIÓN DE ENTORNO
   → Leer texturas: Temperatura, Humedad, Biomasa
   → En posición actual del agente

2. BALANCE ENERGÉTICO
   Ganancia:
     - Fotosíntesis (luz solar local)
     - Depredación
   
   Gasto:
     - Metabolismo basal (f(tamaño, temperatura))
     - Movimiento

3. REPRODUCCIÓN Y MUTACIÓN
   SI Energía > Umbral reproductivo:
     → Dividirse
     → Descendiente hereda genoma + mutaciones aleatorias

4. SELECCIÓN NATURAL
   SI Energía < 0:
     → Agente muere
     → Se convierte en materia orgánica (abono)
```

## Fenómenos Evolutivos Emergentes

| Evento Geológico | Resultado Biológico |
|------------------|---------------------|
| Aislamiento de continente | **Especiación alopátrica** (divergencia) |
| Calentamiento global | Migración a polos o extinción |
| Formación de montañas | Nuevos nichos ecológicos |
| Glaciaciones | Presión selectiva por resistencia al frío |

## Fitness Function Implícita

No hay función de fitness definida explícitamente. La selección natural emerge de:

1. Capacidad de obtener energía del entorno
2. Eficiencia metabólica
3. Adaptación a condiciones locales
4. Éxito reproductivo
