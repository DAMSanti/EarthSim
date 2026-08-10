# ROADMAP.md — Plan de Desarrollo (basado en estado real)

> Este roadmap parte del inventario real de código en [`SPECS.md`](SPECS.md), no de una estimación de calendario. `docs/ROADMAP.md` y `docs/10-hoja-de-ruta.md` son el plan aspiracional original (12+ meses, por fases F1–F5); ese documento fue escrito como visión de producto y sus fechas no están validadas contra velocidad real de desarrollo — trátalo como referencia de *alcance*, no de *fecha*. Aquí se usan **hitos** en vez de meses, en formato checklist para ir marcando.

## Cómo leer esto

- **M0, M1, M2…** son hitos secuenciales, no bloques de tiempo fijo.
- Cada hito tiene una definición de "hecho" verificable en código (no "se ve bien" sino "existe el archivo/test/función que lo prueba").
- Prioridad `🔴` = bloquea el resto del roadmap, `🟡` = mejora sustancial, `🟢` = pulido.
- `[x]` = hecho y verificado en editor/compilación. `[ ]` = pendiente.

---

## M0 — Higiene de proyecto ✅ COMPLETO (10-08-2026)

🔴 Crítico y barato. Un proyecto de +9000 líneas de C++ sin control de versiones era el riesgo más alto del repo.

