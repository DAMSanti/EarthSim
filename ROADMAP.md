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

## F0 — Cimientos: limpiar y unificar 🔴 EN CURSO

**Por qué primero:** hay un bug de mapeo de caras que corrompería en silencio cualquier sistema nuevo que muestree por cara, tres tests en rojo que nadie estaba mirando, y ~150 KB de código muerto que hace ruido en cada búsqueda.

> **Patrón que se repite tres veces en esta fase, y conviene tenerlo presente en las siguientes:** cada bug encontrado aquí era una *tabla escrita a mano* que enumeraba casos de la geometría del cubo — el mapeo cara↔dirección (7 copias), las conexiones de bordes (2 copias), y las pasadas del JFA entre caras. Las tres se sustituyeron por cálculo geométrico directo, que no tiene casos que enumerar y por tanto no puede desincronizarse. Cuando en F4/F5 haga falta vecindad o advección cruzando caras, la respuesta por defecto es la misma: proyectar y reproyectar, no tabular.

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
  - [x] `Simu.CubeSphere.AdjacencyContinuity` — **arreglado (15-08-2026), pero el bug principal estaba en el test**. Afirmaba que ir "arriba" desde una celda de borde y luego "abajo" desde la vecina debía devolver a la celda de partida. **Esa propiedad es falsa en un cubo** y ninguna implementación correcta puede satisfacerla: al cruzar de `+X` hacia arriba se entra en `+Z` por su borde `+U`, no por su borde `−V` (el eje `+V` de `+X` es el eje `+U` de `+Z`), así que "abajo" en el marco local de `+Z` lleva hacia `−Y`, no de vuelta. Solo cumplían el ida-y-vuelta las adyacencias sin rotación relativa: 64 de 192, más 4 casos rotados que caían por poco dentro de la tolerancia. De ahí los 124 fallos exactos.
    Reescrito alrededor de la propiedad que **sí** es cierta y que es además la que necesitan los algoritmos de vecindad: **reciprocidad** (si B es vecina de A, A está entre las vecinas de B), más proximidad geométrica y no-identidad. 24.576 comprobaciones, todas en verde.
    Aprovechando, se sustituyó también la implementación: `GetNeighborCell` tenía una tabla de 24 entradas (cara, borde) → (cara vecina, rotación) más un switch de 16 ramas. Ahora resuelve el cruce por **geometría pura** — se sale del cuadrado UV sin recortar, se reproyecta la dirección, y la rotación relativa entre caras sale sola. Sin tabla que mantener.
    ⚠️ **Pendiente relacionado:** M3 "portó esa tabla" al QuadTree (`CubeSphereQuadTree.cpp:596`), así que existe una **segunda copia** con los mismos errores potenciales. No se ha tocado porque el QuadTree es candidato a borrarse en el punto siguiente; si se decide conservarlo, hay que migrarlo a `GetNeighborCell`.

- [ ] **Una sola asignación placa→celda.** Siguen siendo dos y pueden discrepar cerca de las fronteras: `USphericalVoronoi` usa los centroides de Fibonacci originales, mientras que `RasterizedTectonics::InitializeFromPlateSystem` repite la búsqueda usando `FTectonicPlate::Centroid`, que `CalculatePlateStatistics` recalcula después como promedio de celdas. Desde el cambio a fuerza bruta ambas usan el mismo criterio (centroide más cercano), así que unificarlas es ya solo cuestión de que el ráster lea el mapa del Voronoi en vez de rehacer el cálculo.
- [x] **Borrar código muerto** (15-08-2026; está en git si hace falta recuperarlo). 18 archivos, ~150 KB:
  - `Streaming/ChunkStreamingManager` y `CubeSphereVisualizerComponent` — 0 referencias externas
  - `PlateSimulationGPU` + `PlateMovementShader` + `PlateMovement.usf` + `TectonicRaster.usf` — todos los `Dispatch*` eran `// TODO` vacíos. Si algún día se va a GPU, será contra estructuras de datos que todavía no existen; no hay nada aquí que reutilizar
  - `Nanite/PlanetNaniteMesh` + `NaniteTypes` — **la pieza equivocada**: construía un `UStaticMesh` por parche de forma síncrona. Ver F6 para qué la sustituye
  - `TectonicPlanetActor` y `TectonicVisualizerComponent` — solo existían para orquestar lo anterior
  - `PlanetApproachPawn` actualizado: ya no busca `ATectonicPlanetActor`
  - **Conservados a propósito**, contra la recomendación inicial: `QuadTree/` y `LOD/`. Al revisar su API resultó ser lógica espacial pura (split/collapse, error geométrico, bounds, vecinos, LOD por distancia) sin ninguna dependencia de Nanite — es exactamente la mitad-CPU del esquema de F6, y reescribir un quadtree esférico desde cero no es una tarde. También sobrevive `PlanetMaterialGenerator`, único ejemplo funcionando de creación de materiales por código, que F0.5 probablemente reutilice
