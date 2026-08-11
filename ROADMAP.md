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
- [x] Remoto en GitHub ya existente (confirmado por el usuario, 11-08-2026). Desarrollo sigue siendo local por ahora — no se hace `git push` automáticamente desde aquí sin que se pida explícitamente

---

## M1 — Cerrar la brecha Simulación ↔ Render 🟡 PARCIAL (11-08-2026)

🔴 Es la brecha de mayor impacto (ver `SPECS.md §4, §10.1`): hoy el planeta que se ve no es el planeta que se simula. **Importante: esto es sobre el pipeline Nanite (`PlanetNaniteMesh`/`NanitePlanetActor`), un actor distinto de `TectonicsTestActor`. Nada de lo hecho el 10-08 en `TectonicsTestActor` (ver M1.5) resuelve esto.**

- [x] Sustituir el heightmap placeholder (`Sin(X)*Cos(Y)`) por muestreo real de `FSimplexNoise::SphereFractalNoise` — `Nanite/PlanetNaniteMesh.cpp:GenerateProceduralHeightmap`. Bonus: el placeholder anterior era además discontinuo entre parches/caras (ruido en espacio de textura, no en la esfera); la versión nueva usa la dirección 3D real, sin costuras.
- [ ] Conectar la elevación calculada por la simulación tectónica al heightmap (todavía es ruido, no datos de `RasterizedTectonics`)
- [ ] Verificar visualmente en editor que el heightmap sin costuras se ve bien
- [x] **Bloqueador confirmado**: `Content/` solo contenía `M_VertexColor.uasset`. `NaniteConfig.PlanetMaterial` sin valor por defecto. El material no existía.
- [x] **Material creado por código** (pedido explícitamente por el usuario, 11-08-2026): `Nanite/PlanetMaterialGenerator.h/.cpp` (`#if WITH_EDITOR`), usa `UMaterialEditingLibrary` (API oficial de Epic para crear/editar materiales por código - `CreateMaterialExpression`, `ConnectMaterialExpressions`, `ConnectMaterialProperty`) para construir: `TextureSampleParameter2D "Heightmap"` + `VectorParameter "HeightmapMinMax"` → `LinearInterpolate` (Min↔Max según el heightmap) → metros a cm → pin **Displacement** (confirmado en el código del motor: `r.Nanite.Tessellation` está activado por defecto en UE 5.8, no hace falta ningún cvar extra). Se guarda como asset real en `Content/Materials/M_PlanetDisplacement.uasset` la primera vez que se usa, y `GetPatchMaterial()` en `PlanetNaniteMesh.cpp` lo crea/carga automáticamente si `NaniteConfig.PlanetMaterial` está vacío.
- [x] **Bug adicional encontrado al probar (11-08-2026):** el usuario colocó `NanitePlanetActor` (Simu/, actor standalone y más simple con colores fijos por cara) esperando ver el material — pero ese actor no usa `UPlanetNaniteMesh`/`GetPatchMaterial()` en absoluto, son pipelines distintos. Al revisar el que sí correspondía (`ATectonicPlanetActor`, dueño real de `UPlanetNaniteMesh`), se encontró que **tampoco habría funcionado**: `UPlanetNaniteMesh::SyncWithQuadTree()` (única función que llama a `GetPatchMaterial()`) solo se ejecuta si hay un `LODController` asignado, y `TectonicPlanetActor` nunca creaba ni asignaba uno — el material nunca se habría generado con ningún actor. Corregido: `TectonicPlanetActor` ahora crea un `UCubeLODController` propio y lo conecta en `InitializePlanet()`.
- [x] **Segunda ronda de bugs encontrados al probar (11-08-2026):** con `TectonicPlanetActor` correctamente colocado, el material sí se creó, pero no se veía nada y el framerate caía mucho. Tres causas combinadas, todas corregidas:
  - `PlanetApproachPawn` solo reconocía `ATectonicsTestActor` como objetivo — con `TectonicPlanetActor` en el nivel se quedaba sin referencia de "suelo" (sin velocidad adaptativa ni colisión). Generalizado para reconocer ambos tipos (`FindTargetPlanet`/`GetTargetSurfaceRadius`), con aproximación esférica simple para `TectonicPlanetActor` (sin datos de elevación por dirección en esa clase).
  - `TectonicPlanetActor::PlanetRadius` valía `6371000.0f` (63,71 km) pese a decir en el comentario "6371km = Tierra" — 100x más pequeño que `TectonicsTestActor` (`637100000.0f`), dos planetas de escalas completamente distintas coexistiendo en el mismo nivel. Corregido al valor real.
  - `HeightmapMinMax` por defecto del material era ±12000m — desplazamiento Nanite grande sobre ruido de 6 octavas, candidato claro a teselado carísimo. Reducido a ±100m para la primera prueba a escala segura; es un parámetro del material, se puede subir después sin recompilar.
  - El asset ya generado con los valores viejos se borró para que se regenere limpio.
