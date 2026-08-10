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

- **Motor:** Unreal Engine 5.7 (`Simu.uproject`)
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

- `QuadTree/CubeSphereQuadTree.cpp` (656 líneas): árbol de subdivisión/colapso por cara. TODO: culling de frustum por nodo (`:512`), mapeo de aristas entre caras (`:611`).
- `LOD/CubeLODController.cpp` (499 líneas): selección de LOD por distancia con `TickComponent`. TODO: test de frustum contra bounding sphere del nodo (`:155`).
- `Streaming/ChunkStreamingManager.cpp` (738 líneas): carga/descarga de chunks con `Tick`. No hay streaming asíncrono más allá de los mecanismos estándar de UE.

Los 4 TODOs pendientes son de **correctness/performance**, no cosméticos: sin culling de frustum el coste de LOD escala con el planeta completo, no con lo visible; sin mapeo de aristas entre caras el LOD puede generar grietas (T-junctions) en los bordes del cubo.

## 4. Renderizado — Malla Nanite

**Parcial** — `Nanite/PlanetNaniteMesh.cpp` (840 líneas).

- Genera parches reales de `UStaticMesh`, gestiona un pool de `UStaticMeshComponent`, configura Nanite build settings correctamente.
- **Gap crítico:** `GenerateProceduralHeightmap` (líneas 434-473) rellena el heightmap con `Sin(X)*Cos(Y)` — comentario explícito "Por ahora, datos de placeholder" (:452, :463). **No usa** ni `Noise/SimplexNoise.cpp` (completo pero huérfano) ni la elevación calculada por el motor de tectónica. Esto significa que **el planeta que se renderiza hoy no refleja la simulación tectónica** que corre por debajo.
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

**Conclusión verificable:** hoy la tectónica corre **100% en CPU**. La decisión arquitectónica documentada en `docs/03-geodinamica-tectonica.md` y `docs/09-stack-tecnologico.md` ("Compute Shaders GPU" como pieza central) no está realizada en código, pese a que existen ~1300 líneas de infraestructura GPU parcialmente construida.

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
- **Guardado/serialización**: no existe ningún `SaveGame` ni `FArchive` custom en el proyecto. El estado de una sesión de simulación no sobrevive a un reinicio.

## 10. Riesgos y deuda técnica activa

1. **Desconexión Nanite ↔ Tectónica/Noise** (§4): el output visual no refleja la simulación. Es el gap con mayor impacto percibido para un portfolio ("se ve" pero no está conectado a lo que "se calcula").
2. **Doble implementación de tectónica (CPU real + GPU muerta)**: mantener ~1300 líneas de código GPU no funcional es coste de lectura/mantenimiento sin beneficio actual. Hay que decidir: completar el dispatch o retirar/aislar el código detrás de un flag experimental.
3. **4 TODOs de correctness en LOD/QuadTree** (§3): afectan tanto rendimiento (culling) como corrección visual (grietas en bordes de cara).
4. **Cobertura de test desigual**: solo `CubeSphereGrid` tiene tests automatizados. Tectónica (el subsistema más grande y con más lógica de branching) no tiene ninguno.
5. **Sin control de versiones**: el directorio no es un repositorio git. Para un proyecto de este tamaño (>9000 líneas de C++ propio) es el riesgo más alto y más barato de mitigar.
6. **Sin persistencia**: cualquier sesión de simulación se pierde al cerrar el editor/juego.
