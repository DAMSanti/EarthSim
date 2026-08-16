# SPECS.md — Estado real del código

> **Qué es este documento.** Lo que existe **hoy en el código**, archivo por archivo. No la visión, no el plan.
>
> Para lo que el simulador *debe* hacer, [`REQUISITOS.md`](REQUISITOS.md). Para el orden de trabajo, [`ROADMAP.md`](ROADMAP.md). Para por qué las cosas son como son, [`ANEXO.md`](ANEXO.md).
>
> **Última verificación: 16-08-2026**, con la suite de automatización ejecutada de verdad (no solo compilada) sobre `HEAD` = `ddca8c9`, rama `fix/adveccion-referente`.

---

## 0. Resumen de estado

| Subsistema | Estado | Motor |
|---|---|---|
| Rejilla cubo-esférico + métricas | ✅ Implementado, con tests | CPU |
| Mapeo cara↔dirección (fuente única) | ✅ Implementado, 4 tests | CPU |
| Generación de placas (Voronoi exacto) | ✅ Implementado | CPU |
| Cinemática de placas (advección) | ✅ Implementada y verificada | CPU |
| **Propiedad y material separados** | ✅ Implementado (§3.3) — el arreglo estructural de F1 | CPU |
| Frontera como objeto | ❌ No implementado: se infiere contando reclamantes | — |
| Ciclo de vida de placas (nacer/morir) | ❌ Imposible por construcción | — |
| Dinámica (tirón de losa, polos escribibles) | ❌ No implementado | — |
| Isostasia (Airy) / grosor como estado primario | ✅ Implementado y verificado (§4) | CPU |
| Batimetría por edad (hundimiento térmico) | ✅ Implementado (§4) | CPU |
| Nivel del mar con volumen conservado | ✅ Implementado, resuelto por Newton (§4) | CPU |
| Clima diagnóstico (T / viento / precipitación) | ✅ Implementado, 4 tests (§5) | CPU |
| Drenaje (acumulación de flujo D8) | ✅ Implementado, 3 tests (§6) | CPU |
| Lagos y depresiones | ❌ No implementado | — |
| Erosión / sedimento / suelos | ❌ No implementado | — |
| Atmósfera dinámica (SWE) / ciclo del agua | ❌ No implementado | — |
| Océano, criosfera, carbono, albedo | ❌ No implementado | — |
| Biosfera / agentes | ❌ No implementado | — |
| Visor de campos de diagnóstico | ✅ Implementado, 9 campos, 6 tests (§7) | CPU |
| Renderizado planetario con LOD | ❌ No implementado (§8) | — |
| QuadTree + LOD | ⏸️ Implementado, **dormido** hasta F6 (§9) | CPU |
| Ruido Simplex | ✅ Implementado y en uso | CPU |
| Flujo/difusión simple | ⚠️ Validador de conservación de masa, **no es erosión** | CPU |
| UI de configuración tectónica | ✅ Implementado | — |
| Guardado / serialización | ✅ Implementado y verificado en editor | — |
| Cámara de aproximación | ✅ Implementada | — |
| Vía GPU / compute shaders | 🗑️ **Borrada** (era andamiaje con todos los `Dispatch` vacíos) | — |

**Cifras:** ~15.500 líneas de C++ en 2 módulos, 40 tests de automatización (36 en verde), 9 campos de diagnóstico registrados.

---

## 1. Plataforma

- **Motor:** Unreal Engine **5.8** (`Simu.uproject`; migrado desde 5.7, `BuildSettingsVersion.V7`)
- **Módulos:** `Simu` (Runtime, `Default`) y `CubeSphere` (Runtime, `PostConfigInit`)
- **Plugins habilitados:** ProceduralMeshComponent, Niagara, Water, Landmass, GeometryScripting
- **Plataforma objetivo:** Windows
- **Assets:** `Content/Materials/M_PlanetDisplacement`, `M_PlanetFieldUnlit`, `M_VertexColor`, `Content/test.umap`

