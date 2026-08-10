// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * FSimplexNoise
 * 
 * Implementación de ruido Simplex 3D para generar terreno fractal.
 * Basado en el algoritmo de Ken Perlin (2001), optimizado para esfera.
 * 
 * El ruido Simplex es superior a Perlin Noise porque:
 * - Menos artefactos direccionales
 * - Complejidad O(n²) vs O(2^n) de Perlin
 * - Gradiente más uniforme
 */
class CUBESPHERE_API FSimplexNoise
{
public:
    /**
     * Inicializar con semilla
     */
    static void Initialize(int32 Seed = 0);

    /**
     * Ruido Simplex 3D básico
     * @param X, Y, Z - Coordenadas 3D
     * @return Valor en rango [-1, 1]
     */
    static float Noise3D(float X, float Y, float Z);

    /**
     * Fractal Brownian Motion (fBm) - Ruido multicapa
     * Suma varias octavas de ruido para crear detalle fractal
     * 
     * @param X, Y, Z - Coordenadas 3D
     * @param Octaves - Número de capas (más = más detalle, más caro)
     * @param Lacunarity - Factor de frecuencia entre octavas (típico: 2.0)
     * @param Persistence - Factor de amplitud entre octavas (típico: 0.5)
     * @return Valor aproximadamente en rango [-1, 1]
     */
    static float FractalNoise3D(float X, float Y, float Z, 
                                 int32 Octaves = 6, 
                                 float Lacunarity = 2.0f, 
                                 float Persistence = 0.5f);

    /**
     * Ruido fractal aplicado a un punto en la esfera
     * Automáticamente escala las coordenadas para frecuencia deseada
     * 
     * @param Direction - Dirección normalizada en la esfera
     * @param Frequency - Frecuencia base (más alto = features más pequeñas)
     * @param Octaves - Número de capas de detalle
     * @param Lacunarity - Factor de frecuencia entre octavas
     * @param Persistence - Factor de amplitud entre octavas
     * @return Valor aproximadamente en rango [-1, 1]
     */
    static float SphereFractalNoise(const FVector& Direction,
                                     float Frequency = 4.0f,
                                     int32 Octaves = 6,
                                     float Lacunarity = 2.0f,
                                     float Persistence = 0.5f);

    /**
     * Ruido tipo "Ridged Multifractal" - Crea crestas (montañas)
     * Las cordilleras y costas tienen esta característica
     */
    static float RidgedNoise3D(float X, float Y, float Z,
                                int32 Octaves = 6,
                                float Lacunarity = 2.0f,
                                float Gain = 2.0f);

    /**
     * Domain Warping - Distorsiona coordenadas con ruido
     * Crea formas más orgánicas y menos "ruidosas"
     */
    static FVector WarpDomain(const FVector& Position, float WarpStrength = 0.5f);

private:
    // Tabla de permutaciones (256 valores shuffled, duplicados para overflow)
    static int32 Perm[512];
    static int32 Perm12[512];
    
    // Gradientes para simplex 3D
    static const float Grad3[12][3];
    
    // Helpers
    static int32 FastFloor(float X);
    static float Dot(const float* G, float X, float Y, float Z);
    
    static bool bInitialized;
};
