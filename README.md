# Simu — Gemelo digital planetario

Un simulador de planeta a escala real donde la geografía **no se dibuja: se deduce**. Las montañas salen de que dos placas chocan, los desiertos de que el aire ya descargó su humedad al cruzar esas montañas, y los ríos de por dónde cae el agua después. El objetivo final es usar ese planeta como sustrato para experimentos de evolución biológica.

Escrito en C++ sobre Unreal Engine 5.8.

---

## El principio

> El acoplamiento **es** el producto.

```
tectónica → relieve → clima → agua → erosión → sedimento
    ↑                                              │
    └──── carga isostática ────────────────────────┘
    ↑                                              │
    └──── meteorización → CO₂ → clima ─────────────┘
```

Un relieve que no se erosiona y una lluvia que no depende de las montañas son dos maquetas independientes, no un simulador. Todo lo demás —el ráster de cubo esférico, la separación entre propiedad y material, el visor de campos— existe para que esas flechas funcionen y para poder **comprobar** que funcionan.

Y una regla que se ha ganado su sitio a base de disgustos:

> **Ninguna fase se da por hecha si no se puede ver en pantalla.**
> Un test dice «no ha petado». Mirar el planeta dice «esto no parece la Tierra».

---

## Dónde está el proyecto

| Fase | Estado |
|---|---|
| **F0** Cimientos: rejilla, mapeo de caras, vecindad geométrica | ✅ Completo |
| **F0.5** Visor de campos de diagnóstico | 🟡 Núcleo hecho, faltan campos vectoriales y secciones |
| **F1** Las placas como objetos | 🟡 Cinemática y separación propiedad/material hechas; falta frontera como objeto, ciclo de vida y dinámica |
| **F2** Isostasia, batimetría y nivel del mar | ✅ Completo |
| **F3** Clima diagnóstico | ✅ Completo, sin verificar a ojo |
| **F4** Erosión hidráulica y sedimento | 🟡 Drenaje hecho; falta incisión, sedimento y suelos |
| **F5** Atmósfera dinámica, océano, hielo y carbono | ⬜ Pendiente |
| **F6** Renderizado de producto | ⬜ Pendiente |
| **F7** Biosfera | ⬜ Pendiente |
| **F8** Producto y experimentos | ⬜ Pendiente |

**Lo que ya funciona y se puede ver:** un planeta de radio real con ocho placas que derivan, chocan, forman cordilleras calibradas contra el Himalaya y abren océanos; corteza que envejece y se hunde térmicamente; un nivel del mar que conserva volumen y responde a la tectónica; bandas climáticas con desiertos subtropicales y sombras de lluvia sobre las cordilleras que la propia simulación acaba de levantar; y redes de drenaje que bajan hasta el mar.

