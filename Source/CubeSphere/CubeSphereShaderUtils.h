// CubeSphereShaderUtils.h
// Utilidades para usar el Cubo Esférico en Compute Shaders (HLSL)
// Sprint 1.1 - Preparación para GPU

#pragma once

/**
 * Este archivo contiene las estructuras y funciones que se traducen
 * a HLSL para uso en Compute Shaders.
 * 
 * El archivo .usf correspondiente (CubeSphereCommon.usf) debe incluir
 * las mismas estructuras para mantener compatibilidad CPU-GPU.
 */

#include "CoreMinimal.h"
#include "CubeSphereTypes.h"

/**
 * Estructura de datos de celda optimizada para GPU
 * Empaquetada para alineación de 16 bytes
 */
struct FCubeSphereGPUCell
{
    uint32 FaceAndCoords;  // Face (3 bits) | U (12 bits) | V (12 bits) | Reserved (5 bits)
    
    static FCubeSphereGPUCell Pack(ECSCubeFace Face, int32 U, int32 V)
    {
        FCubeSphereGPUCell Result;
        Result.FaceAndCoords = 
            (static_cast<uint32>(Face) & 0x7) |
            ((static_cast<uint32>(U) & 0xFFF) << 3) |
            ((static_cast<uint32>(V) & 0xFFF) << 15);
        return Result;
    }
    
    void Unpack(ECSCubeFace& OutFace, int32& OutU, int32& OutV) const
    {
        OutFace = static_cast<ECSCubeFace>(FaceAndCoords & 0x7);
        OutU = (FaceAndCoords >> 3) & 0xFFF;
        OutV = (FaceAndCoords >> 15) & 0xFFF;
    }
};

/**
 * Parámetros del grid para pasar a shaders
 */
struct FCubeSphereShaderParams
{
    int32 Resolution;
    float PlanetRadius;
    float InvResolution;    // 1.0 / Resolution
    float CellSize;         // Tamaño angular aproximado de celda
    
    FCubeSphereShaderParams() = default;
    
    FCubeSphereShaderParams(int32 InResolution, float InRadius)
        : Resolution(InResolution)
        , PlanetRadius(InRadius)
        , InvResolution(1.0f / FMath::Max(1, InResolution))
        , CellSize(PI / (3.0f * InResolution))  // Aproximación
    {
    }
};

/**
 * Tabla de adyacencia para GPU
 * Precalculada para evitar branching en shaders
 */
struct FCubeSphereAdjacencyTable
{
    // Para cada cara (6) y dirección (4): cara destino y rotación
    // [FaceIndex * 4 + Direction] = (NeighborFace << 4) | RotationSteps
    uint8 Table[24];
    
    void Initialize();
    
    void GetConnection(ECSCubeFace Face, ENeighborDirection Dir, 
                       ECSCubeFace& OutNeighbor, int32& OutRotation) const
    {
        int32 Index = static_cast<int32>(Face) * 4 + static_cast<int32>(Dir);
        uint8 Packed = Table[Index];
        OutNeighbor = static_cast<ECSCubeFace>(Packed >> 4);
        OutRotation = Packed & 0xF;
    }
};

namespace CubeSphereShaderUtils
{
    /**
     * Genera el código HLSL para las funciones de cubo esférico
     * Útil para depuración y generación de shaders dinámicos
     */
    FString GenerateHLSLCode();
    
    /**
     * Crea un buffer estructurado con la tabla de adyacencia
     */
    // TRefCountPtr<FRDGBuffer> CreateAdjacencyBuffer(FRDGBuilder& GraphBuilder);
}
