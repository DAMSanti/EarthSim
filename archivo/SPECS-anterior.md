> 🗃️ **Documento archivado.** Inventario de código anterior, con secciones ya caducadas conservadas como historial. Reemplazado por [`docs/SPECS.md`](../docs/SPECS.md), reescrito contra el código actual. Ver [`LEEME.md`](LEEME.md).

---

# SPECS.md — Especificación Técnica (Simu)

> Este documento describe el sistema **tal como existe hoy en el código**, no la visión aspiracional. Para la visión completa del producto ver [`docs/01-resumen-ejecutivo.md`](docs/01-resumen-ejecutivo.md) y el resto de `docs/`. Cada sección marca explícitamente **Implementado** / **Parcial** / **No implementado**, con referencias a archivo para poder verificarlo.
>
> **Revisado el 15-08-2026** tras una auditoría de código que corrigió varias afirmaciones de la versión anterior de este documento — señaladas en línea con 🔻.
>
> **Actualizado el 16-08-2026.** §5.1 describe el estado anterior a F1 y se conserva como historial; el estado real de la tectónica hoy está en **§5.7**, incluido su defecto de raíz. La tabla de abajo tenía filas contradictorias (isostasia y clima aparecían a la vez como hechos y como no implementados); corregidas.

## 0. Resumen de estado

| Subsistema | Estado | Motor de cómputo |
|---|---|---|
| Cube-Sphere grid + métricas | ✅ Implementado, con tests | CPU |
| **Movimiento de placas** | 🔴 Implementado pero **las placas se disuelven** (§5.7) | CPU |
| Generación de placas (Voronoi) | ✅ Implementado | CPU |
| Ciclo de vida de placas (nacer/morir) | ❌ Imposible por construcción (§5.7) | — |
| Orogenia / dorsales / subducción | ⚠️ Emergentes de la advección, pero sobre un reparto roto (§5.7) | CPU |
| Isostasia (Airy) / grosor de corteza | ✅ Implementado, estado primario (§12) | CPU |
| Batimetría por edad (hundimiento térmico) | ✅ Implementado (§12) | CPU |
| Nivel del mar con volumen conservado | ✅ Implementado (§12) | CPU |
| `BoundaryInteractions` | ⏸️ Desactivado hasta F1 (era inerte: nadie leía su salida) | — |
| Tectónica — vía GPU | 🗑️ Borrada (15-08-2026, era andamiaje vacío) | — |
| Renderizado planetario con LOD | ❌ No implementado (`PlanetNaniteMesh` borrado, ver §4) | — |
| QuadTree + LOD | ⚠️ Implementado, dormido hasta F6 | CPU |
| Streaming de chunks | 🗑️ Borrado (0 referencias) | — |
| Ruido Simplex | ✅ Implementado y en uso (§7) | CPU |
| Flujo/difusión simple | ⚠️ Test de conservación de masa, **no es erosión** | CPU |
| Visor de campos de simulación | ⚠️ Núcleo implementado, sin verificar en editor (§11) | CPU |
| UI de configuración tectónica | ✅ Implementado | — |
| Guardado/serialización | ✅ Implementado y verificado | — |
| Control de versiones (git) | ✅ Inicializado | — |
| Clima (temperatura/viento/precipitación) | ✅ Implementado (F3), 4 tests | CPU |
| Drenaje (acumulación de flujo) | ✅ Implementado (F4) | CPU |
| Erosión / sedimento | ❌ No implementado | — |
| Atmósfera (SWE) / ciclo del agua | ❌ No implementado | — |
| Climatología (carbono/albedo) | ❌ No implementado | — |
| Biosfera/agentes | ❌ No implementado | — |

## 1. Plataforma

- **Motor:** Unreal Engine 5.8 (`Simu.uproject`; migrado desde 5.7 el 10-08-2026 — ver `Source/*.Target.cs`, `DefaultBuildSettings = BuildSettingsVersion.V7`)
- **Módulos:** `Simu` (Runtime, `Default`), `CubeSphere` (Runtime, `PostConfigInit`)
- **Plugins habilitados:** ProceduralMeshComponent, Niagara, Water, Landmass, GeometryScripting
- **Target:** Windows únicamente
- Nanite se usa vía flags nativos de `UStaticMesh` (`bNanite`), no es un plugin.

## 2. Estructura de datos espacial — Cubo Esférico

**Implementado** — `Source/CubeSphere/CubeSphereGrid.cpp` (762 líneas), `CubeSphereMetrics.cpp` (367 líneas).

