# ROADMAP.md — Lista de pasos

> **Qué es este documento.** Una lista de pasos con casilla. Nada más.
>
> El *qué* y el *por qué* están en [`REQUISITOS.md`](REQUISITOS.md); el *dónde estamos* en [`SPECS.md`](SPECS.md); el *cómo se llegó aquí* en [`ANEXO.md`](ANEXO.md).

**Leyenda:**
`[x]` hecho y verificado · `[~]` hecho en código, sin verificar en el editor · `[ ]` pendiente · `[⏸]` aparcado con motivo escrito

**Reglas de cierre de fase** (las tres, no dos de tres):
1. Los invariantes `I-xx` de la fase, en verde y **saboteados** para comprobar que se ponen rojos.
2. Los campos de la fase, registrados en el visor.
3. **Verificado a ojo en el editor.** Ninguna fase se cierra sin esto.

Las fases son secuenciales por **dependencia técnica**, no por calendario.

---

## F0 — Cimientos ✅

- [x] Mapeo cara↔dirección unificado en una fuente única (`CubeFaceMapping.h`)
- [x] Eliminar las 8 copias del mapeo escritas a mano
- [x] Convenio dextrógiro en las 6 caras; quitar el `bFlipWinding` compensatorio
- [x] Vecindad entre caras por geometría, sin tabla de rotaciones
- [x] Voronoi esférico por fuerza bruta exacta (sustituye al JFA que dejaba 11–19 % sin asignar)
- [x] Arreglar los 3 tests que estaban en rojo
- [x] Borrar código muerto (18 archivos, ~150 KB)
- [x] Un único actor de simulación: `TectonicsTestActor`
- [x] Criterio de asignación placa→celda unificado
- [x] Desactivar `BoundaryInteractions` hasta que tenga consumidor
- [x] Corrección métrica por celda validada por conservación de masa
- [x] Tests: round-trip, quiralidad, acuerdo con la rejilla, cobertura, continuidad de adyacencia

**Invariantes:** I-01, I-02, I-03

---

## F0.5 — Visor de campos 🟡

### Núcleo
- [x] `FPlanetScalarField`: descriptor con Id, etiqueta, unidad, paleta, escala, resolución y rango
- [x] Sin copia de datos: guarda una función que devuelve el array vivo
- [x] `UPlanetFieldRegistry`: registro, campo activo, cálculo de rango, valor→color
- [x] Paleta y escala **ortogonales**: `Sequential`/`Diverging`/`Categorical`/`Terrain` × `Linear`/`Logarithmic`
- [x] Rango automático por percentiles 2/98 (no mín/máx)
- [x] Conmutación con **F/G**, leyenda en el HUD
- [x] Material unlit conmutable con **U** (el iluminado lavaba los colores)
- [x] 6 tests de paleta, escala y registro

### Campos registrados
- [x] Elevación (Terrain, rango fijo a propósito)
- [x] ID de placa (Categorical)
- [x] Edad de corteza (Sequential)
- [x] Tipo de corteza (Categorical)
- [x] Grosor de corteza (Sequential)
- [x] Altura sobre el nivel del mar (Diverging)
- [x] Temperatura (Diverging, centrada en 0 °C)
- [x] Precipitación (Sequential)
- [x] Área drenada (Sequential **logarítmica**)

### Pendiente
- [ ] Campos **vectoriales** como flechas: viento, velocidad de placa, dirección de drenaje
- [ ] Vista de **sección/perfil** a lo largo de un gran círculo (elevación, grosor, capas)
- [ ] Superposición de fronteras de placa sobre cualquier campo
- [ ] Exportar el campo activo a PNG/CSV para análisis externo
- [ ] Curvas temporales en pantalla (altura máxima, tierra emergida, CO₂, diversidad)
- [ ] Histograma del campo activo, con la curva hipsométrica como caso especial

**Invariante:** I-37

---

## F1 — Las placas como objetos 🟡