- [x] `git init` + `.gitignore` (excluye `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, `.vs/`, `.claude/`)
- [x] Primer commit con el baseline completo
- [ ] Decidir remoto (GitHub privado, etc.) — backup fuera de la máquina local, sigue pendiente

---

## M1 — Cerrar la brecha Simulación ↔ Render

🔴 Es la brecha de mayor impacto (ver `SPECS.md §4, §10.1`): hoy el planeta que se ve no es el planeta que se simula. **Importante: esto es sobre el pipeline Nanite (`PlanetNaniteMesh`/`NanitePlanetActor`), un actor distinto de `TectonicsTestActor`. Nada de lo hecho el 10-08 en `TectonicsTestActor` (ver M1.5) resuelve esto.**

- [ ] Sustituir el heightmap placeholder (`Sin(X)*Cos(Y)`) por muestreo real de `SimplexNoise` para el terreno base — `Nanite/PlanetNaniteMesh.cpp:434-473`
- [ ] Conectar la elevación calculada por la simulación tectónica al heightmap que consume `PlanetNaniteMesh`
- [ ] Verificar visualmente: mover placas y confirmar que la malla Nanite se deforma en consecuencia

**Hecho cuando:** al correr la simulación de placas, las montañas/cordilleras generadas por colisión son visibles en la malla Nanite renderizada, sin pasos manuales.

---

## M1.5 — Sesión de depuración de `TectonicsTestActor` ✅ COMPLETO (10-08-2026)

No estaba en el roadmap original — surgió al probar la simulación por primera vez en el editor y encontrar que nada de lo visible era fiable. Registrado aquí como hecho, con hallazgos que alimentan M2/M3.

- [x] **Bug de picos infinitos en fronteras convergentes**: `RasterizedTectonics::Step()` sumaba elevación cada paso sin relajación entre celdas vecinas → paredes casi verticales de 12km. Fix: término de relajación difusiva (`FPlateMovementParams::DiffusionRate`) — `RasterizedTectonics.cpp`
- [x] **Ese primer fix sobrecorrigió**: un blur completo cada paso, acumulado sobre miles de pasos, aplanaba el planeta entero. Fix: mezcla parcial (0.02 por defecto) en vez de reemplazo total — mismo archivo
- [x] **`NanitePlanetActor` fantasma en `test.umap`**: radio = radio real de la Tierra (637.100.000 uu), dejado de pruebas anteriores, envolvía por completo al planeta de prueba (500m). Eliminado del nivel.
- [x] **`PlayerStart` inexistente**: solo había un `PlayerStartPIE0` transitorio que se regeneraba cerca del origen en cada Play, dejando la cámara dentro del planeta. Hace falta un `PlayerStart` persistente colocado a mano (no se puede scriptar en un `.umap` binario) — colocado y verificado.
- [x] **Fórmula de exageración de elevación acoplada al radio**: `ElevationOffset` escalaba con `VisualRadius`, contradiciendo su propio comentario ("1.0 = metros reales") y rompiéndose a escala real. Fix: `Elevation(m) * 100 * ElevationScale`, independiente del radio — `TectonicsTestActor.cpp` (dos copias: `CreatePlanetMesh` y `UpdateMeshColors`)
- [x] **Escala de juguete → escala real de la Tierra**: `VisualRadius` 500m → 6.371km (`637100000.0f`), `ElevationScale` 50→15, `TimeScale` 100→1 (con nota honesta: no está calibrado contra velocidades reales de placas)
- [x] Confirmado (no arreglado, documentado): `PlateSystem`/`BoundaryInteractions` calculan datos de elevación que nadie consume, en paralelo con `RasterizedTectonics` que sí se usa — ver M2

Detalle completo en `SPECS.md §5.3, §5.4`.

---

## M2 — Decidir y resolver CPU vs. GPU en tectónica, y la duplicidad de pipelines CPU

🔴 Dos problemas de la misma familia (código construido en paralelo sin conectar):

**A. GPU sin dispatch real** (~1300 líneas de andamiaje conviviendo con el motor CPU maduro)
- [ ] **Ruta A — Completar el dispatch GPU**: implementar los 4 `Dispatch*Shader` en `PlateSimulationGPU.cpp` (`:311-345`), completar `CreateTextures`/`UploadPlateIDMap` (`:64-73`, `:146-150`) y el path de cómputo en `RasterizedTectonics.cpp` (`:100,107,113,326,405,411`) — si el objetivo es planeta completo a resolución alta en tiempo real
- [ ] **Ruta B — Retirar/aislar el código GPU no funcional** detrás de un flag `experimental`, documentar que CPU es la vía soportada — si CPU ya cumple el rendimiento objetivo (medir antes de decidir)

**B. `PlateSystem`/`BoundaryInteractions` vs `RasterizedTectonics`** (confirmado el 10-08-2026, ver M1.5)
- [ ] Decidir cuál de los dos pipelines de detección de fronteras/elevación es la única fuente de verdad
- [ ] Eliminar o conectar de verdad el que se descarte (`BoundaryInteractions::ElevationRateMaps` hoy no lo consume nadie)

**Hecho cuando:** no queda código con dispatch comentado ni cálculo duplicado sin usar en el árbol principal.

---

## M3 — Cerrar los TODOs de correctness en LOD/QuadTree

🟡 Afecta rendimiento (culling) y corrección visual (grietas entre caras).

- [ ] Culling de frustum por nodo del quadtree — `QuadTree/CubeSphereQuadTree.cpp:512`
- [ ] Mapeo de aristas entre caras del cubo — `QuadTree/CubeSphereQuadTree.cpp:611`
- [ ] Test de frustum contra bounding sphere en el LOD controller — `LOD/CubeLODController.cpp:155`
- [ ] Propagación de Voronoi entre caras — `Tectonics/SphericalVoronoi.cpp:228`

**Hecho cuando:** los 4 TODOs están resueltos o convertidos en tickets explícitos con justificación de por qué se posponen.

---

## M4 — Cobertura de test para el subsistema más grande

🟡 Hoy solo `CubeSphereGrid` tiene tests automatizados. Tectónica —el subsistema con más ramas lógicas— no tiene ninguno, y hoy sabemos (M1.5) que los bugs ahí son sutiles y fáciles de confundir con "así es como se ve".

- [ ] Tests de `BoundaryInteractions` (subducción, orogenia, spreading, transformante) con casos sintéticos de placas conocidas
- [ ] Test de conservación (masa de corteza no debería desaparecer al colisionar/divergir, salvo por las reglas explícitas del modelo)
- [ ] Test de regresión para `PlateKinematics` (rotación por cuaterniones sobre un polo de Euler conocido, resultado verificable analíticamente)
- [ ] Test de equilibrio para `DiffusionRate`: dado un `OrogenyFactor` fijo, la elevación máxima en un boundary convergente debe estabilizarse (no crecer sin límite ni converger a 0) tras N pasos

**Hecho cuando:** existe `Tests/TectonicsTests.cpp` (o equivalente) corriendo en el framework de Automation de UE.

---

## M5 — Persistencia mínima

🟡 Ninguna sesión sobrevive a un reinicio hoy.

- [ ] Serialización de: estado de placas (`TectonicPlateSystem`), heightmap resultante, seed de ruido
- [ ] Guardar/cargar vía `USaveGame` o `FArchive` custom
- [ ] Versión mínima: guardar/cargar un snapshot, no un sistema de autosave/undo

**Hecho cuando:** se puede cerrar el editor, reabrir, cargar un snapshot guardado, y el planeta se ve idéntico.

---

## M6+ — Retomar el roadmap de producto original

Con M1–M5 resueltos, el resto de fases descritas en `docs/ROADMAP.md` (Fase 3: Atmósfera, Fase 4: Erosión/Hidrología, Fase 5: Biosfera) siguen siendo el plan de producto válido:

- [ ] **Atmósfera** (Shallow Water Equations + Coriolis + ciclo del agua) — `docs/04-atmosfera-clima.md`
- [ ] **Erosión real** (Pipe Model reemplazando el `SimpleFlowSimulation` actual) — `docs/05-hidrosfera-erosion.md`
- [ ] **Climatología profunda** (ciclo carbono-silicatos, albedo, feedback hielo-albedo) — `docs/06-climatologia-efecto-invernadero.md`
- [ ] **Biosfera/agentes evolutivos** (Niagara, genoma vectorial) — `docs/07-biosfera-evolucion.md`

Antes de arrancar Atmósfera, conviene decidir si corre en el mismo pipeline CPU que tectónica hoy, o si es el punto natural para invertir en GPU (las SWE son mucho más sensibles a paralelismo que la tectónica rasterizada actual).

---

## Riesgos heredados del plan original (siguen vigentes)

De `docs/ROADMAP.md`: inestabilidad numérica en SWE, VRAM insuficiente a resolución alta, complejidad de bordes del cubo (parcialmente ya materializada como los TODOs de M3), rendimiento por debajo de 60 FPS.

**Riesgo nuevo, confirmado el 10-08-2026:** los parámetros de la simulación (`DiffusionRate`, `OrogenyFactor`, `SpreadingFactor`, `TimeScale`, `ElevationScale`...) no están calibrados contra nada real ni entre sí — se ajustan por observación y son fáciles de desequilibrar (ver M1.5, dos iteraciones para encontrar un punto medio razonable en `DiffusionRate`). Cualquier cambio en uno puede requerir re-ajustar los demás. Vale la pena documentar el punto de equilibrio encontrado como base, no como valor final.