- Proyección cubo → esfera, conversión geo (lat/lon) ↔ cara/celda, adyacencia entre las 6 caras.
- Corrección de distorsión métrica (factores de escala por celda).
- **Test cubierto:** `Source/CubeSphere/Tests/CubeSphereGridTests.cpp` — 27 aserciones. 🔻 **`Simu.CubeSphere.AdjacencyContinuity` falla con 124 errores** (§10.5): la adyacencia entre caras no es continua. Es un bug de corrección abierto, no una laguna de cobertura, y bloquea el drenaje de F4.
- **Convenio de caras:** la tabla de ejes por cara vive en `CubeFaceMapping.h` (fuente única, ver §5.6); `UCubeSphereGrid::GetFaceAxes` delega en ella.

Diseño de referencia ampliado en [`docs/02-estructura-datos-espaciales.md`](docs/02-estructura-datos-espaciales.md).

## 3. LOD y QuadTree

**Implementado y conservado, pero dormido hasta F6.** `Streaming/ChunkStreamingManager` se borró el 15-08-2026 (0 referencias).

Se conservan `QuadTree/` y `LOD/` tras revisar su API: es lógica espacial pura (split/collapse, error geométrico, bounds, vecinos, LOD por distancia) sin dependencia de Nanite, y es la mitad-CPU del esquema de renderizado de F6.

🔻 **Deuda concreta:** `FCubeSphereQuadTree::GetCrossFaceNeighbors` (`CubeSphereQuadTree.cpp:596`) contiene una **copia de la tabla de conexiones de bordes** que M3 portó desde el Grid. Esa tabla se eliminó del Grid el 15-08-2026 por estar mal (ver §2 y §5.6); esta copia sigue ahí y no está cubierta por ningún test. Si se retoma el QuadTree, migrarla a `UCubeSphereGrid::GetNeighborCell`.

**Estado previo, conservado como referencia:**

- `QuadTree/CubeSphereQuadTree.cpp` (~770 líneas): árbol de subdivisión/colapso por cara. Culling de frustum y mapeo de aristas entre caras **resueltos el 11-08-2026** (ver `ROADMAP.md` M3).
- `LOD/CubeLODController.cpp` (~520 líneas): selección de LOD por distancia con `TickComponent`. Frustum culling por cono implementado en `IsInFrustum` (11-08-2026).
- `Streaming/ChunkStreamingManager.cpp` (738 líneas): carga/descarga de chunks con `Tick`. No hay streaming asíncrono más allá de los mecanismos estándar de UE.

Los 4 TODOs pendientes son de **correctness/performance**, no cosméticos: sin culling de frustum el coste de LOD escala con el planeta completo, no con lo visible; sin mapeo de aristas entre caras el LOD puede generar grietas (T-junctions) en los bordes del cubo.

## 4. Renderizado planetario

🔻 **No implementado.** `Nanite/PlanetNaniteMesh.cpp` (840 líneas) se **borró el 15-08-2026**: construía un `UStaticMesh` completo por parche de quadtree, de forma síncrona en el hilo principal, lo que congelaba el editor. Ningún presupuesto por frame arregla que la geometría se recree en vez de instanciarse.

Hoy el único renderizado es el `UProceduralMeshComponent` de `ATectonicsTestActor`: malla fija de `GridResolution = 128` por cara sobre un radio de 6371 km, es decir **~78 km por quad**. Suficiente para ver patrones globales (que es lo que necesitan F1–F3), insuficiente para redes de drenaje (F4) o para bajar a la superficie.

Diseño acordado para F6, para no repetir el error: quadtree + **una sola malla-rejilla pre-construida** instanciada por parche, con desplazamiento en el vertex shader muestreando la textura de elevación. Sobrevive `Nanite/PlanetMaterialGenerator` como único ejemplo funcionando de creación de materiales por código.

**Estado previo, conservado como referencia histórica:**

- Genera parches reales de `UStaticMesh`, gestiona un pool de `UStaticMeshComponent`, configura Nanite build settings correctamente.
- **Gap parcialmente cerrado (11-08-2026):** `GenerateProceduralHeightmap` ya no usa `Sin(X)*Cos(Y)` (placeholder discontinuo entre parches/caras) — ahora usa `FSimplexNoise::SphereFractalNoise` sobre la dirección 3D real en la esfera (continuo, sin costuras). Sigue sin usar la elevación calculada por la simulación tectónica: **el planeta que se renderiza sigue sin reflejar la tectónica**.
- **Material creado por código (11-08-2026):** `Nanite/PlanetMaterialGenerator.h/.cpp` construye y guarda `Content/Materials/M_PlanetDisplacement.uasset` vía `UMaterialEditingLibrary` (API oficial de Epic para edición de materiales por código) — Heightmap+HeightmapMinMax → Lerp → pin Displacement. `GetPatchMaterial()` en `PlanetNaniteMesh.cpp` lo crea/carga automáticamente si `NaniteConfig.PlanetMaterial` está vacío. **Sin verificar visualmente** — construido sin poder previsualizar el grafo, ver `ROADMAP.md` M1.
- El plan original en `docs/08-renderizado-visualizacion.md` describe desplazamiento vía World Position Offset (WPO) en shader sobre Nanite; la implementación actual en cambio genera geometría desplazada en CPU al construir la malla. Es una decisión válida, pero diverge del documento original y debería quedar explícita ahí también.

