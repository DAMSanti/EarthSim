# SPECS.md — Especificación Técnica (Simu)

> Este documento describe el sistema **tal como existe hoy en el código** (10-08-2026), no la visión aspiracional. Para la visión completa del producto ver [`docs/01-resumen-ejecutivo.md`](docs/01-resumen-ejecutivo.md) y el resto de `docs/`. Cada sección aquí marca explícitamente **Implementado** / **Parcial** / **No implementado**, con referencias a archivo para poder verificarlo.

## 0. Resumen de estado

| Subsistema | Estado | Motor de cómputo |
|---|---|---|
| Cube-Sphere grid + métricas | ✅ Implementado, con tests | CPU |
| QuadTree + LOD | ✅ Implementado (2 TODOs) | CPU |
| Streaming de chunks | ✅ Implementado | CPU |
| Malla Nanite / heightmap | ⚠️ Parcial (placeholder) | CPU |
| Ruido Simplex | ✅ Implementado, desconectado | CPU |
| Tectónica de placas | ✅ Implementado (motor CPU maduro) | CPU |
| Tectónica — vía GPU | ⚠️ Andamiaje sin dispatch real | GPU (no funcional) |
| Flujo/difusión simple | ⚠️ Prueba de concepto, no es erosión | CPU |
| UI de configuración tectónica | ✅ Implementado | — |
| Atmósfera (SWE) | ❌ No implementado | — |
| Erosión/hidrología real | ❌ No implementado | — |
| Climatología (carbono/albedo) | ❌ No implementado | — |
| Biosfera/agentes | ❌ No implementado | — |
| Guardado/serialización | ❌ No implementado | — |
| Control de versiones (git) | ❌ No inicializado | — |

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
- **Test cubierto:** `Source/CubeSphere/Tests/CubeSphereGridTests.cpp` — 27 aserciones en un `IMPLEMENT_SIMPLE_AUTOMATION_TEST`. Es el único subsistema con cobertura de test real.

Diseño de referencia ampliado en [`docs/02-estructura-datos-espaciales.md`](docs/02-estructura-datos-espaciales.md).

## 3. LOD, QuadTree y Streaming

**Implementado, con deuda técnica marcada:**

- `QuadTree/CubeSphereQuadTree.cpp` (~770 líneas): árbol de subdivisión/colapso por cara. Culling de frustum y mapeo de aristas entre caras **resueltos el 11-08-2026** (ver `ROADMAP.md` M3).
- `LOD/CubeLODController.cpp` (~520 líneas): selección de LOD por distancia con `TickComponent`. Frustum culling por cono implementado en `IsInFrustum` (11-08-2026).
- `Streaming/ChunkStreamingManager.cpp` (738 líneas): carga/descarga de chunks con `Tick`. No hay streaming asíncrono más allá de los mecanismos estándar de UE.

Los 4 TODOs pendientes son de **correctness/performance**, no cosméticos: sin culling de frustum el coste de LOD escala con el planeta completo, no con lo visible; sin mapeo de aristas entre caras el LOD puede generar grietas (T-junctions) en los bordes del cubo.

## 4. Renderizado — Malla Nanite

**Parcial** — `Nanite/PlanetNaniteMesh.cpp` (840 líneas).

- Genera parches reales de `UStaticMesh`, gestiona un pool de `UStaticMeshComponent`, configura Nanite build settings correctamente.
- **Gap parcialmente cerrado (11-08-2026):** `GenerateProceduralHeightmap` ya no usa `Sin(X)*Cos(Y)` (placeholder discontinuo entre parches/caras) — ahora usa `FSimplexNoise::SphereFractalNoise` sobre la dirección 3D real en la esfera (continuo, sin costuras). Sigue sin usar la elevación calculada por la simulación tectónica: **el planeta que se renderiza sigue sin reflejar la tectónica**.
- **Bloqueador confirmado, no solo sospechado:** `NaniteConfig.PlanetMaterial` (`NaniteTypes.h:75`) no tiene valor por defecto, y `Content/` solo contiene `M_VertexColor.uasset` (para el pipeline de `TectonicsTestActor`, no para Nanite). El material WPO/Displacement del pipeline Nanite **no existe en el proyecto** — `GetPatchMaterial()` devuelve `nullptr` en runtime. Requiere crearlo en el Material Editor (fuera del alcance de edición por código/texto); especificación técnica en `ROADMAP.md` M1, incluyendo el aviso de que Nanite necesita el pin **Displacement**, no World Position Offset clásico, para desplazamiento grande de terreno.
- El plan original en `docs/08-renderizado-visualizacion.md` describe desplazamiento vía World Position Offset (WPO) en shader sobre Nanite; la implementación actual en cambio genera geometría desplazada en CPU al construir la malla. Es una decisión válida, pero diverge del documento original y debería quedar explícita ahí también.

## 5. Tectónica de placas

### 5.1 Motor CPU (núcleo real del sistema, ~2800 líneas)

**Implementado y es el subsistema más maduro del proyecto** tras el grid base:

