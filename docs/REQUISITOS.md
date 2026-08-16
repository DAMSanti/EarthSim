# REQUISITOS.md — Qué tiene que hacer Simu

> **Qué es este documento.** La especificación funcional y física del simulador: *qué* debe existir, con qué modelo, y **cómo se comprueba que está bien**. Es el destilado de toda la documentación de diseño original (`archivo/`), corregido y ampliado con lo que se ha aprendido escribiendo el código.
>
> **Qué NO es.** No es el estado actual (eso es [`SPECS.md`](SPECS.md)), ni el orden de trabajo (eso es [`ROADMAP.md`](ROADMAP.md)), ni el historial de decisiones (eso es [`ANEXO.md`](ANEXO.md)).
>
> **Regla de oro de este documento:** todo requisito trae un **criterio de aceptación medible**. Un requisito que no se puede falsar no es un requisito, es un deseo.

---

## Índice

- [0. Principios de diseño](#0-principios-de-diseño)
- [1. Estructura de datos espaciales](#1-estructura-de-datos-espaciales)
- [2. Geodinámica: tectónica de placas](#2-geodinámica-tectónica-de-placas)
- [3. Isostasia, batimetría y nivel del mar](#3-isostasia-batimetría-y-nivel-del-mar)
- [4. Atmósfera y clima](#4-atmósfera-y-clima)
- [5. Hidrosfera: erosión, sedimento y suelos](#5-hidrosfera-erosión-sedimento-y-suelos)
- [6. Climatología profunda: carbono, albedo y hielo](#6-climatología-profunda-carbono-albedo-y-hielo)
- [7. Biosfera: evolución](#7-biosfera-evolución)
- [8. Renderizado y visualización](#8-renderizado-y-visualización)
- [9. Stack tecnológico](#9-stack-tecnológico)
- [10. Requisitos no funcionales](#10-requisitos-no-funcionales)
- [11. Tabla maestra de invariantes](#11-tabla-maestra-de-invariantes)

---

## 0. Principios de diseño

Estos principios no son estilo: cada uno se ganó su sitio costando trabajo. El detalle de cada episodio está en [`ANEXO.md`](ANEXO.md).

### 0.1 El acoplamiento es el producto

El objetivo no es tener un generador de terreno, un generador de clima y un generador de ríos. Es que **la salida de cada sistema sea la entrada del siguiente, y que el último realimente al primero**:

```
tectónica → relieve → clima → agua → erosión → sedimento
    ↑                                              │
    └──── carga isostática ────────────────────────┘
    ↑                                              │
    └──── meteorización → CO₂ → clima ─────────────┘
```

Un relieve que no se erosiona y una lluvia que no depende de las montañas son dos maquetas independientes, no un simulador. **Cualquier subsistema nuevo debe declarar explícitamente qué lee y quién lee su salida.** Si nadie lee su salida, no se integra todavía (§0.6).

### 0.2 La topografía es emergente, nunca dibujada

Ninguna montaña, costa, río o desierto se coloca a mano ni por ruido decorativo. Todos deben salir de resolver el modelo físico. El ruido procedimental solo se admite como **condición inicial** (rugosidad de partida del Hadeico) o como **detalle sub-celda en el render**, nunca como fuente de estructura.

### 0.3 Estado primario contra estado derivado

Cada magnitud pertenece a una de dos categorías, y hay que declararlo:

| | Definición | Ejemplos |
|---|---|---|
| **Primario** | Se integra en el tiempo. Es el estado que se guarda. | Grosor de corteza, edad de corteza, ID de placa, espesor de sedimento, CO₂ atmosférico, genoma |
| **Derivado** | Se recalcula desde el primario cada paso. **Nunca se le suma nada.** | Elevación, batimetría, nivel del mar, temperatura diagnóstica, caudal |

El motivo es un error real que costó una fase: `BoundaryInteractions` escribía metros de elevación mientras la isostasia reconstruía la elevación entera al final de cada paso. Todo lo que escribía se perdía en el mismo paso ([`ANEXO.md` A9](ANEXO.md#a9-los-dos-puntos-aparcados-de-f1)).

### 0.4 Propiedad y material son cosas distintas

Generalización del hallazgo central de F1 ([`ANEXO.md` A10](ANEXO.md#a10-el-defecto-de-raíz-de-f1-la-propiedad-y-el-material)), y aplica a todo campo que se transporte:

- **La propiedad es una partición.** «¿De quién es esta celda?» Debe seguir siendo una partición en todo momento, y para eso hay que **re-deducirla del estado actual en cada paso**. Deducida de una foto vieja, degenera en ruido.
- **El material es una sustancia.** «¿Qué hay aquí?» **No se puede remuestrear repetidamente sin deshilacharlo**; hay que leerlo una sola vez desde su propio marco, con la transformación acumulada.

Uno exige encadenar, el otro exige no encadenar nunca. **Nunca deben compartir estructura de datos.** Esta regla vale igual para el sedimento sobre la roca, para el agua sobre el terreno y para la población sobre el bioma.

### 0.5 Ninguna fase está hecha si no se ve en pantalla

Un test dice «no ha petado». Mirar el planeta dice «esto no parece la Tierra». Los tres bugs más caros del proyecto sobrevivieron meses porque nadie podía *ver* lo que el código hacía.

Esto obliga a separar dos cosas que suelen mezclarse:

| | Qué es | Cuándo |
|---|---|---|
| **Visualización de diagnóstico** | Campos escalares y vectoriales sobre el globo, con leyenda, conmutables, unlit. Barata, sin LOD | Existe desde el principio, y **cada fase registra sus campos** |
| **Renderizado de producto** | LOD hasta la superficie, materiales PBR, luz, atmósfera | Fase propia, al final |

**Requisito duro:** todo campo que produzca cualquier subsistema se registra en el visor con etiqueta, unidad, paleta y escala. Coste de añadir un campo: registrar una lambda.

### 0.6 No se construye infraestructura sin consumidor

El proyecto acumuló ~1300 líneas de andamiaje GPU (buffers, SRV/UAV, cuatro *global shaders*, dos `.usf` de 300+ y 450+ líneas) donde **todos los `Dispatch` estaban vacíos**, y 693 líneas de física de fronteras que se ejecutaban cada paso sobre 6·Res² celdas para producir un resultado que nadie leía.

**Regla:** una capa se construye cuando existe el código que la va a llamar, no antes. Corolario: **medir antes de optimizar** — la intuición sobre dónde está el coste ha fallado dos veces de dos, y perfilar encontró el cuello de botella en un sitio inesperado las dos veces.

### 0.7 Reglas sobre los tests

1. **Un test que solo se ha visto pasar no demuestra nada.** Hay que sabotear el código y comprobar que se pone rojo. Tres tests del proyecto pasaban sin comprobar nada.
2. **Cota por los dos lados.** Un límite de un solo lado sobre una magnitud que puede irse en ambos es media comprobación (dejó pasar un desplome de tierra emergida del 24,6 % al 11,3 %).
3. **Un test en rojo no prueba lo que dice su nombre.** Antes de creerlo, hay que leer la aserción concreta que falla. Un nombre que no describe su aserción es *peor* que no tener test: induce a error con la autoridad de una medición. Ha costado dos días una vez.
4. **Si un test contradice lo que se ve en pantalla, la sospecha va primero al test.**
5. **Antes de diferir un defecto, hay que nombrar el mecanismo que lo arreglará.** Si no se puede nombrar, no está diferido: está sin arreglar.

---

## 1. Estructura de datos espaciales

### 1.1 El problema

No existe forma de mapear una esfera a un plano rectangular sin distorsión (*Theorema Egregium* de Gauss). La esfera UV clásica concentra celdas infinitamente estrechas en los polos, y la condición CFL obliga entonces a pasos de tiempo minúsculos para todo el planeta.

### 1.2 Estructura elegida: cubo esférico normalizado

| Estructura | Uniformidad | Adyacencia | Afinidad GPU | Veredicto |
|---|---|---|---|---|
| Esfera UV | Pésima (singularidad polar) | Variable | Alta | ❌ Descartada |
| Icosfera | Excelente | 12 pentágonos irregulares | Baja | ⚠️ Solo para agentes |
| HEALPix | Perfecta (iso-área) | Compleja | Media | ⚠️ Solo análisis científico |
| **Cubo esférico** | Buena (<50 % de distorsión) | Regular, 4 vecinos | Óptima (*cube maps* nativos) | ✅ **Seleccionado** |

**R1.1** — Seis caras de Res×Res celdas, proyectadas por normalización del vector de posición. El estado del planeta es siempre `6 × Res²` valores por campo.

**R1.2 — Fuente única del mapeo cara↔dirección.** La tabla de ejes por cara vive en **un solo sitio**. Prohibido escribirla a mano en ningún otro archivo.

> Estaba escrita a mano **8 veces** y ya se había desincronizado: dos caras tenían la V invertida, lo que dejaba el mapa de placas y el de elevación **espejados en los casquetes polares**. Una migración deliberada, buscándolas a propósito, dejó 2 de 8 sin migrar porque su texto no era idéntico.

**R1.3 — Convenio dextrógiro.** `AxisU × AxisV = +Normal` en las seis caras. El convenio antiguo era levógiro en dos caras, lo que invertía el *winding* de los triángulos y obligaba a un `bFlipWinding` compensatorio.

**R1.4 — Vecindad geométrica, no tabular.** El vecino de una celda de borde se obtiene proyectando la dirección 3D del vecino y buscando en qué cara cae, **no** con una tabla de rotaciones de aristas escrita a mano. Las tablas se desincronizan; la geometría no.
*Criterio:* recorrer las 12 aristas del cubo comprobando que `vecino(vecino(c, d), −d) == c` y que la distancia angular entre celdas vecinas nunca supera 1,5× el tamaño de celda.

**R1.5 — Corrección métrica.** Un cubo esférico no es iso-área: una celda de esquina cubre ~40 % menos superficie que una de centro de cara. Todo flujo, toda integral y toda conservación de masa se multiplica por el **factor de escala métrica** precalculado por celda.
*Criterio:* la suma de áreas de celda iguala 4πR² con error < 0,1 %; el solver de difusión conserva masa total con deriva < 10⁻⁶ por cada 1000 pasos.

**R1.6 — Resoluciones desacopladas.** La resolución del ráster de simulación, la de la malla de visualización y la de las texturas de render son **tres parámetros independientes**. Ninguno debe estar cableado a otro.

### 1.3 Organización del estado

Todos los campos comparten forma (`6 × Res²`) y cambian solo en significado y unidad. Se agrupan por dominio para poder subirlos a GPU como *texture arrays* de alta precisión cuando llegue el momento:

| Buffer | Contenido | Tipo |
|---|---|---|
| **Geológico** | ID de placa, tipo de corteza, grosor de corteza (m), edad de corteza (Ma), elevación derivada (m) | primario salvo elevación |
| **Hidrológico** | Lámina de agua (m), velocidad de flujo (2D), sedimento suspendido (m), espesor de sedimento depositado (m), salinidad | primario salvo caudal |
| **Atmosférico** | Altura geopotencial, viento (2D), temperatura (°C), humedad específica, cobertura nubosa, hielo | primario salvo precipitación |
| **Edáfico/ecológico** | N, P, K en suelo, materia orgánica, humedad de suelo, biomasa vegetal, bioma | primario |

---

## 2. Geodinámica: tectónica de placas

El sistema de referencia del proyecto. Todo lo demás está aguas abajo.

### 2.1 Invariante fundamental

> **En cada instante, cada punto de la esfera pertenece a exactamente una placa.**

Obsérvese lo que **no** dice: no menciona el número de placas. El número no se conserva en la Tierra —la Farallón se subdujo casi entera dejando Juan de Fuca, Cocos y Nazca; India y Australia se separaron; el Rift de África Oriental está fabricando una ahora mismo— y no debe conservarse aquí.

**R2.1** — La partición se comprueba **cada paso**: cero celdas sin dueño, cero celdas con dos dueños simultáneos en el estado resuelto.

### 2.2 Génesis de la corteza (Hadeico)

**R2.2** — Fracturación inicial en N placas por **Voronoi esférico exacto**, con centroides por *Poisson disc sampling* para que no queden placas degeneradas.
*Nota:* el Jump Flooding Algorithm es la implementación paralela clásica, pero **dejaba entre el 11 % y el 19 % de celdas sin asignar** a resoluciones bajas. Cualquier implementación acelerada debe validarse contra la fuerza bruta exacta: cobertura del 100 %, sin excepción.

**R2.3 — Fronteras orgánicas.** Los bordes de placa reales no son arcos limpios: son fractales, porque cada uno es la cicatriz de rupturas previas. La frontera inicial se perturba con ruido fractal de varias octavas.
*Criterio:* dimensión fractal del contorno de placa entre 1,1 y 1,3 (una circunferencia da 1,0).

**R2.4 — Propiedades de placa:**

| Propiedad | Tipo | Notas |
|---|---|---|
| ID | entero | único, **estable** mientras la placa exista |
| Polo de Euler | vector unitario | eje de rotación sobre la esfera |
| Velocidad angular | escalar (rad/Ma) | **escribible**: es el estado dinámico, no una constante |
| Tipo de corteza dominante | oceánica / continental | por celda, no por placa: una placa real tiene de las dos |

**R2.5 — Velocidades realistas.** Entre 1 y 15 cm/año (Nazca y Pacífico son las rápidas; Eurasia y África, lentas). Cualquier calibración temporal debe partir de ahí.

### 2.3 Cinemática

**R2.6** — El movimiento es **rotación de cuerpo rígido sobre la esfera**: rotación por cuaternión alrededor del polo de Euler. La velocidad lineal de una celda es `v = ω × r`, que es cero en el polo de Euler y máxima a 90° de él.

**R2.7 — Paso de advección acoplado a la geometría, no al framerate.** La advección se ejecuta cuando el desplazamiento acumulado alcanza **~1 píxel del ráster**, no cada frame. Advectar por debajo de eso no mueve nada y solo añade error de remuestreo.
*Criterio:* el resultado a 1000 Ma debe ser indistinguible corriendo a 30, 60 y 144 fps y con `TimeScale` distinto.

**R2.8 — El reloj no puede mentir.** Si un paso trocea el desplazamiento y agota un techo de sub-pasos, el tiempo sobrante **no puede descartarse en silencio** mientras el contador de tiempo simulado suma el paso entero. Se mide el tiempo descartado **en la línea donde se descarta**, no restando totales.
*Criterio:* tiempo descartado = 0,00 Ma sobre una corrida larga, con `TimeScale` alto. Si no es cero, todas las edades de corteza y todas las tasas del modelo están mal escaladas.

### 2.4 Propiedad y material (aplicación de §0.4)

**R2.9 — La propiedad se decide contra el mundo actual.** La pregunta «¿es tuyo este punto?» se le hace al ráster del mundo de *ahora*, con la rotación **incremental** de esta advección. El mundo es una partición por construcción, así que preguntarle a él no puede degenerar.

**R2.10 — El material vive en el marco propio de su placa.** Cada placa guarda su corteza (edad, tipo, grosor) en un ráster en su propio sistema de referencia, leído con la rotación **acumulada**: **un solo remuestreo** por muchas advecciones que pasen.
*Criterio:* la longitud de cualquier artefacto de deshilachado debe seguir al **tiempo simulado**, no al **número de advecciones**. Si se acorta el paso a la mitad y el artefacto se duplica, el material se está encadenando.

**R2.11 — Las lecturas de material encadenadas se instrumentan.** Cuando el marco de una placa no tiene material en el punto pedido y hay que caer a leer del mundo anterior, eso es exactamente lo que el marco viene a evitar. Se cuenta y se vigila.
*Criterio:* fracción de respaldos < 2 % de las lecturas, y **estable en el tiempo** (si crece, la separación se está deshaciendo).

### 2.5 La frontera como objeto de primera clase

**R2.12** — La frontera de placa **existe como entidad**, no se infiere a posteriori contando reclamantes por celda.

Dos motivos, y el segundo es el grave:

- **El coste está invertido.** Calcular con precisión perfecta el interior de la placa —que solo rota, y es la parte aburrida— cuesta 6·Res²·N rotaciones por advección, mientras que la parte interesante sale de los restos.
- **En la Tierra la tectónica *es* la frontera.** Rifts, subducciones, orogenias y transformantes ocurren todos en una línea de anchura kilométrica. El interior de una placa solo se traslada.

**R2.13 — Clasificación por velocidad relativa, no por conteo.** Para cada segmento de frontera entre las placas A y B, se proyecta la velocidad relativa `v_A − v_B` sobre la **normal de la frontera**:

| Signo y magnitud | Régimen | Consecuencia |
|---|---|---|
| Convergente (normal negativa) | **Subducción** si al menos una es oceánica | La densa baja; vulcanismo de arco; fosa |
| Convergente | **Orogenia** si ambas son continentales | El material se apila: engrosamiento, no destrucción |
| Divergente (normal positiva) | **Rift / dorsal** | Corteza oceánica nueva, edad 0, grosor oceánico |
| Tangencial dominante | **Transformante** | Fricción, sismicidad, **ni crea ni destruye corteza** |

Esto funciona con bandas de **una celda de ancho** y distingue una transformante de un rift, cosa que contar huecos no puede hacer: al discretizar, las dos dejan huecos.

**R2.14 — Reglas de subducción por densidad:**
- Oceánica vs. continental → subduce la oceánica, **siempre** (2900 vs. 2750 kg/m³).
- Oceánica vs. oceánica → subduce la **más vieja**, porque el hundimiento térmico la ha hecho más densa y fría.
- Continental vs. continental → **ninguna subduce**: el material se apila (Himalaya).

**R2.15 — Acreción de arco.** La corteza continental necesita una fuente o solo puede perderse. En la Tierra crece por magmatismo en las zonas de subducción (Andes, Japón): la corteza oceánica que converge se engrosa y, superado un umbral (~20 km), deja de subducir y pasa a continental.
*Restricción medida:* debe limitarse a celdas que **tocan continente de otra placa** — la cabalgante, no cualquier corteza continental cercana. Sin la restricción por tipo, satura: con tiempo suficiente cualquier celda convergente supera el umbral, y bajar la tasa a la mitad no cambia nada (222 % → 220 % de área continental). El límite no era el ritmo, era la superficie afectada. Restringir solo por tipo tampoco basta del todo (16-08-2026): una celda oceánica puede tocar continente de **su propia placa** ya convertido un paso antes, y el frente se propaga solo sin que el margen real importe. Exigir placa distinta ancla la acreción al contacto real con la cabalgante — mejora medida (74,3 % → 68,3 % de cobertura continental final en el fixture de prueba), pero no basta por sí sola para que el área continental converja a un equilibrio: sospecha razonable de que hace falta ciclo de vida de placas (R2.16) para que un margen que ya consumió su placa oceánica deje de alimentar la acreción.

### 2.6 Ciclo de vida de placas

**R2.16** — Una placa puede **nacer** y puede **morir**. Se recalcula por componentes conexas sobre el mapa de IDs cada N advecciones:

- **Muerte:** una placa cuyo territorio cae por debajo de un umbral (o a cero) se elimina; su corteza restante pasa a la vecina que la absorbe.
- **Nacimiento por rift:** una placa cuyo territorio queda partido en dos componentes conexas se divide; la componente menor recibe **ID nuevo y polo de Euler propio**, derivado de la geometría del rift.
- **Fusión:** dos placas cuya frontera común lleva mucho tiempo sin movimiento relativo pueden soldarse (sutura).

*Criterio de aceptación, y es la prueba directa:* **el área de cada placa debe cambiar con el tiempo.** Si el área de las placas es constante, la geometría está congelada por construcción y no se ha arreglado nada, digan lo que digan las demás métricas.

### 2.7 Dinámica: el lazo que falta

**R2.17 — La velocidad angular responde a las fuerzas.** Sin esto hay cinemática, no dinámica: no hay estados de equilibrio, nada regula la fracción de tierra, y ningún supercontinente se rompe y se vuelve a juntar solo.

Motores, por orden de importancia real en la Tierra:

| Fuerza | Modelo mínimo | Efecto |
|---|---|---|
| **Tirón de la losa** (*slab pull*, ~dominante) | ∝ longitud del margen en subducción × edad media de la losa | Una placa con margen largo en subducción **acelera**; si pierde su losa, **frena** |
| **Empuje de dorsal** (*ridge push*) | ∝ longitud de dorsal | Secundario, ~10× menor |
| **Arrastre basal** | ∝ −área × velocidad | Amortiguamiento; frena a los continentes grandes |

**R2.18 — Convección del manto como campo lento.** Un campo de esfuerzo de gran escala, con celdas de convección que se reorganizan en escalas de ~100–200 Ma. Es lo que produce el **ciclo de Wilson** sin que haya que programar «formar Pangea».

*Criterio:* con la dinámica activa, la simulación debe producir agregación y dispersión de supercontinentes con periodo del orden de **300–500 Ma** (el ciclo real ronda los 400).

### 2.8 Vulcanismo y puntos calientes

**R2.19** — Los *hotspots* están anclados al **marco del manto**, no a las placas. Por eso dejan cadenas de islas (Hawái–Emperador) cuya geometría registra el movimiento pasado de la placa que pasa por encima.

**R2.20** — El vulcanismo aporta CO₂ a la atmósfera (§6) y engrosa corteza localmente. Tres fuentes: arco de subducción, dorsal y punto caliente. Las **grandes provincias ígneas** (LIP: Deccan, Siberian Traps) son eventos raros y masivos, y son el gatillo de varias extinciones reales: candidatas a evento discreto.

### 2.9 Criterios de aceptación de la geodinámica

| Invariante | Valor objetivo | Por qué |
|---|---|---|
| Partición completa | 100 % de celdas con exactamente un dueño | §2.1 |
| Balanza creación/destrucción de corteza | 0,95 – 1,05, **estable en el tiempo** | Esfera cerrada: lo que se crea en dorsales se destruye en fosas |
| Celdas sin resolver | < 0,01 % | Cada una es una celda que se queda rancia mientras su entorno se renueva |
| Solape (≥2 reclamantes) | < 1,5 % | Los huecos y solapes **son** tectónica (en una dorsal sobra sitio de verdad), pero deben ocurrir en una banda de una celda, no en el 28 % del planeta |
| Área por placa | **variable** en el tiempo | §2.6 |
| Edad máxima de corteza oceánica | 150–250 Ma | En la Tierra **no hay** fondo oceánico más viejo de ~180 Ma: se recicla entero. Es la validación más limpia de que el reciclado funciona |
| Distribución de edad oceánica | monótona decreciente, casi triangular | Consecuencia de crear a ritmo constante y destruir al azar |
| Fracción de tierra emergida | 25–35 % (Tierra: 29 %) | Y **debe converger al subir resolución** |
| Grosor continental medio | 33–42 km (`ContinentalThickness` ± 20 %) | Un grosor medio muy por encima del inicial delata que algo apila corteza sin control |
| Velocidad de placa | 1–15 cm/año | §2.5 |
| Periodo del ciclo de Wilson | 300–500 Ma | §2.7 |

---

## 3. Isostasia, batimetría y nivel del mar

No estaba en la documentación original y es **la pieza que hace que la geodinámica produzca un planeta y no un mapa de alturas**.

### 3.1 El cambio de estado primario

**R3.1** — El estado primario es el **grosor de corteza**; la elevación se **deriva** por flotación.

Dos problemas que esto resuelve, y ninguno es cosmético:

1. Con la elevación como primario, una colisión continente-continente no tiene dónde apilar el material de la celda perdedora y **la destruye**: el planeta perdía ~29 % de corteza continental cada 200 Ma.
2. Una montaña erosionada **desaparecería** en vez de rebotar isostáticamente, que es lo que hace una montaña real.

### 3.2 Flotación de Airy

**R3.2** — Una columna de corteza de grosor `T` y densidad `ρc` sobre un manto de densidad `ρm` sobresale:

$$h = T \cdot \frac{\rho_m - \rho_c}{\rho_m} - \text{datum}$$

Con densidades **reales** (continental 2750, oceánica 2900, manto 3300 kg/m³) y 35 km de corteza salen 5.833 m; restando el datum, **+840 m**, que es la altura media real de los continentes.

> Que el número correcto salga de densidades reales y no de una constante ajustada a ojo es la validación de la fórmula. Es el patrón a seguir en todo el proyecto.

**R3.3 — Techo por delaminación.** El grosor se acota en ~75 km: por encima, la raíz cortical se vuelve inestable y se desprende. No es un *clamp* arbitrario — 75 km por Airy da ~7.507 m, y el Everest está en 8.849.

### 3.3 Batimetría por hundimiento térmico

**R3.4** — El fondo oceánico no está a profundidad fija: se hunde al enfriarse, siguiendo muy bien la ley empírica

$$d = d_0 + K\sqrt{\text{edad en Ma}}$$

con `d₀ ≈ 2500 m` (profundidad de dorsal) y `K ≈ 350`, estabilizándose hacia 5.750 m.

**Consecuencia que hay que poder ver:** un mapa de profundidad oceánica real es esencialmente **un mapa de la edad del fondo**. Si el campo de edad no tiene consecuencia visible en la batimetría, el acoplamiento no está hecho.

### 3.4 Nivel del mar

**R3.5** — El nivel del mar es **explícito y global**, resuelto cada paso conservando el **volumen de océano**.

**R3.6 — Debe responder a la tectónica.** Mucha dorsal joven ⇒ mucha corteza caliente ocupando volumen ⇒ cuencas menos hondas ⇒ agua desplazada que **inunda continentes**. Es el mecanismo real de los grandes mares epicontinentales del Cretácico.

**R3.7 — Resolución por Newton, no por bisección.** El nivel apenas se mueve entre pasos (el valor anterior ya es buena estimación) y la derivada es gratis: `dV/dS` es el área sumergida, que se cuenta en la misma pasada. La bisección hacía 40 iteraciones × 393.000 celdas = **15,7 M de lecturas por paso para resolver un único número**; Newton converge en 2–3.

### 3.5 Criterios de aceptación

| Invariante | Valor objetivo |
|---|---|
| Altura de 35 km de corteza continental | +840 ± 100 m sin tocar el datum |
| Volumen de océano | conservado, deriva < 0,1 % en 1000 Ma |
| **Curva hipsométrica bimodal** | Dos modos claros: plataforma continental (~0 a +1 km) y llanura abisal (−4 a −5 km). La Tierra tiene esta forma y un planeta con relieve inventado no la tiene |
| Correlación profundidad ↔ √edad | r > 0,8 sobre celdas oceánicas |
| Fracción bajo el techo isostático | < 1 % de la tierra tocando `MaxThickness` |

---

## 4. Atmósfera y clima

El clima debe responder **dinámicamente** a la geografía cambiante (tectónica) y a la composición química (§6). Se ataca en dos escalones deliberadamente separados.

### 4.1 Escalón 1 — Clima diagnóstico (barato)

**Por qué existe:** la erosión hidráulica necesita **caudal**, y el caudal necesita **precipitación**. Sin un clima aunque sea barato, la erosión tendría que inventarse una lluvia uniforme, que es justo lo que impide que se formen desiertos, sombras de lluvia y cuencas realistas.

**R4.1 — Temperatura = f(latitud, altitud).** Gradiente adiabático de **6,5 °C/km**. Referencias: ecuador 27 °C, 45° ≈ 1 °C, polo −25 °C.
*Consecuencia a verificar:* a ~6 km sobre el ecuador se baja de 0 °C. **Por eso hay glaciares ecuatoriales** (Kilimanjaro, Andes). Si no salen, el gradiente no está aplicado.

**R4.2 — Viento por bandas alternantes.** Alisios del este (0–30°), oestes (30–60°), del este otra vez cerca del polo.
*Por qué importa la alternancia y no solo la magnitud:* **es lo que decide qué ladera de una cordillera es barlovento**, y por tanto de qué lado cae el desierto. Un viento monótono pone todos los desiertos del mismo lado del planeta.

**R4.3 — Humedad transportada a barlovento**, con **continentalidad**: lejos del mar llega menos humedad aunque no haya ninguna montaña de por medio.

**R4.4 — Perfil de precipitación por bandas, NO monótono:**

| Banda | mm/año | Realidad |
|---|---|---|
| Ecuatorial (0–10°) | ~2500 | Rama ascendente de Hadley: el aire sube, se enfría y descarga |
| **Subtropical (20–35°)** | **~200** | El aire seco **desciende**. Aquí están Sahara, Arabia, Kalahari, Atacama y los desiertos australianos |
| Latitudes medias (40–60°) | ~1100 | Frente polar, convergencia de masas de aire |
| Polar (>70°) | ~150–200 | El aire frío casi no retiene vapor |

> **El mínimo subtropical es el requisito, no un detalle de ajuste.** Es lo que separa un planeta con desiertos de una bola con lluvia que decrece suavemente hacia los polos. Un test debe comprobar explícitamente que el perfil **no** es monótono.

**R4.5 — Precipitación orográfica y sombra de lluvia.** Ganancia por ascenso forzado (~900 mm/año por km de barrera) y pérdida fraccional de humedad por km superado a barlovento (~45 %/km), acumulada sobre ~300 km aguas arriba.
*Criterio medido sobre terreno simulado real:* sobre ≥50 cordilleras detectadas, el sotavento debe ser más seco en **>85 %** de los casos, con un contraste medio barlovento/sotavento **>3×**.

### 4.2 Escalón 2 — Atmósfera dinámica (Shallow Water Equations)

**R4.6** — Modelo de aguas someras sobre la esfera: la atmósfera como capa delgada de fluido.

**Conservación de momento:**

$$\frac{\partial \mathbf{u}}{\partial t} + (\mathbf{u}\cdot\nabla)\mathbf{u} = -g\nabla h - f\,\mathbf{k}\times\mathbf{u} + \nu\nabla^2\mathbf{u} - C_d\frac{\mathbf{u}|\mathbf{u}|}{H}$$

**Conservación de masa:**

$$\frac{\partial h}{\partial t} + \nabla\cdot(h\mathbf{u}) = 0$$

**R4.7 — Coriolis.** `f = 2Ω sin φ`, con Ω = 7,292·10⁻⁵ rad/s. **Es el requisito, no un adorno**: sin Coriolis el aire va en línea recta del ecuador a los polos y no hay circulación de bandas.
*Criterio de emergencia:* deben salir **solas** tres celdas por hemisferio (Hadley, Ferrel, polar), con la zona de convergencia intertropical y los cinturones de alta presión subtropical en su sitio.

**R4.8 — Estabilidad numérica.** Condición CFL respetada sobre la celda **más pequeña** del cubo esférico (esquina de cara). Se admiten métodos semi-implícitos o semi-lagrangianos y difusión numérica explícita y acotada. Filtro polar innecesario — esa es precisamente la ventaja del cubo esférico.
*Criterio:* energía cinética total acotada durante 10⁶ pasos; ningún NaN; sin ruido de rejilla de dos celdas.

**R4.9 — Fricción superficial dependiente del terreno.** Rugosidad distinta sobre océano, llanura, bosque y montaña.

### 4.3 Termodinámica

**R4.10 — Balance de energía por celda:**

$$C\frac{\partial T}{\partial t} = S(1-\alpha) - \text{OLR}(T, \text{CO}_2) + \nabla\cdot(\text{transporte}) + L\cdot(\text{condensación})$$

- **Insolación** `S`: constante solar 1361 W/m², modulada por ángulo cenital según latitud, hora y **estación**.
- **Inclinación axial** de 23,44°, parametrizable. Las estaciones son requisito: sin ellas no hay monzones ni ciclos de congelación.
- **OLR**: linealización de Budyko `OLR = A + B·T` (A ≈ 203,3 W/m², B ≈ 2,09 W/m²/K) como base, con A dependiente del CO₂ (§6).
- **Capacidad térmica** distinta para océano y tierra: **es la causa de la continentalidad y de los monzones**, no un detalle.

**R4.11 — Ciclo del agua cerrado.**
- **Evaporación** sobre océano y suelo húmedo, ∝ déficit de saturación y viento.
- **Saturación (Clausius-Clapeyron)**: `e_s(T) = 611,2 · exp(17,67·T / (T+243,5))` Pa. La consecuencia que hay que ver: **el aire caliente retiene ~7 % más vapor por cada °C**, y por eso los polos son desiertos pese a estar helados.
- **Precipitación** cuando la humedad supera la saturación; **nieve** por debajo de 0 °C.
*Criterio duro:* **balance de masa de agua cerrado.** Evaporación total − precipitación total − cambio de almacenamiento = 0, con deriva < 0,1 %/1000 pasos. Sumando océano, hielo, humedad atmosférica, agua de suelo y lámina superficial.

**R4.12 — Corrientes oceánicas y transporte de calor.** Aunque sea un modelo reducido: giros forzados por el viento y una circulación termohalina simplificada por diferencia de densidad (temperatura + salinidad).
*Por qué es requisito y no lujo:* el océano transporta **~1 PW** de calor hacia los polos. Sin él los polos salen demasiado fríos y los trópicos demasiado calientes, y todos los biomas quedan desplazados.

### 4.4 Fenómenos que deben emerger, no programarse

| Fenómeno | Causa que debe producirlo |
|---|---|
| Sombra de lluvia / desiertos costeros | Orografía + dirección del viento de banda |
| Monzones | Contraste térmico tierra-mar + estaciones |
| Cinturón de desiertos a 30° | Rama descendente de Hadley |
| Corriente en chorro | Coriolis + gradiente térmico meridional |
| Clima mediterráneo | Migración estacional de los cinturones |
| Efecto de continentalidad | Capacidad térmica + distancia al mar |

---

## 5. Hidrosfera: erosión, sedimento y suelos

La erosión hace tres cosas, y solo la primera es visual: esculpe el relieve, **crea suelo fértil** para la biosfera y **regula el ciclo del carbono** a largo plazo.

### 5.1 Drenaje

**R5.1 — Dirección de flujo D8 con corrección de distancia diagonal.** Cada celda vierte a la vecina de mayor pendiente. **La corrección diagonal es obligatoria**: sin ella el drenaje prefiere sistemáticamente las diagonales y las redes salen sesgadas a 45°.

**R5.2 — Acumulación de flujo en una sola pasada**, recorriendo las celdas de mayor a menor altura: cuando le toca a una celda ya ha recibido todo lo de aguas arriba. `O(N log N)` por el orden, no `O(N²)`.

**R5.3 — Caudal en unidades reales.** m³/año = precipitación (mm/año) × área de celda corregida por métrica − evapotranspiración. Un número sin escala no se puede calibrar contra nada.

**R5.4 — Tratamiento de depresiones.** Las depresiones se resuelven por *priority-flood*: se llenan hasta rebosar, formando **lagos** con nivel propio, y se propaga el flujo por el punto de vertido. Un sumidero sin tratar corta la red aguas abajo.
*Criterio:* < 1 % de celdas de tierra sin salida al mar tras el tratamiento.

**R5.5 — Continuidad entre caras.** El drenaje cruza las aristas del cubo. Cualquier discontinuidad de vecindad corta ríos exactamente en las 12 aristas, y se ve.

### 5.2 Incisión fluvial

**R5.6 — Ley de potencia de corriente (*stream power*):**

$$\frac{\partial z}{\partial t} = U - K\,A^{m}S^{n}$$

con `A` área drenada, `S` pendiente local, `m ≈ 0,5`, `n ≈ 1`, y `K` la erodibilidad **dependiente del tipo de roca** (granito ≪ sedimentaria).

**Consecuencia esperada:** la **relación pendiente-área** de las redes reales, `S ∝ A^(−m/n)` — es decir, log S contra log A da una recta de pendiente ≈ −0,5. Es la firma cuantitativa de una red fluvial real y sirve de test.

**R5.7 — Alternativa admitida: modelo de tuberías virtuales (*pipe model*).** Más caro pero local, y por tanto trivialmente paralelizable en GPU: lámina de agua explícita, flujo entre vecinas por diferencia de altura hidrostática (terreno + agua), capacidad de transporte `C = f(velocidad, pendiente)`, erosión si el sedimento va por debajo de C y deposición si va por encima, más evaporación.
**Decisión:** empezar por *stream power* (barato, escala geológica) y reservar el *pipe model* para cuando haya GPU y se quiera detalle de evento.

### 5.3 Transporte y deposición de sedimento

**R5.8** — El sedimento es una **sustancia transportada** (§0.4): tiene su propio campo de espesor, distinto del grosor de roca madre, y **no se remuestrea con la propiedad**.

**R5.9 — Deposición donde el transporte pierde capacidad:** salida de montaña (abanicos aluviales), llanura de baja pendiente (llanuras aluviales, meandros) y desembocadura (**deltas**).

**R5.10 — Erosión termal / *mass wasting*.** Término difusivo sobre el relieve: la ladera demasiado empinada se derrumba. Es **anterior e independiente** de la erosión hidráulica y la ley de potencia se suma encima, no lo sustituye.
*Aviso de calibración:* forma un equilibrio con la orogenia. Tocar uno obliga a revisar el otro. Y aplicado como suavizado completo cada paso durante miles de pasos, **converge a un planeta perfectamente liso** — tiene que ser una tasa acotada por unidad de tiempo simulado, no una pasada de filtro por frame.

### 5.4 Realimentación isostática

**R5.11** — La erosión y el sedimento **modifican el grosor de corteza**, y por tanto la elevación se recalcula por §3:

- Erosionar una montaña la aligera y **rebota** (por eso los Apalaches siguen ahí después de 300 Ma).
- Depositar sedimento en una cuenca la **hunde**, haciéndole sitio a más sedimento: es lo que crea cuencas sedimentarias de kilómetros de espesor.

Sin este lazo la erosión es un filtro de imagen. Con él, es geología.

### 5.5 Estratigrafía y suelos

**R5.12 — Modelo de capas (vóxeles 2.5D):**

| Capa | Composición | Origen |
|---|---|---|
| **Roca madre** | Granito (continental), basalto (oceánica), sedimentaria (litificada) | Tectónica |
| **Regolito / sedimento** | Arena, limo, arcilla | Meteorización física + deposición |
| **Orgánica (humus)** | Materia orgánica | Descomposición biológica (§7) |

**R5.13 — Firma química NPK.** Cada paquete de sedimento transporta concentraciones de **nitrógeno, fósforo y potasio**. Consecuencia buscada: los valles fluviales y los deltas son **agrícolamente productivos** en la simulación biológica, igual que en la Tierra, y sin que nadie lo haya puesto a mano.

**R5.14 — Meteorización dependiente del clima.** La tasa de degradación de roca a suelo crece con temperatura y humedad (por eso el suelo tropical es profundo y el ártico es fino). **Este mismo flujo alimenta el ciclo del carbono** (§6): es el mismo proceso visto desde otro lado.

### 5.6 Criterios de aceptación

| Invariante | Valor objetivo |
|---|---|
| Redes **dendríticas** | Visualmente ramificadas, no radiales ni en rejilla |
| Pendiente-área | log S vs log A con pendiente −0,4 a −0,6 |
| **Ley de Hack** | `L ≈ 1,4 · A^0,6` entre longitud del cauce principal y área de cuenca |
| Fracción de celdas cauce | satura al subir resolución (la topología está resuelta) |
| Sumideros tras tratamiento | < 1 % |
| Montaña aislada sin levantamiento | se **degrada**, no crece indefinidamente |
| Deltas | presentes en las desembocaduras de los cauces mayores |
| Balance de sedimento | erosionado = transportado + depositado, deriva < 1 % |

---

## 6. Climatología profunda: carbono, albedo y hielo

El planeta debe reaccionar a cambios de escala geológica de forma científicamente correcta, y estos tres lazos son los que hacen que el clima **tenga historia** en vez de ser una función de la geografía.

### 6.1 El termostato: ciclo carbonato-silicatos

**R6.1** — El regulador de la temperatura terrestre a largo plazo. Dos flujos:

| Proceso | Efecto | Mecanismo |
|---|---|---|
| **Desgasificación volcánica** | Calentamiento | Emite CO₂; ∝ longitud de dorsal + actividad de arco + LIPs |
| **Meteorización de silicatos** | Enfriamiento | Lluvia + roca silicatada **secuestra** CO₂, que acaba en carbonatos en el fondo marino |

**R6.2 — La meteorización depende del clima, y eso cierra el lazo.** Forma tipo GEOCARB:

$$W \propto q \cdot \exp\!\left(\frac{T - T_0}{13,7}\right) \cdot f(\text{relieve, tipo de roca, cobertura vegetal})$$

con `q` la escorrentía, que sale directamente de §5.

**Cadena causal que debe funcionar sola:**

```
orogenia masiva → más relieve → más meteorización → baja el CO₂ → el planeta se enfría
alta actividad volcánica → sube el CO₂ → el planeta se calienta → más meteorización → se compensa
```

*Criterio:* ante un aumento **escalón** de la desgasificación, la temperatura debe estabilizarse en un valor nuevo (no dispararse), con constante de tiempo del orden de **0,5–1 Ma**. Es la prueba de que el lazo negativo funciona.

**R6.3 — Forzamiento radiativo del CO₂:** `ΔF = 5,35 · ln(C/C₀)` W/m², con sensibilidad climática ~3 °C por duplicación.

### 6.2 Albedo dinámico

**R6.4** — El albedo de cada celda se calcula desde la superficie real:

$$\alpha = \alpha_{\text{hielo}}f_{\text{hielo}} + \alpha_{\text{agua}}f_{\text{agua}} + \alpha_{\text{tierra}}(\text{bioma})f_{\text{tierra}} + \text{nubes}$$

| Superficie | Albedo |
|---|---|
| Hielo / nieve fresca | 0,6 – 0,9 |
| Agua (ángulo cenital alto) | ~0,06 |
| Desierto / arena | ~0,35 |
| Pradera | ~0,20 |
| Bosque (¡oscuro!) | 0,08 – 0,15 |
| Nubes | 0,5 – 0,7 |

**Nota que importa para §7:** un bosque boreal es **más oscuro** que la tundra que sustituye. La biosfera modifica el albedo, y por tanto el clima. Ese es el acoplamiento de vuelta.

### 6.3 Realimentación hielo-albedo y sus bifurcaciones

**R6.5** — Lazo **positivo**, y por tanto capaz de producir transiciones bruscas:

```
se enfría → avanza el hielo → sube el albedo → se absorbe menos energía → se enfría más → ...
```

**R6.6 — Eventos que el modelo debe poder producir**, no como *scripts* sino como estados del sistema:
- **Glaciaciones** y periodos interglaciares.
- **Snowball Earth**: si la línea de hielo baja de ~30° de latitud, el lazo se dispara y el planeta se congela entero.
- **Salida del snowball**: solo por acumulación de CO₂ volcánico durante decenas de Ma, porque sin océano líquido no hay meteorización que lo consuma. **La histéresis es el resultado interesante**: el planeta no vuelve por donde vino.

*Criterio:* un barrido lento de la constante solar (o de la desgasificación) debe mostrar **bifurcación e histéresis** — dos estados estables para el mismo forzamiento, según de dónde se venga. Es el resultado clásico del modelo de Budyko-Sellers y una validación fuerte.

### 6.4 Criosfera

**R6.7** — El hielo es un **campo con masa propia**, no un color en el mapa:
- **Casquetes y glaciares** que acumulan por nevada y pierden por fusión y ablación.
- **Flujo glaciar** (aunque sea difusión no lineal): el hielo fluye ladera abajo.
- **Erosión glaciar**: valles en **U**, circos y fiordos. Firma geomorfológica distinta de la fluvial (valles en V), y por tanto verificable de un vistazo.
- **Efecto en el nivel del mar**: el agua que está en los casquetes no está en el océano. Un ciclo glacial mueve ~120 m de nivel del mar en la Tierra.

---

## 7. Biosfera: evolución

El objetivo final del proyecto: usar el planeta como laboratorio de evolución. El resto de fases existen para que este sustrato sea honesto.

### 7.1 Principio rector

**R7.1 — No hay función de *fitness* explícita.** La selección natural debe **emerger** de la capacidad de obtener energía del entorno, la eficiencia metabólica, la adaptación a las condiciones locales y el éxito reproductivo. En cuanto se escribe una función de aptitud, deja de ser evolución y pasa a ser optimización de un objetivo elegido por el programador.

### 7.2 Representación de agentes

**R7.2 — Super-individuos.** Cada entidad representa una **población local** de individuos casi idénticos, con un tamaño de población asociado. A escala planetaria no se pueden simular organismos uno a uno, y no hace falta.

**R7.3 — Genoma vectorial:**

| Categoría | Atributos |
|---|---|
| **Tolerancias** | Temperatura óptima y rango, necesidad de agua, tolerancia a salinidad, tolerancia a pH/nutrientes |
| **Morfología** | Tamaño corporal, velocidad, eficiencia digestiva, coste de mantenimiento |
| **Trofia** | Fotosíntesis, herbivoría, carnivoría, detritivoría (fracciones, no categorías rígidas) |
| **Historia vital** | Umbral reproductivo, inversión por descendiente, longevidad |
| **Comportamiento** | Gregarismo, agresividad, dispersión |

**R7.4 — Almacenamiento adecuado a la escala.** Buffer plano de agentes con partición espacial (rejilla o hash) para las consultas de vecindad. Objetivo: **≥10⁶ agentes** actualizados sin caer del presupuesto por frame.

### 7.3 Bucle evolutivo

**R7.5 — Cuatro pasos por tick biológico:**

1. **Evaluación de entorno.** Leer temperatura, humedad, biomasa, nutrientes NPK y luz en la celda del agente.
2. **Balance energético.**
   - *Ganancia*: fotosíntesis (∝ luz × agua × nutrientes), depredación, consumo de biomasa.
   - *Gasto*: metabolismo basal ∝ `M^0,75` (**ley de Kleiber**, no lineal — es lo que hace que ser grande sea eficiente por unidad de masa), modulado por temperatura (`Q₁₀ ≈ 2–3`), más el coste del movimiento.
3. **Reproducción y mutación.** Superado el umbral energético, el agente se divide; el descendiente hereda el genoma con mutación gaussiana acotada.
4. **Selección natural.** Energía negativa ⇒ muerte, y **el cuerpo pasa a materia orgánica del suelo**, cerrando el ciclo de nutrientes con §5.

**R7.6 — Eficiencia trófica ~10 %.** Es lo que limita el número de niveles tróficos a 4–5 y hace que los depredadores sean raros. Requisito de que salga solo, no de que se imponga.

### 7.4 Acoplamiento en los dos sentidos

**R7.7 — El entorno afecta a la vida** (trivial) **y la vida afecta al entorno** (lo interesante):

| Efecto de la biosfera | Sobre |
|---|---|
| Vegetación → albedo | §6 clima |
| Raíces → tasa de meteorización (×2–10) | §6 carbono, §5 suelos |
| Cobertura vegetal → resistencia a la erosión | §5 relieve |
| Evapotranspiración → humedad atmosférica | §4 clima |
| Fotosíntesis → **oxígeno atmosférico** | §6, y es la Gran Oxidación |
| Enterramiento de carbono orgánico | §6 CO₂ a largo plazo |

### 7.5 Fenómenos evolutivos que deben emerger

| Evento geológico | Resultado biológico esperado |
|---|---|
| Aislamiento de un continente | **Especiación alopátrica**: divergencia genética medible |
| Colisión continental | Intercambio biótico y extinción de los perdedores |
| Calentamiento global | Migración hacia los polos, o extinción |
| Formación de cordilleras | Nichos nuevos por gradiente altitudinal |
| Glaciación | Presión selectiva por resistencia al frío; refugios |
| LIP / impacto | **Extinción masiva** y radiación adaptativa posterior |

### 7.6 Instrumentación evolutiva

**R7.8 — El proyecto tiene que poder responder preguntas, no solo mostrar bichos:**
- **Árbol filogenético** registrado, con marcas de especiación y extinción.
- **Distancia genética** entre poblaciones, para detectar especiación objetivamente (agrupamiento en el espacio de genomas).
- **Curva de diversidad** en el tiempo, con detección automática de **extinciones masivas** (caída >50 % de linajes en una ventana corta).
- **Mapa de biomas** emergente, comparable con la clasificación de Whittaker (temperatura × precipitación).

*Criterio de aceptación global:* colocando el mapa de biomas simulado sobre el diagrama de Whittaker, los biomas deben caer donde les corresponde. Desierto donde hay poca lluvia, selva donde hay mucha y calor, tundra donde hace frío, sin que nadie haya escrito «aquí va un desierto».

---

## 8. Renderizado y visualización

### 8.1 Principio: dos renderizadores, no uno

**R8.1** — Existen **dos caminos de dibujo distintos**, con requisitos opuestos, y confundirlos ha costado caro:

| | Visualización de diagnóstico | Renderizado de producto |
|---|---|---|
| Objetivo | Ver **datos** | Ver **un planeta** |
| Geometría | Malla fija, resolución global baja | Quadtree con LOD hasta la superficie |
| Material | **Unlit**, color plano por vértice | PBR, triplanar, splat, Lumen |
| Coste | Despreciable | Presupuestado |
| Disponibilidad | **Siempre, desde el día uno** | Fase propia |

**R8.2 — Requisitos del visor de diagnóstico:**
- Cualquier campo `6 × Res²` se registra con **Id, etiqueta, unidad, paleta y escala**, sin copiar datos (guarda una referencia viva, así la vista nunca se desincroniza del estado).
- **Paleta y escala son ortogonales.** Paleta: `Sequential` (viridis), `Diverging` (centrada en un cero con significado físico), `Categorical` (sin interpolar), `Terrain` (nivel del mar en el centro). Escala: `Linear`, `Logarithmic`.
- **El log es obligatorio, no opcional**: en lineal, el caudal del cauce principal satura y los afluentes son indistinguibles del fondo. El mapa de drenaje **solo existe** en escala logarítmica.
- **Rango automático por percentiles 2/98**, nunca mín/máx: un solo *outlier* aplana el mapa entero.
- Conmutación por teclado, leyenda en pantalla, y **campos vectoriales como flechas** (viento, velocidad de placa, drenaje).
- **Vista de sección** a lo largo de un gran círculo: perfil de elevación, grosor de corteza y capas. Es la única forma de ver la estratigrafía.

### 8.2 Renderizado de producto

**R8.3 — Desacoplar resolución de simulación y de render.** El render puede y debe tener mucho más detalle que la simulación; el detalle sub-celda es procedimental y determinista.

**R8.4 — Geometría: quadtree + malla-rejilla única instanciada.**

> **Corrección explícita del diseño original.** El documento de partida proponía generar geometría Nanite por parche. Se intentó: construía un `UStaticMesh` completo por parche de quadtree, de forma **síncrona en el hilo principal**, y congelaba el editor. Ningún presupuesto por frame arregla que la geometría se **recree** en vez de **instanciarse**.

El diseño correcto:
- **Una sola malla-rejilla** pre-construida (por ejemplo 33×33 vértices), instanciada por parche.
- Desplazamiento en el **vertex shader**, muestreando la textura de elevación de la simulación.
- LOD por distancia sobre el quadtree, con **faldones o *geomorphing*** para que no aparezcan grietas (T-junctions) entre niveles adyacentes.
- Continuidad entre caras del cubo en los bordes del quadtree — mismo problema que R1.4, misma solución.

**R8.5 — Nanite: sí, pero para lo que sirve.** Nanite es para geometría **estática y densa**: rocas, vegetación, detalle colocado sobre la superficie. **No** para la cáscara planetaria que se deforma cada paso. Esta es una revisión deliberada del documento original.

**R8.6 — Texturizado procedimental reactivo:**
- **Mapeo triplanar** para evitar el estiramiento en pendientes verticales.
- **Splat maps dinámicos** derivados directamente de campos de simulación:
  ```
  Material = lerp(Roca, Suelo,  espesor_de_regolito)
  Material = lerp(Material, Hierba, humedad × temperatura_apta)
  Material = lerp(Material, Arena,  aridez)
  Material = lerp(Material, Nieve,  hielo)
  Material = lerp(Material, Roca,   pendiente)
  ```
  El mapa de biomas de §7 es la entrada natural de este bloque.
- Materiales PBR de calidad (Quixel Megascans u origen equivalente).

**R8.7 — Atmósfera y océano:**
- `Sky Atmosphere` con dispersión de **Rayleigh** (cielo azul, atardeceres rojos) y **Mie** (bruma), **con los parámetros acoplados a la densidad y composición atmosférica simuladas**. Si el modelo dice que la atmósfera es densa y con polvo, el cielo tiene que cambiar.
- Nubes volumétricas alimentadas por la cobertura nubosa simulada.
- Océano con color por profundidad, espuma en la costa y hielo marino donde lo haya.
- Iluminación global dinámica (Lumen).

**R8.8 — Escala real y sin trampas.** Radio de 6.371 km. La elevación se aplica como **multiplicador directo sobre metros reales**, con exageración vertical configurable — **nunca como fracción del radio**.

> A escala de juguete (radio 500 m) el error de escalar el desplazamiento como porcentaje del radio pasaba desapercibido; a escala real habría dado montañas de miles de kilómetros.

**R8.9 — Cámara de aproximación.** Desde órbita hasta la superficie, con velocidad interpolada logarítmicamente según la altitud y «colisión» por consulta de altura contra el campo de elevación (la malla planetaria se genera sin colisión física a propósito: recalcularla en cada regeneración es inviable).

### 8.3 Presupuesto de rendimiento

| Objetivo | Valor |
|---|---|
| Framerate de producto | **60 FPS** con la simulación corriendo |
| Presupuesto de simulación por frame | < 8 ms |
| Presupuesto de render por frame | < 8 ms |
| La simulación **no puede bloquear** el hilo de render | Requisito, no aspiración |

---

## 9. Stack tecnológico

### 9.1 Motor: Unreal Engine 5.8

| Tecnología | Para qué se usa aquí |
|---|---|
| **C++ / módulos runtime** | Todo el núcleo de simulación |
| **ProceduralMeshComponent** | Visor de diagnóstico |
| **Nanite** | Detalle estático sobre la superficie (§8.5), *no* la cáscara |
| **Lumen** | Iluminación global dinámica |
| **Niagara** | Agentes biológicos en GPU (§7) |
| **Sky Atmosphere / Volumetric Clouds** | §8.7 |
| **Automation Testing** | Toda la validación física |
| **SaveGame** | Persistencia |

**Alternativa considerada y descartada:** Unity 6 (HDRP + DOTS) tiene mejor ergonomía para compute shaders personalizados y estructuras de datos científicas, pero exigiría escribir el sistema de terreno planetario, el LOD y el renderizado desde cero. **Decisión tomada; no se reabre sin motivo nuevo.**

### 9.2 Dónde corre el cómputo

**R9.1 — Política explícita: CPU hasta que perfilar demuestre que no basta.**

> El proyecto arrancó con «Compute Shaders GPU» como pieza central de la arquitectura y acumuló ~1300 líneas de infraestructura GPU (buffers RDG, SRV/UAV, cuatro global shaders, dos `.usf` de 300+ y 450+ líneas) con **todos los `Dispatch` vacíos**. Coste real: cero funcionalidad, mucho mantenimiento y una brecha permanente entre la arquitectura documentada y el código.

Regla operativa: la migración a GPU es una **optimización con criterio de disparo medido** (ver §10.2), no una decisión de diseño previa. Cuando ocurra, el orden natural es el que impone el coste medido: advección → isostasia → difusión → drenaje → SWE.

**R9.2 — Lo que sí exige GPU desde el principio**: los agentes biológicos de §7, por número (≥10⁶). Ahí la decisión es al revés y no se discute.

### 9.3 Estructuras y librerías

| Componente | Tecnología |
|---|---|
| Rejilla y métricas | C++ (`CubeSphereGrid`, `CubeSphereMetrics`) |
| Tectónica, isostasia, clima, hidrología | C++ (CPU hoy) |
| Ruido procedimental | Simplex 3D / fractal / *ridged*, CPU (y GPU cuando haga falta) |
| Física en GPU | HLSL / `.usf` sobre el RDG de Unreal |
| Agentes | Compute shaders o Niagara |
| Persistencia | `USaveGame` |

**R9.3 — Sin dependencias externas pesadas.** Todo el núcleo físico es propio y auditable. Es un requisito de este proyecto: el valor está en que la física sea comprobable línea a línea.

### 9.4 Hardware objetivo

| Componente | Mínimo | Recomendado |
|---|---|---|
| GPU | RTX 3070 / RX 6800 | RTX 4080+ / RX 7900 XT |
| VRAM | 8 GB | 16+ GB |
| RAM | 32 GB | 64 GB |
| CPU | 8 núcleos | 16+ núcleos |

---

## 10. Requisitos no funcionales

### 10.1 Determinismo y reproducibilidad

**R10.1** — Misma semilla + mismos parámetros ⇒ **mismo planeta, bit a bit**. Sin dependencia del framerate ni del orden de hilos.
**R10.2** — El paso de simulación es **fijo** y está desacoplado del framerate. La física no puede depender de a cuántos fps se corra.
**R10.3** — La persistencia guarda la semilla más el estado evolucionado (los campos primarios de §0.3), no la topología derivada.

### 10.2 Rendimiento e instrumentación

**R10.4** — El coste está **instrumentado por fases**, con media móvil, y visible en pantalla. No se optimiza nada que no se haya medido antes.
**R10.5** — Todo campo nuevo paga `O(Res²)` por las seis caras. Es el riesgo estructural del proyecto: **cada fase añade campos y el coste escala con el número de celdas** (medido: ×16 al pasar de Res 32 a 96).
**R10.6 — Criterio de disparo para migrar a GPU:** cuando una fase instrumentada supere el 40 % del presupuesto por frame de forma sostenida a la resolución de producción.

### 10.3 Verificabilidad

**R10.7** — Cada fase entrega: (a) los invariantes de §11 que le tocan, en verde; (b) sus campos registrados en el visor; (c) **verificación visual en el editor**, no solo tests.
**R10.8** — Los tests se validan **saboteando el código** y comprobando que se ponen rojos.
**R10.9** — El nombre de un test describe exactamente su aserción. Si no, se renombra.

### 10.4 Escalas de tiempo

El simulador integra escalas que difieren en **catorce órdenes de magnitud**, y eso es un requisito arquitectónico, no una curiosidad:

| Proceso | Escala característica | Estrategia |
|---|---|---|
| Deriva continental | 10⁶–10⁸ años | Paso largo, advección por umbral de píxel |
| Ciclo del carbono | 10⁵–10⁶ años | Paso largo |
| Erosión fluvial | 10³–10⁶ años | Paso largo |
| Glaciaciones | 10⁴–10⁵ años | Paso medio |
| Evolución biológica | 10⁰–10⁶ años (generaciones) | Paso medio, sub-muestreado |
| Dinámica atmosférica | horas–días | **Sub-ciclado**: N pasos de atmósfera por cada paso geológico |
| Renderizado | 16 ms | Desacoplado por completo |

**R10.10** — Los sistemas rápidos **se sub-ciclan** dentro del paso lento o se resuelven a su estado de equilibrio (diagnóstico) cuando eso baste. La elección se documenta por subsistema.

### 10.5 Control de la simulación

**R10.11** — Pausa, paso a paso, aceleración temporal, reinicio con semilla, guardado y carga. Configuración de parámetros físicos en caliente, con la advertencia de que las **calibraciones son cruzadas** (§5.10).

---

## 11. Tabla maestra de invariantes

Los criterios de aceptación de todo el simulador, en un sitio. Un requisito sin fila aquí es un requisito sin comprobar.

| # | Invariante | Objetivo | Dominio |
|---|---|---|---|
| I-01 | Suma de áreas de celda = 4πR² | error < 0,1 % | Rejilla |
| I-02 | Vecindad simétrica y continua en las 12 aristas | 0 fallos | Rejilla |
| I-03 | Conservación de masa en difusión | deriva < 10⁻⁶ / 1000 pasos | Rejilla |
| I-04 | Cada punto pertenece a exactamente una placa | 100 % | Tectónica |
| I-05 | Balanza creación/destrucción de corteza | 0,95–1,05, estable | Tectónica |
| I-06 | Celdas sin resolver por advección | < 0,01 % | Tectónica |
| I-07 | Solape (≥2 reclamantes) | < 1,5 % | Tectónica |
| I-08 | Área por placa variable en el tiempo | sí (prueba directa del ciclo de vida) | Tectónica |
| I-09 | Edad máxima de corteza oceánica | 150–250 Ma | Tectónica |
| I-10 | Velocidad de placas | 1–15 cm/año | Tectónica |
| I-11 | Tiempo simulado descartado | 0,00 Ma | Tectónica |
| I-12 | Respaldos de lectura de material | < 2 %, estable | Tectónica |
| I-13 | Periodo del ciclo de Wilson | 300–500 Ma | Tectónica |
| I-14 | Altura de 35 km de corteza continental | +840 ± 100 m | Isostasia |
| I-15 | Volumen de océano conservado | deriva < 0,1 % / 1000 Ma | Isostasia |
| I-16 | Curva hipsométrica bimodal | dos modos claros | Isostasia |
| I-17 | Correlación profundidad ↔ √edad oceánica | r > 0,8 | Isostasia |
| I-18 | Fracción de tierra emergida | 25–35 %, converge con la resolución | Isostasia |
| I-19 | Perfil de precipitación **no monótono** | mínimo subtropical presente | Clima |
| I-20 | Sombra de lluvia sobre terreno real | sotavento más seco en >85 % de cordilleras, contraste >3× | Clima |
| I-21 | Celdas de Hadley/Ferrel/polar emergentes | 3 por hemisferio | Clima |
| I-22 | Balance de masa de agua cerrado | deriva < 0,1 % / 1000 pasos | Clima |
| I-23 | Estabilidad numérica de las SWE | energía acotada, 0 NaN en 10⁶ pasos | Clima |
| I-24 | Relación pendiente-área | −0,4 a −0,6 | Hidrosfera |
| I-25 | Ley de Hack | exponente 0,55–0,65 | Hidrosfera |
| I-26 | Sumideros tras tratamiento | < 1 % | Hidrosfera |
| I-27 | Montaña aislada se degrada | sí | Hidrosfera |
| I-28 | Balance de sedimento | deriva < 1 % | Hidrosfera |
| I-29 | Termostato carbonato-silicatos estabiliza | τ ≈ 0,5–1 Ma ante escalón | Carbono |
| I-30 | Histéresis hielo-albedo | dos estados estables para el mismo forzamiento | Carbono |
| I-31 | Biomas sobre el diagrama de Whittaker | caen donde corresponde | Biosfera |
| I-32 | Especiación por aislamiento | distancia genética creciente tras separar un continente | Biosfera |
| I-33 | Eficiencia trófica emergente | ~10 %, 4–5 niveles | Biosfera |
| I-34 | Determinismo por semilla | idéntico bit a bit | Global |
| I-35 | Independencia del framerate | resultado igual a 30/60/144 fps | Global |
| I-36 | Framerate de producto | 60 FPS con simulación activa | Render |
| I-37 | Cada fase visible en el visor | 100 % de campos registrados | Render |

---

## Documentos relacionados

- [`SPECS.md`](SPECS.md) — qué hay implementado hoy, archivo por archivo.
- [`ROADMAP.md`](ROADMAP.md) — lista de pasos hasta la finalización.
- [`ANEXO.md`](ANEXO.md) — historial, defectos, hipótesis descartadas y mediciones.
- [`../archivo/`](../archivo/) — documentación de diseño original, conservada como referencia.
