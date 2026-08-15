# ROADMAP.md — Plan de Desarrollo (basado en estado real)

> Reescrito el **15-08-2026** tras una auditoría de código completa que contradijo partes de la versión anterior de este documento. Ver [`SPECS.md`](SPECS.md) para el inventario por archivo.
>
> `docs/ROADMAP.md` y `docs/10-hoja-de-ruta.md` son el plan aspiracional original (12+ meses, fases F1–F5). Se conservan como referencia de **alcance**, no de fecha ni de orden.

## Cómo leer esto

- Las fases **F0…F7** son secuenciales por **dependencia técnica**, no por tiempo. Cada una desbloquea la siguiente.
- Cada fase tiene una definición de "hecho" **verificable en código**: existe el archivo/función/test que lo prueba, no "se ve bien".
- `[x]` = hecho y verificado. `[ ]` = pendiente.
- La sección **Historial** al final recoge el trabajo previo (M0–M5) y **qué de aquello resultó ser falso**.

---

## Objetivo del proyecto

Un simulador planetario completo y **acoplado**: tectónica → relieve → clima → agua → erosión → sedimento → de vuelta a la tectónica. El acoplamiento es el objetivo, no un extra: un relieve que no se erosiona y una lluvia que no depende de las montañas son dos maquetas independientes, no un simulador.

---

## Diagnóstico de partida (auditoría 15-08-2026)

Lo que se creía hecho y **no lo está**:

| Creencia | Realidad en código |
|---|---|
| "Motor CPU de tectónica maduro (~2800 líneas)" | Las placas **nunca se mueven**. `UPlateKinematics` solo lo llaman los tests |
| "`BoundaryInteractions` calcula datos que nadie consume" | Correcto, pero peor: `ApplyElevationChanges()` está **vacía**. Se ejecuta 6×Res² celdas por paso para no producir nada |
| "Erosión: reemplazar el `SimpleFlowSimulation` actual" | No hay nada que reemplazar. `SimpleFlowSimulation` es un test de conservación de masa, no hidrología |
| "Hidrosfera parcial" | Cero. No existe ni una constante de nivel del mar |

Lo que se ve hoy en pantalla es un **generador de relieve estático**: un mapa de placas Voronoi fijo + ruido fractal, con crestas que crecen siempre en las mismas líneas hasta topar a 12000 m y difusión que las suaviza. No hay deriva continental, ni apertura/cierre de océanos, ni ciclo de Wilson, ni conservación de corteza.

**Consecuencia para el orden de trabajo:** el movimiento de placas es el bloqueador absoluto. Erosionar un relieve que nunca cambia no es un simulador acoplado, es un filtro de imagen.

---

## F0 — Cimientos: limpiar y unificar 🔴 EN CURSO

**Por qué primero:** hay un bug de mapeo de caras que corrompería en silencio cualquier sistema nuevo que muestree por cara, tres tests en rojo que nadie estaba mirando, y ~150 KB de código muerto que hace ruido en cada búsqueda.

- [x] **Unificar el mapeo cara↔dirección 3D en un único helper** — `CubeFaceMapping.h` (15-08-2026). Estaba escrito a mano 7 veces (5 previstas + 2 más encontradas al migrar: la tabla de tangentes de `RasterizedTectonics` y la generación de vértices de malla) y ya se había desincronizado:
  - `UCubeSphereGrid::GetFaceAxes` para `+Z` da `U=(1,0,0) V=(0,1,0)` → dirección `(U, V, 1)`
  - `URasterizedTectonics` case 4 (`+Z`) usa `FVector(U, -V, 1)` → **V invertida**. Lo mismo en `−Z`, invertida al revés.
  - Efecto: el mapa de IDs de placa (Voronoi, sobre el Grid) y el de elevación (Raster) están **espejados en los dos casquetes polares**. Las fronteras dibujadas no coinciden con las montañas allí.
  - Copias eliminadas: 3 en `TectonicsTestActor` (`GetSurfaceRadiusAtDirection`, `CreatePlanetMesh`, `UpdateMeshColors`), 2 en `RasterizedTectonics` (`InitializeFromPlateSystem`, `ApplyFractalNoise`), la tabla de tangentes de `RasterizedTectonics`, y las 2 de generación de vértices de malla. `UCubeSphereGrid::GetFaceAxes` ahora delega en el helper en vez de tener su propia tabla.
  - Efecto secundario que no se había previsto: el convenio antiguo era **levógiro** en las dos caras polares (`AxisU × AxisV == −Normal`), lo que además de espejar los datos invertía el winding de los triángulos generados allí. Al unificar se corrige también eso.
  - Cubierto por 4 tests nuevos en `Tests/CubeFaceMappingTests.cpp`: ida-y-vuelta exacta, dextrogiro en las 6 caras, acuerdo con el `Grid`, y cobertura de la esfera sin huecos.