Nanite se usaría vía flags nativos de `UStaticMesh`, no es un plugin. **Hoy no se usa.**

### Parámetros por defecto de la simulación

| Parámetro | Valor | Significado |
|---|---|---|
| `RasterResolution` | 256 | Ráster de simulación por cara ⇒ ~39 km/celda |
| `GridResolution` | 128 | Malla de visualización por cara ⇒ ~78 km/quad |
| `VisualRadius` | 6.371 km | Radio real de la Tierra |
| `NumPlates` | 8 | Placas iniciales |
| `TimeScale` | 1,0 | Aceleración temporal |
| `ElevationScale` | 15,0 | Exageración vertical (multiplicador directo sobre metros reales) |

---

## 2. Rejilla cubo-esférico

**Implementado y con cobertura.** `CubeSphereGrid.cpp` (421), `CubeSphereMetrics.cpp` (367), `CubeFaceMapping.h` (135).

- Proyección cubo → esfera; conversión geo (lat/lon) ↔ cara/celda; adyacencia entre las 6 caras.
- **Fuente única del mapeo cara↔dirección** en `CubeFaceMapping.h`. `UCubeSphereGrid::GetFaceAxes` delega en ella. Convenio dextrógiro en las 6 caras.
- **Vecindad por geometría** (`GetNeighborCell`), no por tabla de rotaciones de arista.
- Corrección de distorsión métrica: factores de escala de área por celda.
- **Tests:** `CubeFaceMappingTests.cpp` (4: round-trip, quiralidad, acuerdo con la rejilla, cobertura) y `CubeSphereGridTests.cpp` (3: básico, continuidad de adyacencia, cobertura total). **Los 7 en verde.**

> `Simu.CubeSphere.AdjacencyContinuity` fallaba con 124 errores antes de F0. Ya no.

---

## 3. Tectónica de placas

`RasterizedTectonics.cpp` es el archivo más grande del proyecto (2.383 líneas) y contiene el motor real.

### 3.1 Generación

`SphericalVoronoi.cpp` (313) — Voronoi esférico **por fuerza bruta exacta**, sustituyendo al JFA que dejaba 11–19 % de celdas sin asignar a resolución baja. Las fronteras se perturban con ruido fractal para que las placas no sean casquetes limpios.

`TectonicPlateSystem.cpp` (534) — contenedor de placas y sus propiedades (ID, polo de Euler, velocidad angular, tipo de corteza). Su `Step()` solo envejece placas: el motor real es el ráster.

### 3.2 Estado del mundo

`FTectonicFaceTextureData`, 6 caras × Res²:

| Campo | Tipo | Categoría |
|---|---|---|
| `PlateIDData` | uint8 | primario (propiedad) |
| `CrustAgeData` | float (Ma) | primario (material) |
| `CrustTypeData` | uint8 | primario (material) |
| `CrustThicknessData` | float (m) | **primario** desde F2 |
| `VelocityData` | FVector2f | derivado |
| `ElevationData` | float (m) | **derivado** por isostasia |

### 3.3 Propiedad y material — la arquitectura actual

