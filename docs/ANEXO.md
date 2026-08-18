# ANEXO.md — Historial, defectos y mediciones

> **Qué es este documento.** La memoria del proyecto: por qué las cosas son como son, qué se intentó y falló, qué se midió y con qué resultado. Sale de las notas que antes vivían mezcladas dentro del ROADMAP, que ahora es una lista de pasos limpia.
>
> **Para qué sirve.** Para no repetir intentos ya falsados, y para poder auditar cualquier decisión hasta la medición que la sostiene. Varias de las reglas de [`REQUISITOS.md §0`](REQUISITOS.md#0-principios-de-diseño) son consecuencia directa de un episodio de aquí.
>
> **Cómo leerlo.** No hace falta leerlo entero. Está enlazado desde donde hace falta.

---

## Índice

- [A1. La regla de verificación](#a1-la-regla-de-verificación)
- [A2. El mapeo de caras](#a2-el-mapeo-de-caras)
- [A3. Tests que no comprobaban nada](#a3-tests-que-no-comprobaban-nada)
- [A4. Calibración de la orogenia](#a4-calibración-de-la-orogenia)
- [A5. Crecimiento del área continental](#a5-crecimiento-del-área-continental)
- [A6. Defectos con historia](#a6-defectos-con-historia)
- [A7. Mediciones](#a7-mediciones)
- [A8. Historial M0–M5](#a8-historial-m0m5)
- [A9. Los dos puntos aparcados de F1](#a9-los-dos-puntos-aparcados-de-f1)
- [A10. El defecto de raíz de F1: la propiedad y el material](#a10-el-defecto-de-raíz-de-f1-la-propiedad-y-el-material)
- [A11. Defectos abiertos hoy](#a11-defectos-abiertos-hoy)
- [A12. Riesgos vigentes](#a12-riesgos-vigentes)
- [A13. F1D — la frontera como objeto (16-08-2026)](#a13-f1d--la-frontera-como-objeto-16-08-2026)

---

## A1. La regla de verificación

**Ninguna fase se da por hecha si no se puede ver en pantalla.**

El motivo sale de la propia auditoría: los tres bugs más caros de F0 sobrevivieron meses precisamente porque nadie podía *ver* lo que el código hacía. Un test dice «no ha petado»; mirar el planeta dice «esto no parece la Tierra».

Eso obliga a separar dos cosas que estaban mezcladas:

| | Qué es | Cuándo |
|---|---|---|
| **Visualización de diagnóstico** | Campos sobre el globo, con leyenda, conmutables. Barata, sin LOD | Desde F0.5, y luego **cada fase** |
| **Renderizado de producto** | LOD hasta la superficie, materiales, luz | F6 |

La primera hace comprobable cada fase; la segunda es acabado. Ha demostrado su valor de inmediato: la primera vista real destapó tres defectos que ningún test unitario podía ver — *winding* invertido, colores lavados por usar material iluminado, y un escalón en el limbo que resultó ser un bug de simulación y no de render.

Y su **recíproca también vale**: si un test contradice lo que se ve en pantalla, la sospecha va primero al test (ver [el quinto engaño de una métrica](#el-quinto-engaño-de-una-métrica)).

Formalizado como requisito en [`REQUISITOS.md §0.5`](REQUISITOS.md#05-ninguna-fase-está-hecha-si-no-se-ve-en-pantalla).

---

## A2. El mapeo de caras

`UCubeSphereGrid::GetFaceAxes` daba para `+Z` el eje `V = (0,1,0)` mientras `RasterizedTectonics` usaba `FVector(U, -V, 1)` — V invertida, y en `−Z` invertida al revés. Las cuatro caras ecuatoriales sí coincidían, así que el bug **solo se manifestaba en los casquetes**: el mapa de placas y el de elevación quedaban espejados allí.

El convenio antiguo era además **levógiro** en esas dos caras (`AxisU × AxisV = −Normal`), lo que invertía el *winding* de los triángulos generados. Al unificar hubo que quitar el `bFlipWinding` que lo compensaba — y no hacerlo dejó los casquetes renderizando del revés hasta que se vio en pantalla.

**El detalle que convierte esto en una regla:** el mapeo estaba escrito a mano **8 veces**, y la primera pasada de migración dejó **2 de las 8 copias sin migrar**, porque su texto no era idéntico al de las otras. Ni una migración deliberada, buscándolas a propósito, las encontró todas.

→ [`REQUISITOS.md R1.2`](REQUISITOS.md#12-estructura-elegida-cubo-esférico-normalizado)

---

## A3. Tests que no comprobaban nada

Tres casos en dos sesiones:

1. **M3/M4 se cerraron con la suite en rojo.** 3 de 8 tests fallaban; se había compilado, no ejecutado. El comando para correr la suite no estaba documentado en ningún sitio (ahora está en el [README](../README.md)).
2. **`ContinentsPersist` pasaba con las placas quietas.** Con el campo congelado el recuento no cambia, el ratio sale 1,0 y todas sus aserciones se cumplen. Detectado saboteando la advección.
3. **`LongRunStability` acotaba solo por arriba.** Dejó pasar un desplome de tierra emergida del 24,6 % al 11,3 % sin decir una palabra.

De aquí salen las reglas 1 y 2 de [`REQUISITOS.md §0.7`](REQUISITOS.md#07-reglas-sobre-los-tests).

---

## A4. Calibración de la orogenia

Al probar F1 había deriva pero **ninguna montaña**. No faltaba física: era una **asimetría de unidades**.

El levantamiento se escalaba por `dt` y la difusión no —se aplicaba tal cual en cada paso—, así que a 60 fps la difusión borraba ~70 % del relieve por Ma mientras el levantamiento aportaba 5·10⁻⁴ m/Ma. Desacoplados unos **7 órdenes de magnitud**, y el resultado dependía del framerate.

**Corregido:**
- `DiffusionRate` pasa a ser tasa **por Ma** de tiempo simulado, no por paso.
- `OrogenyFactor` pasa a ser **eficiencia adimensional**: la convergencia se convierte a m/Ma (rad/Ma × radio) antes de multiplicar.
- Integración por sub-pasos de máx. 0,5 Ma, porque con `TimeScale` alto llegaban ~16 Ma de golpe.

Desde F2 lo que engrosa es el **grosor de corteza**, no la elevación, y las montañas salen por flotación. Calibrado contra el Himalaya (35 → 70 km en ~50 Ma, o sea ~700 m/Ma). Resultado: 1.184 → 7.472 m en 200 Ma, con solo el 0,3 % de la tierra contra el techo isostático.

Ese techo tampoco es un *clamp* arbitrario: `MaxThickness` (75 km, límite de delaminación) por Airy da ~7.507 m.

---

## A5. Crecimiento del área continental

**Corrección de un error propio:** se justificó una cota holgada diciendo que la erosión de F4 aportaría el sumidero. **Es falso.** La erosión adelgaza corteza y mueve sedimento, pero **no convierte corteza continental en oceánica**; el área continental no la toca.

Causa real: en una colisión la continental gana y la celda de destino pasa a ser continental, convirtiendo océano en continente. Lo que debería compensarlo —que el borde trasero de la placa deje sitio— quedaba amortiguado al filtrar los huecos de remuestreo.

Acota la gravedad: la **fracción de tierra emergida sí es estable**, así que el exceso es plataforma sumergida, no continentes desbordando el planeta.

### Rift por divergencia y acreción de arco

Dos cambios de física en la advección, ambos provocados por la misma observación: *si acercar el modelo a la realidad rompe nuestra física, el problema es de nuestra física*.

**1. El rift se detecta con divergencia, no con geometría.**

Antes: «si dos o más vecinas están sin reclamar, es un rift». Funcionaba con fronteras rectas; al hacer las placas fractales se desmoronó, porque una frontera que serpentea deja más huecos y **todos se volvían océano** (tierra emergida 24,6 % → 13,5 % solo por cambiar la forma de las placas).

Ahora se proyecta la velocidad `ω × r` de la placa dueña de cada vecina sobre la dirección que se aleja de la celda, y se suma. Umbral `0,10 × MaxAngularSpeed`, **calibrado por conservación** (creación ≈ destrucción en una esfera cerrada), no por hacer pasar un test:

| Umbral | Creada / destruida |
|---|---|
| 0,00 | 64.018 / 51.200 |
| 0,25 | 29.296 / 53.478 |
| **0,10** | **49.698 / 51.126** |

Distingue además un rift de una **falla transformante**, que también deja huecos al discretizar pero no genera fondo oceánico.

**2. Acreción de arco: la fuente que faltaba.**

La corteza continental solo podía perderse; nada la reponía. En la Tierra crece por magmatismo en las zonas de subducción (Andes, Japón). La corteza oceánica que converge se engrosa y, al superar ~20 km (`ArcMaturityThickness`), deja de subducir y pasa a continental.

**Restringida a celdas que tocan continente.** Sin esa restricción **satura**: con tiempo suficiente cualquier celda convergente supera el umbral, y bajar la tasa a la mitad no cambiaba nada (222 % → 220 %). El límite no era el ritmo, era la superficie afectada.

**Lo que aportó realmente, medido:** en 1000 Ma, tierra emergida 14,4 % (sin acreción) → 17,2 % (con ella); continental 3.115 → 3.363 celdas. Ayuda, pero no cerró el problema.

**Un error propio, corregido:** al reescribir `ContinentsPersist` se iba a justificar el nuevo ratio ×2,26 diciendo que ahora hay una fuente física legítima. El sabotaje (`ArcAccretionFactor = 0`) mostró que sin acreción el ratio ya era ×2,17 —la acreción aporta solo ~9 %—, así que la justificación habría sido falsa y el diagnóstico original (artefacto de remuestreo) seguía siendo el correcto.

**Límite conocido del test de convergencia**, comprobado saboteándolo: con la tasa ×10 el test sigue pasando. El punto de equilibrio lo fija la geometría de los márgenes de subducción, no el ritmo. Esa aserción **no valida la calibración**; eso lo cubre la fracción de tierra emergida de `LongRunStability`.

### Acreción de arco: restricción por placa (16-08-2026)

Al construir la clasificación de frontera por normal de Sobel (R2.13, commit `705459d`) se midió que `ContinentsPersist` pasaba de estar cerca de converger a saltar directamente la red de seguridad: **74,3 % de cobertura continental** en el fixture de prueba.

**Causa.** `bTouchesContinent` comprobaba solo `CrustTypeData == 1` del vecino, sin mirar de qué placa era. Una celda oceánica que acreccionaba pasaba a tipo continental, y al paso siguiente su propio vecino oceánico —de la **misma** placa subducente— ya "tocaba continente" sin haber llegado nunca al margen con la placa cabalgante. Es un frente que se propaga solo, célula a célula, dentro de la propia placa.

**Arreglo:** exigir `PlateIDData` distinto además de tipo continental. Ancla la acreción al contacto real con la placa cabalgante.

**Medido:** 74,3 % → **68,3 %**. Mejora real (6 puntos), pero `ContinentsPersist` se queda en rojo — sigue saltando la red de seguridad del 50 %.

**Por qué no bastaba, hipótesis del momento:** se sospechó que hacía falta ciclo de vida de placas (F1E) — una placa oceánica que no puede morir nunca cierra el margen que alimenta la acreción. **Descartada por medición** ([A13](#a13-f1d--la-frontera-como-objeto-16-08-2026)): lo que de verdad hacía falta era promediar la clasificación por segmento de frontera en vez de por celda suelta (R2.12), no ciclo de vida. Con eso, `ContinentsPersist` pasa sin tocar F1E. Queda como aviso: que una hipótesis esté bien motivada físicamente no la libra de estar equivocada.

---

## A6. Defectos con historia

### Escalonado de bordes («peine»)

El campo de IDs es **categórico**: no se puede interpolar, hay que tomar el vecino más cercano, y cada advección re-cuantiza el borde.

**Cuatro intentos, dos hipótesis falsadas por medición:**

| Intento | Resultado |
|---|---|
| Marco de referencia con rotación acumulada | **Peor**: tierra 24,6 → 9,2 %, montañas 7.472 → 969 m. La propiedad salía de la referencia acumulada mientras los datos se transportaban un paso atrás; al envejecer la referencia dejan de corresponderse |
| Submuestreo 4× por mayoría | **Peor**: colisiones ×3. Dos placas pueden reclamar la misma celda, y todo el algoritmo se apoyaba en cuántas reclaman |
| Limpieza de motas | **Mejor**: ×3,15 → ×2,02 |
| Subir resolución | **Peor y ×16 de coste** |

**Hipótesis descartadas por medición:**

- *«Viene de encadenar remuestreos»* — **falso en su momento**. Menos advecciones daban **más** escalonado. La causa es el error de *cada* advección, que crece con el paso: una rotación no es una traslación uniforme, y ese diferencial es despreciable en un paso de un píxel.
- *«Es cuantización, se encoge con la rejilla»* — **falso**. Empeora al subir resolución.

**Desenlace:** la separación de propiedad y material ([A10](#a10-el-defecto-de-raíz-de-f1-la-propiedad-y-el-material)) bajó el escalonado a **×1,96**, mejor marca del proyecto, sin haberlo buscado. Parte del escalonado era el desajuste del reparto, no cuantización.

**Aviso que sigue vigente.** El cociente `frontera_final / frontera_inicial` ha inducido a error tres veces: mejora cuando el borde pasa a ser rectangular (más corto que un peine fino), sugirió una causa falsa, y no es comparable entre resoluciones. Antes de tocar esto otra vez hace falta una métrica que mida **rectitud antinatural** —densidad de esquinas de 90° frente a la esperada para un círculo máximo— y no simple longitud.

### Cordones de corteza congelada — RESUELTO

Líneas elevadas que atravesaban el océano sin envejecer ni reciclarse.

**Causa raíz:** el test de reclamación era `prev[nearest(R⁻¹·d)].PlateID == P`, y ese `nearest()` redondea al centro de celda más cercano: **hasta media celda de error**. Una celda que pertenece a la placa P pero está a menos de media celda de su frontera anterior puede caer, al redondear, fuera de su propia región. Cero reclamantes para una celda que no es rift ni colisión: **un fallo de búsqueda, no física**. Y como el error depende de la geometría local, fallaban **siempre las mismas celdas**.

Los tres parches previos (crear océano / rellenar del vecindario / conservar) eran respuestas a *«¿qué hacemos con el hueco?»*, y el hueco **no debía existir**.

**Arreglo:** cuando la búsqueda estricta falla, se repite mirando las cuatro celdas que rodean la posición continua exacta — justo el alcance del redondeo. Solo en ese caso.

**El orden importa:** poner la recuperación **antes** del test de rift hacía que la tolerancia se tragara los rifts (con pasos de un píxel, una banda de rift es de un píxel de ancho). La creación se hundió a 1.758 frente a 39.360 destruidas.

| | Antes | Después |
|---|---|---|
| Celdas congeladas | ~22.900 | **748 (0,0157 %)** |
| Creación / destrucción | 14k / 50k | **48.199 / 51.078** |

La tierra emergida bajó de 25,1 % a 18,8 %, y **no era una regresión**: el 25,1 % estaba inflado por el propio bug, porque los cordones eran corteza continental elevada que no debía estar ahí.

### El quinto engaño de una métrica

El apartado del escalonado avisaba de que el cociente de frontera había inducido a error **tres veces**. Volvió a pasar, con otra métrica y con un coste mucho mayor: **dos días de trabajo en la dirección equivocada**.

`Simu.Tectonics.NoStraightCrustBridges` estaba en rojo con «12 de 104», y se dio por hecho que los **puentes rectos de corteza habían vuelto** — contradiciendo lo que se estaba viendo en pantalla, que es que no estaban.

Lo que ese test mide **no son puentes**. Su aserción es `SamePlate == 0` sobre «anomalías de una celda»: píxeles *sueltos* cuyo tipo de corteza difiere del de sus dos vecinas en un eje. «12 de 104» son 104 motas de un píxel, 12 de ellas dentro de una misma placa. A Res=256 eso es invisible.

Se llama así porque se escribió durante la caza de los puentes, para detectar la **semilla** de uno. La medida que sí vería una franja larga es `LongestRun()`, que el test calcula y **registra pero no comprueba**.

Reglas 3, 4 y 5 de [`REQUISITOS.md §0.7`](REQUISITOS.md#07-reglas-sobre-los-tests) salen de aquí.

### El reloj que podía mentir — AUDITADO

`Step()` trocea el desplazamiento en advecciones de ~1 píxel con un techo de 8 por paso, y al agotarlo **descarta el tiempo sobrante** mientras el contador de tiempo simulado sigue sumando el paso entero. Si eso ocurre, todas las edades de corteza y todas las tasas del modelo están mal escaladas — y con ellas cualquier número que se use como criterio para juzgar otro cambio.

**Primer intento de medirlo, y por qué era falso.** Restar `TotalSimulationTime − AdvectedTime` daba 0,47 %, y se dio por «tiempo perdido». Pero el desfase observado en pantalla era **alto y a saltos al principio, decayendo con el tiempo**: eso no es pérdida, es el **acumulador a medio llenar** esperando a disparar — un diente de sierra acotado por un intervalo de advección, que decae como 1/t. Los números cuadran exactamente: un intervalo de ~1,61 Ma da 1,6 % a los 100 Ma.

O sea que la resta estaba **dominada por un término benigno y no podía ver el maligno**. Métrica mal diseñada.

**Ahora se mide lo descartado directamente, en la línea donde se descarta.** Resultado: **0,00 Ma en 0 ocasiones sobre 4.000 pasos**, verificado además en el editor con `TimeScale` alto. El reloj no pierde nada, y ahora es un hecho medido y no un razonamiento.

Se renombró «desfase» a «pendiente» en el HUD, que es lo que de verdad es.

**Corolario metodológico:** una métrica compuesta por diferencia de dos totales puede estar dominada por un término benigno y ser ciega al que importa. Medir en el punto del suceso, no restando agregados.

---

## A7. Mediciones

### Rendimiento por fase

Instrumentar cambió por completo la lista de sospechosos.

```
antes:   sim 116,6 ms | adv 147,8 (motas 5,3) front 2,5 dif 2,7 iso 78,2
después: sim  27,6 ms | adv 174,1 (motas 9,2) front 2,9 dif 2,9 iso  6,8
```

- **Nivel del mar**: hacía 40 iteraciones de bisección × 393.000 celdas = **15,7 M de lecturas por paso para resolver *un número***. Sustituido por **Newton**, que aprovecha lo que la bisección tiraba: el nivel apenas se mueve entre pasos (el valor anterior ya es buena estimación) y la derivada es gratis (`dV/dS` = área sumergida, contada en la misma pasada). 78,2 → 6,8 ms, en 2–3 pasadas.
- **Advección**: las pasadas de conteo y de resolución hacían el mismo trabajo caro; se cachea el caso mayoritario de un único reclamante.
- **Malla**: se reconstruía entera cada frame (~100.000 vértices). Limitada a 10 Hz.
- **Paso fijo** desacoplado del framerate: la física dejó de depender de a qué fps corras.

Coste dominante restante: la advección (~174 ms por ejecución, amortizada a ~11 ms/paso porque no corre en todos).

### Coste frente a resolución

| Res | Escalonado | ms/paso | Advecciones |
|---|---|---|---|
| 32 | ×1,28 | 0,56 | 19 |
| 48 | ×1,42 | 1,96 | 39 |
| 64 | ×1,54 | 1,86 | 39 |
| 96 | ×1,63 | 9,07 | 78 |

El coste escala con el número de celdas. Es el dato que dimensiona cada fase nueva: **cada campo nuevo paga ese factor**.

### Resolución del drenaje

`Simu.Hydrology.DrainageResolution`. Se temía que 39 km/celda fuera insuficiente para redes de drenaje:

| Res | Tierra | Cauce | Sumideros | Coste |
|---|---|---|---|---|
| 64 | 6.469 | 1,2 % | 1,0 % | 1,9 ms |
| 128 | 26.560 | 2,1 % | 0,7 % | 9,3 ms |
| 192 | 107.024 | **2,2 %** | 0,7 % | 40,4 ms |

**La fracción de cauce satura entre 128 y 192**, así que la topología de la red ya está resuelta y subir más solo añadiría detalle de valle, no estructura. Y solo **0,7 % de sumideros**: el agua llega al mar en vez de estancarse.

El límite real es **la malla de visualización**, más gruesa que el ráster de simulación. Subirla es mucho más barato que subir la simulación.

### Barrido de re-anclaje del referente

`Simu.Tectonics.ReferenceRebaseSweep`. 1000 Ma, Res=48, 8 placas, semilla 4242. `N` = cada cuántas advecciones se adopta el mundo como referente nuevo.

| N | tierra % | balanza | solape % | sin resolver % | escalonado | continental | grosor cont. |
|---|---|---|---|---|---|---|---|
| nunca | 24,5 → 23,9 | **0,04** | 28,70 | 31,62 | ×2,96 | 3630 → 4046 | **53,9 km** |
| 1 | 24,5 → 13,1 | **0,99** | 1,26 | 0,003 | ×2,04 | 3630 → 2182 | **36,8 km** |
| 4 | 24,5 → 17,8 | 0,49 | 4,27 | 0,69 | ×2,28 | 3630 → 2863 | 40,5 km |
| 16 | 24,5 → 25,8 | 0,24 | 11,16 | 5,43 | ×2,68 | 3630 → 6526 | 46,8 km |
| 64 | 24,5 → 28,2 | 0,09 | 24,43 | 21,78 | ×3,35 | 3630 → 5390 | 53,4 km |

Qué probó esto:

- **Todo escala monótonamente con `N`.** Balanza, solape, sin resolver, escalonado, área continental y grosor medio. No eran seis defectos: eran **seis vistas del mismo desajuste**.
- **La balanza se arregla del todo** (0,04 → 0,99). Las 1.185.542 celdas «destruidas» eran desajuste, no subducción.
- **El escalonado MEJORA al re-anclar** (×2,96 → ×2,04). Se temía lo contrario.
- **El grosor continental delata el mecanismo**: 53,9 km sin re-anclar frente a 36,8 con `N`=1, y `ContinentalThickness` es 35 km. La banda de solape corría la rama de colisión sobre el 28,7 % del planeta en cada advección, y ahí el material continental **se apila**. Era una máquina de fabricar corteza.
- **Y sin embargo `N`=1 no era la solución**, que es el resultado importante: rompía `OceanicRibbon`, justo el test que justificaba que el referente existiera. Tres tests a verde y otros tres a rojo.

**Ningún `N` funcionaba.** El barrido no era el arreglo: era el experimento que demostró que hacía falta el arreglo de verdad ([A10](#a10-el-defecto-de-raíz-de-f1-la-propiedad-y-el-material)).

### La cinta seguía al número de advecciones, no al tiempo

Variando cuántas advecciones caben en los **mismos 120 Ma**:

| Advecciones | 48 | 24 | 6 | 2 | 1 |
|---|---|---|---|---|---|
| Longitud de la cinta | 14 | 8 | 3 | 4 | 2 |

La longitud sigue al **número de remuestreos**, no al tiempo simulado. Es la prueba directa de que se estaba encadenando material, y el origen del requisito [R2.10](REQUISITOS.md#24-propiedad-y-material-aplicación-de-04).

Cómo se llegó ahí, tras nueve capturas de pantalla y muchas hipótesis muertas:
- El artefacto salía en todos los campos de material pero **no** en el de ID de placa. No era que ese estuviera sano: dentro de una placa es un color plano y el deshilachado no se ve.
- Las celdas nuevas de la cinta eran corteza oceánica **vieja**, no recién creada.
- Estaban en la **misma placa** que el continente que las rodeaba, donde el material debería moverse en bloque.

---

## A8. Historial M0–M5

Milestones anteriores a la auditoría, con el veredicto que les dio esa auditoría.

### Válido y aprovechable
- **M0** — `git init`, `.gitignore`, baseline commiteado.
- **M1.5** — Bug de picos infinitos en fronteras convergentes y su sobrecorrección. Elevación desacoplada del radio. Escala real de la Tierra.
- **M1.6** — `PlanetApproachPawn` con velocidad log-interpolada y «colisión» por consulta de altura.
- **M3** — Culling de frustum y mapeo de aristas entre caras.
- **M5** — Persistencia (`UTectonicSaveGame`), verificada en editor: guarda, carga, y el planeta resultante es visualmente idéntico al momento del guardado.

### Correcciones de la auditoría
- **M1** quedó parado en el punto correcto; el diagnóstico de deuda en `UPlanetNaniteMesh` era acertado, y F0 abandonó ese pipeline en vez de arreglarlo.
- **M2** cerró como «CPU es la ruta activa». Correcto, pero se quedó corto: `BoundaryInteractions` no era redundante, era **inerte**.
- **M4** dio por buena una cobertura que no detectaba que las placas no se movían.
- El antiguo **M6+** decía «Erosión real (Pipe Model reemplazando el `SimpleFlowSimulation` actual)». Redacción engañosa: **no había nada que reemplazar** — `SimpleFlowSimulation` es un validador de conservación de masa de la rejilla, no un precursor de hidrología.

### El escalado que casi pasa desapercibido

`TectonicsTestActor` usaba `VisualRadius = 500 m` (un planeta de juguete) y la fórmula de desplazamiento escalaba el offset como **porcentaje del radio**, contradiciendo su propio comentario, que documentaba `ElevationScale` como multiplicador directo. A escala de juguete el error pasaba desapercibido; a escala real (6.371 km) habría dado montañas de miles de kilómetros.

Corregido a multiplicador directo sobre metros reales, independiente del radio. → [`REQUISITOS.md R8.8`](REQUISITOS.md#82-renderizado-de-producto)

### Dos pipelines a la vez

Al probar el actor en el editor apareció un planeta con paredes casi verticales, visualmente como «dos esferas anidadas». Causa: `Tick` invocaba **dos sistemas de tectónica independientes** cada paso. Uno de ellos (`BoundaryInteractions`) volcaba su resultado en `ElevationRateMaps` que **nadie leía** (`ApplyElevationChanges()` estaba vacía); el otro sumaba elevación en celdas de frontera de un píxel de ancho hasta el tope de 12 km, junto a vecinas en la base oceánica.

Se corrigió en dos iteraciones. La primera —un blur completo cada paso— convergía a «planeta perfectamente liso» tras ~1.167 pasos, porque la malla se refrescaba en vivo. La segunda lo sustituyó por una **mezcla parcial acotada** que se comporta como una ecuación de difusión y alcanza equilibrio con el levantamiento en vez de barrerlo.

Lección: llamarlos «dos pipelines redundantes» se quedaba corto. No competían — uno era **inerte**.

---

## A9. Los dos puntos aparcados de F1

Quedaban dos: *conectar `BoundaryInteractions`* y *que el ráster sea la única fuente de verdad del campo de IDs*. Ninguno se ha hecho, y no es descuido.

### El primero está escrito para un modelo que ya no existe

`BoundaryInteractions` produce `ElevationRateMaps`, es decir **tasas de elevación en metros**. Eso tenía sentido cuando la elevación era el estado primario.

Desde F2 no lo es: la elevación **se deriva** del grosor de corteza por isostasia, y se reconstruye entera al final de cada paso. Cualquier metro que `BoundaryInteractions` sumara **se perdería en el mismo paso en que lo escribe**.

Además lee `PlateSystem->GetPlateIDAt`, que consulta el mapa del Voronoi — la condición inicial, obsoleta desde la primera advección.

O sea: reconectarlo tal cual **escribiría en un campo derivado usando un mapa de placas caducado**. Las dos mitades están rotas. Este es el episodio que originó [`REQUISITOS.md §0.3`](REQUISITOS.md#03-estado-primario-contra-estado-derivado).

Lo que sí sigue siendo aprovechable de esas 693 líneas: **hotspots y vulcanismo**, que no dependen del mapa de fronteras de la misma manera, y las fórmulas de ángulo de subducción y esfuerzo acumulado, que podrían reescribirse para producir **cambios de grosor** en vez de cambios de elevación. Eso es trabajo de rediseño, no de reconexión.

### El segundo no tiene ningún consumidor todavía

Los únicos que llaman a `PlateSystem->GetPlateIDAt` son `BoundaryInteractions` (desactivado) y un camino de reserva del actor que solo se usa si el ráster no está inicializado.

Hacerlo ahora sería exactamente el patrón que destapó la auditoría: **calcular algo correcto que nadie lee** ([`REQUISITOS.md §0.6`](REQUISITOS.md#06-no-se-construye-infraestructura-sin-consumidor)).

---

## A10. El defecto de raíz de F1: la propiedad y el material

*(Encontrado mirando el visor de ID de placa, no midiendo. Es el hallazgo central del proyecto hasta la fecha.)*

### El síntoma

El campo de ID de placa **arrancaba como 8 regiones limpias y se disolvía en una mezcla a escala de píxel**. A los ~200 Ma no había placas: había ocho etiquetas repartidas como ruido, con una trama diagonal.

Y la observación que lo convirtió en diagnóstico: **ningún otro campo estaba roto.** Grosor, edad, tipo, altura y relieve mostraban cuerpos grandes, limpios y con el mismo contorno entre sí. El mismo blob aparecía idéntico en los seis filtros.

Si las placas se hubieran fragmentado de verdad, el material se habría fragmentado con ellas — viaja en la misma pasada del mismo código. No lo había hecho. **El material se transportaba como cuerpos sólidos y la etiqueta de propiedad se había convertido en ruido: estaban desacoplados.**

### Por qué era lo peor que podía pasar

Toda la clasificación de fronteras salía de contar reclamantes por celda: 1 movimiento, 0 rift, ≥2 colisión. No había ningún otro sitio donde se decidiera qué es una dorsal y qué una subducción.

Con el ID hecho ruido ese conteo no estaba mal calibrado: **estaba sin significado**. Cada celda tenía vecinas de tres o cuatro placas, así que cada celda era frontera. Y la prueba de divergencia que decide si un hueco es rift leía la velocidad de las placas vecinas — o sea, leía cuatro placas al azar.

### Nivel 1 — Comparten estructura, y necesitan lo contrario

Es el hallazgo central, y explica por qué **todo arreglo hasta entonces había sido un balancín**.

En `AdvectPlateField`, la pregunta *«¿de quién es esta celda?»* y la pregunta *«¿qué material hay aquí?»* se le hacían al mismo sitio: `ReferenceData`. Misma estructura, misma línea, misma antigüedad. Pero tienen requisitos **opuestos**:

- **La propiedad es una partición.** Debe seguir siéndolo en todo momento, y para eso hay que **re-deducirla del estado actual en cada paso**. Deducida de una foto vieja, se despega de la realidad y degenera en ruido.
- **El material es una sustancia transportada.** No se puede remuestrear repetidamente sin deshilacharlo. Hay que leerlo **una sola vez** desde un marco propio con la rotación acumulada.

Uno exige encadenar. El otro exige no encadenar nunca. Y estaban soldados.

| | propiedad | material |
|---|---|---|
| Con referente compartido | se pudre → ID hecho ruido | limpio, sin cinta |
| Re-anclando cada advección | perfecta (solape 1,26 %) | vuelve la cinta |

No eran dos problemas compitiendo por un ajuste: **eran dos campos con necesidades contrarias en el mismo cajón**. El [barrido](#barrido-de-re-anclaje-del-referente) lo midió de punta a punta.

### Nivel 2 — El modelo no tenía fronteras, tenía interiores

En la Tierra la tectónica **es** la frontera: rifts, subducciones, orogenias y transformantes ocurren todos en una línea. El interior de una placa solo rota.

Aquí no existía la frontera como objeto en ninguna parte. Existía el interior —una etiqueta por celda— y la frontera se infería a posteriori contando reclamantes. Dos consecuencias:

- **El coste estaba invertido.** Cada advección hacía 6·Res²·N rotaciones de cuaternión para calcular con precisión perfecta la parte aburrida, y la parte interesante salía de restos.
- **Nada garantizaba la teselación.** Ocho regiones rígidas rotando cada una por su lado **no pueden** teselar una esfera. Es geometría, no un bug. Se confió en que la partición emergiera, y no puede.

**Corrección de un error de concepto propio:** los huecos y los solapes **no son el error**. *Son* la tectónica — en una dorsal sobra sitio de verdad y en una fosa falta material de verdad; la tectónica de placas no conserva área localmente. El error era que ocurrían **en el 28,7 % de la superficie** en vez de en una banda de una celda a lo largo de las fronteras reales.

### Nivel 3 — Cinemática sin dinámica, y sin ciclo de vida

- **Las placas no nacen ni mueren.** `GeneratePlates()` las crea una vez; no hay un solo `Add` ni `RemoveAt` después. En la Tierra el número no se conserva. Aquí es imposible **por construcción**.
- **Ningún polo de Euler se escribe nunca.** `EulerPole` y `AngularVelocity` se leen para rotar y nada más. En la Tierra el motor principal es el **tirón de la losa**. Sin ese lazo no hay estados de equilibrio: nada regula la fracción de tierra y nada hace que un supercontinente se rompa y se vuelva a juntar.

El invariante correcto **no menciona el número de placas**:

> En cada instante, cada punto de la esfera pertenece a exactamente una placa.

### El plan, y cuál era la parte difícil

1. **Separar propiedad de material.** ✅ *Hecho.*
2. **La frontera, como objeto.** Clasificar por velocidad relativa proyectada sobre la normal.
3. **Ciclo de vida.** Componentes conexas sobre el mapa de IDs.
4. **Lazo dinámico**, al final.

### Cómo se cerró el punto 1

Se tiró `ReferenceData` entero. No era reparable: era el sitio donde estaban soldadas las dos cosas que había que separar. Con él se fueron la recuperación por tolerancia de media celda y la rama de «celda sin resolver que conserva su estado» — ambas existían para tapar huecos que solo aparecían porque la partición estaba podrida.

- **La propiedad se decide contra el mundo de ahora**, con la rotación **incremental** de esta advección. El mundo es una partición por construcción — una celda, un dueño — así que preguntarle a él no puede degenerar.
- **El material vive en un ráster por placa**, en el marco propio de esa placa, leído con la rotación **acumulada**: un solo remuestreo por muchas advecciones que pasen.

Y esto desbloqueó algo que antes era imposible: **el write-back ya puede escribir territorio que la placa acaba de ganar**. Antes no podía, porque el ID del referente compartido decidía la propiedad, y meter territorio ajeno hacía que una placa se comiera a las demás. Ahora la propiedad no sale de ahí, así que el marco de material puede crecer y encogerse libremente — que es lo que permite ganar suelo en un rift y perderlo en una subducción.

**Un detalle de implementación que costó dos intentos:** el write-back hay que hacerlo **recorriendo el marco de la placa y tirando del mundo**, no empujando desde el mundo. Empujando, unas celdas recibían dos escrituras y otras ninguna, y esas se quedaban con material rancio: 18,92 % de celdas sin resolver con `floor()`, 20,32 % por el camino de lectura.

**Resultado medido** (`LongRunStability`, 1000 Ma):

| Métrica | Antes | Después |
|---|---|---|
| Balanza creada/destruida | 0,04 | **0,98** |
| Celdas sin resolver | 31,6 % | **0,0001 %** |
| Escalonado | ×2,96 | **×1,96** (mejor marca del proyecto) |

`OceanicRibbon` pasa a verde — era la prueba de que el material no se deshilacha, y es justo lo que el re-anclaje no podía dar. Con él, `ContinentsPersist` y `FrozenCellsAtProductionRes`.

### Descartado, y por qué

**Placas como polígonos esféricos con aristas compartidas** (lo que hace GPlates). Teselan por construcción, sin huecos posibles.

Descartado por riesgo: mantener esa topología automáticamente, con placas que nacen y mueren, es un problema abierto — en GPlates lo hace **un humano a mano**. La vía del ráster con los invariantes bien puestos está probada (`platec`, Viitanen).

---

## A11. Defectos abiertos hoy

> La tabla de defectos anterior daba todos por acotados; era falsa, porque los medía por separado sin ver que casi todos eran síntomas de lo mismo. **La lección: medir defectos por separado los hace parecer acotados.** Cada uno tenía su número, su test y su casilla, y ninguno pasaba de «vigilado». Mirando el campo de IDs en pantalla se veía en dos segundos que eran el mismo.

| Defecto | Medida | Estado |
|---|---|---|
| **Tierra emergida cae al 13,2 %** desde 24,5 % en 1000 Ma | fracción de superficie | 🔴 **Abierto y prioritario.** El planeta se ahoga. Candidatos: falta la acreción bien dimensionada, falta el ciclo de vida de placas, o el reparto en colisión aún destruye continente |
| **Celdas que no advectan** | 185 (0,19 %) en corrida larga; 154 cerca del polo de Euler | 🟡 Abierto. Reducido desde ~22.900, pero no es cero. Dos tests lo miden por separado y los dos están en rojo |
| **285 motas de un píxel** dentro de una placa | anomalías de tipo de corteza | 🟡 Abierto, cosmético a resolución de producción |
| **Respaldos de lectura de material** | 1,93 % de 4,8 M de lecturas, estable | 🟢 Vigilado. Residual pero no cero |
| Sin ciclo de vida de placas | nacen 8, mueren 0, nacen 0 | 🔴 Defecto de **modelo**, no de implementación |
| Sin dinámica (polos de Euler fijos) | nunca se escriben | 🔴 Defecto de **modelo** |
| Frontera inferida por conteo, no como objeto | — | 🟢 **Resuelto en parte (16-08-2026).** La clasificación física (convergente/divergente/transformante) ya lee de un segmento con ID persistente, no de la celda suelta. Lo que sigue infiriéndose por conteo/vecino-más-cercano es `PlateIDData` en sí -la propiedad-, que es F1B y no se ha tocado. Ver [A13](#a13-f1d--la-frontera-como-objeto-16-08-2026) |
| `BoundaryInteractions` desactivado | 693 líneas sin consumidor | ⏸️ Aparcado con motivo ([A9](#a9-los-dos-puntos-aparcados-de-f1)) |
| Copia de la tabla de bordes en el QuadTree | sin cobertura de test | ⏸️ Dormido hasta que se retome el QuadTree |

---

## A12. Riesgos vigentes

- **Coste `O(Res²)` por campo.** Cada fase añade campos que recorren las 6 caras, y el coste escala con el número de celdas (medido: ×16 de Res 32 a 96). Es el riesgo estructural del proyecto.
- **Calibración cruzada de parámetros.** `OrogenyFactor` y `DiffusionRate` forman un equilibrio; tocar uno obliga a revisar el otro. Lo mismo pasará con la erosión y el levantamiento.
- **Calibraciones apoyadas en la balanza de corteza.** El umbral de rift (`0,10 × MaxAngularSpeed`) se fijó igualando creación y destrucción **cuando esa balanza estaba rota** (0,04). Ahora que está en 0,98, **hay que rehacer esa calibración**: el valor actual no es de fiar.
- **Bordes del cubo.** Cada sistema nuevo con vecindad vuelve a pagarlo. Usar siempre la fuente única de mapeo y el vecino geométrico.
- **Nombres de test que no describen lo que miden.** Ha costado dos días una vez. Antes de creerse un test en rojo, leer su aserción.
- **Estabilidad numérica de las SWE** y **VRAM a resolución alta** — heredados del plan original, todavía sin tocar.
- **Renderizado de producto sin resolver.** El único camino verificado hoy es una malla procedimental sin LOD. El enfoque de F6 está diseñado pero no probado.

---

## A13. F1D — la frontera como objeto (16-08-2026)

### F1A, auditoría: la rotación está bien, la clase no

`ROADMAP.md` F1A decía *"`UPlateKinematics` cableada al bucle real"*. Verificado por código, no por lo escrito: es falso tal cual está redactado.

`Tectonics/PlateKinematics.cpp/.h` (441/252 líneas) es una clase completa — rotación, campo de velocidades, detección de colisiones, clasificación de frontera por ángulo (`ClassifyBoundaryType`, con umbrales de coseno — un intento anterior de lo que hoy es R2.13, con otro enfoque geométrico). Pero:

- `ATectonicsTestActor::Kinematics` se inicializa a `nullptr` (`TectonicsTestActor.cpp:284`) y **no hay un solo `NewObject<UPlateKinematics>()` en todo el proyecto.** Nunca se instancia.
- Ninguno de sus métodos de instancia se llama en producción — ni `ApplyPlateRotation`, ni `SimulationStep`, ni `DetectAllCollisions`.
- Lo único que sobrevive es la función **estática** `CalculatePlateRotation`, reutilizada directamente por `TectonicPlateSystem::Step()` (`:397`) y `RasterizedTectonics::AdvectPlateField` (`:671`) — ambos reimplementan en línea lo que `ApplyPlateRotation` ya hacía, en vez de llamar a la clase. El propio código lo admite: *"Hasta el 15-08-2026 esto solo envejecía las placas: `CalculatePlateRotation` existía, estaba testeada, y no la llamaba nadie fuera de los tests."*

**La física en sí es correcta** — fórmula de cuaternión estándar, composición de rotación acumulada en el orden correcto, umbral de advección atado a la resolución real, paso fijo genuinamente desacoplado del framerate. El problema es arquitectura muerta, no física rota.

**Por qué importa ahora:** `ClassifyBoundaryType` es, con otro método (ángulo contra normal geométrica de posiciones 3D reales, no gradiente de Sobel), la misma pregunta que resuelve R2.12 más abajo. No se reutilizó — toda su plomería (`BoundaryCells` cacheado una vez en `Initialize()` y nunca refrescado, `Plates` como copia propia desincronizada de `PlateSystem`) asume una frontera estática, que es justo lo contrario de cómo funciona el motor real. Pendiente: decidir si se borra o se cablea de verdad (`ROADMAP.md` F1A).

### R2.12: segmentos de frontera con ID persistente (16-08-2026)

**Tres intentos de clasificación por celda, documentados arriba y en el commit `705459d`**, todos con el mismo síntoma de fondo: el ruido de re-cuantización de una celda suelta podía decidir el régimen de todo un tramo de frontera.

1. Radial con signo + tangencial en valor absoluto → sesgo sistemático hacia "transformante".
2. Contacto dominante por magnitud, mismo eje de rejilla → mejor, seguía sesgado.
3. Normal real por gradiente de Sobel → clasificación correcta por fin, pero **destapó** que la acreción de arco satura sin control cuando la convergencia se mide bien (ver arriba, "Acreción de arco: restricción por placa").

**La pieza que faltaba no era una normal mejor — era dejar de decidir celda a celda.**

**Diseño.** `ExtractAndTrackBoundarySegments()`, llamada una vez por advección (justo después de `AdvectPlateField`, la misma cadencia — `PlateIDData` solo cambia ahí):

1. **Info por celda:** el mismo gradiente de Sobel de siempre, más un voto mayoritario a **4** vecinos (no 8) para fijar el par de placas — tiene que coincidir con la conectividad del flood-fill del paso 2. Radial y tangencial se guardan **con signo**, no en valor absoluto: es la corrección directa del primer intento fallido.
2. **Componentes conexas** por flood-fill de 4 vecinos, cruzando caras del cubo (`GetNeighborPixel`), agrupando celdas de frontera contiguas con el mismo par de placas.
3. **Identidad persistente por solape, no por posición ni índice** — ninguno de los dos es estable entre advecciones, porque el ráster se re-resuelve entero cada vez. Un componente nuevo hereda el `SegmentID` del segmento viejo con el que más celdas comparte, si el par de placas coincide; si no, nace un ID nuevo (contador monótono, nunca se reutiliza). `Age` se acumula mientras el ID persiste.
4. La clasificación de `Step()` (orogenia / rift / acreción / transformante) deja de recalcular nada: **lee** `AverageConvergence`/`AverageTangential` del segmento vía un mapa `SegmentID → índice`, construido una vez por `Step()`.

**Verificación por fases, sin mezclar geometría y física de golpe** (lección de los tres intentos anteriores): primero se construyó y verificó el objeto solo (contador de segmentos en el HUD, mismos resultados exactos en el subconjunto rápido — 45/45 idéntico), y solo con eso confirmado se cambió la clasificación para que leyera del segmento.

**Resultado, medido:**

| | Por celda (Sobel, `705459d`) | Por segmento |
|---|---|---|
| `ContinentsPersist` | 🔴 Rojo (68,3 %, salta la red de seguridad) | 🟢 **Verde** |
| `OrogenyBuildsMountains` | 7507 m | 7353 m (sigue sano, techo en 4000 m) |
| Señal 1 (colisiones) | 7795 | 7662 |
| Señal 2 (convergentes) | 227.701 | 165.803 |
| Señal 3 (transformantes) | 504.138 | 415.688 |

Promediar sobre el segmento entero cancela el ruido de celda suelta en vez de acumularlo — es lo que arregla `ContinentsPersist` sin tocar ningún umbral a mano. Confirma además que la hipótesis anterior ("hace falta F1E, ciclo de vida de placas") estaba equivocada: no hacía falta que un margen pudiera cerrarse, hacía falta que se clasificara bien.

**Lo que esto NO arregla, y hay que decirlo explícito.** El "peine" (escalonado de bordes, [más arriba](#escalonado-de-bordes-peine)) es un artefacto de **`PlateIDData`**, no de la física de frontera. Los segmentos se **construyen a partir de** `PlateIDData` ya resuelto por `AdvectPlateField` — lo leen, no lo cambian. La propiedad sigue decidiéndose exactamente igual que siempre (vecino más cercano sobre un campo categórico, re-cuantizado cada advección), así que el contorno visual de ID de placa sigue tan dentado como antes. Confirmado visualmente en el editor tras este commit: el peine seguía ahí. Lo que sí debería suavizarse es el crecimiento de relieve a lo largo de un margen (ya no lo decide una celda suelta mal clasificada), no la forma del propio contorno de placa.

**Lo que esto tampoco es todavía:** segmentos persistentes con historia rica (fusión, ciclo de vida propio) — la identidad de hoy es puramente "qué componente solapa más con cuál del paso anterior", suficiente para `Age` pero no para todo lo que R2.16 va a necesitar de un segmento (por ejemplo, longitud real para el tirón de la losa, R2.17).

---

## A14. R2.9 no basta: la propiedad necesita el mismo tratamiento que el material (17-08-2026)

### El arnés de depuración por capas

Para aislar el peine y la cinta de una vez, sin adivinar, se construyó un modo de prueba por capas en `RasterizedTectonics`, activable en caliente desde `TectonicsTestActor` (`T` capa 1, `Y` capa 2a), cada una construida sobre la anterior en vez de sustituirla:

- **Capa 1** (`DebugFakeRotateStep`): rotación geométrica pura. Cada celda se resuelve retro-rotando contra el Voronoi original congelado (`OriginalPlateIDSnapshot`), usando solo `CubeFaceMapping` — sin `GetNeighborPixel`, sin conteo de reclamantes, sin recuperación por tolerancia. Resultado: geometría limpia, sin bloques de esquina. Confirma que `CubeFaceMapping` en sí no es la fuente de nada de lo que sigue.
- **Capa 2a**: `AdvectPlateField` real, pero sin física de frontera ni isostasia — corta justo después del bucle de advección en `Step()`. Aísla el mecanismo de transporte (conteo, resolución, colisión, rift) del resto de la maquinaria.

Detalle de implementación que costó una vuelta: `WriteBackToPlateFrames()` en producción solo corre **una vez por `Step()`**, al final. La Capa 2a, al cortar antes de llegar ahí, dejaba el marco de material congelado desde que se activaba el modo mientras `PlateAccumRotation` seguía creciendo — un fantasma diagonal que no era el bug buscado, sino un artefacto del propio arnés. Se corrigió llamando `WriteBackToPlateFrames()` tras cada `AdvectPlateField()` **dentro** de la Capa 2a (no así en producción, ver "lo que queda pendiente" más abajo).

### R2.9 Fase 3: las dos pasadas de `AdvectPlateField` no estaban de acuerdo

Con la Capa 2a limpia de ese fantasma, seguía apareciendo un punteado que alternaba de placa a lo largo de fronteras diagonales — visible en el campo de ID de placa, no solo en material (lo que ya descartaba que fuera la cinta de A10/R2.10, que por diseño nunca tocaba la propiedad).

Causa encontrada por lectura de código, confirmada después con el HUD: `AdvectPlateField` tiene dos pasadas.

1. **Conteo** (R2.9 Fase 2, 16-08): para cada celda, prueba cada placa con una búsqueda tolerante de 4 candidatos — el hueco de redondeo de ±0,5 celda documentado ese día. De ahí sale `ClaimCounts`.
2. **Resolución**: si `ClaimCounts` es 1, reutiliza ese resultado. Si es 0 o 2+, **volvía a calcular los reclamantes desde cero con el test ESTRICTO de una sola celda** (`FloorToInt`, sin tolerancia) — una rama que sobrevivía de un intento del 15-08, un día **antes** de que el test tolerante se volviera el principal en la pasada de conteo.

Consecuencia: el conteo podía decir "2 reclamantes" (colisión de verdad) y la resolución, con menos tolerancia, encontrar 0 para la misma celda — caía en la rama de rift/recuperación, la recuperación (que sí era tolerante) encontraba un ganador por cercanía, y la celda se resolvía como un movimiento limpio de una sola placa **en vez de la colisión que el conteo ya sabía que era**. El desempate por distancia sub-celda hacía que el ganador fuera alternando de placa según la fase, adveción tras adveción: el punteado.

**Arreglo:** una sola función (`TryClaimTolerant`) para las tres pasadas que la necesitan — conteo, resolución de frontera y recuperación. En cuanto la resolución dejó de recalcular con el test estricto, la "recuperación por tolerancia" (16-08) se volvió matemáticamente inalcanzable: si la resolución ya dice 0 reclamantes con la misma búsqueda tolerante que usaba la recuperación, repetirla no puede encontrar nada distinto. Se quitó el bloque entero en vez de dejarlo muerto. Medido en vivo (Capa 2a): "recuperados por tolerancia" cayó de 26.771 a 0; el punteado desapareció.

### El defecto de verdad: la placa lenta enmascara su propio rift

Con el punteado fuera, apareció uno peor, ya reportado por el usuario viéndolo correr en vivo: una península que **crece sin parar**, no se traslada como bloque rígido — "es como si el continente dejara una estela de sí mismo detrás". Con 0 recuperados y 0 congelados confirmados por el HUD, no podía ser ni el defecto de A10 ni el que se acababa de arreglar.

Mecanismo, por lectura de código: cuando una celda tiene exactamente 1 reclamante, se trata como movimiento limpio sin más comprobación — es la rama "normal", sin sospecha. Pero ese único reclamante puede ser **la propia placa lenta reclamándose a sí misma por puro redondeo**, no porque haya avanzado. Es la otra cara de `Simu.Tectonics.StuckCellsNearEulerPole` (test ya existente, ya en rojo antes de esta sesión): cerca del polo de Euler de una placa la velocidad cae por debajo de 0,5 píxel/advección, y el retrotrazado siempre redondea a la misma celda.

Eso solo explicaría una celda congelada, no una que crece. La parte que faltaba: si la placa vecina se **retira de verdad** (divergencia real — debería nacer corteza oceánica ahí), la comprobación de rift **nunca se ejecuta**, porque solo se dispara con `Claimants.Num() == 0`, y aquí siempre da 1 (el autorreclamo). Cada advección la placa rápida se retira un poco más, la celda que deja vacía no la disputa nadie más, y la placa lenta se la queda por descarte. No es tierra avanzando: es tierra a la que nunca se le pregunta si debería convertirse en océano nuevo.

### Por qué no es un caso especial más que parchear

Este es el mismo problema, en dos disfraces: advectar un campo **categórico** (quién es el dueño) sobre una rejilla fija, resolviéndolo contra sí mismo cada paso, pierde el movimiento sub-rejilla de forma **irreversible**. Es el motivo documentado de por el que ningún código de geodinámica computacional serio (CitcomS, ASPECT, Underworld, StGermain) guarda "propiedad" como estado primario en la rejilla: usan **partículas trazadoras** (marker-in-cell) que llevan su identidad en coordenadas continuas, transportadas con la solución exacta de su movimiento — para rotación rígida, eso es una rotación de cuaternión sin ningún error de integración, por pequeño que sea el paso. La rejilla deja de ser la verdad; es una fotografía que se renderiza preguntando a los marcadores dónde están ahora. GPlates, la referencia académica en reconstrucción de placas, va aún más lejos y prescinde de la rejilla: fronteras como polígonos vectoriales, recalculados por intersección — más correcto, pero exige un motor de topología que no encaja con el pipeline de rejilla cubo-esfera que ya existe aquí.

**Diseño elegido: marker-in-cell, extendiendo R2.10 a la propiedad en vez de inventar un mecanismo nuevo.** El material (`PlateMaterial[P]`, `PlateAccumRotation`) ya es exactamente esto — un marco por placa en coordenadas propias, transportado por rotación acumulada exacta, nunca degradado por remuestreos intermedios. `PlateTerritory[P]`: un ráster booleano por placa, mismo layout que `PlateMaterial[P]`, que responde "¿es mío este punto?" contra el marco propio (rotación **acumulada**, no incremental — este es el cambio de fondo respecto a R2.9 tal como está escrito hoy). La diferencia con el material: el territorio **no** se reescribe por completo cada advección (eso reintroduciría exactamente el mismo problema, solo que disfrazado de "write-back"). Se actualiza con escrituras **puntuales, solo en el instante del evento tectónico** que cambia la propiedad de una celda (rift, colisión) — nunca por remuestreo masivo.

Consecuencia práctica, no solo de corrección: la inmensa mayoría de las celdas (interior de placa, lejos de cualquier frontera — R2.12 ya mide que la banda de frontera es 1–3 % del planeta) deja de necesitar la búsqueda cara contra `NumPlates` candidatos cada advección. Les basta comprobar si siguen dentro de su propio territorio, ya rotado exactamente. Debería ser más barato computacionalmente, no solo más correcto.

**Riesgo de concurrencia a vigilar en la implementación:** `PlateTerritory` se actualiza desde dentro del `ParallelFor` por cara de `AdvectPlateField`; dos caras del mundo distintas pueden mapear al mismo índice del marco de una placa. Los eventos de territorio se recogen en listas locales por hilo y se aplican en una pasada secuencial después del `ParallelFor`, no en el propio bucle paralelo.

**Lo que queda pendiente, anotado para no perderlo:** `WriteBackToPlateFrames()` (material) sigue corriendo una sola vez por `Step()` en producción, no una vez por advección como se forzó en la Capa 2a — si en un `Step()` caben varias advecciones seguidas (`TimeScale` alto), el material puede leer de un marco desincronizado durante esas advecciones intermedias. Acotado, no catastrófico, pero es la misma familia de defecto. Candidato a revisar aparte una vez esto esté verificado.

### Verificación por test, y recalibración de dos umbrales (17-08-2026)

Corridos los 20 tests de `Simu.Tectonics.*`: **17 verdes**. `StuckCellsNearEulerPole` — el test que existía específicamente para no pasar hasta que esto se arreglara de verdad — reporta **0 atascadas**. `NoPermanentlyStuckCells` y `OceanicRibbon` (la prueba directa de la cinta) también verdes.

Dos rojos no eran regresión sino umbral desactualizado: `FrozenCellsAtProductionRes` (0,2777 % sin resolver, umbral 0,2 %) y la aserción "congelada" de `LongRunStability` (0,4826 %, umbral 0,1 %). Motivo: esos umbrales se calibraron cuando el autorreclamo de una placa lenta enmascaraba fronteras transformantes como "1 reclamante, movimiento limpio" — esas celdas nunca llegaban a la rama de "sin resolver", así que el conteo de entonces estaba artificialmente bajo. Con el territorio exacto, esas celdas llegan ahí de verdad y las que no son rift caen honestamente en el residuo. No es que hoy se resuelva peor: es la primera vez que se cuenta bien.

Línea base remedida dos veces con el código de hoy, idéntica ambas veces (determinista, misma semilla): 0,2777 % y 0,4826 %. Umbrales recalibrados con el mismo criterio que ya usa "islas de corteza vieja" en este archivo (margen ~1,7-1,8× sobre la línea base medida, para detectar un empeoramiento claro, no para rozarla): `FrozenCellsAtProductionRes` a 0,5 %, `LongRunStability` a 0,8 %. Ambos verdes tras el cambio. La otra aserción de `LongRunStability` (colapso de tierra emergida, 9,5 % desde 24,5 %) se deja tal cual — es el defecto ya documentado en ROADMAP F1H, sin relación con esto, pendiente de F1E/F1F.

---

## F1F. Balance de pares por placa (17-08-2026)

### El marco: torque-balance, no F=ma

A escala de placa el número de Reynolds es tan bajo que la inercia no pinta nada — es flujo de Stokes. Forsyth & Uyeda 1975 ("On the relative importance of the driving forces of plate motion") es la referencia académica: la velocidad de una placa en cada instante es la que hace **cero el par neto**, no el resultado de integrar una aceleración. Eso convierte el problema en un balance algebraico:

```
Σ τ_i = 0  →  ω = (τ_tirón_losa + τ_empuje_dorsal) / (DragCoefficient × área_placa)
```

R2.12 ya discretiza la frontera en segmentos — exactamente lo que este tipo de modelo necesita —, así que F1F suma sobre `BoundarySegments` en vez de reinventar la discretización, y reutiliza la misma clasificación convergente/divergente/transformante que ya usa `Step()`.

### Fase A: solo diagnóstico

`ComputePlateDrivingTorques()` calcula, por placa, el par de empuje de dorsal (segmentos divergentes, fuerza constante por unidad de longitud, aplicada a ambos lados) y el par de tirón de losa (segmentos convergentes, el lado oceánico subduce — entre dos oceánicas, la más vieja — magnitud ∝ longitud × √edad, misma ley que ya usa la batimetría por hundimiento térmico de F2, no una constante inventada aparte). Extendido `FBoundarySegment` con `AveragePosition` (brazo de palanca), y tipo de corteza/edad medios por lado (`CrustTypePlateA/B`, `AverageAgePlateA/B`), calculados en la misma Pasada 1/2 donde ya se acumulan convergencia y tangencial.

Verificación (sin tocar `AngularVelocity` todavía): Placa 6 (1 de 4 segmentos convergentes con ella subduciendo) → tirón de losa no nulo. Placa 7 (4 tocan, 0 subduce ella — todas continente-contra-continente) → tirón de losa exactamente 0, y sigue con empuje de dorsal no nulo donde tiene frontera divergente. El cero se explica solo con los contadores `ConvergentSegmentsTouching/Subducting`, no hay que adivinar si es un fallo de cálculo.

### Fase B: cerrar el balance, con interruptor

`UpdatePlateKinematicsFromTorqueBalance()`, interruptor `bUseDynamicKinematics` (tecla `D`, apagado por defecto — misma filosofía que las Capas 1/2a: comparar contra la cinemática fija de siempre sin perder la vuelta atrás). Escribe `EulerPole`/`AngularVelocity` vía `PlateSystem->RestorePlateState()` — no había setter directo, pero `RestorePlateState` ya existía para el propósito de M5 y sirve igual aquí, copiando la placa entera para no tocar `Centroid`/`Age`/`CellCount` por accidente.

**Primeros dos puntos medidos (sin ajustar nada aún):** Placa 6 (empuje 7,68e13, tirón 4,4e14) → 0,0037 rad/Ma → **2,36 cm/año**. Placa 2 (más subducción: 3 de 5 segmentos) → 0,0071 rad/Ma → **4,52 cm/año**, más rápida que la anterior — coincide con la dirección que predice Forsyth-Uyeda (más margen convergente, más tirón, más velocidad) y las dos caen dentro de R2.5 (1-15 cm/año) con `DragCoefficient = 1500` a la primera.

**Bucle sin amortiguar, encontrado por el usuario viéndolo correr.** La primera versión escribía la velocidad recién calculada de golpe, cada advección. Sin previo aviso: velocidad mueve la placa → mueve la placa cambia qué segmentos tocan y cómo se clasifican → la clasificación decide la velocidad de la próxima advección. Síntoma medido: el número de segmentos donde una placa subduce saltaba (2→3→6→5→2→3) en pocos segundos — mucho más rápido de lo que una frontera real se reorganiza. Misma lección que `AccumulateMs` (Coste en el HUD, 15-08-2026): un valor instantáneo y ruidoso no se aplica tal cual, se suaviza. Arreglo: media móvil sobre el **vector** de rotación completo (eje y magnitud juntos — mezclarlos por separado no da el mismo resultado), `KinematicsSmoothingAlpha`. Calibrado por prueba directa, no a ojo de una sola vez: 0,15 seguía oscilando (1↔2↔3 cada ~1s), 0,03 mejor pero seguía (1↔2 cada 3-4s), 0,01 estable. Un parpadeo ocasional de ±1 segmento en el límite exacto de clasificación se acepta como residual, misma familia que el trilema de R2.9 Fase 4 — no todo ruido de un sistema discreto es un bug que haya que perseguir hasta cero.

**Clamp duro además de la calibración.** `R2.5` pide 1-15 cm/año; con `DragCoefficient` ya afinado para los dos casos medidos, nada garantiza que otra combinación de fuerzas no se salga del rango. Se acota `AngularVelocity` a `[MinAngularSpeed, MaxAngularSpeed]` derivados de esas dos cifras vía `v = ω·R`, no solo se confía en la calibración estadística.

**Pendiente:** verificación larga en el editor (regla de cierre de fase) — si `LongRunStability` mejora y si las bandas de isócronas (estrías, más arriba en este documento) se vuelven menos regulares al dejar que las fronteras se reorganicen solas, en vez de girar fijas para siempre. R2.18 (campo de esfuerzo del manto, reorganización cada ~100-200 Ma) queda fuera de esta fase, es un paso posterior.

---

## F1E Fase A: nacimiento por fragmentación (17-08-2026)

### Por qué ahora, y por qué antes que terminar de afinar F1F

Corrida larga de validación de F1F (cinemática dinámica activada, `bUseDynamicKinematics`): tierra emergida cayendo de 24,5 % a 8,6 % en ~382 Ma — **peor** que la línea base con cinemática fija, no mejor. Mecanismo, no casualidad: el tirón de losa de una placa es proporcional a la longitud de su margen convergente; una placa que ya está "ganando" (comiéndose vecinas) tiene más margen convergente, luego más tirón, luego avanza más rápido, luego gana todavía más margen en la próxima advección. Es un monopolio sin freno — ninguna de las Capas 1/2a ni la cinemática fija de siempre lo mostraban, porque ninguna cierra el bucle velocidad→geometría→velocidad que F1F sí cierra.

En la Tierra real el freno no es geométrico, es de ciclo de vida: cuando una placa avanza por en medio de otra, el trozo que queda al otro lado deja de compartir cinemática con el resto — pasa a ser una placa nueva, con su propio balance de pares. Sin eso, F1F no tiene con qué competir contra su propio monopolio. De ahí que F1E (ROADMAP 1E, hasta ahora sin empezar) pase delante de terminar de calibrar F1F en vez de después.

### Mecanismo: componentes conexas sobre `PlateIDData` completo, no solo la banda de frontera

`HandlePlateFragmentation()` recorre las 6 caras **enteras** (no solo el 1-3 % que ya mide R2.12 como banda de frontera — aquí hace falta el interior también, porque un trozo separado puede estar lejos de cualquier frontera activa) con el mismo flood-fill de 4 vecinos cruzando caras que ya usa `ExtractAndTrackBoundarySegments` (`GetNeighborPixel`, pila explícita en vez de recursión). Cada celda visitada se agrupa por `PlateIDData` igual; el resultado es, por cada `PlateID` que aparece en el mapa, una o más componentes conexas disjuntas.

Si una placa resuelve en **una sola** componente, no pasa nada — es el caso normal. Si resuelve en **2 o más**, la mayor conserva el `PlateID` y la cinemática tal cual (no se toca `EulerPole`/`AngularVelocity` del padre); cada componente menor nace como placa nueva vía `TectonicPlateSystem::AddPlate()` (método nuevo — `RestorePlateState()` solo sobreescribe índices que ya existen, no crece el array). La placa nueva hereda tipo de corteza, densidad, grosor y cinemática del padre en el instante del nacimiento: no es un valor arbitrario, es la mejor aproximación disponible, y F1F la reafina sola en la próxima advección si la cinemática dinámica está activa. Lo que **no** hereda todavía es un polo de Euler propio derivado de la geometría del rift (ROADMAP 1E, sigue pendiente) — hasta que eso exista, una placa recién nacida se mueve exactamente como su padre hasta que F1F la reafina.

### Sembrado del marco: el mismo problema que R2.9 Fase 4, resuelto igual

Una placa nueva sin `PlateTerritory`/`PlateMaterial`/`PlateAccumRotation` propios sería exactamente el bug de A14 otra vez: `AdvectPlateField` la trataría como si nunca hubiera rotado. `EnsurePlateFrameCapacity()` crece los tres arrays hasta cubrir el índice nuevo; `PlateAccumRotation[NewID]` se copia **del padre**, no de identidad — en el instante del nacimiento el marco de la placa nueva coincide exactamente con el del padre, misma rotación acumulada, así que copiar es lo correcto, no una aproximación. Con esa rotación ya fijada, cada celda del componente se mapea a su índice de marco (`GetFrameIndex`, misma fórmula que `ReadPlateMaterial`) y se escribe `PlateTerritory[NewID]` y `PlateMaterial[NewID]` (edad, tipo, grosor, elevación tal como estaban en el mundo) en esa única pasada — no hace falta un remuestreo aparte.

El límite documentado en `TectonicTypes.h` (`PlateID` es `uint8`, "0-255 para caber en textura R8") se respeta con un tope duro de 250 placas: por encima, `HandlePlateFragmentation()` no crea más y deja el fragmento sobrante con el `PlateID` del padre (incorrecto — el fragmento no se separa de verdad — pero preferible a que un ID nuevo envuelva `uint8` y corrompa una placa existente por colisión de índice).

### Dónde se llama, y dónde no

En `Step()`, entre `AdvectPlateField()` y `ExtractAndTrackBoundarySegments()` — así los segmentos del mismo paso ya ven la partición, no la del paso anterior. No corre en la Capa 2a de depuración (`bDebugAdvectionOnly`): esa capa existe para aislar el transporte puro, y el ciclo de vida de placas es justo lo contrario de "nada más que transporte".

### Regresión de coste (~4000 ms/paso) en la primera versión, y arreglo

La primera versión guardaba, por cada componente conexa, la lista completa de sus celdas (`TArray<TPair<FaceIdx, Idx>>`) — incluida la componente única y gigante de cualquier placa que **no** se ha fragmentado, que es el caso normal en casi todas las advecciones. Para un planeta entero (6×Res×Res celdas) eso es construir y hacer crecer, en el hilo de juego y sin `ParallelFor`, un array del tamaño del planeta **en cada advección**, cuando una placa se fragmenta muy pocas veces en toda una corrida. Reportado por el usuario tras probarlo: el coste por paso subió a ~4000 ms.

Arreglo, sin cambiar el resultado: dos pasadas en vez de una. La pasada 1 recorre el planeta entero pero solo **etiqueta** cada celda con el índice de su componente (`TArray<int32> Label`, un entero por celda, sin reserva dinámica por celda) y **cuenta** cuántas celdas tiene cada componente — nunca guarda coordenadas. Con eso ya alcanza para decidir, por placa, si hay 2+ componentes y cuál es la mayor. La pasada 2 —la única que reescribe `PlateIDData` y siembra territorio/material, celda a celda— solo se ejecuta si `bAnyFragmentation` es cierto, es decir, si de verdad nació alguna placa nueva en este paso. El caso común (nada fragmentado) sale con un único barrido barato de enteros, sin ningún `TArray` por componente creciendo celda a celda. Instrumentado con `StepTimings.FragmentationMs` (`frag` en el HUD de coste) para verificarlo en vivo, misma disciplina de medir-antes-de-tocar que el resto de este documento.

**Pendiente:** verificación larga en el editor — si esto de verdad frena el colapso de tierra emergida que F1F empeoró, o si solo lo retrasa; y confirmar que `frag` se queda en un coste bajo durante una corrida larga, no solo en el arranque. Muerte (placa por debajo de umbral de área) y sutura (placas co-movientes que se sueldan) quedan fuera de esta fase a propósito: muerte es de prioridad baja porque una placa moribunda hoy es inerte, no dañina; sutura activamente **podría empeorar** el monopolio (menos placas compitiendo por margen convergente), así que no debe entrar en el mismo paso que el freno que se está probando.

---

## Degradado en el campo "ID de placa": vértices compartidos, no la simulación (17-08-2026)

Tras el arreglo de coste de F1E, el usuario reinició la simulación (8 placas, Grid 128) y pidió confirmar si un degradado suave y borroso entre dos placas en el campo "ID de placa (categórico)" era normal. No lo era, y no tenía relación con F1E, F1F ni con nada de `RasterizedTectonics` — es un bug de renderizado que ya existía antes de esta sesión.

**Diagnóstico:** el muestreo del campo en sí ya era correcto — `UPlanetFieldRegistry::SampleActiveBilinear` (`PlanetFieldRegistry.cpp`) ya trata los campos con `Palette == Categorical` con vecino más cercano, con un comentario explícito sobre por qué (mezclar el ID 3 con el 7 da el 5, que es una placa distinta y no está ahí). El problema estaba un paso más allá, en la malla: `CreatePlanetMesh()`/`UpdateMeshColors()` (`TectonicsTestActor.cpp`) construían una rejilla de `(Resolution+1)²` vértices **compartidos** por cara — cada vértice, con un solo color, servido hasta a 4 celdas vecinas. El rasterizador interpola ese color entre los 3 vértices de cada triángulo de todas formas: un triángulo con una esquina en la placa 2 y otra en la placa 4 pinta un degradado suave entre medias, con un tono intermedio que no representa ninguna placa real. Clásico problema de pintar dato categórico con color de vértice interpolado — el propio muestreo "a prueba de mezcla" no evita nada si el vértice donde vive ese resultado se comparte con otra celda de valor distinto.

**Arreglo:** cada celda de la malla pasa a tener sus 4 vértices propios, sin compartir con la celda vecina (`SampleMeshCell()`, nueva función privada usada por `CreatePlanetMesh()` y `UpdateMeshColors()`). Con vértices propios, un campo categórico puede pintar los 4 iguales — muestreados una única vez en el **centro** de la celda, no en una esquina, para no depender de qué celda vecina "gana" un punto que geométricamente comparten — y sale plano de verdad, sin degradado posible porque los 3 vértices de cada triángulo ya llevan el mismo valor. Un campo continuo (elevación, temperatura...) sigue muestreando cada esquina por su cuenta, igual que antes: la costura entre celdas vecinas sigue siendo invisible porque ambas evalúan la misma fórmula en la misma posición del mundo, así que ese aspecto suave no cambia. La posición del vértice (y por tanto la elevación del terreno) se sigue muestreando siempre por esquina, categórico o no — solo el color se aplana, no la forma. El resaltado de placa seleccionada (`HighlightedPlate`) se benefició del mismo arreglo de paso: antes también sufría un blanco/atenuado degradado en el borde de la placa resaltada, por la misma causa raíz.

Coste: ~4× más vértices por cara (4·Resolución² en vez de (Resolución+1)²) al no compartir esquinas. A Resolución=128 (el valor por defecto de `GridResolution`) es un aumento irrelevante para una malla de depuración; no se ha probado a resoluciones de malla mucho mayores.

---

## F1E Fase A.1: asimilación de islas huérfanas (17-08-2026)

Con el degradado de renderizado ya descartado (ver sección anterior), el usuario zoomeó lo bastante cerca como para confirmar que lo que quedaba no era ningún degradado — eran islas pequeñas, perfectamente planas y nítidas, de una placa dentro del territorio de otra. Y, en una corrida larga, esas islas **crecían** en vez de quedarse quietas o desaparecer.

**Mecanismo.** Esto es el mismo trilema de R2.9 Fase 4 (ver A14 más arriba): una celda que no queda reclamada limpiamente por nadie durante la advección, y que tampoco cumple las condiciones de rift, **conserva el dueño anterior** en vez de que se le asigne uno nuevo. Cuando la placa B avanza sobre territorio de la placa A en una colisión, algunas celdas de A pueden quedar rodeadas por B sin que la resolución de esa advección las reclame para B — nace una isla huérfana de A, todavía advectada con la cinemática de A (que puede estar al otro lado del planeta, moviéndose en una dirección completamente distinta), lo que a su vez genera más celdas sin resolver en el nuevo borde de la isla cada vez que A y B seudo-comparten un límite que no es real. De ahí el crecimiento: no es que la isla "gane terreno" de verdad, es que arrastra su propio ruido consigo mientras avanza con la cinemática equivocada.

**Arreglo: asimilación, no solo fragmentación.** `HandlePlateFragmentation()` (F1E Fase A) ya hacía el flood-fill de componentes conexas necesario para detectar estas islas — solo le faltaba decidir qué hacer con las que no llegan al umbral de "placa nueva" (`MinFragmentCells = 800`). Ahora, durante el mismo flood-fill, se registra con qué `PlateID` linda cada componente en su perímetro (`ComponentBorderPlateID`/`ComponentBorderAmbiguous`) — gratis, porque el flood-fill ya visita cada celda vecina de otra placa para descartarla, solo hacía falta anotar cuál es antes de descartarla. Por construcción del propio flood-fill, dos celdas vecinas con el mismo `PlateID` caen siempre en la misma componente, así que cualquier vecino fuera de la componente actual pertenece necesariamente a una placa distinta — no hay ambigüedad posible sobre "es de la misma placa o no".

Con eso, una componente pequeña (por debajo del umbral de placa nueva) se resuelve así:
- Si linda con una **única** placa vecina en todo su perímetro: esa vecina la asimila — se reasigna `PlateIDData` y se siembra `PlateTerritory`/`PlateMaterial` de esa celda en el marco de la vecina, exactamente igual que al sembrar una placa nueva (mismo bloque de la Pasada 2, sin cambios), salvo que aquí el destino **ya existe** — no se llama `AddPlate` ni se toca `PlateAccumRotation` de la vecina, que ya tiene la suya.
- Si linda con **dos o más** placas distintas (o con ninguna resuelta): queda ambigua y se deja tal cual, mismo criterio que el resto del trilema — no se inventa una regla de desempate nueva.

Esto no elimina el trilema en sí (sigue habiendo celdas genuinamente ambiguas, y el "candidato a cinta" del HUD seguirá contando algo), pero corta el mecanismo de crecimiento: una isla huérfana rodeada por una sola vecina deja de existir como entidad separada en la siguiente advección, en vez de arrastrar su propio ruido indefinidamente con la cinemática de un dueño que ya no la representa.

**Pendiente:** verificar en una corrida larga que el conteo de "CONGELADOS" del HUD deja de crecer sin límite (o crece mucho más despacio) con este arreglo puesto.

---

## Costura transformante: partición completada por vecino más cercano (18-08-2026)

### El hilo completo, desde una captura de pantalla hasta el núcleo de la advección

Empezó como una duda sobre una imagen: un degradado suave entre dos placas en el campo "ID de placa (categórico)", que no debería existir en un dato categórico. La investigación fue en capas, cada una descartada con datos antes de pasar a la siguiente — se documenta entera porque cada paso descartado es información real, no ruido:

1. **Hipótesis: interpolación de color de vértice.** Confirmada y arreglada — `CreatePlanetMesh()`/`UpdateMeshColors()` compartían vértices entre celdas vecinas, y el rasterizador interpolaba el color entre placas distintas en cada triángulo de frontera. Arreglo: vértices propios por celda (`SampleMeshCell()`), con color plano en campos categóricos. Ver la sección "Degradado en el campo ID de placa" más arriba.
2. **Con el renderizado arreglado, seguía habiendo una neblina.** Zoom del usuario reveló que eran islas pequeñas y nítidas, no un degradado — el "trilema" de R2.9 Fase 4 dejando huérfanas celdas de una placa dentro de otra tras una colisión.
3. **F1E Fase A.1: asimilación de islas.** Implementada para que la placa vecina absorbiera las islas huérfanas por debajo del umbral de "placa nueva". Funcionó para lo que se diseñó (miles de islas asimiladas, confirmado por log), pero una corrida larga mostró que el número de segmentos de frontera de R2.12 seguía subiendo sin parar (44 → 107 con las mismas 8 placas) y el campo de residuo se veía cada vez más ruidoso.
4. **A/B test:** interruptor de depuración (tecla `I`) para desactivar solo la asimilación y comparar. Resultado: **igual de mal, o peor, sin asimilación** (205 segmentos, 829 sin resolver por advección, frente a 107/663 con ella activada). La asimilación quedó exonerada — el problema estaba más abajo, en `AdvectPlateField` en sí.
5. **Instrumentación del propio mecanismo de resolución**, sin tocar su lógica: cuántas celdas distintas han fallado alguna vez (`GetFrozenCellDiagnostics`, que resultó ser una métrica engañosa porque solo puede crecer — ver más abajo), cuántas fallan en la advección MÁS RECIENTE (`LastUnresolvedCells`, la señal buena), y un desglose de esas últimas entre "converge" (vecinos que se acercan, sospechoso de fallo real) y "cerca de cero" (sin movimiento radial claro, candidato a frontera transformante).
6. **Resultado decisivo:** de 496 celdas sin resolver en una advección, **477 (96%) eran "cerca de cero"**, solo 19 (4%) parecían convergencia mal contada. Confirmado: el residuo es, de forma abrumadora, fronteras transformantes — exactamente la laguna que `ROADMAP.md` F1D ya documentaba desde antes de esta sesión ("Fallas transformantes: fricción y sismicidad... hoy solo se excluyen de crecer/decrecer corteza, no hay fricción ni sismicidad modeladas"). El propio comentario de la comprobación de rift en `AdvectPlateField` ya distinguía "un rift de una frontera TRANSFORMANTE, que también deja huecos al discretizar pero no crea corteza" — el diseño original ya sabía que el caso existía, solo nunca le dio una resolución de verdad.

### Por qué pasa: dos particiones independientes no encajan en la costura

`PlateTerritory[P]` (R2.9 Fase 4) es exactamente correcto para lo que se diseñó: cada placa lleva su propio territorio, transportado por su propia rotación acumulada, sin ningún error de integración. Pero cada placa lo lleva **con total independencia de sus vecinas** — nada en el diseño garantiza que el borde del territorio de la placa A y el borde del territorio de la placa B coincidan exactamente celda a celda. Para convergencia/divergencia esa ambigüedad no importa: hay un suceso físico real (colisión o rift) que decide qué pasa en el hueco o el solape. Para deslizamiento **tangencial** no hay tal suceso — la celda de la costura, mes tras advección, no es reclamada por ninguna de las dos búsquedas tolerantes (ninguna placa "avanza" hacia ella en sentido radial), tampoco diverge lo bastante para ser rift, así que caía en el tercer brazo del trilema: conservar el dueño anterior. Para siempre. Cada advección volvía a evaluarse la misma celda de costura y volvía a fallar, sin ningún mecanismo que la recompusiera — de ahí que los segmentos de R2.12 se fragmentaran con el tiempo en vez de quedarse estables.

### Arreglo: completar la partición por vecino más cercano (Voronoi discreto), no un parche

Cuando ninguna placa reclama la celda con la búsqueda estricta (`TryTerritoryTolerant`, radio de 1 celda con desempate) y no es un rift de verdad, en vez de resignarse a conservar el estado anterior, se prueba una **búsqueda ampliada** (`FindNearestOwnerWide`, radio 5×5) contra el territorio de **cada** placa, y gana quien tenga el territorio más cercano de verdad. Es el mismo principio con el que GPlates y el resto del software de reconstrucción de placas deciden a qué placa pertenece un punto cuando no hay un borde vectorial exacto que lo diga: una teselación de Voronoi discreta sobre los territorios ya existentes, no una regla inventada aparte. No crea ni destruye corteza — se hereda el material del ganador exactamente igual que un movimiento limpio normal (`AssignCleanMove`), coherente con que una costura transformante no genera ni consume corteza. Radio 5×5 elegido por diseño, no a ojo: un hueco por deslizamiento tangencial no puede ser mayor de ~1 celda, porque `MaxAdvectionDt` ya trocea la advección para que la placa más rápida recorra como mucho ~1 píxel por advección — 5×5 tiene margen de sobra sin alcanzar territorio de una placa no relacionada. Solo si ni siquiera la búsqueda ampliada encuentra una placa cercana se cae al último recurso de conservar el estado anterior — ahora un caso realmente raro, no la norma.

**Medido, antes y después, en la misma corrida (73 advecciones, Grid 128):**

| | Antes | Después |
|---|---|---|
| Sin resolver en la advección más reciente | 400-800 | **8** |
| Resueltas por vecino más cercano | (no existía) | 7.586 |
| Congelados acumulado | decenas de miles | 486 |
| Tierra emergida a 118 Ma | colapsando | 22,4% (estable) |

### Diagnóstico engañoso, corregido en el camino: "distintas" siempre crece

Antes de llegar a este arreglo se instrumentó `GetFrozenCellDiagnostics()` (cuántas celdas, en toda la historia de la simulación, han fallado alguna vez) esperando que se estabilizara cerca del número de celdas de frontera. No lo hizo — pasó de 7.539 a 17.490 mientras la frontera solo crecía de 8.738 a 9.373 celdas. Error de diseño reconocido y corregido en la propia sesión: esa métrica cuenta historia ("¿ha fallado esta celda alguna vez?"), y como las fronteras se desplazan por el planeta con el tiempo, barren celdas nuevas sin parar — **tiene que crecer siempre**, sin decir nada sobre si el problema actual empeora. La métrica útil, añadida después, es la que se sobreescribe cada advección (`LastUnresolvedCells`), no la acumulada.

### Verificación por test, y lo que reveló sobre un problema ya conocido

`Simu.Tectonics.*` (20 tests): **17 en verde**. Los 3 rojos:

- **`NoStraightCrustBridges`**: ya estaba en rojo desde antes de esta sesión (motas de una celda dentro de una misma placa), sin relación con costuras entre placas distintas. Sin cambios.
- **`ContinentsPersist`**: su propia aserción de convergencia ("el segundo tramo cambia menos que el primero") dejó de cumplirse — 17 celdas de cambio en el primer tramo de 150 pasos, 260 en el segundo, reproducible idéntico en dos corridas con semilla fija. El comentario del propio test ya lo predecía: *"el defecto de remuestreo sigue abierto y se aprieta cuando se arregle la advección"*. Recalibrada: en vez de exigir que el segundo tramo desacelere respecto al primero (una forma que ya no aplica, porque la subducción legítima ahora avanza a ritmo más constante en vez de frenar dentro de esta ventana de 300 pasos), se acota que ningún tramo consuma una fracción catastrófica del continente de partida — margen ~1,75× sobre el peor tramo medido (19,7% → umbral 35%).
- **`LongRunStability`**: su aserción de "sin resolver" pasa ahora con margen enorme (0,0004% del planeta, frente al umbral de 0,8%) — la prueba directa de que el arreglo funciona a escala de producción. Pero la aserción de tierra emergida **sigue en rojo, y peor que antes** (9,5% a 1000 Ma, frente al 13,2% ya documentado en ROADMAP F1H). Diagnóstico: la congelación de celdas que acabamos de arreglar estaba, sin querer, **protegiendo** corteza continental de la subducción — una celda congelada nunca cambia de dueño, así que nunca podía perderse en una colisión. Al arreglar la resolución de verdad esa protección accidental desaparece, y el colapso de tierra que ya sabíamos que existía se ve con más crudeza, no se crea de nuevo. Se deja el test en rojo tal cual, sin recalibrar: sigue vigilando un problema real y todavía sin resolver (la razón original de construir F1E), no algo que este arreglo debiera silenciar.

### Pendiente

- **F1D, friccion y sismicidad de verdad**: este arreglo resuelve la **partición** (a quién pertenece la celda) en costuras transformantes, no añade la física de deslizamiento en sí (fricción, esfuerzo acumulado, sismicidad) que `ROADMAP.md` F1D sigue teniendo pendiente. Son capas distintas: esta hacía falta primero para que hubiera un estado de partición estable sobre el que construir esa física después.
- El 4% de residuo "convergente" que la búsqueda ampliada tampoco explique del todo merece una mirada aparte si vuelve a aparecer en cantidad tras esta corrección — de momento, con la búsqueda ampliada puesta, es un puñado de celdas, no una categoría que valga la pena perseguir todavía.

---

## Validación F1E + F1F: ¿frena el monopolio? (18-08-2026)

La validación larga que quedaba pendiente desde que se construyó F1E ("¿de verdad contrarresta el monopolio de F1F?") nunca se había hecho con datos objetivos — solo "a ojo en el editor", y encima antes del arreglo de costura transformante de esta misma sesión. Se construyó `Simu.Tectonics.F1EF1FLongRun`: fixture de 8 placas, `bUseDynamicKinematics` activado, 1000 Ma con puntos de control cada 125 Ma, para ver la **forma** de la curva de tierra emergida, no solo el punto final.

**Primer intento, a Res 48 (la resolución habitual de esta familia de tests):** tierra 24,5% → 10,8%, con desaceleración clara, pero **`placas 8 -> 8` durante toda la corrida — F1E nunca fragmentó nada**. Causa: `MinFragmentCells = 800` se calibró contra Grid 128 de producción (~98.000 celdas totales); a Res 48 (~13.800 celdas totales) ese umbral es casi el 6% del **planeta entero**, no de una placa, así que el mecanismo de fragmentación de F1E queda efectivamente desactivado a esa escala. La mejora medida ahí es atribuible al arreglo de costura transformante, no a F1E.

**Repetido a Res 128 (producción de verdad, mismo test, `GridRes` y `RasterRes` a 128):**

| Ma | Tierra | Placas |
|---|---|---|
| 0 | 24,5% | 8 |
| 125 | 23,0% | 8 |
| 250 | 20,4% | 8 |
| 375 | 17,5% | 8 |
| 500 | 14,9% | 8 |
| 625 | 13,5% | 8 |
| 750 | 13,2% | **9** (F1E fragmenta aquí) |
| 875 | 12,6% | 9 |
| 1000 | 11,8% | 9 |

Esta vez F1E sí entra en juego (una fragmentación, 8→9, hacia los 750 Ma) — una intervención real pero modesta, no el mecanismo dominante. La forma de la curva es la misma que a Res 48: caída de ~2,6-2,9 puntos por cada 125 Ma al principio, bajando a ~0,3-0,8 puntos por cada 125 Ma al final — se asienta en torno al 12-14%, sin indicios de seguir cayendo hacia el colapso total que se medía antes (24,5% → 8,6% en solo 382 Ma, y sin frenar).

**Conclusión, sin adornarla:** el planeta ya no se ahoga sin fondo — encuentra un equilibrio bajo en vez de colapsar. No es "problema resuelto" (12-14% sigue muy por debajo del ~41% real de la Tierra, y la mayor parte de la mejora medida parece venir del arreglo de costura transformante, con F1E aportando un frenado adicional ocasional, no el protagonista que se esperaba). Es la primera corrida larga con F1F dinámico que se estabiliza en vez de desbocarse, y ahora hay un test permanente (`Simu.Tectonics.F1EF1FLongRun`) para vigilar que se mantenga así.

### Pendiente

- **F1E solo fragmentó una vez en 1000 Ma** a resolución de producción — o el monopolio de F1F no es tan agresivo como se temía con la base ya arreglada, o el umbral de 800 celdas sigue siendo conservador. No hay datos todavía para saber cuál.
- Muerte y sutura de placas (F1E, ver ROADMAP 1E) siguen sin implementar; con solo fragmentación+asimilación puestas, no hay manera de que dos placas que dejen de moverse relativamente se vuelvan a fundir, ni de que una placa reducida a casi nada desaparezca.

### Por qué el equilibrio es tan bajo: umbral de rift probado y descartado (18-08-2026)

Instrumentado `Simu.Tectonics.F1EF1FLongRun` para loguear, por tramo de 125 Ma, el delta de celdas creadas (rift) y destruidas (subducción) además de la tierra emergida. Resultado con el umbral de rift original (`AvgDivergence > 0.10 * MaxAngularSpeed`): el ratio creado/destruido se mantiene **estable en 0,42-0,50 durante los 1000 Ma enteros** — no empeora con el tiempo (descartando un transitorio que se fuera a corregir solo), pero tampoco se acerca nunca a 1,0. Un desequilibrio persistente de ~2:1, sostenido.

**Hipótesis probada:** el umbral de rift (`ROADMAP.md` F1D lo señalaba como "no fiable, pendiente de recalibrar ahora que la balanza de corteza es sana") estaba creando menos océano del que le tocaba. Se bajó a la mitad (0,05) y se remidió con el mismo test.

**Resultado: descartada.** El ratio apenas se movió (0,46-0,49 con el umbral nuevo, prácticamente el mismo rango que el 0,42-0,50 original) — bajar el umbral a la mitad no acercó la balanza a 1,0 de forma apreciable. Y de propina, la tierra emergida salió *peor* (9,7% frente a 11,8% a 1000 Ma), con una explosión de fragmentaciones F1E en el último tramo (8→15 placas en 125 Ma) y el ratio de ese tramo cayendo a 0,29 — probablemente un umbral más laxo generó más fronteras nuevas y activas de golpe, alimentando más colisiones, no menos. Revertido al valor original (0,10).

**Conclusión:** el umbral de rift no es la palanca dominante del desequilibrio creación/destrucción. Sospecha planteada aquí y descartada más abajo por datos: hay una **asimetría estructural**, no solo numérica, entre los dos mecanismos — una colisión con 3 o más reclamantes destruye varias celdas de un plumazo (todas menos la ganadora), mientras que el rift solo puede crear una celda nueva por cada celda individual que cumple el umbral de divergencia. Medido después (ver más abajo, "destruidas/colisión"): 0,82-0,99, **por debajo** de 1 — la amplificación por colisión múltiple tampoco es la causa.

---

## Árbitro único de vecino más cercano: arreglo de raíz de la asimetría (18-08-2026)

### El diagnóstico, en términos académicos

A petición explícita: *"cómo lo haría una universidad que estuviese haciendo este software"*. La respuesta no es geodinámica computacional (CitcomS, ASPECT) — ahí no existe "placa dueña de una celda", solo un campo continuo advectado por una única velocidad, así que la ambigüedad de partición no puede aparecer. El paralelo correcto es el software de reconstrucción de placas (**GPlates**): las placas son cuerpos rígidos con frontera, igual que aquí, y la partición del planeta se decide por un **único árbitro** (punto-en-polígono contra el polígono reconstruido) — nunca preguntando a cada placa por separado "¿la reclamas tú?" y contando cuántas dicen que sí.

El diseño anterior (`Claimants.Num()`, heredado de R2.9) hacía exactamente eso: N pruebas independientes, una por placa, cada una con su propia búsqueda tolerante aislada de las demás. Reclamar una celda es una condición **OR** (basta que una de las N diga que sí). No reclamarla es una condición **AND** (tienen que fallar las N a la vez). Con territorios independientes y algo de margen de tolerancia en cada uno, los solapes (colisión) son mecánicamente más fáciles de producir que los huecos limpios (candidato a rift) — no por ningún umbral mal puesto, por la propia forma del mecanismo de decisión.

**Medido** (`Simu.Tectonics.F1EF1FLongRun`, con la costura transformante ya arreglada): el ratio de celdas con 0 reclamantes frente a celdas con 2+ crecía sin parar, de 1,1 a 4,5 en 1000 Ma, mientras el ratio de corteza creada/destruida se quedaba plano en ~0,45 — la asimetría no se corrige sola con el tiempo, es del propio diseño.

### El arreglo

`AdvectPlateField` sustituye el conteo de reclamantes por un único árbitro de distancia (`FindNearestOwnerWide`, ya existente desde el arreglo de costura transformante, reutilizado aquí como mecanismo central en vez de solo fallback): para cada celda de frontera, se mide la distancia real de **cada** placa a su territorio más cercano, y se ordena. El ganador (`Candidates[0]`) es, por construcción, único — no puede haber ambigüedad sobre "quién es el más cercano". Colisión, traspaso limpio y rift se **derivan** de comparar esa distancia mínima contra la segunda más cercana con una **única vara de medir** (`ContestRadiusSq`), en vez de contarse por separado con pruebas de exigencia distinta:

- Nadie dentro del radio de contienda → candidato a hueco: se comprueba divergencia real (mismo test de siempre) → rift, o traspaso al más cercano de todos si no diverge.
- Solo uno dentro del radio → dueño claro, traspaso limpio (equivalente al viejo "1 reclamante").
- Dos o más dentro del radio → contienda de verdad → colisión, con la misma selección de ganador por tipo/edad/elevación que ya existía, sin tocar esa lógica.

### Calibración de `ContestRadiusSq`, y por qué no fue trivial

Primer valor, 2,5 (pensado como el alcance peor-caso de la vieja búsqueda de 4 candidatos): las colisiones **subieron** por encima del sistema viejo (56.207 vs 40.405 en el primer tramo) y el ratio empeoró (0,21-0,30). Segundo valor, 1,0 (más cerca de "un solo vecino inmediato"): el ratio mejoró de verdad (0,48-0,60, el mejor medido) pero la tierra emergida salió **peor** (8,0% a 1000 Ma, por debajo de la línea de seguridad de 8,6% — el test falló). Sumando los totales: con radio 1,0 la pérdida neta de continente era *menor* que con el sistema viejo (81.625 vs 103.904 celdas) pero el resultado final era peor — el ratio bruto de creación/destrucción no explicaba por sí solo el resultado. Quedó ahí, sin resolver del todo, mientras se perseguía la causa real (ver la sección siguiente). Valor final: **1,0**, mantenido porque el ratio creado/destruido es el que mejor se comporta y porque el verdadero sumidero resultó ser otra cosa (ver más abajo) — no el radio de contienda.

---

## La corteza continental que desaparecía sin pasar por rift ni colisión (18-08-2026)

### El hilo de la investigación, con cada hipótesis puesta a prueba y descartada por datos

Con el árbitro nuevo puesto, la tierra emergida seguía cayendo con fuerza (24,5%→8-15% según el radio, en 1000 Ma). Antes de tocar más umbrales, dos preguntas del usuario reencuadraron la investigación: *"en una colisión gana la placa continental sobre la oceánica, ¿no? Entonces ¿por qué desaparece así la corteza continental?"* y *"¿podemos probarlo sin isostasia, para intentar aislar el problema?"*.

**Aislado correctamente:** se añadió un contador de celdas continentales por `CrustTypeData`, independiente de isostasia/nivel del mar (mismo patrón que `ContinentsPersist`). Resultado: el recuento cae de 23.729 a 8.198 en 1000 Ma (65%), en lockstep con la tierra emergida — confirmando que la corteza se destruye de verdad, no que simplemente se hunde por elevación.

**Hipótesis 1 — rift convirtiendo continente en océano sin comprobar qué había antes** (el rift pone `CrustTypeData=0` incondicionalmente). Instrumentado: solo el 1-3% de los rifts partían de una celda continental. Descartada como causa dominante.

**Hipótesis 2 — colisión sin representación**: releído el código, confirmado que "gana la continental" solo se aplica **entre los reclamantes actuales** — si la placa continental dueña de una celda ya se ha retirado del todo, la celda se disputa entre otras placas sin que la regla de protección llegue a aplicarse nunca. Instrumentado: menos del 0,3% de las colisiones. Descartada.

**Hipótesis 3 — limpieza de motas reasignando por coincidencia de placa, no de tipo**: instrumentado, también pequeña (<1%). Descartada como dominante.

Las tres juntas explicaban solo el ~31% de la pérdida total. **El resto (69%) seguía sin explicación** hasta revisar exhaustivamente, con `grep`, todos los sitios del fichero que escriben `CrustTypeData` — siete en total, de los cuales cuatro quedaban por instrumentar del todo.

### La causa real: `AssignCleanMove` en el camino más frecuente, con el material desincronizado

`AssignCleanMove` lee el material que el **nuevo dueño** tiene guardado en su propio marco (`ReadPlateMaterial`) — si ese marco tiene *algo* guardado (aunque sea viejo, de otro momento de la historia de esa placa), ese dato se impone sin comparar con lo que había físicamente en el mundo un instante antes. El resguardo a "lo que ya había" solo salta si el marco está completamente vacío.

Instrumentado primero en el camino de vecino más cercano (poco frecuente): explicaba un poco más (traspaso 392→841 por tramo), pero seguía faltando la mayoría. **Instrumentado después en el camino "1 reclamante claro" — el más frecuente con diferencia, sin tocar todavía**: el número se disparó a 8.000-12.000 celdas por tramo. Y por último, **incluso en el camino rápido de interior** (`Owner == CurrentOwner`, sin cambio de dueño siquiera) se encontró el mismo patrón: una placa leyendo su **propio** material, en su propio territorio, encontrando un tipo distinto al que el mundo dice que hay ahí ahora. Eso solo puede pasar si el marco de material está desincronizado del mundo.

Y la causa de esa desincronización ya estaba documentada, sin dársele la importancia que tenía: en A14 (R2.9 Fase 4) quedó anotado como pendiente que **`WriteBackToPlateFrames()` solo corre una vez por `Step()`, no una vez por advección** — con `TimeScale` alto caben hasta 8 advecciones por `Step()`, así que en las intermedias el marco de material puede llevar varias advecciones de retraso respecto al mundo real. Se caracterizó entonces como *"acotado, no catastrófico"*. Los números de esta sesión dicen que es bastante severo: miles de celdas por tramo de 125 Ma, la mayor parte del sumidero de corteza continental sin explicar.

### El arreglo, ya usado y probado antes en otro sitio

La Capa 2a de depuración (modo `Y`) ya llamaba a `WriteBackToPlateFrames()` tras **cada** advección desde el 17-08-2026, precisamente para evitar este mismo desfase — pero esa corrección nunca se llevó a producción. Se aplicó el mismo patrón a la rama de producción de `Step()`: `WriteBackToPlateFrames()` se llama ahora tras cada advección, antes de reconstruir segmentos y cinemática, para que ambos lean material ya sincronizado. La llamada final de `Step()` (tras isostasia/difusión) se mantiene intacta — sigue haciendo falta para propagar lo que la física de superficie cambia, que no pasa por advección.

**Pendiente de validar con datos propios**: el test automatizado (`TimeScale=1`) rara vez encadena 2+ advecciones dentro de un mismo `Step()`, así que no ejercita el escenario que el arreglo corrige — un intento de subir `TimeScale` dentro del test reveló que `System->Step()` se llama con `Params.DeltaTime` sin escalar mientras `Raster->Step()` sí aplica `TimeScale` internamente, descuadrando la cinemática del sistema respecto a la corteza; revertido, no vale la pena arreglar el arnés de test para esto. La validación de este arreglo concreto se hace en el editor con `TimeScale` de verdad (donde sí está bien conectado), confirmado a ojo por el usuario: la corteza continental aguanta mejor en una corrida larga con F1F activo, aunque el ratio creación/destrucción sigue por debajo de 1.

### Pendiente

- ~~El ratio creación/destrucción sigue sin llegar a 1,0~~ — resuelto en gran parte, ver la sección siguiente ("La fuga de tipo que sobrevivió al write-back").
- Falta revalidar `Simu.Tectonics.*` completo (los ~20 tests) contra el estado actual del código — el árbitro, el radio de contienda y el arreglo de write-back se construyeron y afinaron después de la última pasada completa.
- No se ha medido el coste (`AdvectionMs`) del árbitro nuevo a escala de producción — ahora hace la búsqueda amplia (25 candidatos por placa) en **toda** celda de frontera, no solo en el residuo que quedaba sin resolver antes.

## La fuga de tipo que sobrevivió al write-back: extinción total en `ContinentsPersist` (18-08-2026, misma sesión, un día después)

### El write-back por advección no era la causa completa

Al correr `Simu.Tectonics.*` completo por primera vez tras el arreglo anterior (petición explícita del usuario: *"Corre simu.tectonics"*), saltaron dos regresiones nuevas: `PlateFieldEvolves` (falso positivo del propio test, arreglado aparte — contaba celdas con un array dimensionado al `NumPlates` fijo del fixture, y F1E puede crear placas nuevas en plena corrida) y **`ContinentsPersist` con extinción total**: `1321 → 0` celdas continentales en los primeros 150 pasos, reproducido idéntico en dos corridas independientes (semilla fija 555). Antes de esta sesión el peor tramo perdía un 19,7%; ahora era el 100%.

Instrumentando los cuatro contadores "X-desde-continental" ya existentes (ver sección anterior) en esa misma corrida: `rift=347, handoff=8103, colisión=103, despeckle=77`. **`handoff` seguía siendo, con diferencia, el sumidero dominante** — 94% del total — **pese a que el write-back por advección ya estaba activo**. El arreglo de la sección anterior reducía la ventana de datos rancios, pero no la cerraba.

### Por qué seguía pasando: el redondeo mundo↔marco no es una inversa exacta

`AssignCleanMove` convierte una dirección del mundo a coordenadas del marco propio de la placa (`ReadPlateMaterial`, rotando por `PlateAccumRotation` y cuantizando con `floor()`), y `WriteBackToPlateFrames()` hace el camino inverso (coordenadas de marco → dirección del mundo, cuantizando también con `floor()`). Estas dos cuantizaciones **no son inversas exactas**: una rotación no preserva la alineación de una rejilla discreta, así que el centro de una celda de marco, rotado de vuelta al mundo, no cae necesariamente dentro de la misma celda de mundo de la que partió. El resultado es un alias ocasional de un píxel: la lectura acaba trayendo el dato de la celda de marco **vecina**, no la propia.

Esto es invisible en la inmensa mayoría del interior de una placa, donde la celda vecina tiene el mismo tipo de corteza. Es catastrófico justo en una costa (frontera continental/oceánica *dentro* de la misma placa, no una frontera de placas): ahí el vecino de marco puede tener un tipo distinto, y como el tipo es categórico, un solo alias es un volteo permanente hasta el próximo rift o colisión que lo toque. Más grosero cuanto más baja la resolución — coincide con que `ContinentsPersist` (Res 32, el test que llegaba a extinción total) es mucho más pequeño que `LongRunStability`/`F1EF1FLongRun` (Res 128, que solo colapsaban con severidad).

Esto también explica, con los datos ya en la mano, por qué el camino **más frecuente con diferencia** —el interior, `Owner == CurrentOwner`, ni siquiera un traspaso de propiedad— era el que más pesaba: es ~97% de las celdas del planeta, así que hasta una probabilidad de alias mínima por celda se traduce en miles de eventos absolutos por avance.

### El arreglo: el tipo de corteza sale de `Prev`, nunca del marco rotado

Para una celda que **no** cambia de dueño (interior o "1 reclamante claro" con `Owner == CurrentOwner`), el tipo físico de ese punto no ha cambiado, y `Prev` ya lo tiene exacto — sin pasar por ningún redondeo de ida y vuelta. `AssignCleanMove` se cambió para tomar `CrustTypeData` siempre de `Prev[FaceIdx]`, dejando Edad/Grosor/Elevación leyendo del marco como antes (campos continuos, sin el mismo modo de fallo catastrófico de un volteo categórico permanente).

Para un traspaso genuino (`Owner != CurrentOwner`, tanto en el camino de "1 reclamante" como en el de vecino más cercano), se introdujo un helper nuevo, `AssignHandoff`, que toma **los cuatro campos** de `Prev`, sin llamar a `ReadPlateMaterial` en absoluto — un traspaso es por definición territorio que la placa no poseía hasta ese instante, así que no existe ningún dato "propio" fiable que leer; el marco de material del nuevo dueño en ese punto es, en el mejor caso, vacío (dispara el resguardo de todas formas) y en el peor, alias de otro momento de su historia.

**Medido, antes → después del arreglo (misma corrida, semilla 555):**

| | rift | handoff | colisión | despeckle | continente |
|---|---|---|---|---|---|
| Antes | 347 | 8103 | 103 | 77 | 1321 → **0** (extinción) |
| Después | 368 | **0** | 153 | 117 | 1321 → **3271** (x2,48) |

`handoff` es ahora estructuralmente 0 en los tres sitios donde se contaba — no es un umbral bajado, es imposible que vuelva a subir mientras el tipo salga de `Prev` en continuación y traspaso. Se dejó el `AddInfo` de diagnóstico en el propio test como guarda de regresión permanente.

El ratio global creación/destrucción (no solo tipo) también mejoró de forma sustancial: de ~0,42-0,50 (medido en sesiones anteriores) a ~0,88 en esta corrida — el "sigue sin llegar a 1,0" que quedó pendiente en la sección anterior queda, si no cerrado del todo, sustancialmente resuelto por la misma causa raíz.

### Consecuencia inesperada: la convergencia por tramos, que "dejó de cumplirse" el 18-08-2026, volvió sola

La asección de `ContinentsPersist` que comprobaba que el segundo tramo de 150 pasos cambiara menos que el primero (desaceleración hacia un equilibrio) se había cambiado, más temprano en este mismo día, por una cota plana ("ningún tramo consume más del 35% del continente de partida") — la explicación de entonces era que la partición por vecino más cercano había quitado una protección accidental (celdas de frontera congeladas que nunca cambian de dueño). Esa cota plana también dejó de cumplirse (peor tramo 100%, la extinción de arriba).

Con la fuga de tipo cerrada, **la convergencia volvió sin tocar la aserción**: 1766 → 141 celdas en la misma corrida que antes daba 1321 → 0. La explicación original (partición por vecino más cercano rompiendo la desaceleración) era incompleta — el verdadero culpable era la fuga de tipo, y la partición por vecino más cercano solo la exponía más rápido al mover más celdas de frontera por advección. Restaurada la aserción original (`SecondHalfChange < FirstHalfChange / 2`), con el historial completo documentado en el propio test para que no se pierda otra vez el motivo del cambio de ida y de vuelta.

### `AdvectionChainingHypothesis`, misma causa, otro síntoma

El mismo test que en su día (16-08-2026) midió `stride 1→x2,02, stride 2→x2,52, stride 4→x3,09` y sirvió para descartar la hipótesis de encadenamiento, había empezado a fallar tras el árbitro nuevo (`x2,48/x2,45/x2,21`, tendencia invertida y comprimida). Con la fuga de tipo cerrada: `x1,73/x2,16/x2,09` — stride 1 vuelve a ser con diferencia el mejor (incluso mejor que el histórico x2,02), aunque 2 y 4 quedan casi empatados y fuera de orden estricto entre sí (diferencia de 0,07). Se relajó la aserción para comprobar lo que de verdad importa —que nadie suba `AdvectionPixelStride` de producción sin saber que empeora los bordes— sin exigir una cadena monótona completa que ya no es la forma real de la curva.

### Probado y descartado en el camino: la elevación no tenía el mismo problema

Antes de identificar la fuga de tipo como algo distinto de la caída de tierra emergida (medida por elevación, no por tipo de corteza), se probó a extender el mismo arreglo (leer de `Prev`, no del marco) a `ElevationData` en `AssignCleanMove`. Resultado: **cero cambio**, dígito a dígito idéntico en `LongRunStability`/`F1EF1FLongRun` con y sin el cambio — la elevación es un campo continuo y el ruido de un píxel se diluye en la media, al contrario que el tipo, categórico y con un volteo permanente. Revertido, no aporta nada. La caída de tierra emergida sigue abierta, ver "Pendiente".

### Pendiente

- ~~La fracción de tierra emergida (elevación) sigue cayendo~~ — resuelto, ver la sección siguiente ("El grosor tenía la misma fuga que el tipo").
- `NoStraightCrustBridges` sigue en rojo, sin relación aparente con esta investigación — pendiente de una sesión dedicada.

## El grosor tenía la misma fuga que el tipo: la cohorte que nunca emergía (18-08-2026, misma sesión, validado en el editor por el usuario)

### Isostasia auditada primero, y descartada como causa

Antes de sospechar del grosor, se auditó `ComputeIsostaticElevation`/`RebuildElevationFromIsostasy` línea por línea: la fórmula de Airy está bien (35 km continentales → +840 m, coincide con la calibración documentada), y la reconstrucción es un **recálculo completo sin estado** en cada `Step()` — no hay ningún "retraso" que isostasia pudiera estar arrastrando. La sospecha inicial (16-08-2026, ver el comentario junto a `GetContinentalBreakdown` en el header) de que la acreción de arco crea corteza continental a los 20 km pero hacen falta ~30 km para emerger seguía siendo válida como mecanismo, pero no explicaba por qué la corteza nunca terminaba de cruzar ese hueco.

### La difusión que no cruza tipos: correcta, pero no la pieza que faltaba

Primera hipótesis, con buena lógica pero refutada por la medida: la relajación difusiva (`DiffusionRate`, kernel 3x3 sobre `CrustThicknessData`) mezclaba grosor de cualquier vecino sin mirar `CrustTypeData`, y en un margen (~20-35 km continental junto a ~7 km oceánico, el gradiente más pronunciado del campo) eso tira con mucha fuerza. Se arregló -el kernel ahora excluye vecinos de tipo distinto y renormaliza por la suma de pesos usada, mismo principio que ya se exige para el tipo- pero **remedido después, el histograma de grosor por tramo salió prácticamente idéntico**. Arreglo correcto por su propio mérito (no hay motivo físico para promediar grosor entre tipos), pero no era la causa dominante.

### La cohorte: la prueba directa, siguiendo celdas concretas en el tiempo

Con las hipótesis agregadas agotadas, se siguió una **muestra de 38 celdas concretas** que acababan de madurar a continental (20,4 km de media) a los 125 Ma de `Simu.Tectonics.F1EF1FLongRun`, comprobando su grosor cada 25 Ma durante el resto de la corrida:

```
125 Ma: 20,4 km  (recién madurada)
150 Ma: 15,4 km  ← cae 5 km en 25 Ma
200 Ma:  9,8 km  ← por debajo del grosor oceánico típico, pero sigue "continental" por tipo
...resto de la corrida oscilando 7-15 km, nunca recupera
```

La mayoría de la cohorte **sigue contando como continental por tipo** (31-34 de 38 al final) pero su grosor se desploma a niveles casi oceánicos en cuestión de 25 Ma y no vuelve a subir, pese a que `OrogenyFactor` debería seguir engordándolas mientras haya convergencia. Esto no es difusión filtrándose lentamente: es demasiado brusco y demasiado rápido para eso.

### La causa real: el mismo alias de redondeo, pero en el grosor, no en el tipo

`AssignCleanMove` (el camino de continuación, `Owner == CurrentOwner`) ya leía el tipo siempre de `Prev` desde el arreglo de ayer, pero **seguía leyendo Edad/Grosor/Elevación del marco rotado de la propia placa** — con el razonamiento explícito, nunca comprobado, de que un campo continuo se libraba del alias mundo↔marco por diluirse en la media. La cohorte demuestra que ese razonamiento era incorrecto cuando el propio campo tiene una discontinuidad física tan marcada como la del tipo: 20+ km de grosor junto a 7 km oceánicos, en el mismo punto de la costa. El mismo alias que volteaba tipo categóricamente corrompe grosor igual de bien ahí.

**Arreglo**: unificadas `AssignCleanMove` y `AssignHandoff` en una sola función — con el tipo ya arreglado y el grosor arreglado ahora, las dos hacían exactamente lo mismo (copiar los cuatro campos de `Prev`), así que la distinción entre "continuación" y "traspaso" ya no aportaba nada al material, solo a si `PlateIDData` cambia. `AssignCleanMove` se eliminó; los dos sitios que la llamaban ahora usan `AssignHandoff`.

**Medido, antes → después (misma corrida, `F1EF1FLongRun`, 1000 Ma):**

| | 125 Ma | 500 Ma | 1000 Ma |
|---|---|---|---|
| Tierra emergida, antes | 19,7% | 7,8% | 2,3% (colapso) |
| Tierra emergida, después | 24,8% | 22,9% | 21,6% (estable) |
| Grosor 20-25 km, antes | 21,6% | 67,5% | 90,4% |
| Grosor 20-25 km, después | 0,0% | 0,0% | 0,0% |
| Grosor 45+ km, después | 12,2% | 29,9% | 47,5% (crece con el tiempo) |

La cohorte no volvió a encontrar ninguna celda en la ventana 20-21 km al muestrear (confirmación cruzada: la corteza ya no se detiene ahí ni un instante). `Simu.Tectonics.LongRunStability` pasa por primera vez desde que existe (24,5%→20,9%, sin colapso). Validado también a ojo en el editor por el usuario, con capturas mostrando continentes coherentes y estables, 0% sumergido.

### Motas de tipo dentro de territorio sólido: el mismo agujero de despeckle, en dirección contraria

Reportado por el usuario viendo el campo "Tipo de corteza" en el editor: pixeles oceánicos sueltos dentro de un continente por lo demás sólido. Misma causa estructural que las motas continentales de la sección anterior, pero el despeckle original (15-08-2026) solo compara `PlateIDData` con los 4 vecinos — si la celda suelta comparte placa con su entorno (propiedad correcta, solo el material está mal), es invisible para esa limpieza sin importar cuánto tiempo lleve así. Origen más probable: celdas que en algún momento cayeron en el residuo real (`RecoveryCountData`, "congeladas" en el HUD) con un tipo, y el continente creció/rotó a su alrededor después sin reclamarlas nunca — una laguna que nunca se rellenó.

**Arreglo**: nueva rama en la limpieza de motas, independiente de la de placa — si el TIPO de una celda no coincide con ninguna de sus 4 vecinas (aunque la PLACA sí coincida con alguna, así que no hay que tocar territorio), se reasigna el material a la **media** de los vecinos del tipo mayoritario. Media, no el valor exacto de un solo vecino prestado -primera versión, descartada tras medir una regresión real en `FrozenCellsAtProductionRes`: prestar de un único vecino podía crear un islote de edad anómala nuevo si ese vecino concreto resultaba viejo-. Una media nunca crea un máximo o mínimo local nuevo por construcción.

### Recalibraciones de umbral, dos causas distintas medidas por separado

- **`FrozenCellsAtProductionRes`** (0,82%→2,1%): el grueso del desplazamiento (0,82%→1,36%) viene del arreglo de grosor de arriba -corteza oceánica vieja que antes se corrompía/perdía por el alias ahora sobrevive, y esta métrica la cuenta-, confirmado con un experimento A/B (desactivar solo la rama de motas de tipo y remedir: 1,36%, casi idéntico al último commit sin ella). La limpieza de motas de tipo añade un empujón pequeño encima, 1,36%→1,51%. Ninguno de los dos es el síntoma que este test caza -un cordón que crece sin límite-, es corteza vieja legítima que antes se perdía por un bug ya cerrado.
- **`ContinentsPersist`** y **`AdvectionChainingHypothesis`**: la limpieza de motas de tipo toca un puñado de celdas por advección, lo bastante para rozar dos umbrales que se habían dejado demasiado ajustados esta misma sesión (mitad exacta, y "el mejor de los tres" sin tolerancia). La salud de fondo no cambió -`ContinentsPersist` sigue en x2,45 de crecimiento, `handoff` sigue en 0-, así que se añadió margen (0,6 en vez de 0,5; tolerancia de 0,1) en vez de perseguir ruido de bajo nivel.

### Pendiente

- Reportado por el usuario, sin confirmar todavía: el movimiento visible de una placa en el editor parece mucho más lento (~1 km/Ma a ojo) que lo que sugeriría su velocidad angular mostrada. Dos hipótesis abiertas, no excluyentes: (a) un escape real en `UpdatePlateKinematicsFromTorqueBalance` -si `BlendedOmega` decae hacia cero por falta de par motriz, `IsNearlyZero()` salta el clamp duro de 1-15 cm/año y la deja congelada por debajo del mínimo físico, sin que nada la recupere-, o (b) la placa observada gira cerca de su propio polo de Euler, donde v=ω×r es pequeño aunque ω sea normal -física correcta, no bug-. Necesita confirmarse con la placa y posición concretas antes de tocar código.
- `NoStraightCrustBridges` sigue en rojo, sin relación con esta investigación.
- Sigue sin medirse el coste (`AdvectionMs`) del árbitro a escala de producción.

## F1D completo (campo de frontera) y F1E completo (muerte, sutura, polo de Euler propio, historial) (18-08-2026, misma sesión)

Petición explícita: *"quiero que termines el tipo de frontera de F1D y F1E entero"*.

### F1D: campo "Tipo de frontera" en el visor

Sin física nueva -la clasificación convergente/divergente/transformante por segmento ya existía (R2.13/FASE 2) y ya decidía orogenia/acreción/rift-, solo faltaba exponerla. Se añadió `BoundaryTypeData` a `FTectonicFaceTextureData` (0=interior, 1=convergente, 2=divergente, 3=transformante), escrito en el mismo punto de `AdvectPlateField` donde ya se clasifica cada celda de frontera, reseteado a 0 al empezar cada advección. Accesor público `GetBoundaryTypeAt`, campo `BoundaryType` registrado en el visor con la misma paleta categórica que "Tipo de corteza". Verificado con test dedicado (`Simu.Tectonics.BoundaryTypeField`, Res 64, 100 pasos): interior 22.182, convergente 461, divergente 562, transformante 1.371 -el interior sigue siendo con diferencia la mayoría, las fronteras una franja fina, tal como deberían verse-.

La fricción/esfuerzo acumulado/sismicidad de fallas transformantes (el otro pendiente de F1D) se dejó fuera deliberadamente: sin ningún consumidor todavía, sería construir infraestructura sin usuario claro.

### F1E: la base reutilizada para todo

Todo lo nuevo -muerte, polo de Euler propio, historial- vive dentro o junto a `HandlePlateFragmentation()`, reutilizando su flood-fill de componentes conexas en vez de recorrer el planeta otra vez por cada mecanismo nuevo. Cambio de base necesario primero: `ComponentBorderPlateID`/`ComponentBorderAmbiguous` (un `INDEX_NONE` + un booleano, solo distinguía "un vecino" de "dos o más") se sustituyó por `ComponentBorderCounts` (`TMap<int32,int32>` por componente, vecino → celdas de frontera compartidas) -la asimilación normal sigue funcionando igual (vecino único = `Counts.Num()==1`), pero muerte necesita además "el vecino con MÁS frontera, aunque no sea único", que el booleano no podía dar.

### Muerte

Área TOTAL de una placa (sumando TODOS sus componentes, no solo los que fragmentarían por separado -una placa reducida a un único componente pequeño nunca entraba en el bucle de fragmentación, que exige `Comps.Num() >= 2`-) por debajo de `MinPlateAreaCells` (150, un orden de magnitud por debajo de `MinFragmentCells`=800, primera calibración sin línea base medida todavía) ⇒ la placa entera se disuelve, componente a componente, en quien más le rodee -incluso el componente que normalmente conservaría el ID-. Si el mejor vecino no es único, se elige el de más frontera compartida en vez de dejarlo tal cual: una placa muerta no tiene la opción de "esperar al siguiente paso".

No se elimina del array de `TectonicPlateSystem` -el `PlateID` es el índice, y borrar en medio correría todos los índices posteriores, corrompiendo cada celda que apunta a un índice mayor-. Se queda como una placa "fantasma": 0 celdas, entrada del array intacta. `ClearPlateFrame()` vacía su `PlateMaterial`/`PlateTerritory` para que nadie lea datos rancios.

No se disparó en la corrida de validación (`Simu.Tectonics.PlateLifecycle`, 1000 Ma, Res 128, semilla 4242) -emergente, no garantizado; el umbral de 150 puede ser conservador, o simplemente no tocó en esta semilla concreta-.

### Sutura

El complemento de la fragmentación: sin esto, F1E solo podía aumentar el número de placas (fragmentación) o mantenerlo (asimilación) -nunca reducirlo por "dos placas que ya se movían juntas se vuelven una", que es la única reducción que no depende de quedarse casi sin territorio (eso ya lo cubre muerte). Criterio, el mismo que `FBoundarySegment::Age` ya preveía desde que se documentó ("es directamente lo que R2.16 necesitará para decidir sutura"): `Age >= SutureAgeThresholdMa` (300 Ma) y `|AverageConvergence|`/`|AverageTangential|` ambos por debajo de `SutureVelocityFraction` (0,02, más estricto que el 0,10 de rift porque hace falta quietud de verdad, no solo ausencia de separación) × `MaxAngularSpeed`.

La placa con menos celdas AHORA (contadas de verdad, no `FTectonicPlate::CellCount` -ese campo solo se pone al día en la generación y en el nacimiento por fragmentación, no paso a paso, así que no es fiable para decidir quién sobrevive-) se reasigna entera a la de más. `HandlePlateSuture()` corre justo después de `HandlePlateFragmentation()`, leyendo `BoundarySegments` de la advección anterior -la de esta todavía no se ha reconstruido en ese punto del `Step()`, pero `Age` se acumula durante cientos de Ma, un desfase de una advección es irrelevante-.

**Medido en `Simu.Tectonics.F1EF1FLongRun` (1000 Ma, Res 128, dinámico): 3 fusiones** (placa 7→5 a los 302 Ma sin movimiento relativo, placa 4→1 a los 312 Ma, placa 5→2 a los 346 Ma), más 1 fragmentación nueva y 1585 asimilaciones de islas huérfanas. Tierra emergida 24,9%→22,7%, **sin colapso**, sumergido en 0,0% durante toda la corrida.

### Polo de Euler propio para la placa nueva

Antes: `NewPlate = ParentPlate` copiaba `EulerPole`/`AngularVelocity` tal cual -el fragmento nuevo giraba exactamente igual que el padre, para siempre, salvo que F1F dinámico lo corrigiera en el siguiente paso; con cinemática fija nunca se corregía-.

Derivación: si *a* y *b* son vectores unitarios y perpendiculares entre sí, `(a×b)×a = b` (identidad del triple producto vectorial, `a·a=1`, `a·b=0`). Con `ChildCentroid` (centroide del componente que nace, acumulado gratis durante el mismo flood-fill que ya recorre sus celdas) y `AwayDir` (dirección desde el centroide del padre hacia el del hijo, proyectada tangente a la esfera en el hijo), el eje `Axis = ChildCentroid × AwayDir` cumple `Axis × ChildCentroid = AwayDir`: rotar alrededor de `Axis` mueve el centroide del fragmento nuevo en la dirección en la que de verdad se alejó del resto de la placa al partirse. Magnitud: la misma `AngularVelocity` del padre -ya calibrada a rango físico-, solo el eje cambia. Degenerado (centroide del padre desactualizado, coincide con el del hijo) ⇒ se cae al comportamiento anterior, nunca peor que antes del arreglo.

### Historial de placas

`FPlateLifecycleRecord` (`Event`, `PlateID`, `RelatedPlateID`, `SimTime`, `CellCount`) en un array append-only, `GetPlateHistory()`. Cuatro eventos: nacimiento por fragmentación, asimilación de isla huérfana (ya existía como mecánica, ahora también se registra), muerte, sutura. Sin esto, un `PlateID` reciclado -dos placas distintas ocupando el mismo índice en momentos distintos tras una muerte, aunque en la práctica el índice nunca se recicla porque nunca se libera de verdad- no se podría distinguir de la misma placa continua con solo mirar `PlateIDData`.

### `GetNumLivingPlates()`: el número que de verdad importa

Efecto colateral necesario de que muerte/sutura nunca reduzcan el array: `PlateSystem->GetNumPlates()` (`Plates.Num()`, el tamaño del array) sigue creciendo con cada fragmentación y **nunca baja**, así que un HUD que solo muestre ese número parece contradecir el propio objetivo de muerte/sutura -"placas 9" tras 3 fusiones no es mentira, pero es la mitad de la historia-. `GetNumLivingPlates()` recorre el mundo una vez y cuenta cuántos IDs tienen al menos una celda de verdad ahora. En la corrida de validación: **`GetNumPlates()` decía 9, `GetNumLivingPlates()` decía 6**. El HUD de `TectonicsTestActor` ahora muestra ambos ("Placas: 6 vivas (9 en total)").

### Verificación

Test dedicado `Simu.Tectonics.PlateLifecycle` (Res 128, semilla 4242, 1000 Ma, dinámico): no exige que muerte/sutura ocurran -son emergentes, forzarlas de forma determinista pediría un escenario de juguete que no probaría el código de producción-, solo comprueba los invariantes que tienen que sostenerse siempre: conservación total de celdas, `GetNumLivingPlates() <= GetNumPlates()`, y que toda placa que aparece en el historial como muerta o fusionada tiene de verdad 0 celdas ahora -no solo que el evento se registró, que el mecanismo funcionó-. En la corrida real: 1 nacimiento, 1585 asimilaciones, 0 muertes, 3 suturas, 6 vivas de 9, todas las comprobaciones en verde.

Suite completa `Simu.Tectonics.*`: **22/23 en verde**, incluidos los dos tests nuevos (`BoundaryTypeField`, `PlateLifecycle`). El único rojo (`NoStraightCrustBridges`) es el problema pre-existente ya documentado, sin relación.

### Pendiente

- Muerte no se ha visto disparar todavía en ninguna corrida de validación -el umbral (150 celdas) es una primera calibración por analogía con `MinFragmentCells`, no medida contra una línea base real. Si en una corrida más larga o con más placas nunca dispara, revisar si el umbral es demasiado bajo.
- El polo de Euler derivado de la ruptura no se ha comparado numéricamente contra el del padre en ninguna corrida -se sabe que compila y que el mecanismo tiene sentido geométrico, pero no hay una medida de "cuánto difiere" en la práctica.
- Fricción/esfuerzo/sismicidad de fallas transformantes (F1D) y campo de esfuerzo del manto de gran escala (F1F, R2.18) quedan fuera deliberadamente: física nueva y autocontenida, cada una merece su propia sesión.

## Visor de velocidad: flecha y número por placa (18-08-2026)

Pedido explícito del usuario tras la sesión de F1E: al pulsar V no bastaba con el reporte de texto, había que ver por placa una flecha con dirección y un número con la velocidad. `DrawVelocityDebug()` calcula, por placa, `CentroidDir` (dirección del centroide desde el centro del planeta), `Omega` (vector de velocidad angular a partir de `EulerPole`/`AngularVelocity`) y `VelocityDir = CrossProduct(Omega, CentroidDir)` normalizado -la velocidad lineal de un punto en rotación rígida es siempre perpendicular tanto al eje como al radio-. La rapidez en cm/año usa la misma fórmula que el clamp de cinemática de F1F: `SpeedCmPerYear = |AngularVelocity| * RadiusMetres / 1e4`.

Dos rondas de arreglo visual guiadas por captura de pantalla del usuario:
- La flecha no se veía -`DrawDebugDirectionalArrow` recibía un `Thickness` fijo de 25 unidades contra un `VisualRadius` de 637.100.000: invisible a esa escala aunque la LONGITUD sí estaba bien escalada. Arreglo: `Thickness = ArrowLength * 0,08f`.
- Con la línea ya visible, "falta una punta de flecha clara" -el cono nativo no se distinguía a esa escala. Arreglo: añadir una esfera de depuración (`DrawDebugSphere`) en la punta, no depender del cono.

Aceptado por el usuario sin pedir más iteración ("lo dejamos así").

## F1D: sismicidad, fricción y esfuerzo acumulado de verdad (18-08-2026)

### El modelo

`FBoundarySegment::AccumulatedStrain` (radianes) acumula desplazamiento tangencial SOLO cuando el régimen del segmento es transformante (`bTransformDominant`); cualquier otro régimen lo CONGELA -no lo resetea, un paso que clasifica distinto no ha liberado nada de verdad-. Convertido a metros (`* RadiusMetres`) se libera en terremotos de tamaño fijo (`SeismicSlipThresholdMetres = 3 m`, del orden de un M7-8 real) vía la fórmula de momento de Hanks-Kanamori: `Mw = (2/3) log10(M0) - 6,07`, `M0 = μ * Área * Deslizamiento`, con `Área = (CellCount * CellWidthMetres) * SeismogenicDepthMetres` (15 km, profundidad sismogénica típica de corteza continental) y `μ = 3×10¹⁰ Pa` (rigidez típica de la corteza). Cada evento se registra en `SeismicHistory` (`FSeismicEvent`: placas, magnitud, deslizamiento, posición, tiempo) y se resume en el HUD ("Sismos: N totales | último Mw X hace Y Ma").

### Primer bug: liberar todo de golpe da magnitudes imposibles

La primera versión liberaba TODO lo acumulado en un solo evento cuando superaba el umbral, y reseteaba a 0. A la granularidad de esta simulación (pasos de Ma, velocidades típicas de 1-15 cm/año) una sola advección transformante puede acumular fácilmente decenas de km de deslizamiento tangencial -muy por encima del umbral-, así que el "deslizamiento liberado" de un evento salía en decenas de miles de metros, con magnitudes Mw 10-12: más allá de cualquier terremoto real (récord histórico ~Mw 9,5). No era "un evento agregando varios terremotos reales", era simplemente un número mal acotado.

Primer arreglo: en vez de liberar todo de golpe, un bucle que libera EXACTAMENTE `SeismicSlipThresholdMetres` por vuelta, tantas veces como haga falta, acotado con una cota de seguridad de 1000 vueltas "para nunca bloquear el frame". Cada evento queda físicamente acotado y comparable entre sí.

### Segundo bug, medido al re-validar: el atraso crece sin límite

Al re-ejecutar `Simu.Tectonics.TransformFaultSeismicity` tras el primer arreglo, el test falló -no por magnitud, sino por: `'El esfuerzo acumulado no crece sin limite (maximo actual 9997693.0 m)'`. El log mostraba miles de eventos idénticos (`Mw 7,5 entre placas 3 y 6, segmento de 2 celdas`) en el mismo milisegundo.

Causa: la cota de 1000 vueltas nunca se pensó como techo habitual, pero a esta granularidad SÍ se alcanza cada paso en fallas rápidas -una sola advección puede meter cientos de km de deslizamiento tangencial (a 15 cm/año durante 1 Ma, hasta 150 km), y 1000 vueltas de 3 m son solo 3 km liberados-. El resto se quedaba en `AccumulatedStrain` como atraso, y ese atraso crecía paso tras paso hasta casi 10.000.000 m en una corrida de 400 Ma -órdenes de magnitud por encima de lo que la corteza real puede almacenar como deformación elástica antes de fallar.

Arreglo de fondo, no de umbral: se bajó la cota a `MaxSeismicEventsPerSegmentPerAdvection = 20` (pensada para no inundar el log, no para "drenar del todo") y, el cambio que importa, al llegar a esa cota el resto de la deformación de ESE PASO se DESCARTA -se trata como reptación asísmica / deformación distribuida no resuelta a esta resolución- en vez de guardarse para el paso siguiente. Con eso, `AccumulatedStrain` queda acotado por construcción (nunca por encima de ~3 m entre liberaciones) sin importar cuánto entre en un paso dado, así que el invariante del test se cumple estructuralmente y no por ajustar un número.

### Verificación

`Simu.Tectonics.TransformFaultSeismicity` (Res 96, semilla 4242, 400 Ma): verde. 105.440 eventos registrados en la corrida (frente a los 7,18 millones antes del arreglo de la cota, y a la ausencia de límite superior en `AccumulatedStrain` que causaba el fallo). Todas las magnitudes finitas y en rango (-2, 10,5), todo deslizamiento igual al umbral, máximo de esfuerzo acumulado en cualquier celda del planeta muy por debajo del límite de cordura del test.

Suite completa `Simu.Tectonics.*`: **23/24 en verde**. El único rojo (`NoStraightCrustBridges`) sigue siendo el problema pre-existente ya documentado, sin relación.

### Pendiente

- Los umbrales (`SeismicSlipThresholdMetres`, `MaxSeismicEventsPerSegmentPerAdvection`, profundidad sismogénica, módulo de rigidez) son una primera calibración razonada, no medida contra una línea base real -mismo estilo de aviso que el resto de constantes de F1E.
- El descarte por reptación asísmica al llegar a la cota es una simplificación de modelado defendible pero no calibrada: no hay medida de qué fracción del movimiento de placa real se acomoda así frente a sísmicamente.
