// PlanetFieldMaterial.h
// Material para las vistas de diagnóstico del visor de campos (ROADMAP.md F0.5).

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

class UMaterialInterface;

/**
 * Crea (o carga si ya existe) `Content/Materials/M_PlanetFieldUnlit`: un material
 * **unlit** que envía el color de vértice directamente a Emissive.
 *
 * POR QUÉ UNLIT, y por qué importa:
 * La primera prueba en editor del visor (15-08-2026) salió con los colores lavados —
 * la paleta categórica define un rojo (0.90, 0.24, 0.24) y en pantalla se veía salmón
 * pálido. La causa es que `M_VertexColor` es un material iluminado, y `TectonicsTestActor`
 * pone dos luces direccionales (sol a intensidad 10 más un relleno a 2). El color que
 * llega al ojo es entonces `paleta x iluminación x exposición`, y de esos tres factores
 * solo uno es el dato.
 *
 * Eso rompe el objetivo entero de F0.5: si el visor existe para juzgar si un campo es
 * plausible, el color en pantalla tiene que ser el color de la paleta, no una versión
 * suya modulada por dónde da el sol. Un mismo valor no puede verse de dos colores según
 * la latitud.
 *
 * La iluminación sí es útil para la vista de relieve (el sombreado ayuda a leer la
 * forma del terreno), así que esto es un modo alternativo y no un reemplazo:
 * `ATectonicsTestActor::bUnlitFieldView` conmuta entre los dos con la tecla U.
 */
UMaterialInterface* GetOrCreateFieldViewMaterial();

#endif // WITH_EDITOR
