# ROADMAP.md — Plan de Desarrollo (basado en estado real)

> Reescrito el **15-08-2026** tras una auditoría de código completa que contradijo partes de la versión anterior de este documento. Ver [`SPECS.md`](SPECS.md) para el inventario por archivo.
>
> `docs/ROADMAP.md` y `docs/10-hoja-de-ruta.md` son el plan aspiracional original (12+ meses, fases F1–F5). Se conservan como referencia de **alcance**, no de fecha ni de orden.

## Cómo leer esto

- Las fases **F0…F7** son secuenciales por **dependencia técnica**, no por tiempo. Cada una desbloquea la siguiente.
- Cada fase tiene una definición de "hecho" **verificable en código**: existe el archivo/función/test que lo prueba, no "se ve bien".
- `[x]` = hecho y verificado. `[ ]` = pendiente.
- La sección **Historial** al final recoge el trabajo previo (M0–M5) y **qué de aquello resultó ser falso**.

### Regla de verificación (decisión del 15-08-2026)

**Ninguna fase se da por hecha si no se puede ver en pantalla.** Cada una de F1–F5 termina con una vista concreta que permite juzgar si el fenómeno simulado es plausible, no solo con un test que pase.

El motivo sale directo de la auditoría: los tres bugs de F0 sobrevivieron meses precisamente porque nadie podía *ver* lo que el código hacía. Un test dice "no ha petado"; solo mirar el planeta dice "esto no parece la Tierra".

Esto obliga a separar dos cosas que antes estaban mezcladas en un único "F6 — renderizado":

| | Qué es | Cuándo |
|---|---|---|
| **Visualización de diagnóstico** | Cualquier campo de la simulación pintado sobre el globo, con leyenda y conmutable en caliente. Barato, no necesita LOD | **F0.5, y luego en cada fase** |
| **Renderizado de producto** | Planeta con LOD desde órbita hasta la superficie, materiales, iluminación | F6 |

La primera es la que hace comprobable cada fase. La segunda es acabado, y esa sí puede esperar.

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

## F0 — Cimientos: limpiar y unificar ✅ COMPLETO (15-08-2026)

**Por qué primero:** hay un bug de mapeo de caras que corrompería en silencio cualquier sistema nuevo que muestree por cara, tres tests en rojo que nadie estaba mirando, y ~150 KB de código muerto que hace ruido en cada búsqueda.

> **Patrón que se repite tres veces en esta fase, y conviene tenerlo presente en las siguientes:** cada bug encontrado aquí era una *tabla escrita a mano* que enumeraba casos de la geometría del cubo — el mapeo cara↔dirección (7 copias), las conexiones de bordes (2 copias), y las pasadas del JFA entre caras. Las tres se sustituyeron por cálculo geométrico directo, que no tiene casos que enumerar y por tanto no puede desincronizarse. Cuando en F4/F5 haga falta vecindad o advección cruzando caras, la respuesta por defecto es la misma: proyectar y reproyectar, no tabular.

- [x] **Unificar el mapeo cara↔dirección 3D en un único helper** — `CubeFaceMapping.h` (15-08-2026). Estaba escrito a mano **8 veces** (5 previstas, más la tabla de tangentes de `RasterizedTectonics` y dos bloques de `UpdateMeshColors`) y ya se había desincronizado:
  - `UCubeSphereGrid::GetFaceAxes` para `+Z` da `U=(1,0,0) V=(0,1,0)` → dirección `(U, V, 1)`
  - `URasterizedTectonics` case 4 (`+Z`) usa `FVector(U, -V, 1)` → **V invertida**. Lo mismo en `−Z`, invertida al revés.
  - Efecto: el mapa de IDs de placa (Voronoi, sobre el Grid) y el de elevación (Raster) están **espejados en los dos casquetes polares**. Las fronteras dibujadas no coinciden con las montañas allí.
  - Copias eliminadas: 3 en `TectonicsTestActor` (`GetSurfaceRadiusAtDirection`, `CreatePlanetMesh`, `UpdateMeshColors`), 2 en `RasterizedTectonics` (`InitializeFromPlateSystem`, `ApplyFractalNoise`), la tabla de tangentes de `RasterizedTectonics`, y las 2 de generación de vértices de malla. `UCubeSphereGrid::GetFaceAxes` ahora delega en el helper en vez de tener su propia tabla.
  - Efecto secundario que no se había previsto: el convenio antiguo era **levógiro** en las dos caras polares (`AxisU × AxisV == −Normal`), lo que además de espejar los datos invertía el winding de los triángulos generados allí. Al unificar se corrige también eso.
  - Cubierto por 4 tests nuevos en `Tests/CubeFaceMappingTests.cpp`: ida-y-vuelta exacta, dextrogiro en las 6 caras, acuerdo con el `Grid`, y cobertura de la esfera sin huecos.
  - ⚠️ **La primera pasada dejó 2 copias sin migrar** dentro de `UpdateMeshColors`, porque su texto no era idéntico al de las otras (una no llevaba comentarios `// Face N`, la otra tenía líneas intermedias distintas). Eso dejó las dos caras polares inconsistentes entre `CreatePlanetMesh` (ya convertida) y `UpdateMeshColors` (no) — una regresión introducida al arreglar el bug original. Detectada y corregida al integrar el visor de F0.5. Es la mejor ilustración posible de por qué el mapeo no debía estar duplicado: ni siquiera una migración deliberada, buscándolas a propósito, las encontró todas a la primera.

