# ROADMAP.md — Plan de Desarrollo (basado en estado real)

> Este roadmap parte del inventario real de código en [`SPECS.md`](SPECS.md), no de una estimación de calendario. `docs/ROADMAP.md` y `docs/10-hoja-de-ruta.md` son el plan aspiracional original (12+ meses, por fases F1–F5); ese documento fue escrito como visión de producto y sus fechas no están validadas contra velocidad real de desarrollo — trátalo como referencia de *alcance*, no de *fecha*. Aquí se usan **hitos** en vez de meses.

## Cómo leer esto

- **M0, M1, M2…** son hitos secuenciales, no bloques de tiempo fijo.
- Cada hito tiene una definición de "hecho" verificable en código (no "se ve bien" sino "existe el archivo/test/función que lo prueba").
- Prioridad `🔴` = bloquea el resto del roadmap, `🟡` = mejora sustancial, `🟢` = pulido.

---

## M0 — Higiene de proyecto (antes de seguir sumando features)

🔴 **Crítico y barato.** Un proyecto de +9000 líneas de C++ sin control de versiones es el riesgo más alto del repo hoy.

| Tarea | Por qué ahora |
|---|---|
| `git init` + primer commit + `.gitignore` (excluir `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `.vs/`) | Sin esto, cualquier regresión es irrecuperable. Es la única tarea de este roadmap con riesgo de pérdida de trabajo si se pospone. |
| Decidir remoto (GitHub privado, etc.) | Backup fuera de la máquina local. |

**Hecho cuando:** `git log` tiene historial y `git status` está limpio en una máquina nueva tras clonar.

---

## M1 — Cerrar la brecha Simulación ↔ Render

🔴 Es la brecha de mayor impacto (ver `SPECS.md §4, §10.1`): hoy el planeta que se ve no es el planeta que se simula.

| Tarea | Archivo de referencia |
|---|---|
| Sustituir el heightmap placeholder (`Sin(X)*Cos(Y)`) por muestreo real de `SimplexNoise` para el terreno base | `Nanite/PlanetNaniteMesh.cpp:434-473` |
| Conectar la elevación calculada por `TectonicPlateSystem::Step` al heightmap que consume `PlanetNaniteMesh` | `Tectonics/TectonicPlateSystem.cpp:374-399` → `Nanite/PlanetNaniteMesh.cpp` |
| Verificar visualmente: mover placas en `TectonicsTestActor` y confirmar que la malla Nanite se deforma en consecuencia | `Test/TectonicsTestActor.cpp` |

**Hecho cuando:** al correr la simulación de placas, las montañas/cordilleras generadas por colisión son visibles en la malla renderizada, sin pasos manuales.

---

## M2 — Decidir y resolver CPU vs. GPU en tectónica

🔴 Actualmente hay ~1300 líneas de andamiaje GPU no funcional conviviendo con un motor CPU maduro. Mantener ambos sin resolver es deuda pura.

Elegir una de dos rutas (no seguir posponiendo):

**Ruta A — Completar el dispatch GPU**
- Implementar los 4 `Dispatch*Shader` reales en `PlateSimulationGPU.cpp` (hoy comentados/placeholder, `:311-345`)
- Completar `CreateTextures` y `UploadPlateIDMap` (`:64-73`, `:146-150`)
- Completar el path de cómputo en `RasterizedTectonics.cpp` (`:100,107,113,326,405,411`)
- Justificado si el objetivo de escala es planeta completo a resolución alta en tiempo real (el caso de uso original en `docs/03`).

**Ruta B — Retirar/aislar el código GPU no funcional**
- Mover `PlateSimulationGPU`, `PlateMovementShader`, el path GPU de `RasterizedTectonics` detrás de un flag `experimental` o a una rama separada
- Documentar en `SPECS.md` que la vía soportada es CPU hasta que haya evidencia de que se necesita GPU (perfilar primero, no asumir)
- Justificado si CPU ya cumple el objetivo de rendimiento actual (medir antes de decidir).

**Hecho cuando:** no queda código con dispatch comentado en el árbol principal — o está despachando de verdad, o no está en el camino activo de ejecución.

---

## M3 — Cerrar los TODOs de correctness en LOD/QuadTree

🟡 Afecta rendimiento (culling) y corrección visual (grietas entre caras).

| Tarea | Archivo:línea |
|---|---|
| Culling de frustum por nodo del quadtree | `QuadTree/CubeSphereQuadTree.cpp:512` |
| Mapeo de aristas entre caras del cubo | `QuadTree/CubeSphereQuadTree.cpp:611` |
| Test de frustum contra bounding sphere en el LOD controller | `LOD/CubeLODController.cpp:155` |
| Propagación de Voronoi entre caras | `Tectonics/SphericalVoronoi.cpp:228` |

**Hecho cuando:** los 4 TODOs están resueltos o convertidos en tickets explícitos con justificación de por qué se posponen.

---

## M4 — Cobertura de test para el subsistema más grande

🟡 Hoy solo `CubeSphereGrid` tiene tests automatizados (`Tests/CubeSphereGridTests.cpp`). Tectónica —el subsistema con más ramas lógicas (subducción/orogenia/spreading/vulcanismo)— no tiene ninguno.

- Tests de `BoundaryInteractions` (subducción, orogenia, spreading, transformante) con casos sintéticos de placas conocidas
- Test de conservación (masa de corteza no debería desaparecer al colisionar/divergir, salvo por las reglas explícitas del modelo)
- Test de regresión para `PlateKinematics` (rotación por cuaterniones sobre un polo de Euler conocido, resultado verificable analíticamente)

**Hecho cuando:** existe `Tests/TectonicsTests.cpp` (o equivalente) corriendo en el framework de Automation de UE.

---

## M5 — Persistencia mínima

🟡 Ninguna sesión sobrevive a un reinicio hoy. Antes de sumar más subsistemas de simulación (que aumentan el estado a serializar), vale la pena resolver esto con el estado actual, más simple.

- Serialización de: estado de placas (`TectonicPlateSystem`), heightmap resultante, seed de ruido
- Guardar/cargar vía `USaveGame` o `FArchive` custom
- Versión mínima: guardar/cargar un snapshot, no un sistema de autosave/undo

**Hecho cuando:** se puede cerrar el editor, reabrir, cargar un snapshot guardado, y el planeta se ve idéntico.

---

## M6+ — Retomar el roadmap de producto original

Con M0–M5 resueltos, el resto de fases descritas en `docs/ROADMAP.md` (Fase 3: Atmósfera, Fase 4: Erosión/Hidrología, Fase 5: Biosfera) siguen siendo el plan de producto válido y no necesitan reescritura — solo recalibrar fechas una vez haya velocidad real medida en M0–M5:

- **Atmósfera** (Shallow Water Equations + Coriolis + ciclo del agua) — `docs/04-atmosfera-clima.md`
- **Erosión real** (Pipe Model reemplazando el `SimpleFlowSimulation` actual) — `docs/05-hidrosfera-erosion.md`
- **Climatología profunda** (ciclo carbono-silicatos, albedo, feedback hielo-albedo) — `docs/06-climatologia-efecto-invernadero.md`
- **Biosfera/agentes evolutivos** (Niagara, genoma vectorial) — `docs/07-biosfera-evolucion.md`

Antes de arrancar Atmósfera, conviene decidir si corre en el mismo pipeline CPU que tectónica hoy, o si es el punto natural para invertir en GPU (las SWE son mucho más sensibles a paralelismo que la tectónica rasterizada actual).

---

## Riesgos heredados del plan original (siguen vigentes)

De `docs/ROADMAP.md`, siguen siendo válidos y no se repiten en detalle aquí: inestabilidad numérica en SWE, VRAM insuficiente a resolución alta, complejidad de bordes del cubo (parcialmente ya materializada como los TODOs de M3), rendimiento por debajo de 60 FPS.