Es el cambio estructural de F1, y lo que hace que el campo de ID de placa siga siendo un mapa de regiones a los 1000 Ma. Diagnóstico completo en [`ANEXO.md` A10](ANEXO.md#a10-el-defecto-de-raíz-de-f1-la-propiedad-y-el-material).

**Antes:** una única estructura `ReferenceData` de la que se leían dos cosas con requisitos opuestos. Resultado medido: balanza de corteza 0,04, 31,6 % de celdas sin resolver, y el campo de ID degenerando de 8 regiones a ruido de píxel en ~200 advecciones.

**Ahora:**

- **La propiedad** se decide contra `FaceData` —el mundo de *ahora*— con la rotación **incremental** de esta advección. El mundo es una partición por construcción.
- **El material** vive en `PlateMaterial[P]`, un `FPlateMaterialFrame` por placa (6·Res² celdas: `Occupied`, `CrustAge`, `CrustType`, `CrustThickness`, `Elevation`) en el marco propio de esa placa, leído con `PlateAccumRotation[P]`. **Un solo remuestreo** por muchas advecciones que pasen.
- `WriteBackToPlateFrames()` devuelve al marco lo que la física cambió, **recorriendo el marco y tirando del mundo** — así cada celda del marco se escribe exactamente una vez.
- `ReadPlateMaterial()` devuelve `false` si el marco no tiene material ahí, y el llamante cae a un respaldo (lectura encadenada). **Instrumentado: 1,93 % de 4,8 M de lecturas, estable.**

`ReferenceData` fue **eliminado**, y con él la recuperación por tolerancia de media celda y la rama de «celda sin resolver que conserva su estado».

**Resultado medido** (`LongRunStability`, 1000 Ma):

| Métrica | Antes | Ahora |
|---|---|---|
| Balanza creada/destruida | 0,04 | **0,98** |
| Celdas sin resolver | 31,6 % | **0,0001 %** |
| Escalonado de bordes | ×2,96 | **×1,96** |

### 3.4 Advección

`AdvectPlateField` — semi-lagrangiana hacia atrás. Para cada celda destino y cada placa `P` se retro-rota el punto y se comprueba si pertenece a `P` en el mundo actual. El **número de reclamantes** clasifica: 1 movimiento, 0 rift, ≥2 colisión.

> **Esta es la limitación de modelo que queda abierta.** La frontera no existe como objeto: se infiere a posteriori del conteo. Es lo que impide clasificar una transformante y lo que hace que el coste esté invertido (se calcula con precisión perfecta el interior, que solo rota).

- **No corre cada paso.** Se acumula tiempo hasta que el desplazamiento máximo alcanza ~1 píxel del ráster (con los valores por defecto, la placa más rápida gira 1/76 de píxel por paso).
- Techo de 8 advecciones por paso para no bloquear el frame. **El tiempo descartado al agotarlo está instrumentado y mide 0,00 Ma sobre 4.000 pasos.**
- Sub-pasos de máx. 0,5 Ma en la física.

### 3.5 Física de frontera implementada

- **Subducción por densidad:** oceánica bajo continental; entre oceánicas, subduce la más vieja.
- **Orogenia:** en colisión continental el material se **apila** (grosor), calibrada contra el Himalaya. `OrogenyFactor` = 0,08 adimensional.
- **Rift por divergencia real:** se proyecta `ω × r` de las placas vecinas sobre la dirección de alejamiento. Umbral `0,10 × MaxAngularSpeed`. ⚠️ **Esta calibración se hizo con la balanza de corteza rota y hay que rehacerla.**
- **Acreción de arco:** corteza oceánica convergente que supera `ArcMaturityThickness` (~20 km) pasa a continental. Restringida a celdas que tocan continente.
- **Difusión / erosión termal:** `DiffusionRate` = 0,02 por Ma, sobre el **grosor**, no sobre la elevación.

### 3.6 Lo que no existe

- **Frontera como objeto** con normal y velocidad relativa proyectada.
- **Ciclo de vida:** `GeneratePlates()` crea las placas una vez; no hay ningún `Add` ni `RemoveAt` posterior. Una placa no puede morir subducida ni nacer de un rift, **por construcción**.
- **Dinámica:** `EulerPole` y `AngularVelocity` se leen para rotar y **nunca se escriben**.
- **Vulcanismo y hotspots:** la física está escrita en `BoundaryInteractions.cpp` (693 líneas) pero el componente está **desactivado**, y con motivo: produce tasas de elevación en metros, y la elevación es un campo derivado desde F2 — todo lo que escribiera se perdería en el mismo paso. Además lee el mapa de Voronoi inicial, caducado desde la primera advección. Ver [`ANEXO.md` A9](ANEXO.md#a9-los-dos-puntos-aparcados-de-f1).

---

## 4. Isostasia, batimetría y nivel del mar

**Implementado y verificado en editor.** Desde F2 el estado primario es el **grosor de corteza** y la elevación se deriva.

- **Flotación de Airy** con densidades reales: continental 2750, oceánica 2900, manto 3300 kg/m³. Una columna de 35 km da 5.833 m; restando `IsostaticDatum` = 4.993 m, **+840 m**, la altura media real de los continentes. El número correcto sale de las densidades, no de una constante ajustada.
- **Techo de grosor:** `MaxThickness` = 75 km (límite de delaminación) ⇒ ~7.507 m por Airy.
- **Batimetría por hundimiento térmico:** `d = 2500 + 350·√(edad en Ma)`, acotada a 5.750 m. Por eso el campo de edad tiene consecuencia visible.
- **Nivel del mar** explícito, resuelto por **Newton** conservando `TargetOceanVolume`. La derivada es gratis (área sumergida, contada en la misma pasada). 2–3 iteraciones frente a las 40 de la bisección.
- **Conservación de corteza continental** en colisión: se apila en vez de destruirse.
- `RebuildElevationFromIsostasy()` reconstruye la elevación entera al final de cada paso.

**Fracción de tierra emergida:** 22–32 % según semilla al inicio (Tierra real: 29 %). ⚠️ **Cae al 13,2 % a los 1000 Ma** — defecto abierto, §12.

---

## 5. Clima diagnóstico

**Implementado, 4 tests en verde.** `Climate/PlanetClimate.cpp` (276), `.h` (175).

Deliberadamente barato: campos diagnósticos, no dinámica de fluidos. Existe porque la erosión necesita caudal y el caudal necesita lluvia.

| Componente | Modelo | Valores |
|---|---|---|
| Temperatura | f(latitud, altitud) | Ecuador 27 °C, polo −25 °C, gradiente 6,5 °C/km |
| Viento | Bandas **alternantes** | Alisios del este, oestes, del este cerca del polo |
| Humedad | Transporte a barlovento + continentalidad | ~8 celdas de alcance |
| Precipitación | Perfil por bandas **no monótono** | 2500 / **200** / 1100 / 150 mm/año |
| Orografía | Ganancia por ascenso | 900 mm/año por km de barrera |
| Sombra de lluvia | Pérdida fraccional | 45 % de humedad por km superado |

**El mínimo subtropical de 200 mm/año es el requisito**, no un detalle: es lo que separa un planeta con desiertos (Sahara, Arabia, Kalahari) de una bola con lluvia decreciente. Un test comprueba explícitamente que el perfil **no** es monótono.

**Validación sobre planeta simulado 200 Ma** (`Simu.Climate.RainShadow`): 74 cordilleras medidas, sotavento más seco en el **89 %**, 1.612 mm/año a barlovento frente a 359 a sotavento — contraste ×4,5.

⚠️ **Sin verificar a ojo en el editor todavía.**

---

## 6. Hidrología — drenaje

**Implementado, 3 tests en verde.** `Hydrology/PlanetHydrology.cpp` (274), `.h` (151).

- **D8 con corrección de distancia diagonal.** Sin ella el drenaje prefiere las diagonales y las redes salen sesgadas a 45°.
- **Acumulación de flujo en una sola pasada**, recorriendo de mayor a menor altura: cuando le toca a una celda ya ha recibido todo lo de arriba.
- **Caudal en unidades reales** (m³/año): mm/año × área de celda corregida.
- Cruza las aristas del cubo con la vecindad geométrica.
- Estadísticas: celdas de tierra, celdas cauce, sumideros, caudal máximo.
- Campo registrado en el visor: **área drenada en escala logarítmica** — en lineal el cauce principal satura y los afluentes son indistinguibles del fondo.

**Resolución resuelta por medición** (`Simu.Hydrology.DrainageResolution`): la fracción de cauce **satura entre Res 128 y 192** (2,1 % → 2,2 %), así que la topología de la red ya está resuelta. Sumideros al 0,7 %.

`LakeDepthData` está declarado pero **no se calcula**: el tratamiento de depresiones es F4B.

**No existe:** incisión fluvial, transporte de sedimento, deposición, estratigrafía, NPK.

---

## 7. Visor de campos de diagnóstico

**Implementado.** `Visualization/PlanetFieldRegistry.cpp` (368), `PlanetScalarField.h` (127), `PlanetFieldMaterial.cpp` (70). 6 tests en verde.

Existe porque todo campo que produce la simulación tiene la misma forma: 6 caras × Res² valores. Cambian el significado y las unidades, no la estructura. Añadir un campo cuesta registrar una lambda.

- `FPlanetScalarField`: Id, etiqueta, unidad, paleta, escala, resolución, rango. **No copia datos** — guarda una `TFunction` que devuelve el array vivo, así la vista nunca se desincroniza.
- **Paleta y escala ortogonales.** Paleta: `Sequential` (viridis), `Diverging` (centrada en un cero con significado), `Categorical` (sin interpolar), `Terrain` (nivel del mar en el centro). Escala: `Linear`, `Logarithmic`.
- **Rango automático por percentiles 2/98**, no mín/máx: un solo outlier aplanaba el mapa entero.
- Integrado en `UpdateMeshColors`; conmutación con **F/G**, leyenda en el HUD, material unlit con **U**.

**9 campos registrados:** elevación, ID de placa, edad de corteza, tipo de corteza, grosor de corteza, altura sobre el nivel del mar, temperatura, precipitación, área drenada.

**Pendiente:** campos vectoriales (flechas) y vista de sección/perfil.

---

## 8. Renderizado planetario

❌ **No implementado.**

El único renderizado hoy es el `UProceduralMeshComponent` de `ATectonicsTestActor`: malla fija de `GridResolution = 128` por cara sobre 6.371 km de radio, es decir **~78 km por quad**. Suficiente para patrones globales, insuficiente para redes de drenaje o para bajar a la superficie. Se reconstruye a 10 Hz como máximo (antes era cada frame, ~100.000 vértices).

`Nanite/PlanetNaniteMesh.cpp` (840 líneas) fue **borrado**: construía un `UStaticMesh` completo por parche de quadtree de forma **síncrona en el hilo principal** y congelaba el editor. Ningún presupuesto por frame arregla que la geometría se recree en vez de instanciarse.

Sobrevive `Nanite/PlanetMaterialGenerator` (120 líneas) como único ejemplo funcionando de creación de materiales por código, vía `UMaterialEditingLibrary`.

**Diseño acordado para F6:** quadtree + una sola malla-rejilla pre-construida instanciada por parche, con desplazamiento en el vertex shader. Ver [`REQUISITOS.md §8.2`](REQUISITOS.md#82-renderizado-de-producto).

### Cámara

`Simu/PlanetApproachPawn.cpp` (347) — cámara de órbita/aproximación con velocidad log-interpolada según altitud y «colisión» por consulta de altura contra `GetSurfaceRadiusAtDirection()`, no por colisión física (la malla se genera deliberadamente sin colisión: recalcularla en cada regeneración es inviable). Fijada como pawn por defecto vía `SimuGameMode` y `Config/DefaultEngine.ini`.

`Simu/SimuHUD.cpp` (110) — HUD 2D con brújula, leyenda del campo activo y estadísticas de simulación.

---

## 9. Subsistemas dormidos o auxiliares

| Componente | Líneas | Estado |
|---|---|---|
| `QuadTree/CubeSphereQuadTree.cpp` | 771 | ⏸️ Lógica espacial pura (split/collapse, error geométrico, culling de frustum, vecinos). **Dormido hasta F6.** Deuda: contiene una copia de la tabla de conexiones de bordes que se eliminó del Grid por estar mal, y no está cubierta por ningún test |
| `LOD/CubeLODController.cpp` | 518 | ⏸️ Selección de LOD por distancia con culling por cono. Dormido hasta F6 |
| `Noise/SimplexNoise.cpp` | 294 | ✅ Simplex 3D, fractal y ridged. **En uso** por `RasterizedTectonics` y `SphericalVoronoi` |
| `SimpleFlowSimulation.cpp` | 400 | ⚠️ Solver de difusión para **validar conservación de masa** de la rejilla. No es erosión ni precursor de una. Solo lo instancia `CubeSphereSubsystem` |
| `Simu/UI/TectonicConfigWidget.cpp` | 306 | ✅ Widget UMG que expone parámetros del simulador |
| `Tectonics/TectonicSaveGame.h` | 80 | ✅ Persistencia. Roundtrip verificado en editor (teclas **K/L**): guarda, carga, planeta idéntico |
| `Shaders/CubeSphereCommon.usf` | 458 | ⚠️ Solo referenciado desde `CubeSphereShaderUtils.h`; sin dispatch activo |
| `Shaders/PlanetDisplacement.usf` | 274 | ⚠️ Ligado al generador de materiales |

**Borrado y no echado de menos:** `PlateSimulationGPU`, `PlateMovementShader`, `Shaders/PlateMovement.usf`, `Shaders/TectonicRaster.usf` (~1.300 líneas de andamiaje GPU con **todos los `Dispatch` vacíos`**), `Streaming/ChunkStreamingManager` (738), `CubeSphereVisualizerComponent`, `Nanite/PlanetNaniteMesh` (840).

**Política vigente:** la tectónica corre **100 % en CPU**, y así seguirá hasta que perfilar demuestre que no basta ([`REQUISITOS.md R9.1`](REQUISITOS.md#92-dónde-corre-el-cómputo)).

---

## 10. Controles

| Tecla | Acción |
|---|---|
| **Espacio** | Pausar / reanudar |
| **R** | Reiniciar simulación |
| **+ / −** | Acelerar / frenar el tiempo |
| **F / G** | Campo de diagnóstico siguiente / anterior |
| **U** | Conmutar material unlit |
| **V** | Fronteras de placa |
| **B** | Alternar visualización auxiliar |
| **1–8** | Seleccionar placa |
| **0** | Deseleccionar |
| **K / L** | Guardar / cargar |
| **O** | Conmutar modo de cámara |
| **WASD / QE / ratón / rueda** | Cámara |

---

## 11. Rendimiento

Instrumentado por fases (`FTectonicStepTimings`), con media móvil, visible en el HUD.

| Fase | Antes | Después |
|---|---|---|
| Nivel del mar + isostasia | 78,2 ms | **6,8 ms** |
| Simulación por frame | 116,6 ms | **27,6 ms** |

- **Nivel del mar:** 40 iteraciones de bisección × 393.000 celdas = 15,7 M de lecturas por paso para resolver **un número**. Newton lo hace en 2–3.
- **Advección:** ~174 ms por ejecución, amortizada a ~11 ms/paso porque no corre en todos.
- **Malla:** limitada a 10 Hz.
- **Paso fijo** desacoplado del framerate.

**Coste frente a resolución:** ×16 al pasar de Res 32 a Res 96. Cada campo nuevo paga ese factor.

---

## 12. Suite de tests — ejecución del 16-08-2026

**40 tests. 36 en verde, 4 en rojo.** Los cuatro fallos son **defectos conocidos y documentados**, no sorpresas: son exactamente lo que quedó abierto tras el arreglo de F1, y los cuatro están en el grupo de tectónica.

### En rojo

| Test | Aserción que falla | Defecto |
|---|---|---|
| `Simu.Tectonics.LongRunStability` | «La tierra emergida no colapsa (**13,2 % desde 24,5 %**)» | El planeta se ahoga |
| `Simu.Tectonics.NoPermanentlyStuckCells` | «Ninguna celda queda fuera de la tectónica (**185 atascadas, 0,1882 %**)» | Celdas que no advectan |
| `Simu.Tectonics.StuckCellsNearEulerPole` | «No quedan celdas permanentemente atascadas (**154**)» | El mismo, medido cerca del polo de Euler |
| `Simu.Tectonics.NoStraightCrustBridges` | «No hay franjas rectas de corteza dentro de una placa (**285 de 327**)» | Motas de un píxel |

⚠️ **Aviso sobre el cuarto, y está documentado por haber costado dos días:** ese test **no mide puentes**. Su aserción cuenta *motas de un píxel* cuyo tipo de corteza difiere del de sus vecinas — a Res=256 son invisibles. Los puentes rectos están resueltos y verificados en pantalla. El nombre engaña. Ver [`ANEXO.md`](ANEXO.md#el-quinto-engaño-de-una-métrica).

**El fallo que importa es el primero: el planeta se ahoga.** El segundo y el tercero son dos vistas del mismo defecto.

### Cobertura por grupo

| Grupo | Tests | Estado |
|---|---|---|
| `Simu.CubeSphere.*` | 7 | ✅ Todos en verde |
| `Simu.Visualization.*` | 6 | ✅ Todos en verde |
| `Simu.Climate.*` | 4 | ✅ Todos en verde |
| `Simu.Hydrology.*` | 3 | ✅ Todos en verde |
| `Simu.Tectonics.*` | 20 | 🔴 16 verde / 4 rojo |

> **Ojo al ejecutar:** el filtro `Automation RunTests Simu` también engancha tests del motor cuya ruta contiene «Simulation» (`Ispc.Physics.ChaosClothingSimulationSolver`, `System.Engine.WorldPartition.StreamingGenerationSimulation`, …). Los del proyecto son los 40 de la tabla.

Tests destacados por lo que cubren: `LongRunStability` (1000 Ma), `CrustBudget` (balanza de corteza), `ContinentsPersist`, `OceanicRibbon` (que el material no se deshilache), `FrozenCellsAtProductionRes`, `AdvectionChainingHypothesis`, `PlateShapeOrganic`, `RainShadow` (sombra de lluvia sobre terreno simulado real), `DrainageResolution`.

---

## 13. Deuda técnica activa

Ordenada por impacto.

1. 🔴 **El planeta se ahoga.** La tierra emergida cae del 24,5 % al 13,2 % en 1000 Ma. Candidatos: la acreción de arco está infradimensionada, falta el ciclo de vida de placas, o el reparto en colisión sigue destruyendo continente. **Bloquea la calibración de todo lo que está aguas abajo.**
2. 🔴 **El modelo no tiene fronteras, tiene interiores.** La frontera se infiere contando reclamantes. Impide clasificar transformantes, invierte el coste computacional, y es la raíz de que los huecos y solapes ocurran en bandas anchas en vez de en una celda.
3. 🔴 **Sin ciclo de vida de placas.** Nacen 8, mueren 0, nacen 0. Imposible por construcción.
4. 🔴 **Sin dinámica.** Los polos de Euler nunca se escriben. Sin el tirón de la losa no hay estados de equilibrio ni ciclo de Wilson.
5. 🟡 **Celdas que no advectan**: 185 (0,19 %) en la corrida larga, 154 en el test cercano al polo de Euler. Es la península parada que se ve en pantalla. Reducido desde ~22.900, pero no es cero.
6. 🟡 **Calibración del umbral de rift sin validez.** Se fijó igualando creación y destrucción cuando esa balanza estaba en 0,04. Ahora está en 0,98: **hay que rehacerla**.
7. 🟡 **Respaldos de lectura de material al 1,93 %.** Residual pero no cero; es lectura encadenada, justo lo que el marco por placa viene a evitar. Vigilado.
8. 🟡 **Parámetros calibrados entre sí.** `OrogenyFactor` y `DiffusionRate` forman un equilibrio; tocar uno obliga a revisar el otro.
9. 🟡 **Coste `O(Res²)` acumulativo.** Cada fase añade campos que recorren las 6 caras. Es el riesgo estructural del proyecto.
10. 🟡 **Clima sin verificar a ojo** en el editor.
11. ⏸️ **Copia de la tabla de bordes en el QuadTree**, sin cobertura de test. Dormida hasta F6.
12. ⏸️ **`BoundaryInteractions` desactivado**: 693 líneas de física de subducción, orogenia, spreading, vulcanismo y hotspots. Aprovechable la parte de hotspots y las fórmulas, reescritas para producir grosor en vez de elevación.
13. ⚪ **Renderizado de producto sin resolver.** El enfoque de F6 está diseñado pero no probado.

---

## 14. Inventario de archivos

### Módulo `CubeSphere`

| Archivo | Líneas | Estado |
|---|---|---|
| `Tectonics/RasterizedTectonics.cpp` / `.h` | 2383 / 876 | ✅ Motor real |
| `Tests/TectonicsTests.cpp` | 2865 | ✅ 33 tests |
| `Test/TectonicsTestActor.cpp` / `.h` | 1551 / 456 | ✅ Único actor que simula y renderiza |
| `QuadTree/CubeSphereQuadTree.cpp` / `.h` / `QuadTreeTypes.h` | 771 / 204 / 309 | ⏸️ Dormido |
| `Tectonics/BoundaryInteractions.cpp` / `.h` | 693 / 460 | ⏸️ Desactivado |
| `LOD/CubeLODController.cpp` / `.h` | 518 / 251 | ⏸️ Dormido |
| `Tectonics/TectonicPlateSystem.cpp` / `.h` | 534 / 223 | ✅ Contenedor de placas |
| `Tectonics/PlateKinematics.cpp` / `.h` | 441 / 252 | ✅ Rotación por cuaterniones |
| `CubeSphereGrid.cpp` / `.h` | 421 / 302 | ✅ |
| `SimpleFlowSimulation.cpp` / `.h` | 400 / 193 | ⚠️ Validador de masa |
| `Visualization/PlanetFieldRegistry.cpp` / `.h` | 368 / 95 | ✅ |
| `CubeSphereMetrics.cpp` / `.h` | 367 / 139 | ✅ |
| `Tectonics/SphericalVoronoi.cpp` / `.h` | 313 / 200 | ✅ |
| `Tests/CubeSphereGridTests.cpp` | 333 | ✅ 3 tests |
| `Noise/SimplexNoise.cpp` / `.h` | 294 / 93 | ✅ En uso |
| `Tests/PlanetFieldTests.cpp` | 291 | ✅ 6 tests |
| `Tectonics/TectonicTypes.h` | 296 | ✅ |
| `Climate/PlanetClimate.cpp` / `.h` | 276 / 175 | ✅ |
| `Hydrology/PlanetHydrology.cpp` / `.h` | 274 / 151 | ✅ |
| `CubeSphereSubsystem.cpp` / `.h` | 232 / 147 | ✅ |
| `Tests/CubeFaceMappingTests.cpp` | 170 | ✅ 4 tests |
| `CubeSphereTypes.h` | 168 | ✅ |
| `CubeFaceMapping.h` | 135 | ✅ **Fuente única** |
| `Nanite/PlanetMaterialGenerator.cpp` / `.h` | 120 / 27 | ⚠️ Sin verificar visualmente |
| `CubeSphereShaderUtils.h` | 99 | ⚠️ Sin dispatch activo |
| `Tectonics/TectonicSaveGame.h` | 80 | ✅ Verificado |
| `Visualization/PlanetFieldMaterial.cpp` / `.h` | 70 / 35 | ✅ |
| `Visualization/PlanetScalarField.h` | 127 | ✅ |
| `CubeSphereModule.cpp` / `.h` | 23 / 15 | ✅ |

### Módulo `Simu`

| Archivo | Líneas | Estado |
|---|---|---|
| `PlanetApproachPawn.cpp` / `.h` | 347 / 165 | ✅ |
| `UI/TectonicConfigWidget.cpp` / `.h` | 306 / 147 | ✅ |
| `SimuHUD.cpp` / `.h` | 110 / 51 | ✅ |
| `SimuGameMode.cpp` / `.h` | 14 / 24 | ✅ |
| `Simu.cpp` / `.h` | 15 / 12 | ✅ |

### Shaders

| Archivo | Líneas | Estado |
|---|---|---|
| `Shaders/CubeSphereCommon.usf` | 458 | ⚠️ Sin dispatch activo |
| `Shaders/PlanetDisplacement.usf` | 274 | ⚠️ Ligado al generador de materiales |
