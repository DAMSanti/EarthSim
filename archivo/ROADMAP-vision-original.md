> 🗃️ **Documento archivado.** Hoja de ruta original por calendario (12+ meses). Reemplazada por [`docs/ROADMAP.md`](../docs/ROADMAP.md), ordenada por dependencia técnica. Ver [`LEEME.md`](LEEME.md).

---

# 🗺️ Roadmap - Simulación Planetaria

> ⚠️ **Este NO es el plan de trabajo activo.** Es la visión de producto original (12+ meses, fases F1–F5), escrita antes de que existiera el código. Sus fechas y su orden no están validados contra el estado real.
>
> El plan activo es [`/ROADMAP.md`](../ROADMAP.md) en la raíz del repositorio, reordenado por dependencia técnica tras la auditoría del 15-08-2026. Usa este documento como referencia de **alcance y contenido físico** (qué modelos usar, qué ecuaciones), no de orden ni de fechas.

## Visión General del Proyecto

**Duración Total Estimada:** 12+ meses  
**Objetivo:** Gemelo Digital planetario con simulación geológica, atmosférica y biológica a 60 FPS

---

```
                                    ROADMAP DE DESARROLLO
    
    Mes 1   2   3   4   5   6   7   8   9   10  11  12  13+
        │   │   │   │   │   │   │   │   │   │   │   │   │
    ════╪═══╪═══╪═══╪═══╪═══╪═══╪═══╪═══╪═══╪═══╪═══╪═══╪════
        │   │   │   │   │   │   │   │   │   │   │   │   │
   F1   ▓▓▓▓▓▓▓▓│   │   │   │   │   │   │   │   │   │   │  El Globo Digital
        │   │   │   │   │   │   │   │   │   │   │   │   │
   F2   │   │   ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│   │   │   │   │   │   │  La Tierra Dinámica
        │   │   │   │   │   │   │   │   │   │   │   │   │
   F3   │   │   │   │   │   ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│   │   │   │  El Aliento del Planeta
        │   │   │   │   │   │   │   │   │   │   │   │   │
   F4   │   │   │   │   │   │   │   │   ▓▓▓▓▓▓▓▓▓▓▓▓│   │  La Piel Viva
        │   │   │   │   │   │   │   │   │   │   │   │   │
   F5   │   │   │   │   │   │   │   │   │   │   │   ▓▓▓▓▓  El Experimento Biológico
        │   │   │   │   │   │   │   │   │   │   │   │   │
    ════╪═══╪═══╪═══╪═══╪═══╪═══╪═══╪═══╪═══╪═══╪═══╪═══╪════
        │   │   │   │   │   │   │   │   │   │   │   │   │
        ▲       ▲           ▲           ▲           ▲
        │       │           │           │           │
      HITO 1  HITO 2      HITO 3      HITO 4      HITO 5
```

---

# 📍 FASE 1: El Globo Digital
## Meses 1-2 | Fundamentos

### Objetivo
Establecer la estructura de datos espaciales que soportará toda la simulación.

### Sprint 1.1 - Cubo Esférico (Semanas 1-3)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Implementar estructura `CubeSphereGrid` | Clase base con 6 caras del cubo | 🔴 Crítica |
| Normalización de vectores | Proyección del cubo a esfera | 🔴 Crítica |
| Sistema de coordenadas | Conversión latitud/longitud ↔ cara/UV | 🔴 Crítica |
| Manejo de adyacencia | Navegación entre celdas en bordes de caras | 🔴 Crítica |

### Sprint 1.2 - Corrección Métrica (Semanas 3-4)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Calcular factores de escala | Mapa de áreas reales por celda | 🔴 Crítica |
| Precalcular distorsión | Textura de corrección en esquinas del cubo | 🟡 Alta |
| Validar conservación de masa | Tests con flujo de agua simple | 🟡 Alta |

### Sprint 1.3 - Sistema de LOD (Semanas 5-8)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Implementar Quadtree por cara | Subdivisión adaptativa | 🟡 Alta |
| LOD basado en distancia | Cámara determina nivel de detalle | 🟡 Alta |
| Streaming de datos | Cargar/descargar chunks dinámicamente | 🟢 Media |
| Integración con Nanite (UE5) | Malla deformable base | 🟡 Alta |