## 5. Tectónica de placas

### 5.1 Motor CPU — 🔻 **las placas nunca se mueven** (auditoría 15-08-2026)

La versión anterior de esta sección lo describía como "el subsistema más maduro del proyecto, ~2800 líneas". Hay líneas, pero **no hay movimiento de placas**. Verificado por `grep` sobre todo `Source/`:

- `Tectonics/PlateKinematics.cpp` (441): rotación por cuaterniones sobre el polo de Euler. **Implementada correctamente y con test, pero nadie la llama.** Las únicas referencias a `UPlateKinematics` fuera de su propio archivo están en `Tests/TectonicsTests.cpp` y en un `class UPlateKinematics;` de forward-declare sin uso. Es código muerto con test de regresión.
- `Tectonics/TectonicPlateSystem.cpp:374` `Step(DeltaTime)`: solo hace `Plate.Age += dt`, llama a `ProcessAllBoundaries` y re-ejecuta `DetectBoundaries()` sobre un mapa Voronoi **que nunca cambia**. El comentario "los límites pueden cambiar con el movimiento" es falso: no hay movimiento.
- `Tectonics/RasterizedTectonics.cpp:317` `Step(Params)`: **nunca escribe en `PlateIDData` ni en `VelocityData`**. Solo envejece la corteza y modifica la elevación en píxeles de frontera que son los mismos para siempre.
- `Tectonics/BoundaryInteractions.cpp` (693): subducción, orogenia, spreading, vulcanismo y hotspots. La física está escrita y vuelca resultados en `ElevationRateMaps`, pero **`ApplyElevationChanges()` (`:626`) está vacía** y `GetElevationRateMap()` no tiene ningún consumidor. Se ejecuta recorriendo 6×Res² celdas por paso para no producir nada. TODO adicional: no existe destrucción de corteza que compense a `CreateNewCrust` (`:609`).
- `Tectonics/SphericalVoronoi.cpp` (404): generación de placas por Voronoi esférico (JFA). En uso, pero 🔻 **con un bug abierto**: el JFA deja un 11–19 % de celdas sin asignar a resolución 32, lo que hace que `GeneratePlates()` devuelva `false` y aborta dos de los tests (§10.5). Sin verificar si también deja huecos a la resolución de producción.
- `Test/TectonicsTestActor.cpp` (1008): el único actor que hoy simula y renderiza relieve de verdad.
- `Tectonics/TectonicPlanetActor.cpp`: **hoy no hace nada útil** — Nanite comentado (no dibuja terreno) y sin `RasterizedTectonics` (no simula relieve). Su `Tick` solo ejecuta el `Step` inerte de arriba.

**Qué es realmente el sistema hoy:** un generador de relieve estático. Mapa de placas Voronoi fijo + ruido fractal, con crestas que crecen siempre en las mismas líneas hasta el tope de 12000 m, amortiguadas por el término difusivo. Sin deriva continental, sin apertura/cierre de océanos, sin ciclo de Wilson, sin conservación de corteza. Ver `ROADMAP.md` F1 para el algoritmo de advección que lo resuelve.

### 5.2 Vía GPU (declarada pero no funcional)

**Andamiaje sin ejecución real** — esto es la brecha más importante del proyecto entre *arquitectura documentada* y *código real*:

- `Tectonics/PlateMovementShader.h/.cpp`: declara 4 global shaders (`FRotatePlatePointsCS`, `FDetectCollisionsCS`, `FCalculateVelocityFieldCS`, `FAdvectElevationCS`) enlazados a `Shaders/Private/PlateMovement.usf` (302 líneas, existe y compila).
- `Tectonics/PlateSimulationGPU.cpp` (345): crea buffers RDG/SRV/UAV reales, pero **las 4 funciones `Dispatch*Shader` son stubs**. `DispatchRotationShader` tiene la llamada a `FComputeShaderUtils::Dispatch` comentada, con el texto literal "TODO: Implementar dispatch real cuando los shaders estén compilados / Por ahora es un placeholder" (`:311-345`). `CreateTextures` (`:64-73`) y `UploadPlateIDMap` (`:146-150`) también son no-op.
- `Tectonics/RasterizedTectonics.cpp` (673) repite el mismo patrón: creación de texturas/buffers en GPU real, pero el path de cómputo son TODOs (`:100, 107, 113, 326, 405, 411`).
- `Shaders/Private/TectonicRaster.usf` (457 líneas) sólo se referencia en un comentario (`TectonicTypes.h:227`), nunca se despacha desde C++.

