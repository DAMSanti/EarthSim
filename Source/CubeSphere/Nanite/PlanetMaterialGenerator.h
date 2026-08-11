// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

class UMaterialInterface;

/**
 * Crea (o carga si ya existe) el material de displacement Nanite del planeta
 * (ROADMAP.md M1). No existia ningun material para este pipeline - Content/ solo
 * tenia M_VertexColor.uasset, para el pipeline distinto de TectonicsTestActor.
 *
 * Grafo: TextureSampleParameter2D "Heightmap" (R16F, [0,1]) + VectorParameter
 * "HeightmapMinMax" (X=Min, Y=Max en metros) -> Lerp -> metros a cm -> pin
 * Displacement (Nanite Displacement, no World Position Offset clasico - Nanite
 * tesela desde el material via ese pin, con r.Nanite.Tessellation=1 por defecto
 * en UE 5.8, verificado en el codigo del motor).
 *
 * Creado y compilado por codigo sin poder previsualizarlo en el Material Editor -
 * verificar visualmente en el editor antes de confiar en el resultado.
 */
UMaterialInterface* GetOrCreatePlanetDisplacementMaterial();

#endif // WITH_EDITOR