**El defecto abierto que importa:** la tierra emergida cae del 24,5 % al 13,2 % en 1000 Ma. El planeta se ahoga. Detalles en [`docs/SPECS.md`](docs/SPECS.md#13-deuda-técnica-activa).

**Suite:** 40 tests de automatización, 36 en verde. Los 4 en rojo son defectos conocidos y documentados, no sorpresas.

---

## Documentación

Cuatro documentos, cada uno con un trabajo distinto. Si solo vas a leer uno, que sea el primero.

| Documento | Responde a |
|---|---|
| [`docs/REQUISITOS.md`](docs/REQUISITOS.md) | **¿Qué tiene que hacer?** La especificación física completa, con criterios de aceptación medibles y una tabla de 37 invariantes |
| [`docs/SPECS.md`](docs/SPECS.md) | **¿Qué hay hoy?** Estado real del código, archivo por archivo, con la deuda técnica ordenada por impacto |
| [`docs/ROADMAP.md`](docs/ROADMAP.md) | **¿Qué falta?** Lista de pasos con casilla, de aquí a la finalización |
| [`docs/ANEXO.md`](docs/ANEXO.md) | **¿Por qué así?** Historial, defectos, hipótesis descartadas y todas las mediciones que sostienen las decisiones |

La documentación de diseño original está conservada en [`archivo/`](archivo/) como referencia de alcance. Sus fechas y su orden no están validados contra el código.

---

## Cómo trabajar aquí

### Compilar

```powershell
& 'E:\Unreal\UE_5.8\Engine\Build\BatchFiles\Build.bat' SimuEditor Win64 Development `
    -Project="D:\Portfolio\Simu\Simu.uproject" -WaitMutex
```

### Pasar la suite de tests

```powershell
& 'E:\Unreal\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' "D:\Portfolio\Simu\Simu.uproject" `
    -ExecCmds="Automation RunTests Simu; Quit" -unattended -nopause -nosplash -NullRHI -stdout
```

Tarda unos 16 minutos: varios tests simulan 1000 Ma. Grupos disponibles: `Simu.CubeSphere`, `Simu.Visualization`, `Simu.Climate`, `Simu.Hydrology`, `Simu.Tectonics`.

> El filtro `Simu` también engancha tests del motor cuya ruta contiene «Simulation». Los del proyecto son los 40 de los cinco grupos de arriba.

### Ejecutar

Abrir `Simu.uproject` en el editor y darle a Play sobre `Content/test.umap`. El actor `ATectonicsTestActor` simula y renderiza; `APlanetApproachPawn` es la cámara.

---

## Controles

| Tecla | Acción |
|---|---|
| **Espacio** | Pausar / reanudar |
| **R** | Reiniciar la simulación |
| **+ / −** | Acelerar / frenar el tiempo |
| **F / G** | Campo de diagnóstico siguiente / anterior |
| **U** | Conmutar material unlit |
| **V** | Mostrar fronteras de placa |
| **1–8 / 0** | Seleccionar / deseleccionar placa |
| **K / L** | Guardar / cargar |
| **O** | Conmutar modo de cámara |
| **WASD · QE · ratón · rueda** | Mover la cámara |

**Campos de diagnóstico registrados:** elevación, ID de placa, edad de corteza, tipo de corteza, grosor de corteza, altura sobre el nivel del mar, temperatura, precipitación y área drenada (esta última en escala logarítmica — en lineal el cauce principal satura y los afluentes son invisibles).

---

## Arquitectura en un minuto

**El planeta es un cubo esférico.** Seis caras de 256×256 celdas, proyectadas por normalización. Uniformidad razonable, adyacencia regular de 4 vecinos, sin singularidad polar y afín a las *cube maps* de la GPU. El mapeo cara↔dirección vive en **un solo archivo**: estaba escrito a mano ocho veces y ya se había desincronizado.

**El estado primario es el grosor de corteza, no la elevación.** La elevación se **deriva** por flotación de Airy con densidades reales, y se reconstruye entera cada paso. Por eso una colisión continental apila material en vez de destruirlo, y por eso una montaña erosionada rebotará en lugar de desaparecer.

**La propiedad y el material van por separado.** Es el hallazgo estructural del proyecto:

- «¿De quién es esta celda?» es una **partición**, y hay que re-deducirla del mundo actual en cada paso o degenera en ruido.
- «¿Qué material hay aquí?» es una **sustancia**, y hay que leerla una sola vez desde el marco propio de su placa o se deshilacha.

Uno exige encadenar; el otro exige no encadenar nunca. Compartían estructura, y por eso todo arreglo era un balancín. Separarlos llevó la balanza de corteza de 0,04 a 0,98 y las celdas sin resolver del 31,6 % al 0,0001 %.

**Todo corre en CPU, a propósito.** El proyecto acumuló ~1300 líneas de andamiaje GPU con todos los `Dispatch` vacíos. Se borró. La migración a compute shaders es una optimización con criterio de disparo medido, no una decisión de diseño previa.

---

## Estructura del repositorio

```
Source/
  CubeSphere/          Núcleo de simulación (módulo runtime)
    Tectonics/           Placas, advección, isostasia, nivel del mar
    Climate/             Clima diagnóstico
    Hydrology/           Drenaje
    Visualization/       Visor de campos
    QuadTree/  LOD/      Dormidos hasta F6
    Noise/  Nanite/      Ruido procedimental, generación de materiales
    Tests/               40 tests de automatización
    Test/                Actor de simulación
  Simu/                Cámara, HUD, UI, GameMode
Shaders/               HLSL (sin dispatch activo todavía)
Content/               Materiales y mapa de pruebas
docs/                  Documentación viva
archivo/               Documentación de diseño original
```

---

## Licencia

Proyecto personal. Todos los derechos reservados.