**Conclusión verificable:** hoy la tectónica corre **100% en CPU**. La decisión arquitectónica documentada en `docs/03-geodinamica-tectonica.md` y `docs/09-stack-tecnologico.md` ("Compute Shaders GPU" como pieza central) no está realizada en código, pese a que existen ~1300 líneas de infraestructura GPU parcialmente construida. **Decisión formal (ROADMAP.md M2, 11-08-2026): se queda así (Ruta B) hasta que perfilar demuestre que CPU es insuficiente** — documentado directamente en `PlateSimulationGPU.h` y `RasterizedTectonics.h`, no solo aquí.

### 5.3 Dos pipelines CPU redundantes ejecutándose a la vez (hallazgo del 10-08-2026)

Al probar `TectonicsTestActor` en el editor apareció un planeta con relieve extremo (paredes casi verticales, visualmente como "dos esferas anidadas": el cuerpo base y una corona de picos). La causa raíz, verificada en código:

- `TectonicsTestActor::Tick` invoca **dos sistemas de tectónica independientes cada paso** (`Test/TectonicsTestActor.cpp:267,280`): `PlateSystem->Step(DeltaTime)` (el motor "oficial" de §5.1, cuyo resultado de elevación — `BoundaryInteractions::ElevationRateMaps` — nunca se consume, ver `ApplyElevationChanges` vacío en `BoundaryInteractions.cpp:626-633`) y, en paralelo, `RasterizedTectonics->Step(Params)`, que hace su **propia** detección de fronteras (comparando IDs de placa entre los 4 vecinos directos en la textura, `RasterizedTectonics.cpp:341-390`) y es el que sí escribe la elevación real que se visualiza.
- Ese segundo pathway sumaba elevación en cada celda de frontera cada paso, con tope en 12000m (`:377`), pero **sin ningún término que reparta esa diferencia con las celdas vecinas** durante la simulación — `SmoothElevation()` solo se invocaba una vez, al inicializar (`TectonicsTestActor.cpp:153-155`). Resultado: tras cientos de pasos, celdas de frontera de 1 píxel de ancho llegaban al tope de 12km junto a vecinas en la base oceánica (-3800m), generando paredes casi verticales que a distancia se emborronan en la "segunda esfera" observada.
- **Corregido, dos iteraciones:** primero se añadió un blur completo (`SmoothElevation`) cada paso — pero `UpdateMeshColors()` (`TectonicsTestActor.cpp:609-766`) resultó estar refrescando posición de vértices en vivo cada 10 frames vía `UpdateMeshSection` (contra lo que se documentó en un momento de esta sesión: no está congelada tras el init). Un blur completo cada paso, visible en vivo y acumulado sobre miles de pasos, converge a "planeta perfectamente liso" — lo que efectivamente se observó a los 1167 pasos. Se corrigió reemplazando el blur completo por una mezcla parcial (`FPlateMovementParams::DiffusionRate`, fracción 0-1 mezclada hacia el valor suavizado local cada paso, default 0.02) en `RasterizedTectonics.cpp::Step()` — se comporta como una ecuación de difusión con tasa acotada, alcanza equilibrio con el levantamiento en vez de barrerlo todo. Sigue siendo el mismo término físico (thermal erosion / mass wasting), distinto y anterior a la erosión hidráulica real de la Fase 4, que se sumará encima de este cuando exista. **La tasa no está calibrada contra nada real** — es un parámetro a ajustar por observación.
- 🔻 **Corrección del 15-08-2026:** llamar a esto "dos pipelines redundantes" se quedó corto. No compiten: `RasterizedTectonics` es el único que produce algo, y `BoundaryInteractions` es **inerte**, no redundante (`ApplyElevationChanges()` vacía). La resolución no es elegir uno, sino **conectar el segundo al primero** — su física de subducción/orogenia/hotspots es justo la que le falta al primero, que solo sabe mirar los 4 vecinos directos. Ver `ROADMAP.md` F1.

### 5.4 Escala real de Tierra + fórmula de exageración de elevación acoplada al radio (hallazgo del 10-08-2026)

`TectonicsTestActor` usaba `VisualRadius = 500m` (un planeta de juguete) y la fórmula de desplazamiento por elevación (`CreatePlanetMesh()` y `UpdateMeshColors()`, dos copias del mismo cálculo) escalaba el offset como `% del radio` (`ElevationNormalized * Radius * ElevationScale / 100`) — esto **contradecía el propio comentario del código**, que documentaba `ElevationScale` como "1.0 = metros reales, 10.0 = 10x exagerado" (un multiplicador directo, no relativo al radio). A escala de juguete el error pasaba desapercibido; al pedir escala real de la Tierra (`VisualRadius = 6371 km`) se habría vuelto absurdo (montañas de miles de km), porque el offset crecía proporcional al radio.