- `Tectonics/SphericalVoronoi.cpp` (404): generación de placas por Voronoi esférico. TODO: propagación entre caras (`:228`).
- `Tectonics/TectonicPlateSystem.cpp` (510): contiene el **loop de simulación real**, `Step(DeltaTime)` (`:374-399`) — envejece placas, invoca `BoundaryInteractionSystem->ProcessAllBoundaries`, re-detecta fronteras.
- `Tectonics/PlateKinematics.cpp` (441): rotación por cuaterniones, cálculo de velocidades.
- `Tectonics/BoundaryInteractions.cpp` (693): subducción, orogenia, expansión (spreading), vulcanismo. TODO: modificación de propiedades de corteza por celda (`:609`).
- `Tectonics/TectonicVisualizerComponent.cpp` (666) y `TectonicPlanetActor.cpp` (165, con `Tick`): orquestación y visualización.
- `Test/TectonicsTestActor.cpp` (1008): actor de debug/test interactivo grande, con su propio `Tick`.

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
- **Sigue sin resolver:** la duplicidad de pipelines en sí (`PlateSystem`/`BoundaryInteractions` calculando datos que nadie lee, mientras `RasterizedTectonics` reimplementa su propia detección de fronteras más simple). Es el mismo patrón que la duplicidad CPU/GPU de §5.2 — código construido en paralelo sin conectar — y no se ha resuelto todavía qué pipeline debería ser la única fuente de verdad.

### 5.4 Escala real de Tierra + fórmula de exageración de elevación acoplada al radio (hallazgo del 10-08-2026)

`TectonicsTestActor` usaba `VisualRadius = 500m` (un planeta de juguete) y la fórmula de desplazamiento por elevación (`CreatePlanetMesh()` y `UpdateMeshColors()`, dos copias del mismo cálculo) escalaba el offset como `% del radio` (`ElevationNormalized * Radius * ElevationScale / 100`) — esto **contradecía el propio comentario del código**, que documentaba `ElevationScale` como "1.0 = metros reales, 10.0 = 10x exagerado" (un multiplicador directo, no relativo al radio). A escala de juguete el error pasaba desapercibido; al pedir escala real de la Tierra (`VisualRadius = 6371 km`) se habría vuelto absurdo (montañas de miles de km), porque el offset crecía proporcional al radio.

**Corregido:** `VisualRadius` por defecto ahora es `637100000.0f` (radio real de la Tierra en cm), y la fórmula pasa a ser `ElevationOffset = Elevation(m) * 100 * ElevationScale` — un multiplicador directo sobre la elevación real, independiente del radio, tal como decía el comentario original. `ElevationScale` por defecto baja de 50 a 15 (a escala real, 50x habría sido igual de absurdo). `TimeScale` por defecto baja de 100 a 1 — no representaba "tiempo geológico real" pese a decirlo el comentario (las constantes de tasa como `OrogenyFactor`/`SpreadingFactor` no están calibradas contra velocidades de placas reales en cm/año; ese trabajo de calibración sigue pendiente).

**Nota de rendimiento sin resolver:** `GridResolution = 128` por cara sobre un radio de 6371 km implica celdas de ~100 km de lado — muy grueso para relieve realista. Subir la resolución multiplica el coste de `RasterizedTectonics::Step()`, la difusión y la generación de malla de forma cuadrática; no se ha tocado porque es una decisión de compromiso rendimiento/fidelidad, no un bug.

## 6. Simulación de flujo simple

**Prueba de concepto, no producción** — `SimpleFlowSimulation.cpp/h` (400/193 líneas).

- `StepDiffusion`, `RunConservationTest`: solver de difusión sobre el cube-sphere grid, usado para validar conservación de masa de la estructura de datos.
- No es un modelo de erosión, hidrología ni ríos. El modelo real ("Pipe Model") descrito en `docs/05-hidrosfera-erosion.md` no existe en código.

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

1. **Desconexión Nanite ↔ Tectónica/Noise** (§4): el output visual no refleja la simulación. Es el gap con mayor impacto percibido para un portfolio ("se ve" pero no está conectado a lo que "se calcula").
2. **Doble implementación de tectónica (CPU real + GPU muerta)**: mantener ~1300 líneas de código GPU no funcional es coste de lectura/mantenimiento sin beneficio actual. Hay que decidir: completar el dispatch o retirar/aislar el código detrás de un flag experimental.
3. **4 TODOs de correctness en LOD/QuadTree** (§3): afectan tanto rendimiento (culling) como corrección visual (grietas en bordes de cara).
4. **Cobertura de test desigual**: solo `CubeSphereGrid` tiene tests automatizados. Tectónica (el subsistema más grande y con más lógica de branching) no tiene ninguno.
5. **Sin control de versiones**: el directorio no es un repositorio git. Para un proyecto de este tamaño (>9000 líneas de C++ propio) es el riesgo más alto y más barato de mitigar.
6. **Sin persistencia**: cualquier sesión de simulación se pierde al cerrar el editor/juego.