### 🎯 HITO 1 - Fin del Mes 2
```
✓ Esfera renderizable con Cubo Esférico
✓ LOD dinámico funcional
✓ Corrección de distorsión implementada
✓ Framework de Compute Shaders listo
```

### Entregables
- [ ] `CubeSphereGrid` clase funcional
- [ ] Render de esfera con Nanite
- [ ] Sistema LOD con Quadtree
- [ ] Primeros Compute Shaders de prueba

---

# 📍 FASE 2: La Tierra Dinámica
## Meses 3-5 | Tectónica de Placas

### Objetivo
Crear el motor de tectónica de placas que genere topografía emergente.

### Sprint 2.1 - Génesis de Placas (Semanas 9-11)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Voronoi Esférico | Distribución de centroides de placas | 🔴 Crítica |
| Jump Flooding Algorithm (JFA) | Asignación paralela de IDs de placa | 🔴 Crítica |
| Definir propiedades de placa | Polo de Euler, velocidad angular, tipo corteza | 🔴 Crítica |
| UI de configuración | Parámetros iniciales de simulación | 🟢 Media |

### Sprint 2.2 - Cinemática de Placas (Semanas 12-14)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Rotación por cuaterniones | Movimiento sobre esfera | 🔴 Crítica |
| Compute Shader de movimiento | Actualización paralela de posiciones | 🔴 Crítica |
| Vectores de velocidad de borde | Calcular dirección en fronteras | 🔴 Crítica |
| Detección de colisiones | Comparar IDs en celdas adyacentes | 🔴 Crítica |

### Sprint 2.3 - Interacciones de Frontera (Semanas 15-17)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Límites convergentes | Subducción y orogenia | 🔴 Crítica |
| Límites divergentes | Creación de corteza oceánica | 🔴 Crítica |
| Límites transformantes | Fricción lateral | 🟡 Alta |
| Vulcanismo | Puntos calientes en zonas de subducción | 🟡 Alta |

### Sprint 2.4 - Tectónica Rasterizada (Semanas 18-20)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Mapa de IDs de Placa (textura) | GPU-friendly data structure | 🔴 Crítica |
| Mapa de Elevación Local | Altura relativa a cada placa | 🔴 Crítica |
| Shader de "movimiento de píxeles" | Rasterización del movimiento | 🔴 Crítica |
| Suma de elevación en colisión | Formación de montañas | 🔴 Crítica |
| Generación de corteza nueva | Rellenar huecos en divergencia | 🔴 Crítica |

### 🎯 HITO 2 - Fin del Mes 5
```
✓ Placas tectónicas en movimiento visible
✓ Continentes formándose y separándose
✓ Montañas emergiendo de colisiones
✓ Dorsales oceánicas generando corteza
```

### Entregables
- [ ] Sistema de placas completo
- [ ] Visualización de movimiento de placas
- [ ] Ciclo de Wilson observable
- [ ] Documentación de parámetros de ajuste

---

# 📍 FASE 3: El Aliento del Planeta
## Meses 6-8 | Atmósfera y Clima

### Objetivo
Implementar un modelo de circulación general simplificado con ciclo del agua.

### Sprint 3.1 - Ecuaciones de Aguas Someras (Semanas 21-24)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Implementar SWE en Compute Shader | Conservación de masa y momento | 🔴 Crítica |
| Efecto Coriolis | $f = 2\Omega \sin \phi$ por latitud | 🔴 Crítica |
| Gradiente de presión | Fuerza por diferencia de altura geopotencial | 🔴 Crítica |
| Fricción superficial | Ralentización por terreno | 🟡 Alta |
| Estabilidad numérica | Condición CFL, suavizado | 🔴 Crítica |

### Sprint 3.2 - Termodinámica (Semanas 25-27)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Insolación por latitud | Ángulo solar y estaciones | 🔴 Crítica |
| Radiación de onda larga | Pérdida de calor nocturna | 🔴 Crítica |
| Advección de calor | Transporte por viento | 🟡 Alta |
| Inclinación axial | Implementar estaciones | 🟡 Alta |
| Balance energético global | Verificar equilibrio | 🟡 Alta |

