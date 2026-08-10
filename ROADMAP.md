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

## M1 — Cerrar la brecha Simulación ↔ Render 🟡 PARCIAL (11-08-2026)

🔴 Es la brecha de mayor impacto (ver `SPECS.md §4, §10.1`): hoy el planeta que se ve no es el planeta que se simula. **Importante: esto es sobre el pipeline Nanite (`PlanetNaniteMesh`/`NanitePlanetActor`), un actor distinto de `TectonicsTestActor`. Nada de lo hecho el 10-08 en `TectonicsTestActor` (ver M1.5) resuelve esto.**

- [x] Sustituir el heightmap placeholder (`Sin(X)*Cos(Y)`) por muestreo real de `FSimplexNoise::SphereFractalNoise` — `Nanite/PlanetNaniteMesh.cpp:GenerateProceduralHeightmap`. Bonus: el placeholder anterior era además discontinuo entre parches/caras (ruido en espacio de textura, no en la esfera); la versión nueva usa la dirección 3D real, sin costuras.
- [ ] Conectar la elevación calculada por la simulación tectónica al heightmap (todavía es ruido, no datos de `RasterizedTectonics`)
- [ ] Verificar visualmente en editor que el heightmap sin costuras se ve bien
- [ ] **Bloqueador identificado para completar esto**: el heightmap generado se consume vía material WPO (`NaniteConfig.PlanetMaterial`, parámetro de textura `"Heightmap"`) — no se pudo verificar desde esta sesión si ese material existe/está asignado en el proyecto ni si su grafo de shader realmente aplica displacement. Revisar en el editor antes de invertir más en generar datos de elevación reales si el lado del material no está conectado, sería trabajo ciego.

**Hecho cuando:** al correr la simulación de placas, las montañas/cordilleras generadas por colisión son visibles en la malla Nanite renderizada, sin pasos manuales. (Sigue sin cumplirse — falta el lado del material y conectar datos reales, no solo ruido.)

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

## M1.6 — Cámara de aproximación al planeta, con colisión

🟡 Pedido explícitamente el 10-08-2026: con relieve realista (poco visible desde lejos, ver M1.5), hace falta poder acercarse a la superficie para juzgar el terreno de verdad — tanto para seguir depurando la simulación como para el producto final.

- [ ] Pawn/cámara con velocidad de movimiento adaptativa a la distancia (escala logarítmica/exponencial): a escala real de la Tierra, la misma velocidad que sirve en órbita (miles de km) es inutilizable a pie de superficie, y viceversa
- [ ] Colisión contra el terreno. Punto de partida importante: hoy `PlanetMesh->CreateMeshSection(...)` se llama explícitamente con colisión desactivada — `TectonicsTestActor.cpp:576`, comentario `false // No crear colisión, es muy pesado`. Dos rutas:
  - Colisión real sobre la malla (`bCreateCollision = true`): más simple, pero cara de recalcular cada vez que `UpdateMeshColors`/`RegeneratePlanetMesh` cambian la geometría
  - Seguimiento de superficie por raycast (la cámara consulta altura local vía `RasterizedTectonics::GetElevationBilinear` en vez de colisión física real): más barato, no requiere geometría de colisión actualizada, patrón común en cámaras planetarias
- [ ] Probar: descender desde vista de planeta completo hasta la superficie y recorrerla sin atravesar montañas ni quedarse enganchado

**Hecho cuando:** se puede pasar de vista orbital a estar de pie sobre una montaña generada por la simulación, sin clipping ni cambios manuales de velocidad de cámara.

---

## M2 — Decidir y resolver CPU vs. GPU en tectónica, y la duplicidad de pipelines CPU ✅ DECIDIDO Y DOCUMENTADO (11-08-2026)

🔴 Dos problemas de la misma familia (código construido en paralelo sin conectar). Resuelto por decisión documentada en código, no por implementación nueva — es lo que el propio "Hecho cuando" de abajo pedía.