**Corregido:** `VisualRadius` por defecto ahora es `637100000.0f` (radio real de la Tierra en cm), y la fórmula pasa a ser `ElevationOffset = Elevation(m) * 100 * ElevationScale` — un multiplicador directo sobre la elevación real, independiente del radio, tal como decía el comentario original. `ElevationScale` por defecto baja de 50 a 15 (a escala real, 50x habría sido igual de absurdo). `TimeScale` por defecto baja de 100 a 1 — no representaba "tiempo geológico real" pese a decirlo el comentario (las constantes de tasa como `OrogenyFactor`/`SpreadingFactor` no están calibradas contra velocidades de placas reales en cm/año; ese trabajo de calibración sigue pendiente).

**Nota de rendimiento sin resolver:** `GridResolution = 128` por cara sobre un radio de 6371 km implica celdas de ~100 km de lado — muy grueso para relieve realista. Subir la resolución multiplica el coste de `RasterizedTectonics::Step()`, la difusión y la generación de malla de forma cuadrática; no se ha tocado porque es una decisión de compromiso rendimiento/fidelidad, no un bug.

### 5.5 Cámara de aproximación (M1.6, 11-08-2026)

**Implementado, sin verificar en editor** — `Simu/PlanetApproachPawn.h/.cpp`: pawn de cámara libre con velocidad log-interpolada según altitud, y "colisión" por consulta de altura contra `ATectonicsTestActor::GetSurfaceRadiusAtDirection()` (nueva, reutiliza la fórmula de elevación de la malla) en vez de colisión física real — la malla se genera deliberadamente sin colisión (`TectonicsTestActor.cpp:576`, demasiado cara de recalcular en cada regeneración). `Simu/SimuGameMode.h/.cpp` + `GlobalDefaultGameMode` en `Config/DefaultEngine.ini` lo fijan como pawn por defecto. Ver `ROADMAP.md` M1.6 para el aviso sobre overrides de GameMode a nivel de World Settings.

### 5.6 Mapeo cara↔dirección inconsistente en los polos (hallazgo del 15-08-2026)

Bug latente, aún **no corregido**. El proyecto convierte entre `(cara, U, V)` y dirección 3D en cinco sitios distintos, escritos a mano, y dos de ellos discrepan:

- `CubeSphereGrid.cpp:152` (`GetFaceAxes`, `+Z`): `U=(1,0,0)`, `V=(0,1,0)` ⇒ dirección `(U, V, 1)`
- `RasterizedTectonics.cpp:150` (case 4, `+Z`): `FVector(U, -V, 1)` ⇒ **V invertida**

Lo mismo en `−Z`, invertida en sentido contrario (`Grid: (U,-V,-1)` vs `Raster: (U,V,-1)`). Las cuatro caras ecuatoriales sí coinciden.

**Efecto:** el mapa de IDs de placa (Voronoi, construido sobre el `Grid`) y el mapa de elevación (`RasterizedTectonics`) están **espejados en los dos casquetes polares**. Las fronteras de placa que dibuja el visualizador no coinciden allí con el relieve. `RasterizedTectonics` es internamente consistente (`TectonicsTestActor` invierte el mismo mapeo torcido al muestrear), por eso el relieve se ve plausible y el bug pasa desapercibido hasta que se cruzan los dos campos.

**Copias del mapeo a unificar:** 3 en `TectonicsTestActor` (`GetSurfaceRadiusAtDirection`, `CreatePlanetMesh`, `UpdateMeshColors`) y 2 en `RasterizedTectonics` (`InitializeFromPlateSystem`, `ApplyFractalNoise`). Es la razón por la que se desincronizó. Ver `ROADMAP.md` F0.

**Relacionado:** hay **dos asignaciones distintas de placa→celda** que no dan el mismo resultado — Voronoi/JFA sobre el `Grid`, y una búsqueda de centroide más cercano `O(Res²·N)` en `InitializeFromPlateSystem`.

### 5.7 La advección hoy, y su defecto de raíz (16-08-2026)

> §5.1 describe el estado **anterior** a F1 ("las placas nunca se mueven"), ya superado: hoy sí se mueven. Esta sección describe lo que hay ahora.

**Cómo funciona.** `RasterizedTectonics::AdvectPlateField` (`:616`) hace advección semi-lagrangiana hacia atrás. Para cada celda destino y cada placa `P` se retro-rota el punto con la rotación acumulada inversa de `P` y se comprueba si en ese sitio el estado de referencia pertenecía a `P`. El **número de reclamantes** clasifica: 1 movimiento, 0 rift, ≥2 colisión.