- [ ] 🔴 **Arreglar los 3 tests que ya estaban en rojo** (descubierto el 15-08-2026 al ejecutar la suite; verificado que fallan también en `HEAD` sin ninguno de los cambios de F0). Que M3 y M4 se cerraran como "✅ completo" con la suite roja indica que **nunca se llegó a ejecutar**, solo a compilar:
  - [x] `Simu.Tectonics.BoundaryInteractionsSanity` y `Simu.Tectonics.ElevationStaysBounded` — **arreglados (15-08-2026)**. Ambos abortaban en la primera aserción porque `GeneratePlates()` devolvía `false`: el Jump Flooding dejaba entre el 11 % y el 19 % de las celdas sin asignar. Causa raíz: **las pasadas de salto del JFA no cruzaban entre caras del cubo** (admitido en un comentario del propio código); la propagación entre caras avanzaba una sola celda por pasada y solo sobre las filas de borde, así que cualquier cara del cubo sin ningún centroide dentro no llegaba a rellenarse en las log2(Res)+2 pasadas disponibles. Con ~12 placas repartidas por Fibonacci sobre 6 caras, que alguna cara quede sin centroide es lo normal.
    **Solución: eliminar el JFA**, no repararlo. El JFA es una aproximación que compensa con miles de semillas o en GPU; aquí hay 12-30 placas y esto se ejecuta una sola vez al generar el planeta. La fuerza bruta (para cada celda, el centroide más cercano) es exacta por definición, garantiza cobertura total, no tiene casos límite entre caras, y cuesta milisegundos. Se fue con ella ~150 líneas de propagación cross-face delicada, `JFAIterations`, y cuatro helpers privados que quedaron muertos.
    Confirmación de que ahora sí ejercitan lo que dicen: `ElevationStaysBounded` pasó de 24 ms (abortaba) a 389 ms (corre sus 500 pasos).
  - [x] `Simu.CubeSphere.AdjacencyContinuity` — **arreglado (15-08-2026), pero el bug principal estaba en el test**. Afirmaba que ir "arriba" desde una celda de borde y luego "abajo" desde la vecina debía devolver a la celda de partida. **Esa propiedad es falsa en un cubo** y ninguna implementación correcta puede satisfacerla: al cruzar de `+X` hacia arriba se entra en `+Z` por su borde `+U`, no por su borde `−V` (el eje `+V` de `+X` es el eje `+U` de `+Z`), así que "abajo" en el marco local de `+Z` lleva hacia `−Y`, no de vuelta. Solo cumplían el ida-y-vuelta las adyacencias sin rotación relativa: 64 de 192, más 4 casos rotados que caían por poco dentro de la tolerancia. De ahí los 124 fallos exactos.
    Reescrito alrededor de la propiedad que **sí** es cierta y que es además la que necesitan los algoritmos de vecindad: **reciprocidad** (si B es vecina de A, A está entre las vecinas de B), más proximidad geométrica y no-identidad. 24.576 comprobaciones, todas en verde.
    Aprovechando, se sustituyó también la implementación: `GetNeighborCell` tenía una tabla de 24 entradas (cara, borde) → (cara vecina, rotación) más un switch de 16 ramas. Ahora resuelve el cruce por **geometría pura** — se sale del cuadrado UV sin recortar, se reproyecta la dirección, y la rotación relativa entre caras sale sola. Sin tabla que mantener.
    ⚠️ **Pendiente relacionado:** M3 "portó esa tabla" al QuadTree (`CubeSphereQuadTree.cpp:596`), así que existe una **segunda copia** con los mismos errores potenciales. No se ha tocado porque el QuadTree es candidato a borrarse en el punto siguiente; si se decide conservarlo, hay que migrarlo a `GetNeighborCell`.

