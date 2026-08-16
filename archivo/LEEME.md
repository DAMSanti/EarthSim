# archivo/ — Documentación superada

> ⚠️ **Nada de esta carpeta es documentación viva.** Se conserva como referencia histórica y porque parte de su contenido físico sigue siendo válido, pero **no describe el estado del proyecto ni el plan de trabajo**.
>
> La documentación viva está en [`../docs/`](../docs/):
>
> | Documento | Responde a |
> |---|---|
> | [`../docs/REQUISITOS.md`](../docs/REQUISITOS.md) | ¿Qué tiene que hacer el simulador? |
> | [`../docs/SPECS.md`](../docs/SPECS.md) | ¿Qué hay hoy en el código? |
> | [`../docs/ROADMAP.md`](../docs/ROADMAP.md) | ¿Qué falta? |
> | [`../docs/ANEXO.md`](../docs/ANEXO.md) | ¿Por qué así? |

---

## Qué hay aquí, y en qué se ha convertido

### Documentos de diseño original (01–10)

Escritos **antes de que existiera el código**, como visión de producto. Su valor residual es de **alcance y contenido físico**: qué modelos usar, qué ecuaciones. No de orden, ni de fechas, ni de arquitectura.

| Archivo | Destilado en |
|---|---|
| `01-resumen-ejecutivo.md` | [`REQUISITOS.md`](../docs/REQUISITOS.md) §0, y el [README](../README.md) |
| `02-estructura-datos-espaciales.md` | `REQUISITOS.md` §1 |
| `03-geodinamica-tectonica.md` | `REQUISITOS.md` §2 |
| `04-atmosfera-clima.md` | `REQUISITOS.md` §4 |
| `05-hidrosfera-erosion.md` | `REQUISITOS.md` §5 |
| `06-climatologia-efecto-invernadero.md` | `REQUISITOS.md` §6 |
| `07-biosfera-evolucion.md` | `REQUISITOS.md` §7 |
| `08-renderizado-visualizacion.md` | `REQUISITOS.md` §8 |
| `09-stack-tecnologico.md` | `REQUISITOS.md` §9 |
| `10-hoja-de-ruta.md` | `ROADMAP.md` |
| `README.md` | Índice del conjunto original |
| `Instrucciones.md` | Vacío (0 bytes). El «documento original completo» al que apunta el índice **nunca existió en el repositorio** |

### Dónde el destilado se apartó del original, y por qué

Tres puntos en los que `REQUISITOS.md` **contradice** deliberadamente a estos documentos. No son omisiones:

1. **Nanite no dibuja la cáscara planetaria.** El original proponía generar geometría Nanite por parche. Se implementó (840 líneas) y **congelaba el editor**: construía un `UStaticMesh` completo por parche, síncrono, en el hilo principal. Nanite se reserva para geometría estática sobre la superficie. → `REQUISITOS.md` R8.4–R8.5
2. **La física no arranca en GPU.** El original ponía los compute shaders como pieza central de la arquitectura. Se construyeron ~1300 líneas de andamiaje con **todos los `Dispatch` vacíos**, y se borraron. La migración a GPU pasa a ser una optimización con criterio de disparo medido. → `REQUISITOS.md` R9.1
3. **La isostasia no estaba en el original, y es la pieza que faltaba.** El grosor de corteza como estado primario, con la elevación derivada por flotación de Airy, es lo que hace que la geodinámica produzca un planeta y no un mapa de alturas. → `REQUISITOS.md` §3

Además, `REQUISITOS.md` añade lo que el original no tenía: **criterios de aceptación medibles** para cada requisito, y una tabla de 37 invariantes verificables.

### Documentos de planificación superados

| Archivo | Qué era | Reemplazado por |
|---|---|---|
| `ROADMAP-vision-original.md` | La hoja de ruta de 12 meses por calendario, F1–F5 | [`ROADMAP.md`](../docs/ROADMAP.md), ordenado por **dependencia técnica** y no por fechas |
| `ROADMAP-plan-anterior.md` | El plan reescrito tras la auditoría del 15-08-2026, con el anexo mezclado dentro | [`ROADMAP.md`](../docs/ROADMAP.md) (los pasos) + [`ANEXO.md`](../docs/ANEXO.md) (todo lo demás) |
| `SPECS-anterior.md` | Inventario de código a 16-08-2026, con secciones ya caducadas conservadas como historial | [`SPECS.md`](../docs/SPECS.md), reescrito contra el código actual y con la suite ejecutada de verdad |