Estructuras implicadas:

- `FaceData` — el mundo. 6 caras × Res², con `PlateIDData`, `ElevationData`, `VelocityData`, `CrustAgeData`, `CrustTypeData`, `CrustThicknessData`.
- `ReferenceData` (`:704`) — copia del estado inicial. La advección **siempre** muestrea de aquí, para no encadenar remuestreos.
- `PlateAccumRotation` (`:705`) — rotación acumulada por placa desde el inicio.
- `WriteBackToReference` (`:1764`) — devuelve al referente lo que la física cambió sobre el mundo.

**El defecto de raíz.** `WriteBackToReference` escribe elevación, edad, tipo, grosor y velocidad, y **deliberadamente no escribe `PlateIDData`** (`:1840`, con el razonamiento en el comentario). O sea: la **huella** de cada placa en el referente es el Voronoi inicial y no cambia nunca.

De ahí salen dos cosas, y la segunda es la grave:

1. **Ocho huellas rígidas rotando cada una por su lado no teselan la esfera.** Es geometría, no un bug de implementación. Medido: 28,7 % de las actualizaciones con ≥2 reclamantes, 31,6 % sin resolver, balanza de corteza 47.999/1.185.542.

2. **La propiedad y el material se leen de la misma estructura, y necesitan lo contrario.** La propiedad es una *partición* y hay que re-deducirla del estado actual en cada paso o degenera; el material es una *sustancia* y hay que leerlo una sola vez o se deshilacha. Estando soldados, arreglar uno rompe el otro — comprobado con un barrido de re-anclaje: con el referente el ID se pudre y el material está limpio; re-anclando en cada advección el ID es perfecto y vuelve la cinta del material.

**Efecto visible:** el campo de ID de placa arranca como 8 regiones limpias y a los ~200 Ma es una mezcla a escala de píxel, mientras grosor, edad, tipo y relieve siguen mostrando cuerpos limpios y coherentes entre sí. Las placas han dejado de existir como objetos.

**Consecuencias sobre lo que hay documentado como calibrado:**

- El umbral de rift `0,10 × MaxAngularSpeed` (`:943`) se fijó igualando creación y destrucción de corteza. Esa balanza estaba rota al hacerlo, así que **no está calibrado**.
- El grosor continental medio es 53,9 km con `ContinentalThickness` = 35 km: la banda de solape corre la rama de colisión sobre el 28,7 % del planeta, y ahí el material continental se apila (`:1249`).

**Lo que además no existe:** ciclo de vida de placas (`GeneratePlates()` las crea una vez; no hay ningún `Add` ni `RemoveAt` posterior, así que una placa no puede morir subducida ni nacer de un rift) y realimentación dinámica (`EulerPole` y `AngularVelocity` se leen para rotar y nunca se escriben).

Plan de arreglo y verificación en `ROADMAP.md` A10.

## 6. Simulación de flujo simple

**Prueba de concepto, no producción** — `SimpleFlowSimulation.cpp/h` (400/193 líneas).

- `StepDiffusion`, `RunConservationTest`: solver de difusión sobre el cube-sphere grid, usado para validar conservación de masa de la estructura de datos. Su propio header lo declara: *"Sprint 1.2 — Validación del sistema de corrección métrica"*.
- 🔻 **No es un modelo de erosión, hidrología ni ríos, ni un precursor de uno.** El `ROADMAP.md` anterior decía "Pipe Model reemplazando el `SimpleFlowSimulation` actual", lo que sugería que había hidrología parcial que sustituir. No la hay: la erosión es terreno virgen. `grep -r "Erosion|Precipitat|Rainfall|SeaLevel|Climate|WaterLevel"` sobre todo `Source/` devuelve **0 resultados** (el único `Temperature` es `MagmaTemperature` de vulcanismo). No existe ni una constante de nivel del mar — el océano es "elevación negativa" pintada de azul.
- Solo lo instancia `CubeSphereSubsystem`; nadie llama a sus métodos en producción.

## 7. Ruido procedimental

**Implementado, completo, pero huérfano** — `Noise/SimplexNoise.cpp` (294 líneas): simplex 3D, fractal, ridged noise en CPU. No tiene versión GPU y actualmente ningún subsistema lo consume en producción (ver gap de §4).

## 8. UI

**Implementado** — `Simu/UI/TectonicConfigWidget.cpp` (306 líneas): widget UMG que expone parámetros del simulador de placas. Sin stubs detectados.

## 9. Subsistemas no implementados (solo existen como documento)

Confirmado por ausencia total de referencias en `Source/`:

- **Atmósfera** (Shallow Water Equations, Coriolis, ciclo del agua) — [`docs/04-atmosfera-clima.md`](docs/04-atmosfera-clima.md)
- **Hidrosfera/erosión real** (Pipe Model, estratigrafía NPK) — [`docs/05-hidrosfera-erosion.md`](docs/05-hidrosfera-erosion.md)
- **Climatología profunda** (ciclo carbono-silicatos, albedo dinámico) — [`docs/06-climatologia-efecto-invernadero.md`](docs/06-climatologia-efecto-invernadero.md)
- **Biosfera/agentes evolutivos** (genoma vectorial, metabolismo, Niagara) — [`docs/07-biosfera-evolucion.md`](docs/07-biosfera-evolucion.md)
- **Guardado/serialización**: implementado y verificado el 11-08-2026 — `Tectonics/TectonicSaveGame.h` + `ATectonicsTestActor::SaveSimulation/LoadSimulation` (teclas K/L). Roundtrip probado en editor: guarda, carga, y el planeta resultante es visualmente idéntico al momento del guardado. Ver `ROADMAP.md` M5 para el razonamiento de por qué basta con guardar la semilla + estado evolucionado (no toda la topología).

## 10. Riesgos y deuda técnica activa

Ordenados por impacto, tras la auditoría del 15-08-2026.

> **Reordenado el 16-08-2026.** El riesgo 1 ya no es que las placas no se muevan —se mueven— sino que **se disuelven**:

0. 🔴 **Las placas dejan de existir como objetos** (§5.7). El campo de ID degenera de 8 regiones a ruido de píxel en ~200 advecciones, y toda la clasificación de fronteras se apoya en contar reclamantes sobre ese campo. Arrastra la balanza de corteza, el área continental, el grosor medio y el escalonado. Bloquea F4: la erosión correría sobre un relieve producido en parte por rifts y subducciones inventados. Plan en `ROADMAP.md` A10.
1. ~~**Las placas no se mueven** (§5.1).~~ **Resuelto en F1.** Se mueven. Ver §5.7 para lo que hay hoy.
2. **Mapeo de caras inconsistente en los polos** (§5.6). Corrompe en silencio cualquier sistema nuevo que cruce campos por cara — es decir, todos los de las fases siguientes. Es F0.
3. **`BoundaryInteractions` inerte** (§5.1): 693 líneas de física real ejecutándose cada paso sobre 6×Res² celdas sin que nadie lea el resultado. Coste puro hasta que F1 le dé consumidor.
4. **Código muerto: ~150 KB de ~430 KB de fuentes.** `Streaming/ChunkStreamingManager` (27 KB) y `CubeSphereVisualizerComponent` (16 KB) tienen 0 referencias externas; `PlateSimulationGPU` + `PlateMovementShader` + los 4 `.usf` (~50 KB) tienen todos los `Dispatch*` vacíos; `Nanite/`+`LOD/`+`QuadTree/` (~80 KB) solo los usa un actor cuyo Nanite está comentado.
5. 🔻 **La suite de tests está en rojo y nadie lo sabía**: 3 de 8 tests `Simu.*` fallan, y fallaban ya antes de los cambios de F0 (verificado ejecutando la suite en `HEAD` limpio, 15-08-2026). M3 y M4 se cerraron como "✅ completo" habiendo solo **compilado**, no ejecutado:
   - `Simu.CubeSphere.AdjacencyContinuity`: 124 errores de continuidad entre caras. Bug de corrección real que bloquea el drenaje de F4.
   - `Simu.Tectonics.BoundaryInteractionsSanity` y `Simu.Tectonics.ElevationStaysBounded`: abortan en la primera aserción porque `GeneratePlates()` devuelve `false` — el JFA de `SphericalVoronoi` deja un 11–19 % de celdas sin asignar a resolución 32. Nunca han llegado a comprobar lo que su nombre dice.
6. **Cobertura de test engañosa incluso en los que pasan**: **ninguno detecta que las placas no se mueven** — `UPlateKinematics` se prueba de forma aislada mientras nadie lo llama en producción. Testear la unidad no sirve si no está cableada.
7. **Parámetros sin calibrar entre sí**: `DiffusionRate`, `OrogenyFactor`, `SpreadingFactor`, `TimeScale`, `ElevationScale`. Tocar uno obliga a reajustar los demás (§5.3, §5.4).
8. **Coste `O(Res²)` acumulativo y acoplado al framerate**: con `GridResolution = 128` sobre 6371 km las celdas son de ~100 km — demasiado grueso para relieve realista, y subir resolución escala cuadráticamente. Sin presupuesto de tiempo por paso se repite la espiral de la muerte de M1.
9. **Renderizado planetario sin resolver** (§4): Nanite por parche resultó inviable (build síncrono bloqueante) y está desactivado. El único camino verificado hoy es el `ProceduralMesh` de `TectonicsTestActor`, sin LOD.


