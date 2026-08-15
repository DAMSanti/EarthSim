// CubeFaceMapping.h
// Fuente única de verdad para la conversión entre (cara, U, V) y dirección 3D.
//
// POR QUÉ EXISTE ESTE ARCHIVO (ROADMAP.md F0, 15-08-2026):
// Esta conversión estaba escrita a mano en cinco sitios distintos y dos de ellos
// discrepaban. `UCubeSphereGrid::GetFaceAxes` daba para +Z el eje V = (0,1,0), mientras
// que `URasterizedTectonics` usaba `FVector(U, -V, 1)`, es decir V invertida (y en -Z
// invertida en sentido contrario). Las cuatro caras ecuatoriales sí coincidían, así que
// el bug solo se manifestaba en los dos casquetes polares: el mapa de IDs de placa
// (construido sobre el Grid) y el de elevación (construido sobre el Raster) quedaban
// espejados allí, y las fronteras dibujadas no coincidían con el relieve.
//
// Cada sistema era internamente consistente, por eso pasó desapercibido: el relieve se
// veía plausible hasta que se cruzaban los dos campos. Cualquier sistema futuro que
// necesite vecindad o cruce de campos entre caras (drenaje en F4, advección en F5)
// habría vuelto a pagar el mismo problema.
//
// CONVENIO ADOPTADO: el de `UCubeSphereGrid::GetFaceAxes`, porque es el que usan Voronoi,
// la detección de fronteras y `GetCrossFaceNeighbors`, y porque es el único de los dos
// que es dextrógiro en las 6 caras (U × V == Normal siempre, ver test de regresión).
//
// Nadie debe volver a escribir un `switch` sobre ECSCubeFace para esto. Si necesitas la
// conversión, llama aquí.

#pragma once

#include "CoreMinimal.h"
#include "CubeSphereTypes.h"

namespace CubeFaceMapping
{
    /**
     * Ejes locales de una cara del cubo.
     * Invariante verificado por test: AxisU × AxisV == Normal en las 6 caras (dextrógiro).
     */
    inline void GetFaceAxes(ECSCubeFace Face, FVector& OutAxisU, FVector& OutAxisV, FVector& OutNormal)
    {
        switch (Face)
        {
        case ECSCubeFace::PositiveX:
            OutNormal = FVector(1, 0, 0);  OutAxisU = FVector(0, 1, 0);  OutAxisV = FVector(0, 0, 1);  break;
        case ECSCubeFace::NegativeX:
            OutNormal = FVector(-1, 0, 0); OutAxisU = FVector(0, -1, 0); OutAxisV = FVector(0, 0, 1);  break;
        case ECSCubeFace::PositiveY:
            OutNormal = FVector(0, 1, 0);  OutAxisU = FVector(-1, 0, 0); OutAxisV = FVector(0, 0, 1);  break;
        case ECSCubeFace::NegativeY:
            OutNormal = FVector(0, -1, 0); OutAxisU = FVector(1, 0, 0);  OutAxisV = FVector(0, 0, 1);  break;
        case ECSCubeFace::PositiveZ:
            OutNormal = FVector(0, 0, 1);  OutAxisU = FVector(1, 0, 0);  OutAxisV = FVector(0, 1, 0);  break;
        case ECSCubeFace::NegativeZ:
            OutNormal = FVector(0, 0, -1); OutAxisU = FVector(1, 0, 0);  OutAxisV = FVector(0, -1, 0); break;
        default:
            OutNormal = FVector(0, 0, 1);  OutAxisU = FVector(1, 0, 0);  OutAxisV = FVector(0, 1, 0);  break;
        }
    }

    /**
     * (cara, U, V) con U,V en [-1,1] -> punto sobre el cubo (sin normalizar).
     * Normalizarlo da la dirección sobre la esfera.
     */
    inline FVector FaceUVToCubePoint(ECSCubeFace Face, float U, float V)
    {
        FVector AxisU, AxisV, Normal;
        GetFaceAxes(Face, AxisU, AxisV, Normal);
        return Normal + AxisU * U + AxisV * V;
    }

    /** Igual que la anterior, pero devolviendo ya la dirección normalizada sobre la esfera. */
    inline FVector FaceUVToDirection(ECSCubeFace Face, float U, float V)
    {
        return FaceUVToCubePoint(Face, U, V).GetSafeNormal();
    }

    /**
     * Dirección 3D -> (cara dominante, U, V) con U,V en [-1,1].
     * Inversa exacta de FaceUVToDirection: la cara elegida es aquella cuyo eje domina.
     * No hace falta que Dir venga normalizada.
     */
    inline void DirectionToFaceUV(const FVector& Dir, ECSCubeFace& OutFace, float& OutU, float& OutV)
    {
        const FVector Abs(FMath::Abs(Dir.X), FMath::Abs(Dir.Y), FMath::Abs(Dir.Z));

        if (Abs.X >= Abs.Y && Abs.X >= Abs.Z)
        {
            OutFace = Dir.X >= 0.0f ? ECSCubeFace::PositiveX : ECSCubeFace::NegativeX;
        }
        else if (Abs.Y >= Abs.X && Abs.Y >= Abs.Z)
        {
            OutFace = Dir.Y >= 0.0f ? ECSCubeFace::PositiveY : ECSCubeFace::NegativeY;
        }
        else
        {
            OutFace = Dir.Z >= 0.0f ? ECSCubeFace::PositiveZ : ECSCubeFace::NegativeZ;
        }

        FVector AxisU, AxisV, Normal;
        GetFaceAxes(OutFace, AxisU, AxisV, Normal);

        // El punto sobre el cubo es Dir escalado hasta que su componente normal valga 1.
        // Proyectarlo sobre los ejes locales devuelve exactamente los U,V de partida.
        const float NormalComponent = FVector::DotProduct(Dir, Normal);
        if (FMath::IsNearlyZero(NormalComponent))
        {
            OutU = 0.0f;
            OutV = 0.0f;
            return;
        }

        const float InvScale = 1.0f / NormalComponent;
        OutU = FVector::DotProduct(Dir, AxisU) * InvScale;
        OutV = FVector::DotProduct(Dir, AxisV) * InvScale;
    }

    // ============================================================
    // VARIANTES EN COORDENADAS DE TEXTURA [0,1]
    // Las usan los sistemas rasterizados (RasterizedTectonics y sucesores).
    // ============================================================

    /** Índice de píxel (X,Y) sobre una textura de lado Resolution -> dirección en la esfera (centro del píxel). */
    inline FVector PixelToDirection(ECSCubeFace Face, int32 X, int32 Y, int32 Resolution)
    {
        const float U = (static_cast<float>(X) + 0.5f) / static_cast<float>(Resolution) * 2.0f - 1.0f;
        const float V = (static_cast<float>(Y) + 0.5f) / static_cast<float>(Resolution) * 2.0f - 1.0f;
        return FaceUVToDirection(Face, U, V);
    }

    /** Dirección 3D -> (cara, TexU, TexV) con TexU,TexV en [0,1], listos para muestreo bilineal. */
    inline void DirectionToFaceTexUV(const FVector& Dir, ECSCubeFace& OutFace, float& OutTexU, float& OutTexV)
    {
        float U, V;
        DirectionToFaceUV(Dir, OutFace, U, V);
        OutTexU = (U + 1.0f) * 0.5f;
        OutTexV = (V + 1.0f) * 0.5f;
    }
}