- [ ] 🔴 **Arreglar los 3 tests que ya estaban en rojo** (descubierto el 15-08-2026 al ejecutar la suite; verificado que fallan también en `HEAD` sin ninguno de los cambios de F0). Que M3 y M4 se cerraran como "✅ completo" con la suite roja indica que **nunca se llegó a ejecutar**, solo a compilar:
  - [x] `Simu.Tectonics.BoundaryInteractionsSanity` y `Simu.Tectonics.ElevationStaysBounded` — **arreglados (15-08-2026)**. Ambos abortaban en la primera aserción porque `GeneratePlates()` devolvía `false`: el Jump Flooding dejaba entre el 11 % y el 19 % de las celdas sin asignar. Causa raíz: **las pasadas de salto del JFA no cruzaban entre caras del cubo** (admitido en un comentario del propio código); la propagación entre caras avanzaba una sola celda por pasada y solo sobre las filas de borde, así que cualquier cara del cubo sin ningún centroide dentro no llegaba a rellenarse en las log2(Res)+2 pasadas disponibles. Con ~12 placas repartidas por Fibonacci sobre 6 caras, que alguna cara quede sin centroide es lo normal.
    **Solución: eliminar el JFA**, no repararlo. El JFA es una aproximación que compensa con miles de semillas o en GPU; aquí hay 12-30 placas y esto se ejecuta una sola vez al generar el planeta. La fuerza bruta (para cada celda, el centroide más cercano) es exacta por definición, garantiza cobertura total, no tiene casos límite entre caras, y cuesta milisegundos. Se fue con ella ~150 líneas de propagación cross-face delicada, `JFAIterations`, y cuatro helpers privados que quedaron muertos.
    Confirmación de que ahora sí ejercitan lo que dicen: `ElevationStaysBounded` pasó de 24 ms (abortaba) a 389 ms (corre sus 500 pasos).
  - [ ] `Simu.CubeSphere.AdjacencyContinuity` — **124 errores** de continuidad de adyacencia entre caras. Sigue abierto. Es un bug real de corrección en el cruce entre caras, y **bloquea F4**: el drenaje necesita justo esa vecindad para enrutar ríos por los bordes del cubo.

- [ ] **Una sola asignación placa→celda.** Hoy hay dos que no coinciden: Voronoi/JFA sobre el Grid, y una búsqueda de centroide más cercano `O(Res²·N)` en `InitializeFromPlateSystem`. La segunda sobra.
- [ ] **Borrar código muerto** (está en git si hace falta recuperarlo):
  - `Streaming/ChunkStreamingManager` (27 KB) — 0 referencias externas
  - `CubeSphereVisualizerComponent` (16 KB) — 0 referencias externas
  - `PlateSimulationGPU` + `PlateMovementShader` + los 4 `.usf` (~50 KB) — todos los `Dispatch*` son `// TODO` vacíos
- [ ] **Decidir el actor único.** `TectonicPlanetActor` hoy no hace nada útil: Nanite comentado (no dibuja terreno) y sin `RasterizedTectonics` (no simula relieve). `TectonicsTestActor` es el que funciona. Propuesta: quedarse con uno solo y retirar `Nanite/`, `LOD/`, `QuadTree/` a F6 — la ruta correcta para un planeta es **malla base + displacement desde textura** (lo que ya hace el test actor), no un `UStaticMesh` Nanite por parche construido síncronamente.
- [ ] **Desactivar la llamada a `BoundaryInteractions::ProcessAllBoundaries`** hasta F1, donde por fin tendrá consumidor. Hoy es coste puro. **No borrar el archivo**: contiene física real (ángulos de subducción, esfuerzo acumulado, hotspots) que se conecta en F1.