- [x] **Criterio de asignación placa→celda unificado** (15-08-2026). `RasterizedTectonics::InitializeFromPlateSystem` usaba `FTectonicPlate::Centroid`, que `CalculatePlateStatistics` recalcula como promedio de celdas, mientras el Voronoi usaba los centroides de Fibonacci: dos mapas distintos cerca de las fronteras. Ahora el ráster lee los centroides del Voronoi y aplica el mismo criterio (producto escalar mayor), así que solo pueden diferir por resolución, no por criterio.
  - **Pendiente para F1**, deliberadamente: que el ráster sea la *única* fuente de verdad y que `UTectonicPlateSystem` lea de él. Hacerlo ahora sería a medias — en cuanto las placas se muevan, el mapa del Voronoi queda obsoleto al primer paso y pasa a ser solo la condición inicial, que es su papel correcto.
- [x] **Borrar código muerto** (15-08-2026; está en git si hace falta recuperarlo). 18 archivos, ~150 KB:
  - `Streaming/ChunkStreamingManager` y `CubeSphereVisualizerComponent` — 0 referencias externas
  - `PlateSimulationGPU` + `PlateMovementShader` + `PlateMovement.usf` + `TectonicRaster.usf` — todos los `Dispatch*` eran `// TODO` vacíos. Si algún día se va a GPU, será contra estructuras de datos que todavía no existen; no hay nada aquí que reutilizar
  - `Nanite/PlanetNaniteMesh` + `NaniteTypes` — **la pieza equivocada**: construía un `UStaticMesh` por parche de forma síncrona. Ver F6 para qué la sustituye
  - `TectonicPlanetActor` y `TectonicVisualizerComponent` — solo existían para orquestar lo anterior
  - `PlanetApproachPawn` actualizado: ya no busca `ATectonicPlanetActor`
  - **Conservados a propósito**, contra la recomendación inicial: `QuadTree/` y `LOD/`. Al revisar su API resultó ser lógica espacial pura (split/collapse, error geométrico, bounds, vecinos, LOD por distancia) sin ninguna dependencia de Nanite — es exactamente la mitad-CPU del esquema de F6, y reescribir un quadtree esférico desde cero no es una tarde. También sobrevive `PlanetMaterialGenerator`, único ejemplo funcionando de creación de materiales por código, que F0.5 probablemente reutilice
- [x] **Actor único: `TectonicsTestActor`** (15-08-2026). Era el único que simulaba relieve y lo dibujaba. Conviene renombrarlo en algún momento: ya no es un actor de test, es *el* actor del planeta.
- [x] **Desactivada la llamada a `BoundaryInteractions::ProcessAllBoundaries`** hasta F1 (15-08-2026), donde por fin tendrá consumidor. **No se borra el archivo**: contiene física real (ángulos de subducción, esfuerzo acumulado, hotspots) que se conecta en F1. Desactivada también `DetectBoundaries()` en el paso: recalculaba 6×Res² celdas un mapa que no puede haber cambiado, porque las placas todavía no se mueven. Ambas se reactivan en F1.

**Hecho cuando:** existe un único helper de mapeo de caras con test de ida-y-vuelta ✅, **la suite `Simu.*` pasa entera en verde** ✅ (11/11 el 15-08-2026), y `grep` de las clases borradas no devuelve nada.

**Cómo ejecutar la suite** (no estaba documentado en ningún sitio, de ahí que se cerraran hitos sin correrla):

```
& 'E:\Unreal\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "D:\Portfolio\Simu\Simu.uproject" `
    -ExecCmds="Automation RunTests Simu; Quit" -unattended -nopause -nosplash -NullRHI -log -stdout