## 11. Visor de campos de diagnóstico (F0.5, 15-08-2026)

**Núcleo implementado, sin verificar visualmente en el editor** — `Visualization/PlanetScalarField.h`, `Visualization/PlanetFieldRegistry.h/.cpp`.

Existe porque todo campo que produce la simulación tiene la misma forma: 6 caras × Res² valores. Cambian el significado y las unidades, no la estructura. Un único visor los pinta todos, y añadir un campo nuevo cuesta registrar una lambda, no escribir un visualizador.

- `FPlanetScalarField`: descriptor con Id, etiqueta, unidad, paleta, escala, resolución y rango. **No copia datos**: guarda una `TFunction` que devuelve el array vivo de una cara, así la vista nunca se desincroniza del estado.
- `UPlanetFieldRegistry`: guarda los campos, mantiene el activo, calcula rangos y traduce valor → color.
- **Paleta y escala son ortogonales.** La paleta interpreta un valor ya normalizado (`Sequential` viridis, `Diverging` centrada en un cero con significado, `Categorical` sin interpolar, `Terrain` con el nivel del mar en el centro); la escala transforma el valor antes (`Linear`, `Logarithmic`). El log es imprescindible para el caudal acumulado de F4: en lineal, el cauce principal satura y los afluentes son indistinguibles del fondo.
- Rango automático por **percentiles 2/98**, no mín/máx: un solo outlier aplanaba el mapa entero.
- Integrado en `ATectonicsTestActor::UpdateMeshColors`; se conmuta con **F / G** y la leyenda aparece en el HUD.
- Campos registrados hoy: elevación (rango fijo a propósito, para que se note si el relieve se desboca), ID de placa, edad de corteza, tipo de corteza.
- **Pendiente:** campos vectoriales (flechas) y vista de sección/perfil.
- Cobertura: `Tests/PlanetFieldTests.cpp`, 6 tests.


## 12. Isostasia, batimetría y nivel del mar (F2, 15/16-08-2026)

**Implementado y verificado en editor.** Desde F2 el estado primario ya no es la elevación sino el **grosor de corteza**; la elevación se deriva.

- **Flotación de Airy**: una columna de grosor T y densidad Rc sobre manto Rm sobresale `T·(Rm−Rc)/Rm`. Con densidades reales (continental 2750, oceánica 2900, manto 3300) y 35 km de corteza salen 5.833 m; restando el datum, +840 m — la altura media real de los continentes. Que el número correcto salga de densidades reales y no de una constante ajustada es la validación de la fórmula.
- **Batimetría por hundimiento térmico**: `d = 2500 + 350·√(edad en Ma)`, acotado a 5.750 m. Por eso un mapa de profundidad oceánica real es esencialmente un mapa de la edad del fondo, y por eso el campo de edad tiene ahora consecuencia visible.
- **Nivel del mar** explícito, resuelto por **Newton** (no bisección — ver §13) para conservar el volumen de océano. Responde a la tectónica: mucha dorsal joven ⇒ corteza caliente ocupando volumen ⇒ cuenca menos honda ⇒ agua desplazada que inunda continentes.
- **Conservación de corteza continental**: en colisión continente-continente el material se apila en vez de destruirse; las montañas salen de ahí por flotación, sin término de levantamiento inventado.

Fracción de tierra emergida resultante: 22–32 % según la semilla, frente al 29 % real.

## 13. Rendimiento del paso de simulación

Instrumentado por fases (`FTectonicStepTimings`, visible en el HUD). Medir cambió la lista de sospechosos por completo — la intuición falló dos veces antes de instrumentar.

| Fase | Antes | Después |
|---|---|---|
| Nivel del mar + isostasia | 78,2 ms | **6,8 ms** |
| Simulación por frame | 116,6 ms | **27,6 ms** |

- **Nivel del mar**: hacía 40 iteraciones de bisección, cada una recorriendo 6×Res² celdas — 15,7 M de lecturas por paso para resolver un único número. Sustituido por Newton, que aprovecha dos cosas que la bisección tiraba: el nivel apenas se mueve entre pasos (el valor anterior ya es buena estimación) y la derivada es gratis (`dV/dS` = área sumergida, contada en la misma pasada). 2-3 pasadas.
- **Advección**: las pasadas de conteo y de resolución hacían el mismo trabajo caro. Se cachea el caso mayoritario (un único reclamante).
- **Paso fijo** desacoplado del framerate: la física dejó de depender de a qué fps corras.

Coste dominante restante: la advección (~174 ms por ejecución, amortizada a ~11 ms/paso porque no corre en todos).