- [x] Añadido HUD de depuración a `TectonicPlanetActor` (pedido por el usuario, mismo estilo que `TectonicsTestActor`) - tiempo simulado, pasos, nº de placas, radio. Aviso importante documentado en el propio código: como la construcción Nanite en `InitializePlanet()` es síncrona y bloquea el hilo principal, este texto **no puede aparecer mientras carga**, solo justo cuando termina - no hay forma de dar feedback "en vivo" durante ese bloqueo sin resolver primero la deuda arquitectónica de arriba (build asíncrono).
- [x] Causa de "no veo nada" identificada: no era un bug, el actor se había colocado a ~8.874 km del origen (arrastrado en el viewport sin querer). Recolocado a `(0,0,0)`.
- [ ] **Aún sin verificar visualmente tras esta ronda de fixes** (colocación a 0,0,0 + HUD + PlayerStart reposicionado a ~15.000km de distancia).
- [x] Brújula de depuración añadida a `PlanetApproachPawn` (pedida por el usuario): flecha roja fija delante de la cámara que siempre apunta hacia `TargetPlanet`, más distancia en km en pantalla — para saber si el planeta simplemente está fuera de encuadre.
- [x] **Segundo cuelgue confirmado ("0.001 FPS") incluso con `MaxSplitsPerFrame = 0`.** Esto descarta que el problema fuera solo la cascada de creación de parches (eso ya estaba mitigado) — hay algo más caro sin diagnosticar, candidato principal: coste de renderizado de Nanite Displacement en sí (re-teselado por frame según la vista). No se puede perfilar sin el editor abierto, así que en vez de seguir mitigando a ciegas, **se desactivó por completo la generación de parches Nanite** en `TectonicPlanetActor::InitializePlanet()` (código comentado, no solo un límite) hasta que se pueda diagnosticar de verdad con acceso al editor (Task Manager con columna GPU durante el cuelgue sería el primer paso).
- **Conclusión de esta ronda de M1**: el pipeline `UPlanetNaniteMesh`/Nanite Displacement no es viable para verificación en tiempo real tal como está - dos intentos de mitigación (límite de splits, y antes el material a escala reducida) no lo resolvieron. `TectonicsTestActor` (ProceduralMesh, sin Nanite) sigue siendo la única vía de este proyecto verificada como funcional para ver la simulación tectónica. Recomendación para retomar M1: perfilar primero con el editor abierto (Session Frontend / Unreal Insights) antes de tocar más código a ciegas.
- [x] **Limpieza de clases (pedida explícitamente por el usuario, 11-08-2026):** `TestPlanetActor` y `NanitePlanetActor` (ambas en `Source/Simu/`) eran prototipos tempranos sin relación con la tectónica, causantes de casi toda la confusión de las pruebas de hoy. Se comprobó primero con un commandlet (`-run=CleanupPlanetActors`, ver nota abajo) que la versión guardada en disco de `test.umap` ya no tenía instancias de ninguna de las dos — **borradas las clases fuente por completo** (`TestPlanetActor.h/.cpp`, `NanitePlanetActor.h/.cpp`), no solo las instancias del nivel. El commandlet, ya sin uso una vez confirmado esto, también se borró.
  - Nota técnica de la sesión de depuración: el commandlet se quedó colgado la primera vez por Live Coding enganchándose al proceso (arreglado con `-nolivecoding`), y sus logs informativos no se veían por usar severidad `Log` en vez de `Warning` en un contexto donde ese nivel se filtra - ninguno de los dos problemas es específico de este proyecto, son gotchas generales de commandlets en UE 5.8.