### 1A. Cinemática ✅
- [x] Rotación por cuaternión sobre el polo de Euler, cableada al bucle real; los centroides rotan (auditado 16-08-2026 — ver [`ANEXO.md`](ANEXO.md#f1a-auditoría-la-rotación-está-bien-la-clase-no) para el matiz de `UPlateKinematics`)
- [x] Advección semi-lagrangiana hacia atrás sobre el ráster
- [x] Advección por **umbral de ~1 píxel**, no cada paso
- [x] Sub-pasos de máx. 0,5 Ma para que `TimeScale` alto no meta 16 Ma de golpe
- [x] Paso fijo desacoplado del framerate
- [ ] Decidir destino de `UPlateKinematics`: borrarla o cablearla de verdad (441 líneas, código muerto salvo una función estática — ver ANEXO)

### 1B. Propiedad y material separados ✅
- [x] Diagnosticar la raíz: propiedad y material compartían `ReferenceData` con requisitos opuestos
- [x] Barrido de re-anclaje para demostrar que **ningún** punto intermedio funciona
- [x] Tirar `ReferenceData` entero
- [x] La propiedad se decide contra el mundo actual, con rotación **incremental**
- [x] El material vive en un ráster **por placa**, leído con rotación **acumulada**
- [x] Write-back recorriendo el marco de la placa y tirando del mundo (no al revés)
- [x] Write-back capaz de escribir territorio recién ganado
- [x] Instrumentar los respaldos de lectura encadenada (1,93 %, estable)
- [x] Auditar el reloj: medir el tiempo descartado **en la línea donde se descarta**

### 1C. Física de frontera existente ✅
- [x] Colisión por densidad: oceánica subduce bajo continental
- [x] Entre oceánicas subduce la **más vieja**
- [x] Orogenia calibrada contra el Himalaya
- [x] Rift detectado por **divergencia real**, no por contar huecos
- [x] Acreción de arco, restringida a celdas que tocan continente **de otra placa** (corregido 16-08-2026, ver [`ANEXO.md`](ANEXO.md#acreción-de-arco-restricción-por-placa-16-08-2026))
- [x] Advección conjunta de edad, tipo y grosor junto al ID
- [x] Placas de forma fractal, no arcos limpios
- [x] Limpieza de motas de un píxel

### 1D. La frontera como objeto ⬅ **siguiente**
- [x] Extraer los segmentos de frontera del mapa de IDs (componentes conexas, con ID persistente entre advecciones — ver [`ANEXO.md`](ANEXO.md#r212-segmentos-de-frontera-con-id-persistente-16-08-2026))
- [x] Calcular la **normal** de cada segmento
- [x] Clasificar por velocidad relativa proyectada sobre la normal: convergente / divergente / transformante
- [x] Distinguir subducción de orogenia por tipo de corteza de los dos lados (ya en la selección de ganador de colisión, verificado en la auditoría de F1C)
- [x] Aplicar la física **solo en la banda de frontera**, no en el 28 % del planeta (resuelto como efecto lateral de F1B; confirmado hoy por el propio recuento de celdas de segmento, 1–3 % del planeta)
- [x] Fallas transformantes: la **partición** en la costura ya no se queda congelada sin resolver — completada por vecino más cercano cuando la búsqueda estricta no reclama la celda y no es rift (`FindNearestOwnerWide`, 18-08-2026, ver [`ANEXO.md`](ANEXO.md#costura-transformante-partición-completada-por-vecino-más-cercano-18-08-2026)). Medido: sin resolver por advección de 400-800 a un puñado; segmentos de R2.12 dejan de fragmentarse sin parar.
- [x] **Árbitro único de vecino más cercano** (18-08-2026): el conteo de reclamantes independientes (`Claimants.Num()`, uno por placa, cada uno con su propia prueba SI/NO) se sustituyó por un único árbitro de distancia — colisión/traspaso/rift se derivan de comparar la distancia más cercana contra la segunda con la misma vara de medir, en vez de contarse por separado con pruebas de exigencia distinta (reclamar era una condición OR, no reclamar una condición AND — estructuralmente asimétrico). Mismo principio que usa GPlates para particionar el planeta. Ver [`ANEXO.md`](ANEXO.md#árbitro-único-de-vecino-más-cercano-arreglo-de-raíz-de-la-asimetría-18-08-2026)
- [ ] Fallas transformantes: fricción, esfuerzo acumulado y sismicidad de verdad — sigue sin crear ni destruir corteza, el punto de arriba solo resuelve a quién pertenece la celda, no añade la física de deslizamiento en sí
- [ ] Registrar «tipo de frontera» como campo categórico en el visor
- [x] Recalibrar el umbral de rift ahora que la balanza de corteza es sana — probado (18-08-2026, ver [`ANEXO.md`](ANEXO.md#por-qué-el-equilibrio-es-tan-bajo-umbral-de-rift-probado-y-descartado-18-08-2026)): bajarlo a la mitad apenas movió el ratio creado/destruido (0,42-0,50 → 0,46-0,49) y empeoró la tierra emergida. **No es la palanca dominante**, revertido al valor original. El desequilibrio 2:1 sigue sin explicar — sospecha sin confirmar en la asimetría estructural colisión (destruye varias celdas a la vez) vs. rift (crea una por una)

### 1E. Ciclo de vida de placas 🟡

> Motivo de traerlo antes de tiempo: F1F (balance de pares) crea una realimentación positiva sin freno — la placa que ya gana margen convergente tira más fuerte y gana todavía más margen — y solo el ciclo de vida la contrarresta en la Tierra real. Ver [`ANEXO.md`](ANEXO.md#f1e-fase-a-nacimiento-por-fragmentación-17-08-2026).

- [x] Etiquetado de **componentes conexas** sobre el mapa de IDs (`HandlePlateFragmentation`, flood-fill de 4 vecinos cruzando caras, mismo patrón que R2.12)
- [ ] **Muerte:** placa por debajo del umbral de área ⇒ se elimina, su corteza pasa a la vecina
- [x] **Nacimiento por fragmentación:** placa partida en dos o más componentes ⇒ la mayor conserva el ID, cada trozo menor nace como placa nueva (`AddPlate`)
- [x] **Asimilación de islas huérfanas:** un trozo por debajo del umbral de placa nueva, rodeado enteramente por una sola placa vecina, se le asigna a esa vecina en vez de conservar el dueño viejo indefinidamente (ver [`ANEXO.md`](ANEXO.md#f1e-fase-a1-asimilación-de-islas-huérfanas-17-08-2026)) — corta el crecimiento de las islas residuales del trilema de R2.9 Fase 4, no lo elimina (una isla ambigua, con 2+ vecinas distintas, se sigue dejando tal cual)
- [ ] Asignar polo de Euler propio a la placa nueva, derivado de la geometría del rift (hoy hereda la cinemática del padre tal cual; F1F la reafina si la cinemática dinámica está activa)
- [ ] **Sutura:** dos placas sin movimiento relativo prolongado se sueldan
- [x] Sembrar el marco de material y territorio de la placa nueva al nacer (`PlateTerritory`/`PlateMaterial`/`PlateAccumRotation` copiados del padre en el instante del nacimiento)
- [ ] Gestionar el marco de material al morir y fusionar
- [ ] Historial de placas (quién nació de quién, cuándo murió)
- [x] Verificado con datos, no solo a ojo — `Simu.Tectonics.F1EF1FLongRun` (18-08-2026): con F1F dinámico activo sobre la base ya arreglada (ver costura transformante más abajo), la tierra emergida se **estabiliza** en 12-14% en vez de colapsar sin fon                                                                                                                                                    do (antes: 24,5%→8,6% en 382 Ma y seguía cayendo). F1E fragmentó solo una vez en 1000 Ma a resolución de producción — mejora real pero modesta, la mayor parte de la estabilización parece venir del arreglo de frontera, no de F1E en sí (ver [`ANEXO.md`](ANEXO.md#validación-f1e--f1f-¿frena-el-monopolio-18-08-2026)). El equilibrio (12-14%) sigue muy por debajo del ~41% real — no es "resuelto", es "ya no se desboca"

### 1F. Dinámica
- [x] Calcular longitud de margen en subducción por placa (`ComputePlateDrivingTorques`, a partir de los segmentos de R2.12 — ver [`ANEXO.md`](ANEXO.md#f1f-balance-de-pares-por-placa-17-08-2026))
- [x] **Tirón de la losa:** ∝ longitud del margen × √edad media de la losa (misma ley que el hundimiento térmico de F2, no una constante inventada aparte)
- [x] **Empuje de dorsal:** ∝ longitud de dorsal
- [x] **Arrastre basal:** ∝ −área × velocidad (calibrado, `DragCoefficient`)
- [x] Integrar la velocidad angular desde el par resultante (`EulerPole`/`AngularVelocity` escribibles vía `RestorePlateState`; interruptor `D`, cinemática fija de siempre por defecto)
- [ ] Campo de esfuerzo del manto de gran escala, reorganizable cada ~100–200 Ma
- [x] Acotar velocidades al rango real de 1–15 cm/año (clamp duro, no solo calibración)
- [x] **Verificado con datos, no solo a ojo** — mismo test que F1E (`Simu.Tectonics.F1EF1FLongRun`, 18-08-2026): con la cinemática dinámica puesta, el planeta se estabiliza en vez de colapsar (ver ANEXO). R2.18 (campo de esfuerzo del manto) sigue pendiente por separado

### 1G. Vulcanismo
- [ ] Puntos calientes anclados al **marco del manto**, no a las placas
- [ ] Cadenas de islas como registro del movimiento pasado de la placa
- [ ] Vulcanismo de arco en las zonas de subducción
- [ ] Grandes provincias ígneas (LIP) como evento discreto y raro
- [ ] Reaprovechar de `BoundaryInteractions` lo que sirve: hotspots, ángulo de subducción, esfuerzo acumulado — reescrito para producir **grosor**, no elevación
- [⏸] Reconectar `BoundaryInteractions` tal cual — no: escribe en un campo derivado con un mapa de placas caducado
- [⏸] Ráster como única fuente de verdad del campo de IDs — no: todavía sin consumidor

### 1H. Defectos abiertos de F1
- [x] **Tierra emergida (por elevación) caía sin freno — 24,5 % → 0,9-2,3 % —** por la misma fuga de alias mundo↔marco que corrompía el tipo, aplicada esta vez al GROSOR (`AssignCleanMove` seguía leyendo Edad/Grosor/Elevación del marco propio de la placa, con el razonamiento -nunca comprobado- de que un campo continuo se libraba del alias por diluirse en la media). Confirmado siguiendo una cohorte de 38 celdas concretas: madurando a 20,4 km, caían a 7-15 km en solo 25 Ma y nunca recuperaban. Arreglado unificando `AssignCleanMove`/`AssignHandoff` en una sola función que lee siempre de `Prev` — `LongRunStability` pasa por primera vez (24,5%→20,9%, estable), `F1EF1FLongRun` idem. Validado también a ojo en el editor. Ver [`ANEXO.md`](ANEXO.md#el-grosor-tenía-la-misma-fuga-que-el-tipo-la-cohorte-que-nunca-emergía-18-08-2026-misma-sesión-validado-en-el-editor-por-el-usuario)
- [x] **Motas de corteza oceánica dentro de un continente sólido** (reportado por el usuario en el editor) — el despeckle original solo compara `PlateIDData`, nunca `CrustTypeData`, así que una celda de tipo aislado pero de la misma placa que su entorno era invisible para la limpieza. Arreglada con una rama nueva que reasigna material a la media de los vecinos del tipo mayoritario (no a un vecino prestado, que podía crear un islote de edad anómala nuevo — medido y descartado). Ver ANEXO.md, misma sección.
- [x] Celdas que no advectan cerca del polo de Euler, y el caso peor — una frontera divergente vecina enmascarada por el autorreclamo, que crecía territorio sin límite en vez de crear corteza nueva. **R2.9 Fase 4: `PlateTerritory[P]`, marker-in-cell por placa** (mecanismo y diseño en [`ANEXO.md` A14](ANEXO.md#a14-r29-no-basta-la-propiedad-necesita-el-mismo-tratamiento-que-el-material-17-08-2026)). Verificado por test, no solo a ojo: `StuckCellsNearEulerPole` (0 atascadas), `NoPermanentlyStuckCells`, `OceanicRibbon`, `FrozenCellsAtProductionRes` — los 4 en verde 17-08-2026. Umbrales de "sin resolver" en `FrozenCellsAtProductionRes` y `LongRunStability` recalibrados contra la línea base honesta post-arreglo (ver ANEXO A14)
- [x] R2.9 Fase 3: las pasadas de conteo y resolución de `AdvectPlateField` usaban tolerancias distintas y podían discrepar sobre quién reclama una celda, disfrazando colisiones reales de movimientos limpios (punteado que alternaba de placa en fronteras diagonales) — unificadas en `TryClaimTolerant`; recuperación por tolerancia quitada por quedar matemáticamente inalcanzable (ver ANEXO A14)
- [ ] 285 motas de un píxel dentro de una misma placa (`NoStraightCrustBridges` en rojo — **su nombre no describe su aserción**, renombrarlo)
- [ ] Métrica de **rectitud antinatural** para el escalonado (densidad de esquinas de 90°), que reemplace al cociente de longitud de frontera
- [x] `WriteBackToPlateFrames()` solo corría una vez por `Step()`, no una vez por advección — con `TimeScale` alto caben varias advecciones por `Step()` y el material leía de un marco desincronizado en las intermedias. Se caracterizó como "acotado, no catastrófico"; resultó ser un sumidero grande de corteza continental. Arreglado el 18-08-2026 llevando a producción el mismo patrón que ya usaba la Capa 2a de depuración. **No era la causa completa**: el sumidero dominante real (94% del total, incluso en el camino de interior sin cambio de dueño) era que la conversión mundo↔marco rotado (`floor()` en ambos sentidos) no es una inversa exacta y ocasionalmente alias-ea la celda de marco vecina — invisible en el interior homogéneo de una placa, catastrófico justo en una costa, categórico y permanente. Arreglado aparte (mismo día): el tipo de corteza sale siempre de `Prev` en continuación y traspaso, nunca del marco rotado de una placa (`AssignCleanMove`/`AssignHandoff` nuevo). **Validado con test automatizado** (no solo a ojo): `Simu.Tectonics.ContinentsPersist` pasa de extinción total (1321→0 celdas) a x2,48 de crecimiento convergente; el ratio global creación/destrucción de corteza mejora de ~0,45 a ~0,88. Ver [`ANEXO.md`](ANEXO.md#la-fuga-de-tipo-que-sobrevivió-al-write-back-extinción-total-en-continentspersist-18-08-2026-misma-sesión-un-día-después)

**Invariantes:** I-04 … I-13
**Hecho cuando:** en el visor de ID de placa se ven **regiones, no ruido**; el área de cada placa **cambia** con el tiempo; y se observa un ciclo de agregación y dispersión de supercontinentes.

---

## F2 — Isostasia y nivel del mar ✅

- [x] **Grosor de corteza** como estado primario; la elevación se deriva
- [x] Flotación de **Airy** con densidades reales (2750 / 2900 / 3300 kg/m³)
- [x] +840 m para 35 km continentales, sin ajustar el datum a ojo
- [x] Techo de grosor por delaminación (75 km ⇒ ~7.507 m)
- [x] **Batimetría por hundimiento térmico**: `d = 2500 + 350·√(edad)`, tope 5.750 m
- [x] Nivel del mar explícito, con volumen de océano conservado
- [x] Resolución por **Newton** en vez de bisección (78,2 → 6,8 ms)
- [x] Conservación de corteza continental: en colisión el material se apila, no se destruye
- [x] El nivel del mar responde a la tectónica (dorsal joven ⇒ inundación continental)
- [x] Reconstrucción completa de la elevación desde el grosor al final de cada paso
- [x] Campos en el visor: grosor de corteza, altura sobre el nivel del mar
- [ ] **Curva hipsométrica** como campo de diagnóstico, comprobando que es bimodal
- [ ] Test de correlación profundidad ↔ √edad sobre celdas oceánicas

**Invariantes:** I-14 … I-18

---

## F3 — Clima diagnóstico ✅

> Deliberadamente barato: campos diagnósticos, no dinámica de fluidos. Existe porque la erosión necesita caudal y el caudal necesita lluvia.

- [x] Temperatura = f(latitud, altitud), gradiente adiabático de 6,5 °C/km
- [x] Referencias: ecuador 27 °C, 45° ≈ 1 °C, polo −25 °C
- [x] Consecuencia verificada: por encima de ~6 km sobre el ecuador se baja de 0 °C (glaciares ecuatoriales)
- [x] Viento por bandas **alternantes**: alisios del este, oestes, del este cerca del polo
- [x] Humedad transportada a barlovento, con continentalidad
- [x] Perfil de precipitación por bandas **no monótono**, con el mínimo subtropical de 200 mm/año
- [x] Precipitación orográfica (~900 mm/año por km de barrera)
- [x] Sombra de lluvia (~45 % de humedad perdida por km superado, acumulada ~300 km)
- [x] El aire frío retiene menos vapor: polos como desiertos
- [x] Campos en el visor: temperatura (Diverging en 0 °C), precipitación (Sequential)
- [x] 4 tests: perfil de temperatura, bandas de precipitación (comprueba que **no** es monótono), alternancia de vientos, sombra de lluvia sobre terreno real
- [x] Validación sobre planeta simulado 200 Ma: 74 cordilleras, sotavento más seco en el **89 %**, 1.612 vs. 359 mm/año (contraste ×4,5)
- [ ] Verificar en el editor a ojo
- [ ] Viento como campo **vectorial** en el visor (va con F0.5)

**Invariantes:** I-19, I-20

---

## F4 — Erosión hidráulica, sedimento y suelos 🟡

### 4A. Drenaje ✅
- [x] Dirección de flujo **D8** sobre la esfera
- [x] **Corrección de distancia diagonal** (sin ella las redes salen sesgadas a 45°)
- [x] Acumulación de flujo en una sola pasada, de mayor a menor altura
- [x] Caudal en **unidades reales** (m³/año): mm/año × área de celda
- [x] Continuidad del drenaje cruzando las aristas del cubo
- [x] Campo en el visor: área drenada en escala **logarítmica**
- [x] Resolución resuelta por medición: la fracción de cauce **satura** entre Res 128 y 192
- [x] 3 tests: resolución, flujo cuesta abajo, red dendrítica

### 4B. Lagos y depresiones
- [ ] Detección de depresiones cerradas
- [ ] Relleno por *priority-flood* hasta el punto de vertido
- [ ] **Lagos** con nivel propio y balance de agua (entrada, evaporación, salida)
- [ ] Propagación del flujo aguas abajo del vertedero
- [ ] Lagos endorreicos (sin salida al mar) donde la evaporación iguala la entrada
- [ ] Campo en el visor: profundidad de lago

### 4C. Incisión fluvial
- [ ] Ley de potencia de corriente: `∂z/∂t = U − K·A^m·S^n`, con m≈0,5 y n≈1
- [ ] Erodibilidad `K` dependiente del **tipo de roca**
- [ ] Migrar aquí la erosión termal que hoy vive como `DiffusionRate`
- [ ] Calibrar contra tasas reales de incisión (0,01–1 mm/año según el contexto)
- [ ] Campo en el visor: tasa de erosión

### 4D. Sedimento
- [ ] Espesor de sedimento como campo primario, **separado del grosor de roca madre**
- [ ] Sedimento suspendido y capacidad de transporte
- [ ] Deposición en abanicos aluviales (salida de montaña)
- [ ] Deposición en llanuras aluviales (baja pendiente)
- [ ] **Deltas** en las desembocaduras
- [ ] Balance de sedimento cerrado: erosionado = transportado + depositado
- [ ] Campo en el visor: espesor de sedimento

### 4E. Realimentación isostática
- [ ] La erosión adelgaza la corteza ⇒ la montaña **rebota**
- [ ] La carga de sedimento hunde la cuenca ⇒ hace sitio a más sedimento
- [ ] Revisar la cota de crecimiento del área continental con este lazo activo
- [ ] Comprobar que una montaña aislada sin levantamiento **se degrada**

### 4F. Estratigrafía y suelos
- [ ] Modelo de capas: roca madre / regolito / orgánica
- [ ] Tipos de roca: granito, basalto, sedimentaria (litificación del sedimento enterrado)
- [ ] Meteorización dependiente de temperatura y humedad
- [ ] **NPK** por celda, transportado con el sedimento
- [ ] Mapa de fertilidad emergente: valles y deltas productivos
- [ ] Campos en el visor: tipo de roca, espesor de suelo, fertilidad

**Invariantes:** I-24 … I-28
**Hecho cuando:** redes dendríticas con relación pendiente-área correcta, deltas en las desembocaduras, y una montaña aislada que se degrada en vez de crecer indefinidamente.

---

## F5 — Atmósfera dinámica, océano, hielo y carbono

### 5A. Shallow Water Equations
- [ ] Solver SWE sobre el cubo esférico: masa y momento
- [ ] Gradiente de presión (altura geopotencial)
- [ ] **Coriolis** `f = 2Ω sin φ`
- [ ] Fricción superficial dependiente del terreno
- [ ] Difusión numérica explícita y acotada
- [ ] Condición **CFL** respetada sobre la celda más pequeña (esquina de cara)
- [ ] Sub-ciclado: N pasos de atmósfera por paso geológico
- [ ] Método semi-implícito o semi-lagrangiano si el explícito no aguanta
- [ ] Verificar que emergen solas las celdas de Hadley, Ferrel y polar
- [ ] Verificar la corriente en chorro y los cinturones de alta presión subtropical
- [ ] Campos vectoriales en el visor: viento, presión

### 5B. Termodinámica
- [ ] Insolación por ángulo cenital: latitud, hora, estación
- [ ] **Inclinación axial** de 23,44°, parametrizable ⇒ estaciones
- [ ] Radiación saliente linealizada (Budyko: `OLR = A + B·T`)
- [ ] Capacidad térmica distinta para océano y tierra
- [ ] Advección de calor por el viento
- [ ] Balance energético global verificable (neto ≈ 0 en equilibrio)
- [ ] Ciclo diurno

### 5C. Ciclo del agua cerrado
- [ ] Evaporación ∝ déficit de saturación × viento
- [ ] Clausius-Clapeyron explícita: `e_s(T) = 611,2·exp(17,67T/(T+243,5))`
- [ ] Advección de humedad
- [ ] Condensación, nubosidad y precipitación
- [ ] Nieve por debajo de 0 °C
- [ ] Humedad de suelo y evapotranspiración
- [ ] **Balance de masa de agua cerrado** sumando océano, hielo, atmósfera, suelo y lámina
- [ ] Sustituir el clima diagnóstico de F3 manteniendo sus tests como referencia de cordura

### 5D. Océano
- [ ] Giros oceánicos forzados por el viento
- [ ] Circulación termohalina simplificada (temperatura + salinidad)
- [ ] Transporte de calor hacia los polos (~1 PW en la Tierra)
- [ ] Salinidad como campo, con aporte de ríos y pérdida por evaporación
- [ ] Hielo marino
- [ ] Campos en el visor: corrientes (vectorial), temperatura superficial del mar, salinidad

### 5E. Criosfera
- [ ] Hielo como campo con **masa**, con acumulación por nevada y pérdida por fusión
- [ ] Flujo glaciar (difusión no lineal)
- [ ] Casquetes polares y glaciares de montaña
- [ ] **Erosión glaciar**: valles en U, circos, fiordos
- [ ] Efecto sobre el nivel del mar (un ciclo glacial mueve ~120 m)
- [ ] Campo en el visor: espesor de hielo

### 5F. Ciclo del carbono y albedo
- [ ] CO₂ atmosférico como estado primario
- [ ] Desgasificación volcánica ∝ longitud de dorsal + arco + LIPs
- [ ] Meteorización de silicatos: `W ∝ q·exp((T−T₀)/13,7)·f(relieve, roca, vegetación)`
- [ ] Enterramiento de carbonatos en el fondo marino
- [ ] Forzamiento radiativo `ΔF = 5,35·ln(C/C₀)`, sensibilidad ~3 °C por duplicación
- [ ] **Albedo dinámico** por superficie: hielo, agua, desierto, pradera, bosque, nubes
- [ ] Realimentación hielo-albedo
- [ ] Verificar que el termostato estabiliza ante un escalón de desgasificación (τ ≈ 0,5–1 Ma)
- [ ] Verificar **histéresis**: dos estados estables para el mismo forzamiento
- [ ] Snowball Earth alcanzable, y la salida solo por acumulación de CO₂
- [ ] Campos en el visor: CO₂, albedo, tasa de meteorización

**Invariantes:** I-21, I-22, I-23, I-29, I-30

---

## F6 — Renderizado de producto

### 6A. Geometría
- [ ] Quadtree por cara con selección de LOD por distancia (reactivar el que está dormido)
- [ ] Migrar la copia de la tabla de bordes del QuadTree al vecino geométrico
- [ ] **Una sola malla-rejilla** pre-construida, instanciada por parche
- [ ] Desplazamiento en el **vertex shader** muestreando la textura de elevación
- [ ] Faldones o *geomorphing* para eliminar grietas entre niveles adyacentes
- [ ] Continuidad del quadtree cruzando las aristas del cubo
- [ ] Streaming de parches, asíncrono
- [ ] Detalle sub-celda procedimental y determinista

### 6B. Datos a GPU
- [ ] Mover los campos de simulación a **texture arrays** (`R32G32B32A32_FLOAT`)
- [ ] Buffers agrupados por dominio: geológico, hidrológico, atmosférico, edáfico
- [ ] Migrar a compute shaders las fases que superen el criterio de disparo medido
- [ ] Cachear el dibujo de fronteras de placa

### 6C. Materiales
- [ ] Mapeo **triplanar**
- [ ] Splat maps dinámicos alimentados por campos de simulación
- [ ] Mezcla roca / suelo / hierba / arena / nieve por humedad, temperatura, pendiente y sedimento
- [ ] Entrada del mapa de biomas de F7
- [ ] Materiales PBR de calidad

### 6D. Atmósfera y océano visuales
- [ ] `Sky Atmosphere` con Rayleigh y Mie **acoplados a la atmósfera simulada**
- [ ] Nubes volumétricas alimentadas por la cobertura nubosa simulada
- [ ] Océano con color por profundidad, espuma costera y hielo marino
- [ ] Iluminación global dinámica (Lumen)
- [ ] Ciclo día/noche y estacional visible

### 6E. Cámara y presupuesto
- [ ] Aproximación de órbita a superficie con velocidad log-interpolada (ya existe; revalidar con LOD)
- [ ] Presupuesto por frame: < 8 ms simulación, < 8 ms render
- [ ] La simulación no bloquea el hilo de render
- [ ] 60 FPS con la simulación activa

**Invariante:** I-36

---

## F7 — Biosfera

### 7A. Framework de agentes
- [ ] Buffer plano de agentes en GPU
- [ ] Partición espacial para consultas de vecindad
- [ ] **Super-individuos** con tamaño de población asociado
- [ ] Posición sobre la esfera y muestreo del entorno
- [ ] Objetivo de escala: ≥10⁶ agentes dentro del presupuesto

### 7B. Genoma
- [ ] Tolerancias: temperatura óptima y rango, agua, salinidad, nutrientes
- [ ] Morfología: tamaño, velocidad, eficiencia digestiva, coste de mantenimiento
- [ ] Trofia como **fracciones**, no categorías rígidas: fotosíntesis, herbivoría, carnivoría, detritivoría
- [ ] Historia vital: umbral reproductivo, inversión por descendiente, longevidad
- [ ] Comportamiento: gregarismo, agresividad, dispersión

### 7C. Metabolismo
- [ ] Fotosíntesis ∝ luz × agua × nutrientes
- [ ] Herbivoría sobre biomasa vegetal
- [ ] Carnivoría sobre otros agentes
- [ ] Metabolismo basal ∝ `M^0,75` (Kleiber)
- [ ] Modulación por temperatura (`Q₁₀ ≈ 2–3`)
- [ ] Coste energético del movimiento
- [ ] Verificar eficiencia trófica emergente ~10 % y 4–5 niveles

### 7D. Evolución
- [ ] Reproducción al superar el umbral energético
- [ ] Herencia del genoma con mutación gaussiana acotada
- [ ] Muerte por energía negativa
- [ ] El cuerpo pasa a **materia orgánica del suelo** (cierra el ciclo con F4)
- [ ] Dispersión y migración hacia recursos
- [ ] Sin función de *fitness* explícita en ninguna parte

### 7E. Acoplamiento de vuelta
- [ ] Vegetación → albedo
- [ ] Raíces → tasa de meteorización (×2–10)
- [ ] Cobertura vegetal → resistencia a la erosión
- [ ] Evapotranspiración → humedad atmosférica
- [ ] Fotosíntesis → **oxígeno atmosférico** (Gran Oxidación)
- [ ] Enterramiento de carbono orgánico → CO₂ a largo plazo

### 7F. Instrumentación evolutiva
- [ ] **Árbol filogenético** registrado, con especiación y extinción
- [ ] Distancia genética entre poblaciones ⇒ detección objetiva de especiación
- [ ] Curva de diversidad en el tiempo
- [ ] Detección automática de **extinciones masivas** (caída >50 % de linajes)
- [ ] Mapa de **biomas** emergente
- [ ] Comparación con el diagrama de **Whittaker** (temperatura × precipitación)
- [ ] Campos en el visor: biomasa, bioma, densidad de población, diversidad

**Invariantes:** I-31, I-32, I-33

---

## F8 — Producto y experimentos

### 8A. Herramientas
- [ ] UI completa de control: pausa, paso a paso, velocidad, semilla, reinicio
- [ ] Edición de parámetros físicos en caliente, con aviso de calibraciones cruzadas
- [ ] Línea temporal navegable con eventos marcados (rift, colisión, glaciación, extinción)
- [ ] Comparador de dos corridas con la misma semilla y un parámetro distinto
- [ ] Exportación de datos para análisis externo

### 8B. Escenarios
- [ ] Arranque en Hadeico y evolución hasta el presente
- [ ] Carga de una configuración de placas concreta (por ejemplo, Pangea)
- [ ] Escenarios de perturbación: impacto, LIP, cambio de constante solar
- [ ] Barridos de parámetros por lotes, sin editor

### 8C. Cierre
- [ ] Los 37 invariantes de `REQUISITOS.md §11` en verde, todos saboteados
- [ ] Rendimiento de producto sostenido a la resolución objetivo
- [ ] Documentación de todos los parámetros físicos y su calibración
- [ ] Empaquetado y perfil de hardware verificado

**Invariantes:** I-34, I-35, y el conjunto completo

---

## Documentos relacionados

- [`REQUISITOS.md`](REQUISITOS.md) — qué tiene que hacer y cómo se comprueba
- [`SPECS.md`](SPECS.md) — qué hay hoy en el código
- [`ANEXO.md`](ANEXO.md) — historial, defectos y mediciones