**Hecho cuando:** existe un único helper de mapeo de caras con test de ida-y-vuelta ✅, **la suite `Simu.*` pasa entera en verde**, y `grep` de las clases borradas no devuelve nada.

**Cómo ejecutar la suite** (no estaba documentado en ningún sitio, de ahí que se cerraran hitos sin correrla):

```
& 'E:\Unreal\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "D:\Portfolio\Simu\Simu.uproject" `
    -ExecCmds="Automation RunTests Simu; Quit" -unattended -nopause -nosplash -NullRHI -log -stdout
```

---

## F1 — Movimiento real de placas 🔴 EL BLOQUEADOR

**Por qué:** sin esto no hay tectónica, y sin tectónica cambiante el resto del simulador no tiene nada que simular.

Algoritmo elegido: **advección hacia atrás del campo de IDs de placa**. Para cada píxel de dirección `d`, se rota `d` hacia atrás por la rotación de cada placa (`−ω·Δt` sobre su polo de Euler) y se pregunta quién poseía ese punto el paso anterior:

- **Un candidato** → la placa simplemente se movió.
- **Dos o más** → colisión. Según los tipos de corteza: océano-continente y océano-océano ⇒ subducción; continente-continente ⇒ orogenia.
- **Ninguno** → hueco. Es un rift: se crea corteza oceánica nueva con edad 0.

Esto hace que subducción, dorsales y apertura de océanos sean **emergentes**, no guionizadas — y es donde `BoundaryInteractions` por fin tiene a quién alimentar.

- [ ] Llamar de verdad a `UPlateKinematics::CalculatePlateRotation` desde `UTectonicPlateSystem::Step` y acumular la rotación por placa
- [ ] Advección hacia atrás del campo `PlateIDData` en `URasterizedTectonics::Step`
- [ ] Resolución de colisión (2+ candidatos) por tipo de corteza
- [ ] Creación de corteza en huecos (0 candidatos), con edad 0 y elevación de dorsal
- [ ] Destrucción de corteza en subducción — hoy `CreateNewCrust` no tiene contraparte (`TODO` en `BoundaryInteractions.cpp:609`)
- [ ] Conectar `ElevationRateMaps` de `BoundaryInteractions` al raster: rellenar `ApplyElevationChanges()`, que hoy está vacía
- [ ] Reactivar `ProcessAllBoundaries` (desactivada en F0)
- [ ] Advectar también `CrustAge` y `CrustType` junto con el ID (si no, la corteza "cambia de tipo" al moverse)

**Coste:** `O(Res²·N)` por paso. Con Res=512 y 12 placas son ~3,1 M operaciones — asumible si el paso tectónico corre a baja frecuencia (no cada frame). Presupuestarlo explícitamente, no dejarlo en el `Tick`.

**Hecho cuando:**
- Test: tras N pasos, el centroide de una placa se ha desplazado la distancia angular que predice `ω·N·Δt` (±tolerancia).
- Test: el número de celdas por placa cambia con el tiempo (las placas crecen y menguan) — falla si el campo de IDs sigue congelado.
- Test de conservación: área de corteza creada en dorsales ≈ área destruida en subducción, dentro de un margen.
- Visual: dos continentes que empiezan separados colisionan y levantan una cordillera en el punto de contacto.

---

## F2 — Isostasia y nivel del mar 🔴

**Por qué antes del agua:** sin nivel del mar explícito no hay costa, y sin costa no hay dónde depositar sedimento ni desde dónde evaporar.

- [ ] Constante/estado de **nivel del mar** explícito (hoy no existe: el océano es "elevación negativa" pintada de azul)
- [ ] **Grosor de corteza** como campo simulado, no solo elevación. La elevación pasa a derivarse de la isostasia, no a ser el estado primario
- [ ] **Equilibrio isostático** (flotación de Airy): corteza gruesa/ligera flota alta. Es lo que hace que las montañas tengan raíz y que al erosionarlas la superficie rebote
- [ ] **Conservación del volumen de océano**: el nivel del mar sube si la cuenca oceánica se reduce (dorsales jóvenes y calientes ocupan volumen)
- [ ] Subsidencia térmica: la corteza oceánica se hunde al enfriarse con la edad (`CrustAge` ya se rastrea, hoy no se usa para nada)

**Hecho cuando:** una cordillera erosionada en F4 rebota isostáticamente en vez de desaparecer; y el nivel del mar responde a la edad media de la corteza oceánica.

---

## F3 — Clima mínimo viable 🟡

**Por qué aquí y no después de la erosión:** la erosión hidráulica necesita **caudal**, y el caudal necesita **precipitación**. Sin esto, F4 tendría que inventarse una lluvia uniforme, que es exactamente lo que impide que se formen desiertos, sombras de lluvia y cuencas realistas.

Esta fase es deliberadamente **barata**: campos diagnósticos, no dinámica de fluidos. La atmósfera completa es F5.

- [ ] **Temperatura** = f(latitud, altitud, ¿estación?). Gradiente adiabático con la altura
- [ ] **Humedad** transportada por un campo de viento sencillo (bandas por latitud: alisios, oestes, polares). No SWE todavía
- [ ] **Precipitación orográfica**: la humedad precipita al subir sobre relieve, y la masa de aire queda seca a sotavento ⇒ sombras de lluvia
- [ ] Evaporación proporcional a temperatura sobre superficie de agua

**Hecho cuando:** el mapa de precipitación muestra una asimetría clara barlovento/sotavento en una cordillera generada por F1, y desiertos en el interior de continentes grandes.

---

## F4 — Erosión hidráulica y transporte de sedimento 🟡

**Por qué ahora:** ya hay relieve que cambia (F1), costa donde depositar (F2) y lluvia que lo alimenta (F3).

- [ ] **Acumulación de flujo** sobre la esfera: dirección de drenaje por celda y caudal acumulado aguas abajo. Hay que resolver el cruce entre caras del cubo (`GetCrossFaceNeighbors` ya existe, de M3)
- [ ] **Tratamiento de depresiones** (lagos/sumideros): rellenar o enrutar. Sin esto el drenaje se atasca
- [ ] **Incisión fluvial** (stream power): erosión ∝ caudal^m · pendiente^n
- [ ] **Transporte y deposición** de sedimento: capacidad de carga, deposición al perder pendiente ⇒ llanuras aluviales y deltas
- [ ] **Erosión termal / mass wasting**: ya existe de facto como `FPlateMovementParams::DiffusionRate`. **Migrarla aquí** con su justificación física en vez de dejarla como un hack anti-picos dentro del paso tectónico
- [ ] Realimentación a F2: el sedimento depositado añade masa (subsidencia), la roca erosionada la quita (rebote isostático)

**Hecho cuando:** aparecen redes de drenaje dendríticas visibles, los ríos desembocan en el mar formando deltas, y una montaña aislada se degrada con el tiempo en vez de crecer indefinidamente.

**Nota de calibración:** este es el punto donde `DiffusionRate`/`OrogenyFactor` dejan de ser parámetros libres. La erosión debe equilibrar el levantamiento tectónico — si no, o se aplana el planeta o se dispara a 12000 m. Ese equilibrio es un resultado observable, no un valor a fijar a mano.

---

## F5 — Atmósfera y ciclo del agua completo 🟢

Sustituye el clima diagnóstico de F3 por dinámica real.

- [ ] Shallow Water Equations + Coriolis — `docs/04-atmosfera-clima.md`
- [ ] Ciclo del agua cerrado: evaporación → advección → condensación → precipitación → escorrentía → océano, con **balance de masa verificable**
- [ ] Corrientes oceánicas y transporte de calor
- [ ] Hielo: casquetes polares, glaciares, y su erosión (distinta de la fluvial)
- [ ] Climatología profunda: ciclo carbono-silicatos, albedo, realimentación hielo-albedo — `docs/06-climatologia-efecto-invernadero.md`

**Riesgo conocido:** las SWE son numéricamente inestables si el paso de tiempo no respeta CFL. Es el candidato natural para invertir en GPU (mucho más sensible a paralelismo que la tectónica rasterizada).

---

## F6 — Rendimiento, LOD y GPU 🟢

Deliberadamente **al final**. Optimizar un pipeline cuya física aún no está definida es tirar el trabajo.

- [ ] Reconsiderar el renderizado planetario: malla base + displacement desde textura, frente al `UStaticMesh` Nanite por parche que se abandonó en F0
- [ ] Reintroducir QuadTree/LOD/streaming sobre el enfoque elegido
- [ ] Mover los campos de simulación a texturas GPU y los pasos a compute shaders
- [ ] Cachear el dibujo de fronteras de placa (deuda de M1: `DrawPlateBoundaries` recorre la rejilla entera cada frame; hoy solo está desactivado por defecto, no arreglado)

---

## F7 — Biosfera 🟢

- [ ] Agentes evolutivos, genoma vectorial, Niagara — `docs/07-biosfera-evolucion.md`

---

## Riesgos vigentes

- **Calibración cruzada de parámetros.** `DiffusionRate`, `OrogenyFactor`, `SpreadingFactor`, `TimeScale`, `ElevationScale` no están calibrados contra nada real ni entre sí; tocar uno obliga a reajustar los demás. F4 debería convertir parte de esto en un equilibrio emergente en vez de valores fijados a mano.
- **Coste `O(Res²)` acumulativo.** Cada fase añade campos que recorren las 6 caras. Sin presupuesto de tiempo por paso y desacople del framerate, se repite la espiral de la muerte de M1.
- **Bordes del cubo.** Cada sistema nuevo que necesite vecindad (drenaje en F4, advección en F5) vuelve a pagar el problema de las costuras entre caras. Es la razón por la que F0 unifica el mapeo antes de empezar.
- **Estabilidad numérica de las SWE** y **VRAM a resolución alta** (heredados del plan original).

---

## Historial — M0 a M5 (10 y 11-08-2026)

Trabajo previo, resumido. Se conserva porque documenta bugs reales y decisiones, pero **léelo con las correcciones de la auditoría del 15-08-2026**.

### Válido y aprovechable
- **M0** — `git init`, `.gitignore`, baseline commiteado.
- **M1.5** — Sesión de depuración de `TectonicsTestActor`: bug de picos infinitos en fronteras convergentes (fix: término difusivo) y su sobrecorrección (fix: mezcla parcial 0.02). Fórmula de exageración de elevación desacoplada del radio. Escala de juguete → escala real de la Tierra.
- **M1.6** — `PlanetApproachPawn`: cámara con velocidad interpolada logarítmicamente según altitud, y "colisión" por consulta de altura (`GetSurfaceRadiusAtDirection`) en vez de colisión física. Brújula de depuración.
- **M3** — Culling de frustum (`UCubeLODController::IsInFrustum`) y mapeo de aristas entre caras (`FCubeSphereQuadTree::GetCrossFaceNeighbors`). **`GetCrossFaceNeighbors` se reutiliza en F4** para el drenaje.
- **M5** — Persistencia (`UTectonicSaveGame`): guarda semilla, estado cinemático y elevación; la topología se regenera desde la semilla. Verificado en editor.

### Correcciones de la auditoría
- **M1** quedó parado en el punto correcto: la elevación de `RasterizedTectonics` nunca llegó al heightmap Nanite. El diagnóstico de "deuda arquitectónica seria" en `UPlanetNaniteMesh` (build síncrono y bloqueante por parche) era acertado — **F0 propone abandonar ese pipeline**, no arreglarlo.
- **M2** cerró como "decidido y documentado" que CPU es la ruta activa. Correcto, pero la conclusión de que existían "dos pipelines CPU redundantes" se quedó corta: `BoundaryInteractions` no es redundante, es **inerte** (`ApplyElevationChanges()` vacía).
- **M4** dio por buena la cobertura de tests. Los tests pasan, pero **ninguno detecta que las placas no se mueven** — `UPlateKinematics` se testea de forma aislada mientras nadie lo llama en producción. F1 añade los tests que habrían pillado esto.
- El antiguo **M6+** listaba "Erosión real (Pipe Model reemplazando el `SimpleFlowSimulation` actual)". Redacción engañosa: no hay nada que reemplazar.