**A. GPU sin dispatch real** — **Ruta B elegida**: CPU ya cumple el rendimiento necesario (verificado en sesión: 500 pasos de `RasterizedTectonics::Step` en tests sin problema), y no hay evidencia que justifique invertir en Ruta A (completar dispatch real) sin antes perfilar. `PlateSimulationGPU.h` y `RasterizedTectonics.h` (SyncFromGPU/SyncToGPU) llevan ahora comentarios explícitos marcando que no están en el camino activo, con la razón y la referencia a esta decisión. Código conservado, no borrado (Ruta B tal cual la definía el roadmap).

**B. `PlateSystem`/`BoundaryInteractions` vs `RasterizedTectonics`** — **`RasterizedTectonics` es la fuente de verdad** para elevación (confirmado por grep: nada lee `BoundaryInteractions::ElevationRateMaps`). `BoundaryInteractions.h` documenta esto junto a `ApplyElevationChanges`. La clase se conserva porque sigue siendo necesaria para otra lógica (SlabDepth, AccumulatedStress, vulcanismo, clasificación de fronteras) — no se borra `ElevationRateMaps` todavía porque acoplarlo o eliminarlo de forma segura requeriría más contexto del que da esta sesión; queda marcado explícitamente como código muerto pendiente en vez de ambiguo.

**Hecho cuando:** no queda código con dispatch comentado sin explicar ni cálculo duplicado sin usar *sin documentar la decisión*. ✅ Ambos casos documentados en el propio código fuente, no solo en este roadmap.

---

## M3 — Cerrar los TODOs de correctness en LOD/QuadTree ✅ COMPLETO (11-08-2026)

🟡 Afecta rendimiento (culling) y corrección visual (grietas entre caras).

- [x] Culling de frustum por nodo — implementado en `UCubeLODController::IsInFrustum` (test de cono cámara/nodo con bounding sphere conservador), no en `UpdateVisibleFaces` (confirmado sin callers, dejado como no-op documentado)
- [x] Mapeo de aristas entre caras del cubo — `FCubeSphereQuadTree::GetCrossFaceNeighbors` implementado, portando la tabla de conexión y la transformación de rotación de `UCubeSphereGrid::GetNeighborCell` a coordenadas UV continuas
- [x] Test de frustum contra bounding sphere en el LOD controller — mismo fix que el primer punto (es la misma función)
- [x] Propagación de Voronoi entre caras — confirmado que la segunda pasada de `USphericalVoronoi::GenerateVoronoiTessellation` ya la resuelve correctamente; el TODO era una nota obsoleta sobre la primera pasada (JFA), no un bug real. Comentario corregido.

**Hecho cuando:** los 4 TODOs están resueltos o convertidos en tickets explícitos con justificación de por qué se posponen. ✅ Compilado y verificado sin errores nuevos.

---

## M4 — Cobertura de test para el subsistema más grande ✅ NÚCLEO COMPLETO (11-08-2026)

🟡 Hoy solo `CubeSphereGrid` tenía tests automatizados. Creado `Tests/TectonicsTests.cpp` con 3 suites:

- [x] Test de regresión para `PlateKinematics` (rotación por cuaterniones sobre un polo de Euler conocido — 90° sobre +Z lleva (1,0,0)→(0,1,0) — y caso DeltaTime=0 = identidad)
- [x] Test de `ClassifyBoundaryType` (convergente/divergente/transformante/ninguno según los 4 casos analíticos de velocidad relativa vs. normal)
- [x] Test de regresión directo del bug de esta sesión: corre `RasterizedTectonics::Step` 500 veces y verifica que la elevación se mantiene acotada (≤12000m, ≥-12000m) **y** que sigue habiendo variación de relieve (>100m entre min y max) — falla tanto si vuelve el bug de picos infinitos como si vuelve la sobrecorrección que aplana el planeta
- [ ] Tests de `BoundaryInteractions` específicos (subducción/orogenia/spreading/transformante) con casos sintéticos — pendiente, cobertura parcial vía el test de elevación acotada arriba
- [ ] Test de conservación de masa de corteza — pendiente

**Hecho cuando:** existe `Tests/TectonicsTests.cpp` corriendo en el framework de Automation de UE. ✅ (cobertura ampliable después, no bloquea)

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
