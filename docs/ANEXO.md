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
| Frontera inferida por conteo, no como objeto | — | 🔴 Defecto de **modelo** |
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