### Sprint 3.3 - Ciclo del Agua (Semanas 28-32)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Evaporación | Tasa proporcional a temperatura | 🔴 Crítica |
| Transporte de humedad | Advección con el viento | 🔴 Crítica |
| Ecuación de Clausius-Clapeyron | Saturación del aire | 🔴 Crítica |
| Precipitación | Lluvia cuando humedad > saturación | 🔴 Crítica |
| Nieve y hielo | Precipitación bajo 0°C | 🟡 Alta |
| Efectos orográficos | Sombra de lluvia | 🟡 Alta |

### 🎯 HITO 3 - Fin del Mes 8
```
✓ Patrones de viento globales realistas
✓ Celdas de Hadley, Ferrel y Polar visibles
✓ Lluvia y nieve según condiciones
✓ Estaciones funcionando
✓ Sombra de lluvia observable
```

### Entregables
- [ ] Sistema atmosférico completo
- [ ] Visualización de vientos
- [ ] Mapa de precipitación
- [ ] Ciclo día/noche y estacional

---

# 📍 FASE 4: La Piel Viva
## Meses 9-11 | Erosión e Hidrología

### Objetivo
Esculpir el terreno con erosión hidráulica y generar suelos con composición química.

### Sprint 4.1 - Erosión Hidráulica (Semanas 33-37)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Modelo de Tuberías Virtuales | Pipe Model en GPU | 🔴 Crítica |
| Acumulación de agua | Lluvia → capa de agua | 🔴 Crítica |
| Flujo entre celdas | Diferencia de altura hidrostática | 🔴 Crítica |
| Capacidad de transporte | $C = f(\text{velocidad}, \text{pendiente})$ | 🔴 Crítica |
| Erosión | Sedimento < C → profundizar | 🔴 Crítica |
| Deposición | Sedimento > C → elevar | 🔴 Crítica |
| Evaporación | Reducción gradual del agua | 🟡 Alta |

### Sprint 4.2 - Redes Fluviales (Semanas 38-40)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Formación de ríos | Cauces permanentes | 🔴 Crítica |
| Lagos | Acumulación en depresiones | 🟡 Alta |
| Deltas | Deposición en desembocaduras | 🟡 Alta |
| Llanuras aluviales | Zonas de inundación | 🟡 Alta |
| Océanos | Nivel del mar y costas | 🔴 Crítica |

### Sprint 4.3 - Estratigrafía y Suelos (Semanas 41-44)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Capas de terreno | Roca madre, regolito, humus | 🔴 Crítica |
| Composición NPK | Nitrógeno, Fósforo, Potasio | 🔴 Crítica |
| Firma química de sedimentos | Transporte de nutrientes | 🟡 Alta |
| Tipos de roca | Granito, basalto, sedimentaria | 🟡 Alta |
| Meteorización | Degradación de roca a suelo | 🟡 Alta |

### 🎯 HITO 4 - Fin del Mes 11
```
✓ Redes fluviales dendríticas naturales
✓ Lagos y mares interiores
✓ Deltas y valles fértiles
✓ Suelos con composición química variable
✓ Ciclo de nutrientes básico
```

### Entregables
- [ ] Sistema de erosión completo
- [ ] Ríos y lagos visibles
- [ ] Mapa de fertilidad del suelo
- [ ] Visualización de estratigrafía

---

# 📍 FASE 5: El Experimento Biológico
## Mes 12+ | Evolución

### Objetivo
Implementar agentes biológicos que evolucionen en respuesta al entorno dinámico.

### Sprint 5.1 - Framework de Agentes (Semanas 45-48)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Estructura de agente en GPU | Buffer de agentes masivo | 🔴 Crítica |
| Genoma vectorial | Tolerancias, morfología, comportamiento | 🔴 Crítica |
| Posicionamiento en esfera | Coordenadas de cada agente | 🔴 Crítica |
| Lectura de entorno | Sampling de texturas ambientales | 🔴 Crítica |

### Sprint 5.2 - Metabolismo y Energía (Semanas 49-52)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Fotosíntesis | Ganancia de energía por luz | 🔴 Crítica |
| Herbívoros | Consumo de biomasa vegetal | 🔴 Crítica |
| Carnívoros | Depredación de otros agentes | 🟡 Alta |
| Metabolismo basal | Gasto por tamaño y temperatura | 🔴 Crítica |
| Movimiento | Gasto energético por desplazamiento | 🟡 Alta |

