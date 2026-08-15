// PlanetHydrology.cpp

#include "PlanetHydrology.h"
#include "../Tectonics/RasterizedTectonics.h"
#include "../Climate/PlanetClimate.h"
#include "../CubeFaceMapping.h"

void UPlanetHydrology::Initialize(URasterizedTectonics* InTectonics, UPlanetClimate* InClimate)
{
    Tectonics = InTectonics;
    Climate = InClimate;

    if (!Tectonics || !Tectonics->IsInitialized())
    {
        bIsInitialized = false;
        return;
    }

    Resolution = Tectonics->GetTextureResolution();
    const int32 PixelsPerFace = Resolution * Resolution;

    DownstreamIndex.SetNum(6);
    DownstreamFace.SetNum(6);
    DischargeData.SetNum(6);
    LakeDepthData.SetNum(6);

    for (int32 F = 0; F < 6; ++F)
    {
        DownstreamIndex[F].Init(INDEX_NONE, PixelsPerFace);
        DownstreamFace[F].SetNumZeroed(PixelsPerFace);
        DischargeData[F].SetNumZeroed(PixelsPerFace);
        LakeDepthData[F].SetNumZeroed(PixelsPerFace);
    }

    bIsInitialized = true;
}

void UPlanetHydrology::Recompute()
{
    if (!bIsInitialized || !Tectonics || !Tectonics->IsInitialized())
    {
        return;
    }

    const float SeaLevel = Tectonics->GetSeaLevel();
    const int32 PixelsPerFace = Resolution * Resolution;

    Stats = FHydrologyStats();

    // ============================================================
    // PASO 1: direccion de drenaje (D8)
    //
    // Cada celda de tierra manda su agua al vecino MAS BAJO de los ocho que la rodean. Se
    // usan los ocho y no cuatro porque con solo cuatro las redes salen con artefactos en
    // cruz: un valle diagonal tiene que ir en zigzag y la red hereda esa cuadricula.
    //
    // El vecino se pide a GetClimateNeighbor, que cruza entre caras del cubo. Reimplementar
    // aqui la vecindad seria repetir el error que produjo los bugs de F0.
    // ============================================================
    const int32 DX[8] = { 1,  1,  0, -1, -1, -1,  0,  1 };
    const int32 DY[8] = { 0,  1,  1,  1,  0, -1, -1, -1 };

    // La distancia importa: un vecino diagonal esta mas lejos, asi que para la misma
    // diferencia de altura su PENDIENTE es menor. Sin corregirlo, el drenaje prefiere
    // sistematicamente las diagonales y las redes salen sesgadas a 45 grados.
    const float StepLength[8] = { 1.0f, 1.41421f, 1.0f, 1.41421f, 1.0f, 1.41421f, 1.0f, 1.41421f };

    ParallelFor(6, [&](int32 FaceIdx)
    {
        const ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        const TArray<float>& Elev = Tectonics->GetElevationData(Face);

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const int32 Idx = Y * Resolution + X;
                DownstreamIndex[FaceIdx][Idx] = INDEX_NONE;

                const float Here = Elev[Idx];
                if (Here < SeaLevel)
                {
                    continue;   // bajo el mar: el agua ya ha llegado
                }

                float BestSlope = 0.0f;
                int32 BestFace = INDEX_NONE;
                int32 BestIdx = INDEX_NONE;

                for (int32 D = 0; D < 8; ++D)
                {
                    ECSCubeFace NF;
                    int32 NX, NY;
                    if (!Tectonics->GetClimateNeighbor(Face, X, Y, DX[D], DY[D], NF, NX, NY))
                    {
                        continue;
                    }

                    const int32 NFaceIdx = static_cast<int32>(NF);
                    const int32 NIdx = NY * Resolution + NX;
                    const float There = Tectonics->GetElevationData(NF)[NIdx];

                    const float Slope = (Here - There) / StepLength[D];
                    if (Slope > BestSlope)
                    {
                        BestSlope = Slope;
                        BestFace = NFaceIdx;
                        BestIdx = NIdx;
                    }
                }

                if (BestIdx != INDEX_NONE)
                {
                    DownstreamFace[FaceIdx][Idx] = static_cast<uint8>(BestFace);
                    DownstreamIndex[FaceIdx][Idx] = BestIdx;
                }
            }
        }
    });

    // ============================================================
    // PASO 2: acumulacion de caudal
    //
    // Se recorren las celdas de MAYOR A MENOR altura y se pasa el agua cuesta abajo. El
    // orden garantiza que cuando le toca a una celda ya ha recibido todo lo que le llega de
    // arriba, asi que una sola pasada basta: no hace falta iterar hasta converger.
    //
    // Es secuencial por naturaleza (cada celda depende de las de arriba), asi que no se
    // paraleliza como el resto. Es tambien la razon de que este paso sea el candidato
    // dificil si algun dia se lleva a GPU.
    // ============================================================
    struct FCellRef
    {
        float Elevation;
        int32 Face;
        int32 Index;
    };

    TArray<FCellRef> Ordered;
    Ordered.Reserve(6 * PixelsPerFace);

    for (int32 F = 0; F < 6; ++F)
    {
        const TArray<float>& Elev = Tectonics->GetElevationData(static_cast<ECSCubeFace>(F));
        for (int32 i = 0; i < PixelsPerFace; ++i)
        {
            DischargeData[F][i] = 0.0f;
            LakeDepthData[F][i] = 0.0f;

            if (Elev[i] >= SeaLevel)
            {
                Ordered.Add({ Elev[i], F, i });
            }
        }
    }

    Stats.LandCells = Ordered.Num();
    if (Stats.LandCells == 0)
    {
        return;
    }

    Ordered.Sort([](const FCellRef& A, const FCellRef& B) { return A.Elevation > B.Elevation; });

    // Aporte local: la lluvia que cae sobre la propia celda. Se convierte de mm/ano a
    // m3/ano multiplicando por el area de celda, para que el caudal tenga unidades reales
    // y no sea un numero sin escala.
    const float RadiusMetres = 6371000.0f;
    const float CellSideMetres = (PI * 0.5f * RadiusMetres) / static_cast<float>(Resolution);
    const float CellAreaM2 = CellSideMetres * CellSideMetres;

    for (const FCellRef& Cell : Ordered)
    {
        float LocalPrecipMm = 1000.0f;   // valor de reserva si no hay clima conectado
        if (Climate && Climate->IsInitialized())
        {
            const int32 X = Cell.Index % Resolution;
            const int32 Y = Cell.Index / Resolution;
            LocalPrecipMm = Climate->GetPrecipitationAt(static_cast<ECSCubeFace>(Cell.Face), X, Y);
        }

        // mm/ano sobre m2 -> m3/ano
        const float LocalVolume = (LocalPrecipMm / 1000.0f) * CellAreaM2;

        DischargeData[Cell.Face][Cell.Index] += LocalVolume;
        const float Total = DischargeData[Cell.Face][Cell.Index];

        const int32 DownIdx = DownstreamIndex[Cell.Face][Cell.Index];
        if (DownIdx != INDEX_NONE)
        {
            const int32 DownFace = static_cast<int32>(DownstreamFace[Cell.Face][Cell.Index]);
            DischargeData[DownFace][DownIdx] += Total;
        }
        else
        {
            // Minimo local: el agua se queda. Es un lago o una cuenca endorreica, que
            // existen de verdad - el Caspio es el mayor del mundo - pero si salen
            // demasiados es que el relieve tiene hoyos numericos y el drenaje se atasca
            // antes de organizarse en redes.
            ++Stats.SinkCells;
            LakeDepthData[Cell.Face][Cell.Index] = Total / FMath::Max(CellAreaM2, 1.0f);
        }

        Stats.MaxDischarge = FMath::Max(Stats.MaxDischarge, Total);

        // "Es un cauce" si por ahi pasa mucha mas agua de la que cae sobre la propia celda:
        // entonces viene de aguas arriba.
        if (Total > LocalVolume * 100.0f)
        {
            ++Stats.ChannelCells;
        }
    }
}