```

---

## F0.5 — Visor de campos: la herramienta que hace comprobable todo lo demás 🟡 NÚCLEO HECHO (15-08-2026)

**Por qué existe esta fase:** sin ella, cada fase siguiente tendría que improvisar su propia visualización, y acabaríamos con cinco maneras distintas de pintar el globo (el mismo patrón de duplicación que causó los tres bugs de F0). Con ella, cada fase nueva sale a pantalla escribiendo un puñado de líneas.

La idea: **cualquier campo escalar de la simulación** —elevación, edad de corteza, precipitación, caudal acumulado, espesor de sedimento— es lo mismo: 6 caras × Res² valores. Un único visor los pinta todos.

- [x] `FPlanetScalarField` (6 caras × Res², con nombre, unidad, paleta, escala y rango) y `UPlanetFieldRegistry` donde cada sistema publica los suyos. Los datos **no se copian**: el campo guarda una lambda que devuelve el array vivo, así la vista siempre refleja el estado actual sin sincronización explícita
- [x] Coloreado de la malla de `TectonicsTestActor` desde el campo activo. Generaliza el `UpdateMeshColors` que antes solo sabía pintar color de placa modulado por elevación
- [x] **Paleta y escala separadas**, corrigiendo el diseño que figuraba antes aquí. En el planteamiento original "logarítmica" era una rampa más, y es un error de categoría: el logaritmo es una **escala** (transforma el valor) y la paleta es un **mapa de color** (interpreta el valor ya normalizado). Son ortogonales — cualquier paleta puede pintarse en log:
  - Paletas: `Sequential` (viridis), `Diverging` (azul-blanco-rojo, centrada en un cero con significado), `Categorical` (20 colores discretos, sin interpolar), `Terrain` (azules bajo el nivel del mar, verde-marrón-blanco encima)
  - Escalas: `Linear`, `Logarithmic`
- [x] Conmutar con **F / G**, y leyenda en el HUD con nombre, escala, rango en uso y unidad
- [x] Rango automático por **percentiles 2/98** en vez de mín/máx crudos: un único píxel extremo comprimía todo el resto del rango y dejaba el mapa plano
- [x] Registrados los 4 campos que hoy existen: elevación (Terrain, rango **fijo** a propósito — con rango automático la escala se reajusta sola y es imposible notar que el relieve crece), ID de placa, edad de corteza, tipo de corteza
- [x] 6 tests en `Tests/PlanetFieldTests.cpp`: escala lineal y saturación, escala log y monotonía, rango automático frente a outliers, simetría de la paleta divergente, no-interpolación de campos categóricos, y gestión del registro
- [ ] Campos vectoriales (velocidad de placa, viento, dirección de drenaje) como flechas de debug sobre el globo
- [ ] Vista de sección/perfil: elevación a lo largo de un gran círculo. Es la forma más rápida de ver si una cordillera tiene un perfil plausible o es una pared de un píxel
- [x] **Primera verificación en editor (15-08-2026)** — y salieron tres defectos que ningún test unitario podía haber visto. Justifica por sí sola la regla de "ninguna fase se da por hecha si no se puede ver en pantalla":
  1. **Muescas en V en la silueta y hemisferios que desaparecían.** Regresión de F0: `CreatePlanetMesh` tenía un `bFlipWinding` que invertía los triángulos de `PositiveZ` y `NegativeZ`. Existía para compensar que el convenio antiguo era **levógiro justo en esas dos caras**. Al unificar el convenio (las 6 pasaron a dextrógiras) el parche dejó de compensar y pasó a romper: los dos casquetes quedaron con las normales hacia dentro, y por *backface culling* se veía a través de ellos. Eliminado; con convenio uniforme el winding es el mismo en las 6 caras.
  2. **Colores lavados.** La paleta categórica define un rojo `(0.90, 0.24, 0.24)` y en pantalla salía salmón pálido: `M_VertexColor` es un material iluminado y el actor pone dos direccionales (sol a 10 + relleno a 2), así que lo que llegaba al ojo era `paleta × iluminación × exposición`. Eso invalida el propósito del visor — un mismo valor no puede verse de dos colores según dónde dé el sol. Añadido `M_PlanetFieldUnlit` (generado por código, vertex color → Emissive), por defecto, con **tecla U** para volver al iluminado, que sí ayuda a leer la forma del relieve.
  3. **Escalón en el limbo.** No era un artefacto de render sino un **bug de simulación**: `SmoothElevation` y la difusión de `Step()` recortaban con `FMath::Clamp` al borde de la cara, o sea trataban cada cara como una imagen aislada. En el borde el kernel se muestreaba a sí mismo en vez de al vecino real del otro lado de la costura, y como la difusión corre en cada paso, la discontinuidad crecía a lo largo de las 12 aristas del cubo. Corregido reproyectando geométricamente (mismo patrón que `GetNeighborCell`), con instantánea previa para que el resultado no dependa del orden de las caras.
- [x] Test de regresión `Simu.Tectonics.SeamContinuity`. **La primera versión del test no detectaba el bug**: comparaba saltos entre vecinos tras 300 pasos, y el ruido fractal deja saltos tan grandes por todas partes que el sesgo de costura quedaba enterrado. Rediseñado con suavizado fuerte (60 iteraciones), que aplana el interior de cada cara y hace destacar cualquier escalón que sobreviva en las costuras. **Verificado que falla al restaurar el código antiguo**, que es lo único que demuestra que un test sirve
- [x] **Cámara de órbita** (15-08-2026, pedida por el usuario). El vuelo libre estorbaba para inspeccionar campos: cualquier giro desencuadraba el planeta. Ahora el ratón orbita alrededor del centro manteniendo la distancia y mirando siempre al centro, con W/S o rueda para acercarse. **O** conmuta a vuelo libre, que sigue haciendo falta para bajar a la superficie.
  - El estado orbital se guarda como lat/lon/distancia propios, no se deriva de la transform: acumular giros sobre la transform cuela *roll* y la cámara acaba escorada sola.
  - El zoom es proporcional a la altitud, no un paso fijo — es lo que hace que se sienta igual a 15.000 km que a 10 km.
  - Latitud recortada a ±89° en vez de dar la vuelta: al pasar por el polo la longitud se invierte de golpe y la cámara pega un tirón.
- [x] 🔻 **El ratón no capturaba, y probablemente no lo hizo nunca.** No había un solo `SetInputMode` ni `bShowMouseCursor` en todo `Source/`, y sin captura `GetInputMouseDelta()` devuelve cero siempre: ningún giro con ratón podía funcionar, ni la órbita nueva ni el vuelo libre de M1.6. Encaja con que M1.6 se cerrara como "implementado, sin verificar en editor".
- [x] **La brújula al planeta pasa a ser HUD 2D real** (`ASimuHUD`). Era un `DrawDebugDirectionalArrow` dibujado en el MUNDO a 500 uu delante de la cámara, así que se comportaba como un objeto de la escena: al girar se quedaba clavada en el centro mientras el planeta pasaba por detrás, dando la impresión de que la cámara orbitaba *la flecha*. Ahora solo se dibuja en modo libre y solo si el planeta está fuera de pantalla, anclada al borde y no al centro (en el centro tapaba justo lo que se mira).
- [ ] ⚠️ **Pendiente de volver a mirar en el editor** tras todas estas correcciones

**Hecho cuando:** se puede recorrer con una tecla los campos que hoy ya existen (ID de placa, elevación, edad de corteza, tipo de corteza) con leyenda correcta, y añadir un campo nuevo cuesta registrarlo, no escribir un visualizador.

### Aviso de resolución, que muerde en F4

La malla de `TectonicsTestActor` es de `GridResolution = 128` por cara. Sobre un radio de 6371 km, el lado de una cara mide ~10.000 km, así que **un quad son ~78 km**. El ráster de simulación va a 256 (~39 km por celda).

Para F1, F2 y F3 eso vale: continentes, sombras de lluvia y cinturones climáticos se miden en miles de km. **Para F4 no vale**: una red de drenaje a 39 km por celda no es una red de drenaje. Antes de F4 habrá que decidir entre subir la resolución del ráster (coste cuadrático) o añadir un modo de vista regional que simule y dibuje una sola cara con más detalle. Anotado aquí para que sea una decisión y no una sorpresa.

---

## F1 — Movimiento real de placas 🟡 NÚCLEO HECHO (15-08-2026)

**Por qué:** sin esto no hay tectónica, y sin tectónica cambiante el resto del simulador no tiene nada que simular.

Algoritmo elegido: **advección hacia atrás del campo de IDs de placa**. Para cada píxel de dirección `d`, se rota `d` hacia atrás por la rotación de cada placa (`−ω·Δt` sobre su polo de Euler) y se pregunta quién poseía ese punto el paso anterior:

- **Un candidato** → la placa simplemente se movió.
- **Dos o más** → colisión. Según los tipos de corteza: océano-continente y océano-océano ⇒ subducción; continente-continente ⇒ orogenia.
- **Ninguno** → hueco. Es un rift: se crea corteza oceánica nueva con edad 0.

Esto hace que subducción, dorsales y apertura de océanos sean **emergentes**, no guionizadas — y es donde `BoundaryInteractions` por fin tiene a quién alimentar.

- [x] `UTectonicPlateSystem::Step` llama de verdad a `UPlateKinematics::CalculatePlateRotation` y rota el centroide de cada placa. El polo de Euler se mantiene fijo (referencia = manto): es una simplificación, pero hace el movimiento predecible analíticamente, que es lo que necesitan los tests
- [x] **Advección hacia atrás del campo de IDs** — `URasterizedTectonics::AdvectPlateField`. Semi-lagrangiana: para cada píxel de la rejilla nueva se pregunta de dónde viene, en vez de empujar cada píxel viejo hacia donde va. Empujar hacia delante deja huecos y solapes por redondeo y no da forma natural de detectar colisiones; preguntando hacia atrás cada celda se resuelve exactamente una vez y **el número de reclamantes es por sí solo la clasificación del borde**
- [x] **La advección no corre en cada paso, y no es una optimización sino lo correcto.** Con los valores por defecto la placa más rápida gira ~8e-5 rad por paso mientras un píxel abarca ~6.1e-3 rad: 1/76 de píxel. Advectar ahí no movería nada y cada remuestreo mete difusión numérica, así que hacerlo 76 veces en vez de una emborrona el campo a cambio de nada. Se acumula hasta que el desplazamiento alcanza un píxel
- [x] Resolución de colisión por tipo de corteza:
  - océano vs continente → subduce el océano (más denso). Es la razón de que los continentes duren miles de millones de años mientras el fondo oceánico se recicla entero
  - océano vs océano → subduce **la más vieja**, que se ha enfriado y es más densa
  - continente vs continente → ninguna subduce; se queda la más alta
- [x] Creación de corteza en huecos (0 reclamantes): rift, corteza oceánica nueva con edad 0 y elevación de dorsal. Se suelda a la placa que estaba antes ahí, que es la que se aleja
- [x] Destrucción de corteza en subducción: cada reclamante perdedor es una celda que desaparece. Es la contraparte que faltaba (el TODO de `BoundaryInteractions.cpp:609`), y ahora hay contabilidad explícita en `FTectonicAdvectionStats`
- [ ] Conectar `ElevationRateMaps` de `BoundaryInteractions` al raster: rellenar `ApplyElevationChanges()`, que hoy está vacía
- [ ] Reactivar `ProcessAllBoundaries` (desactivada en F0)
- [x] Se advectan `CrustAge`, `CrustType` y `Elevation` junto con el ID, y se recalcula `Velocity` tras resolver la propiedad (depende de dónde está el punto **ahora** y de quién lo posee ahora)

**Coste:** `O(Res²·N)` por paso. Con Res=512 y 12 placas son ~3,1 M operaciones — asumible si el paso tectónico corre a baja frecuencia (no cada frame). Presupuestarlo explícitamente, no dejarlo en el `Tick`.

**Hecho cuando:**
- [x] `Simu.Tectonics.PlateCentroidsMove` — el centroide recorre el ángulo que predice la fórmula del cono (`cos(recorrido) = cos²α + sin²α·cos θ`), no una aproximación
- [x] `Simu.Tectonics.PlateFieldEvolves` — las placas cambian de tamaño; falla si el campo vuelve a congelarse
- [x] `Simu.Tectonics.CrustBudget` — se crea y se destruye corteza, y ninguna supera al doble de la otra
- [x] `Simu.Tectonics.ContinentsPersist` — los continentes sobreviven; detecta que la regla de subducción esté invertida
- [x] `Simu.Tectonics.OrogenyBuildsMountains` — **8.645 m de elevación máxima tras 200 Ma** (partiendo de 2.217 m de ruido inicial), sin saturar el tope de 12.000 m. Añadido tras probar F1 en el editor: había deriva de placas pero **ninguna montaña**.
  - Causa: no faltaba física, era una **asimetría de unidades**. El levantamiento se escalaba por `dt` y la difusión **no** — se aplicaba tal cual en cada paso. A 60 fps eso significa que la difusión borraba ~70 % del relieve por Ma mientras el levantamiento aportaba 5·10⁻⁴ m/Ma: desacoplados unos 7 órdenes de magnitud, y el resultado además dependía del framerate.
  - `DiffusionRate` pasa a ser una tasa **por Ma**, integrada como tal.
  - `OrogenyFactor` pasa a ser una **eficiencia adimensional**: la convergencia se convierte a m/Ma (rad/Ma × radio del planeta) antes de multiplicar, así que el número se lee directo — 0.005 = por cada metro que dos placas se acercan, la frontera sube 5 mm. Antes era una constante sin unidades multiplicando rad/Ma, sin significado físico posible.
  - **Integración por sub-pasos** (máx. 0.5 Ma cada uno): el `dt` que llega no está acotado porque el usuario puede subir `TimeScale` a 1000, y a 60 fps eso son ~16 Ma de golpe. Integrar eso de una vez daba saltos de miles de metros y una fracción de mezcla mayor que 1, que en vez de suavizar oscila. Que el resultado dependa del framerate no es aceptable en un simulador.
- [ ] Visual: confirmar las cordilleras en el editor — **pendiente**

**Contabilidad medida (15-08-2026):**

| Magnitud | Valor |
|---|---|
| Elevación máxima tras 200 Ma | 2.217 m → **8.645 m** |
| Corteza creada / destruida | +59.168 / −56.606 (**4,3 % de desequilibrio**) |
| Corteza continental tras la simulación | 1.380 → 976 celdas (**71 %**) |

> ⚠️ **Ese 71 % es un problema físico conocido, no un éxito.** En una colisión continente-continente ninguna de las dos subduce: la corteza se **engrosa**. Pero como hoy el estado primario es la elevación y no el grosor, la resolución de colisión no tiene forma de apilar material y destruye la celda perdedora. Perder un 29 % de continente cada 200 Ma implicaría no quedar nada en ~700 Ma, y en la Tierra el área continental es aproximadamente constante desde hace miles de millones de años. **Lo arregla F2**, que convierte el grosor de corteza en el estado primario y deriva la elevación por isostasia — es exactamente el caso de uso que lo justifica.

> **Los cuatro tests se validaron saboteando el código** (desactivando la advección y la rotación) para comprobar que fallan. `ContinentsPersist` **no fallaba**: con el campo congelado el recuento no cambia, el ratio sale 1.0 y todas sus aserciones se cumplen. Se reforzó exigiendo que la advección se haya ejecutado y que haya habido colisiones. Es el segundo test de esta sesión que pasaba sin comprobar nada.

**Pendiente de F1, siguiente tanda:**
- [ ] Rellenar `BoundaryInteractions::ApplyElevationChanges()` y reactivar `ProcessAllBoundaries` para que su física (ángulos de subducción, esfuerzo acumulado, hotspots) alimente por fin la elevación del ráster
- [ ] Reactivar `DetectBoundaries()`, que ahora sí tiene sentido: los límites cambian de verdad
- [ ] Que el ráster sea la **única fuente de verdad** del campo de IDs y `UTectonicPlateSystem` lea de él — desde ahora el mapa del Voronoi es solo la condición inicial y queda obsoleto al primer paso

**Renderizable cuando** (campos nuevos que F1 publica al visor de F0.5):
- **ID de placa** (rampa categórica) animado en el tiempo: se ve la deriva. Es la comprobación de un vistazo de que el bloqueador está resuelto
- **Edad de la corteza** (secuencial): debe aparecer el patrón de bandas simétricas a ambos lados de las dorsales, como los mapas reales del fondo oceánico. Si no aparece, el spreading está mal
- **Tipo de frontera** (categórica: convergente / divergente / transformante)
- **Velocidad de placa** como campo vectorial de flechas
- La prueba visual que lo resume todo: dejarlo correr y ver si los continentes se agrupan y se dispersan — un ciclo de Wilson

---

## F2 — Isostasia y nivel del mar 🔴

**Por qué antes del agua:** sin nivel del mar explícito no hay costa, y sin costa no hay dónde depositar sedimento ni desde dónde evaporar.

- [ ] Constante/estado de **nivel del mar** explícito (hoy no existe: el océano es "elevación negativa" pintada de azul)
- [ ] **Grosor de corteza** como campo simulado, no solo elevación. La elevación pasa a derivarse de la isostasia, no a ser el estado primario
- [ ] **Equilibrio isostático** (flotación de Airy): corteza gruesa/ligera flota alta. Es lo que hace que las montañas tengan raíz y que al erosionarlas la superficie rebote
- [ ] **Conservación del volumen de océano**: el nivel del mar sube si la cuenca oceánica se reduce (dorsales jóvenes y calientes ocupan volumen)
- [ ] Subsidencia térmica: la corteza oceánica se hunde al enfriarse con la edad (`CrustAge` ya se rastrea, hoy no se usa para nada)

**Hecho cuando:** una cordillera erosionada en F4 rebota isostáticamente en vez de desaparecer; y el nivel del mar responde a la edad media de la corteza oceánica.

**Renderizable cuando:**
- **Máscara tierra/mar** con una costa de verdad, no "azul si la elevación es negativa". Es el primer momento en que el planeta se parece a un planeta
- **Espesor de corteza** (secuencial) y **anomalía isostática** (divergente respecto al equilibrio): las cordilleras deben tener raíz visible
- **Batimetría por edad**: el fondo oceánico debe hundirse al alejarse de las dorsales
- Perfil de sección cruzando una costa: debe verse plataforma continental, talud y llanura abisal

---

## F3 — Clima mínimo viable 🟡

**Por qué aquí y no después de la erosión:** la erosión hidráulica necesita **caudal**, y el caudal necesita **precipitación**. Sin esto, F4 tendría que inventarse una lluvia uniforme, que es exactamente lo que impide que se formen desiertos, sombras de lluvia y cuencas realistas.

Esta fase es deliberadamente **barata**: campos diagnósticos, no dinámica de fluidos. La atmósfera completa es F5.

- [ ] **Temperatura** = f(latitud, altitud, ¿estación?). Gradiente adiabático con la altura
- [ ] **Humedad** transportada por un campo de viento sencillo (bandas por latitud: alisios, oestes, polares). No SWE todavía
- [ ] **Precipitación orográfica**: la humedad precipita al subir sobre relieve, y la masa de aire queda seca a sotavento ⇒ sombras de lluvia
- [ ] Evaporación proporcional a temperatura sobre superficie de agua

**Hecho cuando:** el mapa de precipitación muestra una asimetría clara barlovento/sotavento en una cordillera generada por F1, y desiertos en el interior de continentes grandes.

**Renderizable cuando:**
- **Precipitación** (secuencial) sobre el globo, comparable de un vistazo con un mapa climático real: cinturón húmedo ecuatorial, franjas desérticas subtropicales, sombras de lluvia tras las cordilleras
- **Temperatura** (divergente en torno a 0 °C, que es el umbral con significado físico: hielo)
- **Viento** como campo vectorial: deben verse las bandas por latitud
- Comprobación cruzada: superponer precipitación sobre relieve y ver si la sombra de lluvia cae realmente detrás de la montaña

---

## F4 — Erosión hidráulica y transporte de sedimento 🟡

**Por qué ahora:** ya hay relieve que cambia (F1), costa donde depositar (F2) y lluvia que lo alimenta (F3).

- [ ] **Acumulación de flujo** sobre la esfera: dirección de drenaje por celda y caudal acumulado aguas abajo. El cruce entre caras lo resuelve `UCubeSphereGrid::GetNeighborCell`, reescrito geométricamente en F0 y cubierto por el test de reciprocidad. **No** usar `FCubeSphereQuadTree::GetCrossFaceNeighbors`: arrastra la copia de la tabla de bordes que se eliminó del Grid por estar mal
- [ ] **Tratamiento de depresiones** (lagos/sumideros): rellenar o enrutar. Sin esto el drenaje se atasca
- [ ] **Incisión fluvial** (stream power): erosión ∝ caudal^m · pendiente^n
- [ ] **Transporte y deposición** de sedimento: capacidad de carga, deposición al perder pendiente ⇒ llanuras aluviales y deltas
- [ ] **Erosión termal / mass wasting**: ya existe de facto como `FPlateMovementParams::DiffusionRate`. **Migrarla aquí** con su justificación física en vez de dejarla como un hack anti-picos dentro del paso tectónico
- [ ] Realimentación a F2: el sedimento depositado añade masa (subsidencia), la roca erosionada la quita (rebote isostático)

**Hecho cuando:** aparecen redes de drenaje dendríticas visibles, los ríos desembocan en el mar formando deltas, y una montaña aislada se degrada con el tiempo en vez de crecer indefinidamente.

**Renderizable cuando:**
- **Caudal acumulado en escala logarítmica** — este es *el* mapa de F4. En lineal no se ve nada; en log aparecen las redes dendríticas. Si no salen ramificadas, el enrutado de drenaje está mal
- **Tasa de erosión** y **espesor de sedimento** (divergente: erosión negativa, deposición positiva)
- **Lagos y depresiones** marcados, para ver si el tratamiento de sumideros funciona
- Curva temporal de altura máxima del planeta: debe estabilizarse en un equilibrio entre levantamiento tectónico y erosión, no dispararse a 12000 m ni aplanarse a cero
- ⚠️ Aquí es donde muerde el aviso de resolución de F0.5: a 39 km por celda no hay red de drenaje que ver

**Nota de calibración:** este es el punto donde `DiffusionRate`/`OrogenyFactor` dejan de ser parámetros libres. La erosión debe equilibrar el levantamiento tectónico — si no, o se aplana el planeta o se dispara a 12000 m. Ese equilibrio es un resultado observable, no un valor a fijar a mano.

---

## F5 — Atmósfera y ciclo del agua completo 🟢

Sustituye el clima diagnóstico de F3 por dinámica real.

- [ ] Shallow Water Equations + Coriolis — `docs/04-atmosfera-clima.md`
- [ ] Ciclo del agua cerrado: evaporación → advección → condensación → precipitación → escorrentía → océano, con **balance de masa verificable**
- [ ] Corrientes oceánicas y transporte de calor
- [ ] Hielo: casquetes polares, glaciares, y su erosión (distinta de la fluvial)
- [ ] Climatología profunda: ciclo carbono-silicatos, albedo, realimentación hielo-albedo — `docs/06-climatologia-efecto-invernadero.md`

**Renderizable cuando:**
- **Humedad y nubosidad** animadas: deben formarse y disiparse sistemas, no quedarse estáticos
- **Corrientes oceánicas** como campo vectorial, con su transporte de calor visible en el mapa de temperatura
- **Hielo** (casquetes y glaciares) avanzando y retrocediendo con el clima
- Gráfica de **balance de masa de agua** en el tiempo: la suma de océano + hielo + humedad + agua superficial debe ser constante. Es la comprobación más dura de esta fase y la más fácil de leer

**Riesgo conocido:** las SWE son numéricamente inestables si el paso de tiempo no respeta CFL. Es el candidato natural para invertir en GPU (mucho más sensible a paralelismo que la tectónica rasterizada).

---

## F6 — Renderizado de producto: LOD hasta la superficie 🟢

Ojo con lo que esta fase **no** es: no es "por fin se ve algo". Desde F0.5 se ve todo, en vistas de diagnóstico sobre el globo. Lo que falta aquí es el **acabado**: poder bajar hasta el suelo con detalle, materiales y luz creíbles.

Va al final porque optimizar un pipeline cuya física aún no está definida es tirar el trabajo, no porque la visualización se posponga.

**Diseño decidido (15-08-2026), para no repetir el error de M1:** quadtree + **una sola malla-rejilla pre-construida**, instanciada por parche con distinta escala/offset, y desplazamiento en el *vertex shader* muestreando la textura de elevación. Lo que **no** se vuelve a intentar es construir un `UStaticMesh` por parche (el enfoque de `PlanetNaniteMesh`, borrado en F0): el `UStaticMesh::Build` síncrono por parche es lo que congelaba el editor, y ninguna cantidad de presupuesto por frame arregla que la geometría se recree en vez de instanciarse.

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