- [ ] Sigue pendiente decidir si a largo plazo se mantienen ambos `TectonicPlanetActor` (Nanite) y `TectonicsTestActor` (ProceduralMesh, ya verificado) o solo uno — ahora mismo coexisten porque se necesitan los dos para terminar de verificar el material Nanite.
- [x] **Bug grave encontrado al probar (11-08-2026): congelación total ("0.00001 FPS").** Causa raíz confirmada en código: `UPlanetNaniteMesh::SyncWithQuadTree()` construye una malla Nanite real (`UStaticMesh::Build` completo) **de forma síncrona en el hilo principal** por cada hoja nueva del quadtree, sin ningún límite propio. Con subdivisión activa cerca de un planeta de miles de km de radio, `UCubeLODController` pedía muchísimos niveles de detalle en cascada → decenas/cientos de builds Nanite síncronos → congelación total. Mitigación aplicada: `LODConfig.MaxSplitsPerFrame = 0` en `TectonicPlanetActor::InitializePlanet()`, que mantiene el quadtree en sus 6 hojas raíz (una por cara) para siempre, sin subdividir nunca. **Esto NO es una solución real**, solo evita el cuelgue para poder verificar el material. El problema de fondo (build Nanite síncrono por parche, sin async ni presupuesto por frame) sigue sin resolver y es una limitación arquitectónica seria de `UPlanetNaniteMesh` para uso en tiempo real — nadie había ejercitado este camino antes de hoy (`GetPatchMaterial`/`SyncWithQuadTree` nunca se llamaban, ver hallazgo anterior de esta misma sesión).
- [ ] Conectar la elevación real de `RasterizedTectonics` al heightmap (hoy sigue siendo solo ruido de `GenerateProceduralHeightmap`, ver punto de arriba) — el material ya está listo para recibirla, pendiente el lado de generación de datos.
- [ ] **Deuda arquitectónica nueva y seria**: `UPlanetNaniteMesh::GeneratePatchMesh` construye Nanite de forma síncrona y bloqueante por parche, sin presupuesto por frame ni asincronía. Con `MaxSplitsPerFrame = 0` el sistema es inutilizable como LOD real (6 parches fijos, sin más detalle nunca). Para que este pipeline sea usable en tiempo real hace falta repensar cómo se genera Nanite por parche - candidatos: build asíncrono (`Async`/task graph), un presupuesto explícito de builds-por-frame en `SyncWithQuadTree` (no solo en el conteo de splits del quadtree), o replantear si generar `UStaticMesh` con Nanite por parche es siquiera el enfoque correcto frente a una malla base única con Displacement (que es, de hecho, lo que ya hace `TectonicsTestActor` sin usar Nanite en absoluto).

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

## M1.6 — Cámara de aproximación al planeta, con colisión ✅ IMPLEMENTADO (11-08-2026, sin verificar en editor)

🟡 Pedido explícitamente el 10-08-2026: con relieve realista (poco visible desde lejos, ver M1.5), hace falta poder acercarse a la superficie para juzgar el terreno de verdad.

