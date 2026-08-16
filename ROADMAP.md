# ROADMAP.md — Plan de Desarrollo

> Basado en el inventario real de código ([`SPECS.md`](SPECS.md)), no en una estimación de calendario. Reescrito el 15-08-2026 tras una auditoría que contradijo la versión anterior; actualizado el 16-08-2026.
>
> **Estructura:** el plan y sus checklists van arriba. Todo lo demás —defectos abiertos, hipótesis descartadas, mediciones e historial— está en el [Anexo](#anexo) al final, enlazado desde donde hace falta.

## Objetivo

Un simulador planetario completo y **acoplado**: tectónica → relieve → clima → agua → erosión → sedimento → de vuelta a la tectónica. El acoplamiento es el objetivo, no un extra: un relieve que no se erosiona y una lluvia que no depende de las montañas son dos maquetas independientes, no un simulador.

## Cómo leer esto

- Las fases **F0…F7** son secuenciales por **dependencia técnica**, no por tiempo.
- Cada fase tiene una definición de "hecho" verificable en código, y otra de "renderizable": **ninguna fase se da por hecha si no se puede ver en pantalla** ([por qué](#a1-la-regla-de-verificación)).
- `[x]` = hecho y verificado. `[ ]` = pendiente.

---

## Estado por fases

### F0 — Cimientos ✅ COMPLETO (15-08-2026)

- [x] Mapeo cara↔dirección unificado en `CubeFaceMapping.h` — estaba escrito a mano **8 veces** y ya se había desincronizado ([detalle](#a2-el-mapeo-de-caras))
- [x] Vecindad entre caras por geometría, sin tabla de rotaciones
- [x] Voronoi por fuerza bruta exacta, sustituyendo un JFA que dejaba 11–19 % de celdas sin asignar
- [x] Los 3 tests que estaban en rojo, arreglados ([detalle](#a3-tests-que-no-comprobaban-nada))
- [x] Código muerto borrado: 18 archivos, ~150 KB
- [x] Actor único: `TectonicsTestActor`
- [x] Criterio de asignación placa→celda unificado
- [x] `BoundaryInteractions` desactivado hasta que tenga consumidor

### F0.5 — Visor de campos 🟡 NÚCLEO HECHO

- [x] `FPlanetScalarField` + `UPlanetFieldRegistry`, sin copiar datos
- [x] Paleta y escala **ortogonales** (`Sequential`/`Diverging`/`Categorical`/`Terrain` × `Linear`/`Logarithmic`)
- [x] Rango automático por percentiles 2/98
- [x] Conmutar con **F/G**, leyenda en el HUD, material unlit con **U**
- [x] 6 campos registrados: elevación, ID de placa, edad y tipo de corteza, grosor, altura sobre el nivel del mar
- [ ] Campos vectoriales (velocidad, viento, drenaje) como flechas
- [ ] Vista de sección/perfil a lo largo de un gran círculo

### F1 — Movimiento real de placas 🟡 NÚCLEO HECHO

- [x] `UPlateKinematics` por fin cableada; los centroides rotan
- [x] **Advección hacia atrás** del campo de IDs: el número de reclamantes *es* la clasificación del borde (1 = movimiento, 0 = rift, ≥2 = colisión)
- [x] Colisión por densidad: océano subduce bajo continente; entre océanos subduce la más vieja
- [x] Creación de corteza en rifts y destrucción en subducción, con contabilidad explícita
- [x] Se advectan edad, tipo y grosor junto con el ID
- [x] La advección no corre en cada paso, sino al acumular ~1 píxel de desplazamiento
- [x] Orogenia calibrada contra el Himalaya ([detalle](#a4-calibración-de-la-orogenia))
- [x] Verificado en editor: deriva, cordilleras de ~8.600 m y **bandas de edad en las dorsales**
- [ ] ⏸️ **Aparcado, y con motivo** — los dos puntos que quedaban se replantean tras F2 ([por qué](#a9-los-dos-puntos-aparcados-de-f1))

### F2 — Isostasia y nivel del mar ✅ COMPLETO (16-08-2026)

- [x] **Grosor de corteza** como estado primario; la elevación se deriva
- [x] **Flotación de Airy** con densidades reales → +840 m para 35 km continentales, la altura media real de los continentes
- [x] **Batimetría por hundimiento térmico**: `d = 2500 + 350·√(edad)`
- [x] **Nivel del mar** explícito, con volumen de océano conservado
- [x] Conservación de corteza continental: en colisión el material se apila, no se destruye
- [x] Tierra emergida resultante: 22–32 % según semilla (Tierra real: 29 %), y **converge al subir resolución**
- [x] Renderizable: grosor de corteza y altura sobre el nivel del mar registrados en el visor

### F3 — Clima mínimo viable ✅ COMPLETO (16-08-2026)

**Por qué antes de la erosión:** la erosión hidráulica necesita **caudal**, y el caudal necesita **precipitación**. Sin esto, F4 tendría que inventarse una lluvia uniforme, que es justo lo que impide que se formen desiertos, sombras de lluvia y cuencas realistas.

Deliberadamente barata: campos diagnósticos, no dinámica de fluidos. La atmósfera completa es F5.

- [x] **Temperatura** = f(latitud, altitud), gradiente adiabático de 6,5 °C/km. Ecuador 27 °C, 45° 1 °C, polo −25 °C; a 6 km sobre el ecuador se baja de 0 °C, que es por lo que hay glaciares ecuatoriales
- [x] **Viento** por bandas: alisios del este, oestes, del este otra vez cerca del polo. Que **alternen** es lo que decide qué ladera es barlovento, y por tanto de qué lado cae el desierto
- [x] **Humedad** transportada a barlovento, con continentalidad (lejos del mar llega menos aunque no haya montañas)
- [x] **Precipitación orográfica y sombra de lluvia**. Perfil por bandas: 2500 mm/año en el ecuador, **200 en los subtrópicos** (donde están el Sahara, Arabia y el Kalahari), 1100 en latitudes medias, 200 en los polos. No es monótono: el mínimo subtropical es lo que separa un planeta con desiertos de una bola con lluvia decreciente
- [x] El aire frío retiene menos vapor: los polos salen como desiertos pese a estar helados

**Hecho:** `Simu.Climate.RainShadow` sobre un planeta simulado 200 Ma — **74 cordilleras medidas, sotavento más seco en el 89 %, 1.612 mm/año a barlovento frente a 359 a sotavento**. Un contraste de 4,5×.

Cuatro tests: perfil de temperatura, bandas de precipitación (comprueba explícitamente que **no** sea monótono), alternancia de vientos, y sombra de lluvia sobre terreno real.

**Renderizable:** temperatura (divergente centrada en 0 °C, que es el umbral con significado físico) y precipitación (secuencial) registradas en el visor. Pendiente el viento como campo vectorial, que va con los campos vectoriales de F0.5.

⚠️ **Sin verificar en el editor todavía.**

### F4 — Erosión hidráulica y sedimento 🟡 DRENAJE HECHO (16-08-2026)

- [x] **Acumulación de flujo** sobre la esfera. D8 con **corrección de distancia diagonal** — sin ella el drenaje prefiere las diagonales y las redes salen sesgadas a 45°. Recorrido de mayor a menor altura: una sola pasada basta, porque cuando le toca a una celda ya ha recibido todo lo de arriba
- [x] Caudal en **unidades reales** (m³/año), no un número sin escala: mm/año × área de celda
- [ ] Tratamiento de depresiones (lagos/sumideros)
- [ ] **Incisión fluvial** (stream power): erosión ∝ caudal^m · pendiente^n
- [ ] Transporte y deposición de sedimento → llanuras aluviales y deltas
- [ ] Migrar aquí la erosión termal que hoy vive como `DiffusionRate`
- [ ] Realimentación isostática: el sedimento hunde, la erosión rebota
- [ ] Revisar la cota de crecimiento continental ([por qué](#a5-crecimiento-del-área-continental))

**Hecho cuando:** redes de drenaje dendríticas, deltas en las desembocaduras, y una montaña aislada que se degrada en vez de crecer indefinidamente.

**Renderizable cuando:** **caudal acumulado en escala logarítmica** — es *el* mapa de F4; en lineal no se ve nada. Más tasa de erosión, espesor de sedimento y curva temporal de altura máxima.

✅ **Aviso de resolución, resuelto por medición.** Se temía que 39 km/celda fuera insuficiente para redes de drenaje. `Simu.Hydrology.DrainageResolution` lo mide:

| Res | Tierra | Cauce | Sumideros | Coste |
|---|---|---|---|---|
| 64 | 6.469 | 1,2 % | 1,0 % | 1,9 ms |
| 128 | 26.560 | 2,1 % | 0,7 % | 9,3 ms |
| 192 | 107.024 | **2,2 %** | 0,7 % | 40,4 ms |

**La fracción de cauce satura entre 128 y 192**, así que la topología de la red ya está resuelta y subir más solo añadiría detalle de valle, no estructura. Y solo **0,7 % de sumideros**: el agua llega al mar en vez de estancarse.

El límite real es **la malla de visualización** (`GridResolution = 128`, ~78 km), que es más gruesa que el ráster de simulación. Subirla es mucho más barato que subir la simulación.

### F5 — Atmósfera y ciclo del agua completo 🟢

- [ ] Shallow Water Equations + Coriolis
- [ ] Ciclo del agua cerrado con balance de masa verificable
- [ ] Corrientes oceánicas y transporte de calor
- [ ] Hielo: casquetes, glaciares y su erosión
- [ ] Climatología profunda: carbono-silicatos, albedo, realimentación hielo-albedo

### F6 — Renderizado de producto 🟢

- [ ] Quadtree + **malla-rejilla única instanciada por parche**, desplazamiento en vertex shader (**no** `UStaticMesh` por parche: es lo que congelaba el editor)
- [ ] Reintroducir LOD y streaming sobre ese enfoque
- [ ] Mover campos a texturas GPU y pasos a compute shaders
- [ ] Cachear el dibujo de fronteras de placa

### F7 — Biosfera 🟢

- [ ] Agentes evolutivos, genoma vectorial, Niagara

---

## Defectos abiertos

Ninguno bloquea, todos están medidos y acotados. Detalle completo en [A6](#a6-defectos-abiertos-en-detalle).

| Defecto | Medida | ¿Lo arregla una fase posterior? |
|---|---|---|
| Escalonado de bordes de placa | ×2,3 | **No.** Y está bloqueado por falta de métrica válida |
| Crecimiento del área continental | converge a ×2,27 (21,7 % de la superficie) | **No** (la erosión no convierte continente en océano) |
| Tierra emergida decae en 1000 Ma | 24,5 % → 17,2 % | Parcialmente: falta el sumidero de la erosión-sedimento (F4) |
| Celdas sin resolver en la advección | 0,016 % | Residual, vigilado por test |

## Riesgos vigentes

- **Coste `O(Res²)` por campo.** Cada fase añade campos que recorren las 6 caras, y el coste escala con el número de celdas (medido: ×16 de Res 32 a 96). Es el riesgo principal de F3/F4.
- **Calibración cruzada de parámetros.** `OrogenyFactor` y `DiffusionRate` forman un equilibrio; tocar uno obliga a revisar el otro.
- **Bordes del cubo.** Cada sistema nuevo con vecindad vuelve a pagarlo. Usar siempre `CubeFaceMapping` y `GetNeighborCell`.
- **Estabilidad numérica de las SWE** y **VRAM a resolución alta** (heredados del plan original).

---

## Cómo trabajar aquí

```
# Compilar
& 'E:\Unreal\UE_5.8\Engine\Build\BatchFiles\Build.bat' SimuEditor Win64 Development `
    -Project="D:\Portfolio\Simu\Simu.uproject" -WaitMutex

# Pasar la suite (30 tests)
& 'E:\Unreal\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "D:\Portfolio\Simu\Simu.uproject" `
    -ExecCmds="Automation RunTests Simu; Quit" -unattended -nopause -nosplash -NullRHI -stdout
```

**Reglas que se han ganado su sitio:**

1. **Un test que solo se ha visto pasar no demuestra nada.** Sabotear el código y comprobar que falla. Tres tests de esta sesión pasaban sin comprobar nada.
2. **Cota por los dos lados.** Un límite de un solo lado en una magnitud que puede irse en ambos es media comprobación.
3. **Medir antes de optimizar.** La intuición falló dos veces; instrumentar por fases encontró el coste real en un sitio inesperado.
4. **Antes de diferir un defecto, nombrar el mecanismo que lo arreglará.** Si no se puede, no está diferido: está sin arreglar.

---

# Anexo

## A1. La regla de verificación

**Ninguna fase se da por hecha si no se puede ver en pantalla.** El motivo sale de la propia auditoría: los tres bugs de F0 sobrevivieron meses precisamente porque nadie podía *ver* lo que el código hacía. Un test dice "no ha petado"; mirar el planeta dice "esto no parece la Tierra".

Eso obliga a separar dos cosas que estaban mezcladas:

| | Qué es | Cuándo |
|---|---|---|
| **Visualización de diagnóstico** | Campos sobre el globo, con leyenda, conmutables. Barata, sin LOD | **F0.5, y luego cada fase** |
| **Renderizado de producto** | LOD hasta la superficie, materiales, luz | F6 |

La primera hace comprobable cada fase; la segunda es acabado. Ha demostrado su valor: la primera vista real destapó tres defectos que ningún test unitario podía ver (winding invertido, colores lavados por el material iluminado, y un escalón en el limbo que resultó ser un bug de simulación, no de render).

## A2. El mapeo de caras

`UCubeSphereGrid::GetFaceAxes` daba para `+Z` el eje `V = (0,1,0)` mientras `RasterizedTectonics` usaba `FVector(U, -V, 1)` — V invertida, y en `−Z` invertida al revés. Las cuatro caras ecuatoriales sí coincidían, así que el bug solo se manifestaba en los casquetes: el mapa de placas y el de elevación quedaban **espejados** allí.

El convenio antiguo era además **levógiro** en esas dos caras (`AxisU × AxisV = −Normal`), lo que invertía el winding de los triángulos generados. Al unificar hubo que quitar el `bFlipWinding` que lo compensaba — y no hacerlo dejó los casquetes renderizando del revés hasta que se vio en pantalla.

La primera pasada dejó **2 de las 8 copias sin migrar**, porque su texto no era idéntico al de las otras. Es la mejor ilustración de por qué no debía estar duplicado: ni una migración deliberada, buscándolas a propósito, las encontró todas.

## A3. Tests que no comprobaban nada

Tres casos en dos sesiones:

1. **M3/M4 se cerraron con la suite en rojo.** 3 de 8 tests fallaban; se había compilado, no ejecutado. El comando para correr la suite no estaba documentado en ningún sitio.
2. **`ContinentsPersist` pasaba con las placas quietas.** Con el campo congelado el recuento no cambia, el ratio sale 1.0 y todas sus aserciones se cumplen. Detectado saboteando la advección.
3. **`LongRunStability` acotaba solo por arriba.** Dejó pasar un desplome de tierra emergida del 24,6 % al 11,3 % sin decir una palabra.

## A4. Calibración de la orogenia

Al probar F1 había deriva pero **ninguna montaña**. No faltaba física: era una **asimetría de unidades**. El levantamiento se escalaba por `dt` y la difusión no —se aplicaba tal cual en cada paso—, así que a 60 fps la difusión borraba ~70 % del relieve por Ma mientras el levantamiento aportaba 5·10⁻⁴ m/Ma. Desacoplados unos 7 órdenes de magnitud, y el resultado dependía del framerate.

Corregido: `DiffusionRate` pasa a ser tasa **por Ma**; `OrogenyFactor` pasa a ser **eficiencia adimensional** (la convergencia se convierte a m/Ma antes de multiplicar). Integración por sub-pasos de máx. 0,5 Ma, porque con `TimeScale` alto llegaban ~16 Ma de golpe.

Desde F2 lo que engrosa es el **grosor de corteza**, no la elevación, y las montañas salen por flotación. Calibrado contra el Himalaya (35 → 70 km en ~50 Ma). Resultado: 1.184 → 7.472 m en 200 Ma, con solo el 0,3 % de la tierra contra el techo isostático. Ese techo tampoco es un clamp arbitrario: `MaxThickness` (75 km, límite de delaminación) por Airy da ~7.507 m.

## A5. Crecimiento del área continental

**Corrección de un error propio:** se justificó la cota holgada diciendo que la erosión de F4 aportaría el sumidero. **Es falso.** La erosión adelgaza corteza y mueve sedimento, pero **no convierte corteza continental en oceánica**; el área continental no la toca.

Causa real: en una colisión la continental gana y la celda de destino pasa a ser continental, convirtiendo océano en continente. Lo que debería compensarlo —que el borde trasero de la placa deje sitio— quedó amortiguado al filtrar los huecos de remuestreo.

Acota la gravedad: la **fracción de tierra emergida sí es estable**, así que el exceso es plataforma sumergida, no continentes desbordando el planeta.

### Rift por divergencia y acreción de arco (16-08-2026)

Dos cambios de física en la advección, ambos provocados por la misma observación del usuario: *si acercar el modelo a la realidad rompe nuestra física, el problema es de nuestra física*.

**1. El rift se detecta con divergencia, no con geometría.** Antes: «si dos o más vecinas están sin reclamar, es un rift». Funcionaba con fronteras rectas; al hacer las placas fractales se desmoronó, porque una frontera que serpentea deja más huecos y todos se volvían océano (tierra emergida 24,6 % → 13,5 % solo por cambiar la forma de las placas). Ahora se proyecta la velocidad `ω × r` de la placa dueña de cada vecina sobre la dirección que se aleja de la celda y se suma. Umbral `0,10 × MaxAngularSpeed`, **calibrado por conservación** (creación ≈ destrucción en una esfera cerrada), no por hacer pasar un test: 0,00 → 64018/51200; 0,25 → 29296/53478; 0,10 → 49698/51126. Distingue además un rift de una **falla transformante**, que también deja huecos al discretizar pero no genera fondo oceánico.

**2. Acreción de arco: la fuente que faltaba.** La corteza continental solo podía perderse; nada la reponía. En la Tierra crece por magmatismo en las zonas de subducción (Andes, Japón). La corteza oceánica que converge se engrosa y, al superar ~20 km (`ArcMaturityThickness`), deja de subducir y pasa a continental. Restringida a celdas que tocan continente: sin esa restricción **satura** —con tiempo suficiente cualquier celda convergente supera el umbral— y bajar la tasa a la mitad no cambiaba nada (222 % → 220 %). El límite no era el ritmo sino la superficie afectada.

**Lo que aportó realmente, medido:** en 1000 Ma, tierra emergida 14,4 % (sin acreción) → 17,2 % (con ella); continental 3115 → 3363 celdas. Ayuda, pero **no cierra el problema**: la tierra sigue cayendo desde el 24,5 % inicial.

**Un error propio, corregido:** al reescribir `ContinentsPersist` se iba a justificar el nuevo ratio ×2,26 diciendo que ahora hay una fuente física legítima. El sabotaje (`ArcAccretionFactor = 0`) mostró que sin acreción el ratio ya era ×2,17 —la acreción aporta solo ~9 %—, así que la justificación habría sido falsa y el diagnóstico original (artefacto de remuestreo) sigue siendo el correcto.

**Límite conocido del test de convergencia**, comprobado saboteándolo: con la tasa ×10 el test sigue pasando. El punto de equilibrio lo fija la geometría de los márgenes de subducción, no el ritmo. Esa aserción **no valida la calibración**; eso lo cubre la fracción de tierra emergida de `LongRunStability`.

## A6. Defectos abiertos en detalle

### Escalonado de bordes ("peine")

El campo de IDs es **categórico**: no se puede interpolar, hay que tomar el vecino más cercano, y cada advección re-cuantiza el borde.

**Cuatro intentos, dos hipótesis falsadas por medición:**

| Intento | Resultado |
|---|---|
| Marco de referencia con rotación acumulada | **Peor**: tierra 24,6 → 9,2 %, montañas 7.472 → 969 m. La propiedad salía de la referencia acumulada mientras los datos se transportaban un paso atrás; al envejecer la referencia dejan de corresponderse |
| Submuestreo 4× por mayoría | **Peor**: colisiones ×3. Dos placas pueden reclamar la misma celda, y todo el algoritmo se apoya en cuántas reclaman |
| Limpieza de motas | **Mejor**: ×3,15 → ×2,02. Es lo que está en el código |
| Subir resolución | **Peor y ×16 de coste** ([tabla](#a7-mediciones)) |

**Hipótesis descartadas por medición:**

- *"Viene de encadenar remuestreos"* — **falso**. Menos advecciones dan **más** escalonado. La causa es el error de *cada* advección, que crece con el paso: una rotación no es una traslación uniforme, y ese diferencial es despreciable en un paso de un píxel. El paso ya está en su valor óptimo.
- *"Es cuantización, se encoge con la rejilla"* — **falso**. Empeora al subir resolución.

**Bloqueado por falta de métrica.** El cociente `frontera_final/frontera_inicial` ha inducido a error tres veces: mejora cuando el borde pasa a ser rectangular (más corto que un peine fino), sugirió una causa falsa, y no es comparable entre resoluciones. Antes de un quinto intento hace falta una métrica que mida **rectitud antinatural** —densidad de esquinas de 90° frente a la esperada para un círculo máximo— y no simple longitud.

### Cordones de corteza congelada — RESUELTO (16-08-2026)

Líneas elevadas que atravesaban el océano sin envejecer ni reciclarse.

**Causa raíz:** el test de reclamación era `prev[nearest(R⁻¹·d)].PlateID == P`, y ese `nearest()` redondea al centro de celda más cercano: **hasta media celda de error**. Una celda que pertenece a la placa P pero está a menos de media celda de su frontera anterior puede caer, al redondear, fuera de su propia región. Cero reclamantes para una celda que no es rift ni colisión: **un fallo de búsqueda, no física**. Y como el error depende de la geometría local, fallaban **siempre las mismas celdas**.

Los tres parches previos (crear océano / rellenar del vecindario / conservar) eran respuestas a *"¿qué hacemos con el hueco?"*, y el hueco **no debía existir**.

**Arreglo:** cuando la búsqueda estricta falla, se repite mirando las cuatro celdas que rodean la posición continua exacta — justo el alcance del redondeo. Solo en ese caso: las celdas de interior y de colisión no se tocan.

**El orden importa:** poner la recuperación antes del test de rift hacía que la tolerancia se tragara los rifts (con pasos de un píxel, una banda de rift es de un píxel de ancho). La creación se hundió a 1.758 frente a 39.360 destruidas.

| | Antes | Después |
|---|---|---|
| Celdas congeladas | ~22.900 | **748 (0,0157 %)** |
| Creación / destrucción | 14k / 50k | **48.199 / 51.078** |

La tierra emergida bajó de 25,1 % a 18,8 %, y **no es una regresión**: el 25,1 % estaba inflado por el propio bug, porque los cordones eran corteza continental elevada que no debía estar ahí.

## A7. Mediciones

### Rendimiento por fase

Instrumentar cambió por completo la lista de sospechosos.

```
antes:  sim 116.6 ms | adv 147.8 (motas 5.3) front 2.5 dif 2.7 iso 78.2
después: sim  27.6 ms | adv 174.1 (motas 9.2) front 2.9 dif 2.9 iso  6.8
```

- **Nivel del mar**: hacía 40 iteraciones de bisección × 393.000 celdas = 15,7 M de lecturas por paso para resolver *un número*. Sustituido por **Newton**, que aprovecha lo que la bisección tiraba: el nivel apenas se mueve entre pasos, y la derivada es gratis (`dV/dS` = área sumergida, contada en la misma pasada). 78,2 → 6,8 ms.
- **Advección**: las pasadas de conteo y resolución hacían el mismo trabajo caro; se cachea el caso mayoritario de un único reclamante.
- **Malla**: se reconstruía entera cada frame (~100.000 vértices). Limitada a 10 Hz.
- **Paso fijo** desacoplado del framerate: la física dejó de depender de a qué fps corras.

### Coste frente a resolución

| Res | Escalonado | ms/paso | Advecciones |
|---|---|---|---|
| 32 | ×1,28 | 0,56 | 19 |
| 48 | ×1,42 | 1,96 | 39 |
| 64 | ×1,54 | 1,86 | 39 |
| 96 | ×1,63 | 9,07 | 78 |

El coste escala con el número de celdas. Es el dato que dimensiona F3 y F4: **cada campo nuevo paga ese factor**.

## A8. Historial M0–M5 (10 y 11-08-2026)

### Válido y aprovechable
- **M0** — `git init`, `.gitignore`, baseline commiteado.
- **M1.5** — Bug de picos infinitos en fronteras convergentes y su sobrecorrección. Elevación desacoplada del radio. Escala real de la Tierra.
- **M1.6** — `PlanetApproachPawn` con velocidad log-interpolada y "colisión" por consulta de altura.
- **M3** — Culling de frustum y mapeo de aristas entre caras.
- **M5** — Persistencia (`UTectonicSaveGame`), verificada en editor.

### Correcciones de la auditoría
- **M1** quedó parado en el punto correcto; el diagnóstico de deuda en `UPlanetNaniteMesh` era acertado, y F0 abandonó ese pipeline en vez de arreglarlo.
- **M2** cerró como "CPU es la ruta activa". Correcto, pero se quedó corto: `BoundaryInteractions` no era redundante, era **inerte**.
- **M4** dio por buena una cobertura que no detectaba que las placas no se movían.
- El antiguo **M6+** decía "Erosión real (Pipe Model reemplazando el `SimpleFlowSimulation` actual)". Redacción engañosa: no había nada que reemplazar.

## A9. Los dos puntos aparcados de F1

Quedaban dos: *conectar `BoundaryInteractions`* y *que el ráster sea la única fuente de verdad del campo de IDs*. Ninguno se ha hecho, y no es descuido.

### El primero está escrito para un modelo que ya no existe

`BoundaryInteractions` produce `ElevationRateMaps`, es decir **tasas de elevación en metros**. Eso tenía sentido cuando la elevación era el estado primario.

Desde F2 no lo es: la elevación **se deriva** del grosor de corteza por isostasia, y `RebuildElevationFromIsostasy` la reconstruye entera al final de cada paso. Cualquier metro que `BoundaryInteractions` sumara se perdería en el mismo paso en que lo escribe.

Además lee `PlateSystem->GetPlateIDAt`, que consulta el mapa del Voronoi — la condición inicial, obsoleta desde la primera advección.

O sea: reconectarlo tal cual **escribiría en un campo derivado usando un mapa de placas caducado**. Las dos mitades están rotas.

Lo que sí sigue siendo aprovechable de esas 693 líneas: **hotspots y vulcanismo**, que no dependen del mapa de fronteras de la misma manera, y las fórmulas de ángulo de subducción y esfuerzo acumulado, que podrían reescribirse para producir **cambios de grosor** en vez de cambios de elevación. Eso es trabajo de rediseño, no de reconexión, y encaja mejor junto a F4 —cuando la erosión también toque el grosor— que colgando de F1.

### El segundo no tiene ningún consumidor todavía

Los únicos que llaman a `PlateSystem->GetPlateIDAt` son `BoundaryInteractions` (desactivado) y un camino de reserva del actor que solo se usa si el ráster no está inicializado.

Hacerlo ahora sería exactamente el patrón que destapó la auditoría: **calcular algo correcto que nadie lee**. Se hace cuando el primero lo necesite, y entonces será su prerequisito natural.

### Consecuencia para el estado de F1

F1 cumple su definición de hecho: las placas se mueven, la subducción y las dorsales son emergentes, hay conservación de corteza, y está verificado en pantalla con bandas de edad en las dorsales. Lo que queda no es F1 sin terminar, es **trabajo que cambió de sitio**.

## A10. Documentos relacionados

- [`SPECS.md`](SPECS.md) — inventario por archivo, con el estado real de cada subsistema.
- [`docs/`](docs/) — visión de producto original. Referencia de **alcance y contenido físico** (qué modelos, qué ecuaciones), no de orden ni fechas.
