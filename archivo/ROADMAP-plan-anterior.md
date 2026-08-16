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

### F1 — Movimiento real de placas 🔴 REABIERTO (16-08-2026)

> **Las placas se disuelven.** El campo de IDs arranca como 8 regiones limpias y a los ~200 Ma es una mezcla a escala de píxel. Visto en el visor, confirmado midiendo. No es un defecto acotado: es que **las placas han dejado de existir como objetos**, y toda la clasificación de fronteras se apoya en ellas. Diagnóstico completo en [A10](#a10-el-defecto-de-raíz-de-f1-la-propiedad-y-el-material).

Lo que sigue siendo válido:

- [x] `UPlateKinematics` cableada; los centroides rotan
- [x] Colisión por densidad: océano subduce bajo continente; entre océanos subduce la más vieja
- [x] Se advectan edad, tipo y grosor junto con el ID
- [x] La advección no corre en cada paso, sino al acumular ~1 píxel de desplazamiento
- [x] Orogenia calibrada contra el Himalaya ([detalle](#a4-calibración-de-la-orogenia))
- [x] Los "puentes" rectos de corteza: **resueltos y verificados en pantalla** ([por qué el test dice lo contrario](#el-quinto-engaño-de-una-métrica-16-08-2026))

Lo que hay que rehacer:

- [ ] **Separar la propiedad del material.** Hoy comparten `ReferenceData` y necesitan lo contrario: la propiedad hay que re-deducirla del mundo actual en cada paso; el material no se puede remuestrear más de una vez ([A10](#a10-el-defecto-de-raíz-de-f1-la-propiedad-y-el-material))
- [ ] **La frontera, como objeto.** Clasificar por velocidad relativa proyectada sobre la normal de la frontera, no contando reclamantes
- [ ] **Ciclo de vida de placas.** Hoy nacen 8 y son 8 para siempre: una placa no puede morir subducida ni nacer de un rift, y eso es imposible *por construcción*, no por falta de código
- [ ] **Lazo dinámico** (después de todo lo anterior): la velocidad angular responde al tirón de la losa en subducción. Sin él hay cinemática, no dinámica
- [ ] ⏸️ Los dos puntos aparcados de F1 siguen aparcados ([por qué](#a9-los-dos-puntos-aparcados-de-f1))

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

> **Hay uno que sí bloquea, y desde el 16-08-2026 es el único que importa.** La tabla anterior daba todos los defectos por acotados; era falsa, porque los medía por separado sin ver que casi todos son síntomas de lo mismo.

| Defecto | Medida (16-08-2026) | ¿Es de raíz? |
|---|---|---|
| **Las placas se disuelven** | ID de placa: de 8 regiones a mezcla de píxel en ~200 advecciones | **SÍ — es la raíz** ([A10](#a10-el-defecto-de-raíz-de-f1-la-propiedad-y-el-material)) |
| Balanza de corteza rota | creada 47.999 / destruida 1.185.542 (ratio 0,04) | Síntoma. Con el reparto sano sube a 0,99 |
| Solape (celdas con ≥2 reclamantes) | 28,7 % de las actualizaciones | Síntoma. Con el reparto sano baja a 1,26 % |
| Celdas sin resolver | **31,6 %** (el ROADMAP decía 0,016 %: cifra caducada) | Síntoma. Con el reparto sano baja a 0,003 % |
| Grosor continental medio | 53,9 km, con `ContinentalThickness` = 35 km | Síntoma: la banda de solape apila corteza |
| Área continental creciendo | hasta cubrir el planeta | Síntoma: el desempate continental-gana-a-oceánica es un trinquete sobre el 28,7 % |
| Escalonado de bordes | ×2,96 (mejora a ×2,04 con el reparto sano) | Parcialmente síntoma |
| Sin ciclo de vida de placas | nacen 8, mueren 0, nacen 0 | Defecto **independiente**, de modelo |
| Sin dinámica (polos de Euler fijos) | nunca se escriben | Defecto **independiente**, de modelo |

La lección de esta tabla: **medir defectos por separado los hizo parecer acotados.** Cada uno tenía su número, su test y su casilla, y ninguno pasaba de "vigilado". Mirando el campo de IDs en pantalla se ve en dos segundos que son el mismo.

## Riesgos vigentes

- **Coste `O(Res²)` por campo.** Cada fase añade campos que recorren las 6 caras, y el coste escala con el número de celdas (medido: ×16 de Res 32 a 96). Es el riesgo principal de F3/F4.
- **Calibración cruzada de parámetros.** `OrogenyFactor` y `DiffusionRate` forman un equilibrio; tocar uno obliga a revisar el otro.
- **Calibraciones apoyadas en la balanza de corteza.** El umbral de rift (`0,10 × MaxAngularSpeed`) se fijó igualando creación y destrucción. Esa balanza estaba rota al hacerlo, así que **el umbral no está calibrado**: hay que rehacerlo cuando el reparto sea sano, y hasta entonces no es un valor de fiar.
- **Bordes del cubo.** Cada sistema nuevo con vecindad vuelve a pagarlo. Usar siempre `CubeFaceMapping` y `GetNeighborCell`.
- **Nombres de test que no describen lo que miden.** Ha costado dos días una vez ([detalle](#el-quinto-engaño-de-una-métrica-16-08-2026)). Antes de creerse un test en rojo, leer su aserción.
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

**Nota (16-08-2026): las cifras de esta tabla están caducadas.** Se midieron antes de que el referente entrara en juego. Hoy las celdas sin resolver son el **31,6 %**, no el 0,0157 %, y la balanza es 47.999/1.185.542. El arreglo de los cordones sigue siendo correcto y los cordones no han vuelto; lo que cambió el orden de magnitud es otra cosa ([A10](#a10-el-defecto-de-raíz-de-f1-la-propiedad-y-el-material)).

### El quinto engaño de una métrica (16-08-2026)

El apartado del escalonado avisaba de que el cociente de frontera había inducido a error **tres veces**. Ha vuelto a pasar, con otra métrica y con un coste mucho mayor: dos días de trabajo en la dirección equivocada.

`Simu.Tectonics.NoStraightCrustBridges` estaba en rojo con "12 de 104", y se dio por hecho que los **puentes rectos de corteza habían vuelto** — contradiciendo lo que el usuario estaba viendo en pantalla, que es que no están.

Lo que ese test mide **no son puentes**. Su aserción es `SamePlate == 0` sobre "anomalías de una celda": píxeles *sueltos* cuyo tipo de corteza difiere del de sus dos vecinas en un eje. "12 de 104" son 104 motas de un píxel, 12 de ellas dentro de una misma placa. A Res=256 eso es invisible.

Se llama así porque se escribió durante la caza de los puentes, para detectar la **semilla** de uno. La medida que sí vería una franja larga es `LongestRun()`, que el test calcula y **registra pero no comprueba**.

**Los puentes están resueltos.** Lo confirman las capturas del usuario y lo confirma leer la aserción.

Reglas que salen de esto, y que valen para todo el proyecto:

1. **Un test en rojo no prueba lo que dice su nombre.** Antes de creerlo, leer la aserción concreta que falla.
2. **Si un test contradice lo que se ve en pantalla, la sospecha va primero al test.** La regla de verificación ([A1](#a1-la-regla-de-verificación)) dice que ninguna fase se da por hecha si no se ve en pantalla; su recíproca también vale.
3. **Un test cuyo nombre no describe su aserción es peor que no tener test**, porque induce a error con la autoridad de una medición.

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

### Barrido de re-anclaje del referente (16-08-2026)

`Simu.Tectonics.ReferenceRebaseSweep`. 1000 Ma, Res=48, 8 placas, semilla 4242. `N` = cada cuántas advecciones se adopta el mundo como referente nuevo; `nunca` es el comportamiento de hoy.

| N | tierra % | balanza | solape % | sin resolver % | escalonado | continental | grosor cont. |
|---|---|---|---|---|---|---|---|
| nunca | 24,5 → 23,9 | **0,04** | 28,70 | 31,62 | ×2,96 | 3630 → 4046 | **53,9 km** |
| 1 | 24,5 → 13,1 | **0,99** | 1,26 | 0,003 | ×2,04 | 3630 → 2182 | **36,8 km** |
| 4 | 24,5 → 17,8 | 0,49 | 4,27 | 0,69 | ×2,28 | 3630 → 2863 | 40,5 km |
| 16 | 24,5 → 25,8 | 0,24 | 11,16 | 5,43 | ×2,68 | 3630 → 6526 | 46,8 km |
| 64 | 24,5 → 28,2 | 0,09 | 24,43 | 21,78 | ×3,35 | 3630 → 5390 | 53,4 km |

Qué prueba esto:

- **Todo escala monótonamente con `N`.** Balanza, solape, sin resolver, escalonado, área continental y grosor medio. No son seis defectos: son seis vistas del desajuste del reparto.
- **La balanza se arregla del todo** (0,04 → 0,99). Las 1.185.542 celdas "destruidas" eran desajuste, no subducción.
- **El escalonado MEJORA al re-anclar** (×2,96 → ×2,04, que iguala la mejor marca histórica). Se temía lo contrario. Parte del escalonado también era el desajuste.
- **El grosor continental delata el mecanismo**: 53,9 km sin re-anclar frente a 36,8 con `N`=1, y `ContinentalThickness` es 35 km. La banda de solape corría la rama de colisión sobre el 28,7 % del planeta en cada advección, y ahí el material continental **se apila**. Era una máquina de fabricar corteza.
- **Y sin embargo `N`=1 no es la solución**, que es el resultado importante del barrido: rompe `OceanicRibbon` (*"encadenar advecciones no alarga la cinta (13 con 48 advecciones, 1 con 1)"*), que es justo el test que justifica que el referente exista. Tres tests pasan a verde y otros tres se ponen en rojo.

**Ningún `N` funciona**, y por qué no puede funcionar está en [A10](#a10-el-defecto-de-raíz-de-f1-la-propiedad-y-el-material). El barrido no era el arreglo: era el experimento que demuestra que hace falta el arreglo de verdad.

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

~~F1 cumple su definición de hecho.~~ **Anulado el 16-08-2026.** Esa conclusión se apoyaba en que había conservación de corteza, y no la había: la balanza estaba en 0,04. Ver [A10](#a10-el-defecto-de-raíz-de-f1-la-propiedad-y-el-material).

## A10. El defecto de raíz de F1: la propiedad y el material

*(16-08-2026. Encontrado mirando el visor de ID de placa, no midiendo.)*

### El síntoma

El campo de ID de placa **arranca como 8 regiones limpias y se disuelve en una mezcla a escala de píxel**. A los ~200 Ma no hay placas: hay ocho etiquetas repartidas como ruido, con una trama diagonal.

Y la observación que lo convierte en diagnóstico: **ningún otro campo está roto.** Grosor, edad, tipo, altura y relieve muestran cuerpos grandes, limpios y con el mismo contorno entre sí. El mismo blob aparece idéntico en los seis filtros.

Si las placas se hubieran fragmentado de verdad, el material se habría fragmentado con ellas — viaja en la misma pasada del mismo código. No lo ha hecho. **El material se transporta como cuerpos sólidos y la etiqueta de propiedad se ha convertido en ruido: están desacoplados.**

### Por qué es lo peor que podía pasar

Toda la clasificación de fronteras sale de contar reclamantes por celda: 1 movimiento, 0 rift, ≥2 colisión. No hay ningún otro sitio donde se decida qué es una dorsal y qué una subducción.

Con el ID hecho ruido ese conteo no está mal calibrado: **está sin significado**. Cada celda tiene vecinas de tres o cuatro placas, así que cada celda es frontera. Y la prueba de divergencia que decide si un hueco es rift lee la velocidad de las placas vecinas — o sea, lee cuatro placas al azar.

### Nivel 1 — La propiedad y el material comparten estructura, y necesitan lo contrario

Es el hallazgo central, y explica por qué **todo arreglo hasta ahora ha sido un balancín**.

En `AdvectPlateField`, la pregunta *"¿de quién es esta celda?"* y la pregunta *"¿qué material hay aquí?"* se le hacen al mismo sitio: `ReferenceData`. Misma estructura, misma línea, misma antigüedad. Pero tienen requisitos **opuestos**:

- **La propiedad es una partición.** Debe seguir siéndolo en todo momento, y para eso hay que **re-deducirla del estado actual en cada paso**. Deducida de una foto vieja, se despega de la realidad y degenera en ruido.
- **El material es una sustancia transportada.** No se puede remuestrear repetidamente sin deshilacharlo. Hay que leerlo **una sola vez** desde un marco propio con la rotación acumulada.

Uno exige encadenar. El otro exige no encadenar nunca. Y están soldados.

| | propiedad | material |
|---|---|---|
| Con referente (hoy) | se pudre → ID hecho ruido | limpio, sin cinta |
| Re-anclando cada advección | perfecta (solape 1,26 %) | vuelve la cinta |

No son dos problemas que compiten por un ajuste: **son dos campos con necesidades contrarias en el mismo cajón**. Cualquier valor de `N` mejora uno y rompe el otro. El [barrido](#barrido-de-re-anclaje-del-referente-16-08-2026) lo mide de punta a punta.

### Nivel 2 — El modelo no tiene fronteras, tiene interiores

En la Tierra la tectónica **es** la frontera: rifts, subducciones, orogenias y transformantes ocurren todos en una línea. El interior de una placa solo rota.

Aquí no existe la frontera como objeto en ninguna parte. Existe el interior —una etiqueta por celda— y la frontera se infiere a posteriori contando reclamantes. Dos consecuencias:

- **El coste está invertido.** Cada advección hace 6·Res²·N rotaciones de cuaternión para calcular con precisión perfecta la parte aburrida, y la parte interesante sale de restos.
- **Nada garantiza la teselación.** Ocho regiones rígidas rotando cada una por su lado **no pueden** teselar una esfera. Es geometría, no un bug. Se confió en que la partición emergiera, y no puede.

**Corrección de un error de concepto propio:** los huecos y los solapes no son el error. *Son* la tectónica — en una dorsal sobra sitio de verdad y en una fosa falta material de verdad; la tectónica de placas no conserva área localmente. El error es que ocurren **en el 28,7 % de la superficie** en vez de en una banda de una celda a lo largo de las fronteras reales.

### Nivel 3 — Cinemática sin dinámica, y sin ciclo de vida

- **Las placas no nacen ni mueren.** `GeneratePlates()` las crea una vez; no hay un solo `Add` ni `RemoveAt` después. En la Tierra el número no se conserva: la Farallón se subdujo casi entera (quedan Juan de Fuca, Cocos y Nazca), India y Australia se separaron, y el Rift de África Oriental está fabricando una ahora. Aquí es imposible **por construcción**.
- **Ningún polo de Euler se escribe nunca.** `EulerPole` y `AngularVelocity` se leen para rotar y nada más. En la Tierra el motor principal es el **tirón de la losa**: una placa con un margen largo en subducción acelera, y una que pierde su losa frena. Sin ese lazo no hay estados de equilibrio: nada regula la fracción de tierra y nada hace que un supercontinente se rompa y se vuelva a juntar.

El invariante correcto **no menciona el número de placas**:

> En cada instante, cada punto de la esfera pertenece a exactamente una placa.

### El plan

1. **Separar propiedad de material.** ✅ *Hecho (commit `f8708c2`).* El ráster del mundo es la única verdad sobre quién posee qué, y la pregunta *"¿es tuyo este punto?"* se le hace al mundo de ahora, nunca a una foto vieja. El material vive en un ráster **por placa**, en el marco de esa placa, leído con una sola rotación acumulada.
2. **Propiedad total y frontera por física.** Ver [abajo](#el-paso-2-en-detalle-por-qué-es-legítimo-y-dónde-no-lo-es).
3. **Ciclo de vida.** Componentes conexas sobre el mapa de IDs: sin celdas, la placa muere; partida en dos, nace una con su propio polo.
4. **Lazo dinámico**, al final: la velocidad angular responde al tirón de la losa en subducción.

**Se tira `ReferenceData` entero.** No es reparable: es el sitio donde están soldadas las dos cosas que hay que separar. Con él se van la recuperación por tolerancia de media celda y la rama de "celda sin resolver que conserva su estado" — ambas existen para tapar huecos que solo aparecen porque la partición está podrida.

**No se toca** el ráster de cubesfera, la isostasia de F2, el nivel del mar, el clima ni el drenaje. Todos leen el mundo y el mundo sigue igual.

### El paso 2 en detalle: por qué es legítimo, y dónde no lo es

*(Escrito porque el usuario pidió garantía explícita de que esto es la forma correcta de calcular la física y no un maquillaje para que se vea bien. Las dos piezas no tienen el mismo grado de legitimidad y conviene no venderlas juntas.)*

**2a. La propiedad es total.** Cada punto de la litosfera pertenece a exactamente una placa: ni a cero ni a dos. Eso no es una aproximación, es la definición de placa. El código actual **viola esa ley** y luego parchea las violaciones — de ahí los tres caminos (recuperación por tolerancia, celda sin resolver, y el respaldo de material). Imponer la ley no es maquillaje: es dejar de romperla, y **elimina los tres parches de golpe**.

Que el criterio sea *el más cercano* sí es una elección de discretización, no una ley. Lo que la hace defendible es que es **imparcial**: no prefiere continental, ni oceánica, ni al dueño anterior. Esas preferencias son justamente lo que generaba los trinquetes.

**Y por sí sola no basta**, que es la parte fácil de colar: en una frontera convergente **la geometría no debe decidir** quién se queda la celda. Lo decide la **densidad** — la oceánica subduce bajo la continental, y entre dos oceánicas subduce la más vieja y fría. O sea: *la geometría decide dónde está la frontera, la física decide qué pasa en ella*. Las dos piezas juntas no son un parche; la 2a sola sí lo sería.

**2b. La clasificación sale de la velocidad relativa**, proyectada sobre la normal de la frontera: se alejan → divergente; se acercan → convergente; se deslizan → transformante. **Así se clasifican las fronteras de placa en geofísica**, y es la razón de que la categoría "transformante" exista. Contar reclamantes es un *sustituto geométrico* de esto que solo funciona si el reparto es perfecto, y en una rejilla nunca lo es.

**La consecuencia fuerte, y es el argumento de verdad:** si la creación en dorsales y la destrucción en fosas salen **del mismo campo de velocidades relativas**, la conservación de corteza **deja de calibrarse y pasa a cumplirse por construcción**. Las placas son rígidas (no cambian de área por dentro) y la esfera es cerrada (área total fija), luego lo que se abre en todas las dorsales iguala lo que se cierra en todas las fosas. Se sigue de la cinemática, no de un ajuste.

Con eso **desaparece el umbral `0,10 × MaxAngularSpeed`** y desaparece el riesgo de que quede sin calibrar. Es el mismo principio que hace fisica la isostasia de F2: el número sale de una ley, no de que un test pase.

**Lo que este paso NO arregla, y no se va a afirmar que sí:**

- **La tierra emergida en 13,2 %.** Es otro problema; puede que no se mueva con esto.
- **Las placas siguen siendo rígidas y con polos de Euler fijos.** La clasificación será correcta *dadas* las velocidades, pero las velocidades siguen sin ser dinámicas. Eso es el punto 4.
- **La frontera conserva ±media celda de imprecisión.** Límite de la rejilla.
- **La península parada.** Se quita la familia de parches donde vive, en vez de adivinar cuál de los tres la produce — tras fallar dos veces adivinando. Es lo correcto aunque el síntoma sobreviva; si sobrevive, se dice.

**Cómo detectar que esto se ha convertido en un parche.** Sería maquillaje si aparece cualquiera de estas:

- elegir al dueño de forma que la tierra emergida quede en un número bonito
- meter un factor que frene el crecimiento continental "porque si no crece demasiado"
- tocar un umbral hasta que pase un test
- preferir continental sobre oceánica **fuera** de una frontera convergente real

### Cómo se verifica

- Balanza creada/destruida ≈ 1, y estable en el tiempo
- Solape < 1 %
- **El área de cada placa cambia con el tiempo.** Hoy es exactamente constante *por construcción*: si tras el arreglo sigue constante, no se arregló nada. Es la prueba directa.
- `OceanicRibbon` en verde — la cinta no vuelve. Es lo que el re-anclaje no podía dar.
- **En el visor de ID de placa: regiones, no ruido.** Es el criterio que descubrió el defecto y el que dice cuándo está cerrado.

### Descartado, y por qué

**Placas como polígonos esféricos con aristas compartidas** (lo que hace GPlates). Teselan por construcción, sin huecos posibles. Descartado por riesgo: mantener esa topología automáticamente, con placas que nacen y mueren, es un problema abierto — en GPlates lo hace un humano a mano. La vía del ráster con los invariantes bien puestos está probada (`platec`, Viitanen).

## A11. Documentos relacionados

- [`SPECS.md`](SPECS.md) — inventario por archivo, con el estado real de cada subsistema.
- [`docs/`](docs/) — visión de producto original. Referencia de **alcance y contenido físico** (qué modelos, qué ecuaciones), no de orden ni fechas.