### Sprint 5.3 - Reproducción y Mutación (Semanas 53-56)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Umbral reproductivo | División cuando energía suficiente | 🔴 Crítica |
| Herencia genómica | Copia de genoma a descendiente | 🔴 Crítica |
| Mutaciones aleatorias | Variación en genes | 🔴 Crítica |
| Muerte y descomposición | Energía → materia orgánica en suelo | 🔴 Crítica |

### Sprint 5.4 - Comportamientos Avanzados (Semanas 57+)

| Tarea | Descripción | Prioridad |
|-------|-------------|-----------|
| Migración | Movimiento hacia recursos | 🟡 Alta |
| Gregarismo | Formación de grupos | 🟢 Media |
| Especiación | Divergencia por aislamiento | 🟡 Alta |
| Visualización de árbol filogenético | Historial evolutivo | 🟢 Media |

### 🎯 HITO 5 - Mes 12+
```
✓ Millones de agentes simulados en GPU
✓ Evolución observable en tiempo real
✓ Especiación por aislamiento geográfico
✓ Extinciones masivas por eventos climáticos
✓ Ecosistemas complejos emergentes
```

### Entregables
- [ ] Sistema de agentes evolutivos
- [ ] Visualización de poblaciones
- [ ] Herramientas de análisis evolutivo
- [ ] Documentación de parámetros genéticos

---

# 🔧 Tareas Transversales (Todo el Proyecto)

## Infraestructura

| Tarea | Fase | Descripción |
|-------|------|-------------|
| Setup de proyecto UE5 | F1 | Configuración inicial del motor |
| Pipeline de Compute Shaders | F1 | Framework reutilizable |
| Sistema de guardado | F2 | Serialización del estado planetario |
| Profiling de rendimiento | Todas | Mantener 60 FPS |
| Tests automatizados | Todas | Validación de física |

## Visualización

| Tarea | Fase | Descripción |
|-------|------|-------------|
| Splat maps dinámicos | F1-F4 | Texturizado procedimental |
| Sky Atmosphere | F3 | Cielo físicamente correcto |
| Visualización de datos | Todas | Debug de simulación |
| UI de control de tiempo | F1 | Acelerar/pausar simulación |

## Optimización

| Tarea | Fase | Descripción |
|-------|------|-------------|
| GPU memory management | F2+ | Evitar cuellos de botella |
| Async compute | F3+ | Solapar simulación y render |
| Variable rate shading | F4+ | Optimizar según importancia |

---

# 📊 Métricas de Éxito por Fase

| Fase | Métrica | Objetivo |
|------|---------|----------|
| F1 | FPS con esfera vacía | ≥ 60 FPS |
| F2 | Tiempo por paso tectónico | < 16ms |
| F3 | Celdas atmosféricas simuladas | > 1M/frame |
| F4 | Pasos de erosión por segundo | > 60 |
| F5 | Agentes simultáneos | > 1M |

---

# ⚠️ Riesgos y Mitigaciones

| Riesgo | Probabilidad | Impacto | Mitigación |
|--------|--------------|---------|------------|
| Inestabilidad numérica SWE | Alta | Alto | Métodos semi-implícitos, suavizado |
| VRAM insuficiente | Media | Alto | LOD agresivo, streaming |
| Rendimiento < 60 FPS | Media | Alto | Reducir resolución de simulación |
| Complejidad de bordes del cubo | Alta | Medio | Tests exhaustivos de adyacencia |
| Evolución no convergente | Media | Medio | Ajuste de parámetros de mutación |

---

# 📅 Calendario de Hitos

```
2024-2025 (Ejemplo)

Ene │ Feb │ Mar │ Abr │ May │ Jun │ Jul │ Ago │ Sep │ Oct │ Nov │ Dic │ Ene+
────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────
    │     │     │     │     │     │     │     │     │     │     │     │
 ▼  │     │  ▼  │     │     │  ▼  │     │     │  ▼  │     │     │  ▼  │
H1  │     │ H2  │     │     │ H3  │     │     │ H4  │     │     │ H5  │
    │     │     │     │     │     │     │     │     │     │     │     │
```

---

## 🚀 Próximos Pasos Inmediatos

1. **Configurar proyecto Unreal Engine 5**
2. **Implementar clase `CubeSphereGrid` básica**
3. **Crear primer Compute Shader de prueba**
4. **Renderizar esfera con desplazamiento simple**