- [x] **Actor único: `TectonicsTestActor`** (15-08-2026). Era el único que simulaba relieve y lo dibujaba. Conviene renombrarlo en algún momento: ya no es un actor de test, es *el* actor del planeta.
- [ ] **Desactivar la llamada a `BoundaryInteractions::ProcessAllBoundaries`** hasta F1, donde por fin tendrá consumidor. Hoy es coste puro. **No borrar el archivo**: contiene física real (ángulos de subducción, esfuerzo acumulado, hotspots) que se conecta en F1.

**Hecho cuando:** existe un único helper de mapeo de caras con test de ida-y-vuelta ✅, **la suite `Simu.*` pasa entera en verde** ✅ (11/11 el 15-08-2026), y `grep` de las clases borradas no devuelve nada.

**Cómo ejecutar la suite** (no estaba documentado en ningún sitio, de ahí que se cerraran hitos sin correrla):

```
& 'E:\Unreal\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "D:\Portfolio\Simu\Simu.uproject" `
    -ExecCmds="Automation RunTests Simu; Quit" -unattended -nopause -nosplash -NullRHI -log -stdout
```

---

## F0.5 — Visor de campos: la herramienta que hace comprobable todo lo demás 🔴

**Por qué existe esta fase:** sin ella, cada fase siguiente tendría que improvisar su propia visualización, y acabaríamos con cinco maneras distintas de pintar el globo (el mismo patrón de duplicación que causó los tres bugs de F0). Con ella, cada fase nueva sale a pantalla escribiendo un puñado de líneas.

La idea: **cualquier campo escalar de la simulación** —elevación, edad de corteza, precipitación, caudal acumulado, espesor de sedimento— es lo mismo: 6 caras × Res² valores. Un único visor los pinta todos.

- [ ] Tipo común de campo escalar (6 caras × Res², con nombre, unidad y rango) y un registro donde cada sistema publica los suyos
- [ ] Componente visor que mapea el campo activo a color de vértice sobre la malla de `TectonicsTestActor`. Generaliza el `UpdateMeshColors` que ya existe, que hoy solo sabe pintar elevación
- [ ] Rampas de color por tipo de dato: **secuencial** (elevación, temperatura), **divergente** (anomalías respecto a un cero con significado: isostasia, balance hídrico), **categórica** (ID de placa, tipo de frontera), y **logarítmica** (caudal acumulado — sin log, un río se pierde entre la cuenca)
- [ ] Conmutar campo con una tecla, y leyenda en pantalla con nombre, unidad y mín/máx del rango en uso
- [ ] Campos vectoriales (velocidad de placa, viento, dirección de drenaje) como flechas de debug sobre el globo
- [ ] Vista de sección/perfil: elevación a lo largo de un gran círculo. Es la forma más rápida de ver si una cordillera tiene un perfil plausible o es una pared de un píxel

**Hecho cuando:** se puede recorrer con una tecla los campos que hoy ya existen (ID de placa, elevación, edad de corteza, tipo de corteza) con leyenda correcta, y añadir un campo nuevo cuesta registrarlo, no escribir un visualizador.

### Aviso de resolución, que muerde en F4

La malla de `TectonicsTestActor` es de `GridResolution = 128` por cara. Sobre un radio de 6371 km, el lado de una cara mide ~10.000 km, así que **un quad son ~78 km**. El ráster de simulación va a 256 (~39 km por celda).

Para F1, F2 y F3 eso vale: continentes, sombras de lluvia y cinturones climáticos se miden en miles de km. **Para F4 no vale**: una red de drenaje a 39 km por celda no es una red de drenaje. Antes de F4 habrá que decidir entre subir la resolución del ráster (coste cuadrático) o añadir un modo de vista regional que simule y dibuje una sola cara con más detalle. Anotado aquí para que sea una decisión y no una sorpresa.

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