bool UPlanetHydrology::GetDownstreamCell(ECSCubeFace Face, int32 X, int32 Y,
                                         ECSCubeFace& OutFace, int32& OutX, int32& OutY) const
{
    const int32 F = static_cast<int32>(Face);
    if (!bIsInitialized || F < 0 || F >= 6 || X < 0 || X >= Resolution || Y < 0 || Y >= Resolution)
    {
        return false;
    }

    const int32 Idx = Y * Resolution + X;
    const int32 DownIdx = DownstreamIndex[F][Idx];
    if (DownIdx == INDEX_NONE)
    {
        return false;
    }

    OutFace = static_cast<ECSCubeFace>(DownstreamFace[F][Idx]);
    OutX = DownIdx % Resolution;
    OutY = DownIdx / Resolution;
    return true;
}

float UPlanetHydrology::GetDischargeAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    const int32 F = static_cast<int32>(Face);
    if (!bIsInitialized || F < 0 || F >= 6 || X < 0 || X >= Resolution || Y < 0 || Y >= Resolution)
    {
        return 0.0f;
    }
    return DischargeData[F][Y * Resolution + X];
}

const TArray<float>& UPlanetHydrology::GetDischargeData(ECSCubeFace Face) const
{
    static const TArray<float> Empty;
    const int32 F = static_cast<int32>(Face);
    return (bIsInitialized && F >= 0 && F < 6) ? DischargeData[F] : Empty;
}

const TArray<float>& UPlanetHydrology::GetLakeDepthData(ECSCubeFace Face) const
{
    static const TArray<float> Empty;
    const int32 F = static_cast<int32>(Face);
    return (bIsInitialized && F >= 0 && F < 6) ? LakeDepthData[F] : Empty;
}