- [x] `Simu/PlanetApproachPawn.h/.cpp`: pawn de cámara libre con velocidad interpolada **logarítmicamente** entre `MinSpeed`/`MaxSpeed` según la altitud sobre el terreno local (altitud varía en varios órdenes de magnitud - metros a miles de km - así que interpolación lineal habría dejado casi todo el rango útil comprimido en unos pocos frames)
- [x] "Colisión" elegida: **Ruta B (consulta de altura)**, no colisión física real. Nuevo método `ATectonicsTestActor::GetSurfaceRadiusAtDirection()` reutiliza exactamente la misma fórmula que `CreatePlanetMesh`/`UpdateMeshColors` (nunca se desincroniza de lo que se ve), y el pawn recorta radialmente su posición si intenta meterse por debajo. Justificación: la malla se regenera con colisión desactivada a propósito (`TectonicsTestActor.cpp:576`, "es muy pesado") - recalcular colisión física real cada vez sería justo el problema de rendimiento que ese comentario evita
- [x] Input por sondeo directo de teclado/ratón (mismo patrón que `TectonicsTestActor::HandleInput`), sin depender de assets de Enhanced Input que no se pueden crear sin el editor: WASD mueve, Q/E baja/sube, ratón mira
- [x] `Simu/SimuGameMode.h/.cpp` + `GlobalDefaultGameMode` en `Config/DefaultEngine.ini` para que este pawn sea el que se posee al darle a Play (no se pudo hacer solo con .ini porque `AGameModeBase::DefaultPawnClass` no está marcado `config` en el motor)
- [x] Corregido (11-08-2026, feedback del usuario): `MaxSpeed` por defecto era 30 km/s - a distancias orbitales (~15.000km) se sentía "MUY lento" (varios minutos para cruzar). Subido a 2.000 km/s (~7,5s para cruzar 15.000km) y `MaxSpeedAltitude` a ~1,5x el radio terrestre para que la curva logarítmica llegue a velocidad máxima a esa distancia
- [ ] **No verificado en el editor.** Aviso importante: si el nivel `test.umap` tiene un GameMode Override en World Settings, ese override gana sobre `GlobalDefaultGameMode` del proyecto y el pawn nuevo no se usaría - si al darle a Play sigue apareciendo `DefaultPawn`, revisar World Settings > GameMode Override y ponerlo a "None" (o a `SimuGameMode` explícitamente)

**Hecho cuando:** se puede pasar de vista orbital a estar de pie sobre una montaña generada por la simulación, sin clipping ni cambios manuales de velocidad de cámara. Código completo; falta la prueba real en editor.

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
- [x] Test de sanidad de `BoundaryInteractions`: corre `PlateSystem->Step` 50 veces (que internamente llama a `BoundaryInteractions::ProcessAllBoundaries`) y verifica que `SlabDepth`/`AccumulatedStress` se mantienen finitos - guarda contra NaN/Inf silenciosos en las fórmulas de subducción/orogenia
- [ ] Tests específicos por tipo de frontera (subducción/orogenia/spreading/transformante) con casos sintéticos de placas conocidas — pendiente, cobertura hoy es de sanidad general, no por caso
- [ ] Test de conservación de masa de corteza — pendiente. Nota: la implementación actual de `CreateNewCrust` no tiene una operación de destrucción de corteza equivalente (ver TODO en `BoundaryInteractions.cpp:609`), así que un test de conservación estricto fallaría hoy por diseño incompleto, no por bug - escribirlo tendría más sentido junto con esa implementación pendiente

**Hecho cuando:** existe `Tests/TectonicsTests.cpp` corriendo en el framework de Automation de UE. ✅ (cobertura ampliable después, no bloquea)

---

## M5 — Persistencia mínima ✅ COMPLETO Y VERIFICADO (11-08-2026)

🟡 Ninguna sesión sobrevivía a un reinicio.

- [x] `UTectonicSaveGame` (`Tectonics/TectonicSaveGame.h`): guarda semilla, parámetros de grid/raster, estado cinemático evolucionado de cada placa (`FTectonicPlate` completo) y la elevación acumulada de las 6 caras
- [x] Guardado/carga vía `USaveGame` + `UGameplayStatics::SaveGameToSlot/LoadGameFromSlot`
- [x] Diseño verificado por código (no solo asumido): `USphericalVoronoi::Initialize` y `UTectonicPlateSystem::InitializePlateProperties` llaman a `FMath::RandInit(Config.RandomSeed [+1000])` explícitamente, así que la topología (IDs de placa por celda) es reproducible solo con la semilla — no hace falta serializarla, se regenera en `LoadSimulation` llamando a `InitializeSystems()` con la misma semilla y sobreescribiendo encima el estado cinemático y la elevación guardados
- [x] Atajos en `TectonicsTestActor`: `K` guarda, `L` carga (slot fijo `"SimuSnapshot"`)
- [x] Verificado en editor (11-08-2026): `K` guarda y `L` carga sin errores en una sesión de Play real
- [x] Confirmado por el usuario: el planeta se ve idéntico tras cargar (mismas placas, mismo relieve)

**Hecho cuando:** se puede cerrar el editor, reabrir, cargar un snapshot guardado, y el planeta se ve idéntico. ✅

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
