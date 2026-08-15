// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "RasterizedTectonics.h"
#include "TectonicPlateSystem.h"
#include "SphericalVoronoi.h"
#include "PlateKinematics.h"
#include "../CubeSphereGrid.h"
#include "../CubeFaceMapping.h"
#include "../Noise/SimplexNoise.h"

DEFINE_LOG_CATEGORY_STATIC(LogRasterizedTectonics, Log, All);

// ============================================================
// URasterizedTectonics IMPLEMENTATION
// ============================================================

URasterizedTectonics::URasterizedTectonics()
{
    FaceData.SetNum(6);
}

URasterizedTectonics::~URasterizedTectonics()
{
    ReleaseResources();
}

void URasterizedTectonics::Initialize(UCubeSphereGrid* InGrid, UTectonicPlateSystem* InPlateSystem, int32 TextureResolution)
{
    if (bIsInitialized)
    {
        ReleaseResources();
    }

    if (!InGrid || !InPlateSystem)
    {
        UE_LOG(LogRasterizedTectonics, Error, TEXT("Initialize: Invalid Grid or PlateSystem"));
        return;
    }

    Grid = InGrid;
    PlateSystem = InPlateSystem;
    Resolution = FMath::RoundUpToPowerOfTwo(TextureResolution);

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Initializing RasterizedTectonics with resolution %d"), Resolution);

    // Inicializar datos CPU
    const int32 PixelsPerFace = Resolution * Resolution;
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];
        Face.PlateIDData.SetNumZeroed(PixelsPerFace);
        Face.ElevationData.SetNumZeroed(PixelsPerFace);
        Face.VelocityData.SetNum(PixelsPerFace);
        Face.CrustAgeData.SetNumZeroed(PixelsPerFace);
        Face.CrustTypeData.SetNumZeroed(PixelsPerFace);

        // Inicializar velocidades a cero
        for (int32 i = 0; i < PixelsPerFace; ++i)
        {
            Face.VelocityData[i] = FVector2f::ZeroVector;
        }
        
        Face.bIsValid = true;
    }

    // Poblar texturas desde el sistema de placas
    InitializeFromPlateSystem();

    bIsInitialized = true;
    StepCount = 0;
    TotalSimulationTime = 0.0f;

    UE_LOG(LogRasterizedTectonics, Log, TEXT("RasterizedTectonics initialized successfully"));
}

void URasterizedTectonics::ReleaseResources()
{
    if (!bIsInitialized)
    {
        return;
    }

    ReleaseGPUResources();

    // Limpiar datos CPU
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];
        Face.PlateIDData.Empty();
        Face.ElevationData.Empty();
        Face.VelocityData.Empty();
        Face.CrustAgeData.Empty();
        Face.CrustTypeData.Empty();
        Face.bIsValid = false;
    }

    bIsInitialized = false;
    UE_LOG(LogRasterizedTectonics, Log, TEXT("RasterizedTectonics resources released"));
}

void URasterizedTectonics::CreateGPUResources()
{
    // TODO: Crear texturas GPU y buffers cuando se necesite aceleración GPU
    // Por ahora la simulación se hace en CPU
    bGPUResourcesCreated = true;
}

void URasterizedTectonics::ReleaseGPUResources()
{
    // TODO: Liberar recursos GPU
    bGPUResourcesCreated = false;
}

void URasterizedTectonics::UpdatePlateDataBuffer()
{
    // TODO: Actualizar buffer GPU con datos de placas
}

void URasterizedTectonics::InitializeFromPlateSystem()
{
    if (!PlateSystem || !Grid)
    {
        return;
    }

    const TArray<FTectonicPlate>& Plates = PlateSystem->GetPlates();

    // Se usan los MISMOS centroides que USphericalVoronoi (los de Fibonacci, sobre la
    // esfera unitaria), no FTectonicPlate::Centroid. Este último lo recalcula
    // CalculatePlateStatistics como promedio de las celdas de cada placa, así que
    // difiere del que uso el Voronoi y producía una asignación placa->celda distinta
    // cerca de las fronteras (ROADMAP.md F0). Con los mismos centroides y el mismo
    // criterio (más cercano por producto escalar), los dos mapas solo pueden diferir
    // por la distinta resolución de cada uno, no por el criterio.
    //
    // La unificación completa - que este ráster sea la única fuente de verdad del campo
    // de IDs y que UTectonicPlateSystem lea de aquí - es parte de F1: en cuanto las
    // placas se muevan, el mapa del Voronoi queda obsoleto al primer paso y pasa a ser
    // solo la condición inicial.
    TArray<FVector> Centroids;
    if (USphericalVoronoi* Voronoi = PlateSystem->GetVoronoi())
    {
        Centroids = Voronoi->GetCentroids();
    }
    const bool bHasVoronoiCentroids = (Centroids.Num() == Plates.Num());
    if (!bHasVoronoiCentroids)
    {
        UE_LOG(LogRasterizedTectonics, Warning,
            TEXT("Sin centroides de Voronoi (%d para %d placas); se recurre a FTectonicPlate::Centroid"),
            Centroids.Num(), Plates.Num());
    }

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Initializing textures from %d plates"), Plates.Num());

    // Para cada píxel, determinar qué placa lo "posee"
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                // Convención unificada en CubeFaceMapping (ver ROADMAP.md F0): antes había
                // aquí un switch propio que invertía V en las dos caras polares respecto
                // al del Grid, dejando espejados el mapa de placas y el de elevación.
                const FVector SphereDir = CubeFaceMapping::PixelToDirection(
                    static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);

                // Placa de centroide más cercano. Sobre la esfera unitaria el producto
                // escalar mayor equivale a la distancia geodésica menor, igual que en
                // USphericalVoronoi::AssignCellsToPlates.
                int32 ClosestPlateID = 0;
                float BestDot = -2.0f;

                for (int32 PlateIdx = 0; PlateIdx < Plates.Num(); ++PlateIdx)
                {
                    const FVector Centroid = bHasVoronoiCentroids
                        ? Centroids[PlateIdx]
                        : Plates[PlateIdx].Centroid.GetSafeNormal();

                    const float Dot = static_cast<float>(FVector::DotProduct(SphereDir, Centroid));
                    if (Dot > BestDot)
                    {
                        BestDot = Dot;
                        ClosestPlateID = PlateIdx;
                    }
                }

                const FTectonicPlate& ClosestPlate = Plates[ClosestPlateID];
                const bool bIsContinental = (ClosestPlate.CrustType == ECrustType::Continental);
                const FVector ClosestEulerPole = ClosestPlate.EulerPole;
                const float ClosestAngularVelocity = ClosestPlate.AngularVelocity;

                const int32 LinearIdx = GetLinearIndex(X, Y);
                Face.PlateIDData[LinearIdx] = static_cast<uint8>(ClosestPlateID);
                
                // ====================================================
                // ELEVACIÓN CON RUIDO FRACTAL PARA COSTAS ORGÁNICAS
                // ====================================================
                
                // Elevación base por tipo de corteza
                float BaseElevation = bIsContinental ? 840.0f : -3800.0f;
                
                // Ruido fractal multicapa para variación natural
                // Frecuencia 8 = ~8 "continentes" de ruido alrededor de la esfera
                float LargeNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 4.0f, 4, 2.0f, 0.5f);
                float MediumNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 12.0f, 4, 2.0f, 0.5f);
                float SmallNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 32.0f, 3, 2.0f, 0.5f);
                float TinyNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 80.0f, 2, 2.0f, 0.5f);
                
                // Combinar escalas (más peso a las grandes)
                // Large: variación regional ~2000m
                // Medium: colinas ~800m
                // Small: detalle ~200m
                // Tiny: micro-detalle ~50m
                float NoiseValue = LargeNoise * 2000.0f 
                                 + MediumNoise * 800.0f 
                                 + SmallNoise * 200.0f 
                                 + TinyNoise * 50.0f;
                
                // El ruido desplaza la "frontera" entre continental y oceánico
                // Esto crea costas fractales en lugar de líneas rectas
                if (bIsContinental)
                {
                    // Corteza continental: variación positiva
                    Face.ElevationData[LinearIdx] = BaseElevation + NoiseValue * 0.8f;
                }
                else
                {
                    // Corteza oceánica: menos variación, más profunda
                    Face.ElevationData[LinearIdx] = BaseElevation + NoiseValue * 0.3f;
                }
                
                // Tipo de corteza
                Face.CrustTypeData[LinearIdx] = bIsContinental ? 1 : 0;
                
                // Edad inicial aleatoria (en millones de años)
                Face.CrustAgeData[LinearIdx] = FMath::FRandRange(0.0f, 200.0f);
                
                // Calcular velocidad tangencial basada en el polo de Euler
                // V = omega x r (velocidad angular cruzada con posición)
                FVector AngularVelocityVec = ClosestEulerPole * ClosestAngularVelocity;
                FVector Velocity3D = FVector::CrossProduct(AngularVelocityVec, SphereDir);
                
                // Proyectar a 2D tangente a la superficie (usando U y V locales)
                // Para simplificar, usamos las componentes Y y Z de la velocidad
                // Ejes locales de la cara desde la tabla única (ver CubeFaceMapping.h).
                // La copia que había aquí también tenía la V polar invertida.
                FVector TangentU, TangentV, FaceNormal;
                CubeFaceMapping::GetFaceAxes(static_cast<ECSCubeFace>(FaceIdx), TangentU, TangentV, FaceNormal);
                
                float VelU = FVector::DotProduct(Velocity3D, TangentU);
                float VelV = FVector::DotProduct(Velocity3D, TangentV);
                Face.VelocityData[LinearIdx] = FVector2f(VelU, VelV);
            }
        }
    }

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Textures initialized from plate system with velocities"));
}

void URasterizedTectonics::ApplyFractalNoise(const FFractalNoiseParams& Params)
{
    if (!bIsInitialized || !Grid)
    {
        UE_LOG(LogRasterizedTectonics, Warning, TEXT("ApplyFractalNoise: System not initialized"));
        return;
    }

    // Inicializar el generador de ruido con la semilla
    FSimplexNoise::Initialize(Params.Seed);

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Applying fractal noise with seed %d"), Params.Seed);

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                // Misma conversión unificada que InitializeFromPlateSystem (CubeFaceMapping.h).
                // Es imprescindible que ambas usen exactamente la misma: este ruido se suma
                // encima de la elevación que aquella escribió.
                const FVector SphereDir = CubeFaceMapping::PixelToDirection(
                    static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);
                const int32 LinearIdx = GetLinearIndex(X, Y);
                const bool bIsContinental = (Face.CrustTypeData[LinearIdx] == 1);
                
                // Ruido fractal multicapa
                float LargeNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 4.0f, 4, 2.0f, 0.5f);
                float MediumNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 12.0f, 4, 2.0f, 0.5f);
                float SmallNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 32.0f, 3, 2.0f, 0.5f);
                float TinyNoise = FSimplexNoise::SphereFractalNoise(SphereDir, 80.0f, 2, 2.0f, 0.5f);
                
                float NoiseValue = LargeNoise * Params.LargeScaleAmplitude 
                                 + MediumNoise * Params.MediumScaleAmplitude 
                                 + SmallNoise * Params.SmallScaleAmplitude 
                                 + TinyNoise * Params.TinyScaleAmplitude;
                
                // Aplicar ruido según tipo de corteza
                float NoiseFactor = bIsContinental ? Params.ContinentalNoiseFactor : Params.OceanicNoiseFactor;
                Face.ElevationData[LinearIdx] += NoiseValue * NoiseFactor;
            }
        }
    }

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Fractal noise applied"));
}

float URasterizedTectonics::GetPixelAngularSize() const
{
    // Una cara abarca 90 grados repartidos en Resolution pixeles. Es una aproximacion:
    // cerca de las esquinas del cubo los pixeles cubren mas angulo por la distorsion
    // gnomonica, pero para decidir "ya toca advectar" sobra con el valor del centro.
    return (Resolution > 0) ? (PI * 0.5f / static_cast<float>(Resolution)) : 0.0f;
}

float URasterizedTectonics::GetCrustAgeAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    const int32 FaceIdx = static_cast<int32>(Face);
    if (!bIsInitialized || FaceIdx < 0 || FaceIdx >= 6 || !IsValidCoord(X, Y))
    {
        return 0.0f;
    }
    return FaceData[FaceIdx].CrustAgeData[GetLinearIndex(X, Y)];
}

int32 URasterizedTectonics::GetCrustTypeAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    const int32 FaceIdx = static_cast<int32>(Face);
    if (!bIsInitialized || FaceIdx < 0 || FaceIdx >= 6 || !IsValidCoord(X, Y))
    {
        return 0;
    }
    return static_cast<int32>(FaceData[FaceIdx].CrustTypeData[GetLinearIndex(X, Y)]);
}

// ============================================================
// ADVECCION DE PLACAS (ROADMAP.md F1)
//
// Este es el metodo que hace que las placas se muevan de verdad. Hasta el 15-08-2026 el
// campo de IDs no se tocaba nunca: el sistema era un generador de relieve estatico, con
// crestas creciendo siempre en las mismas lineas.
//
// El algoritmo es adveccion HACIA ATRAS (semi-lagrangiana). Para cada pixel de la rejilla
// nueva se pregunta de donde viene, en vez de empujar cada pixel viejo hacia donde va.
// La diferencia importa: empujar hacia delante deja huecos y solapes por redondeo, y no
// da ninguna forma natural de detectar colisiones. Preguntando hacia atras, cada celda de
// destino se resuelve exactamente una vez, y el NUMERO DE RECLAMANTES es por si solo la
// clasificacion del borde:
//
//   1 reclamante  -> movimiento normal
//   0 reclamantes -> las placas se separan: rift, corteza nueva
//   2 o mas       -> convergen: subduccion u orogenia
//
// No hay que detectar "esto es una dorsal" en ningun sitio: sale del conteo.
// ============================================================
void URasterizedTectonics::AdvectPlateField(float DeltaTime)
{
    if (!bIsInitialized || !PlateSystem)
    {
        return;
    }

    const TArray<FTectonicPlate>& Plates = PlateSystem->GetPlates();
    const int32 NumPlates = Plates.Num();
    if (NumPlates == 0)
    {
        return;
    }

    // Rotacion INVERSA de cada placa: lleva un punto de "ahora" al lugar que ocupaba
    // hace DeltaTime.
    TArray<FQuat> InverseRotations;
    InverseRotations.Reserve(NumPlates);
    for (const FTectonicPlate& Plate : Plates)
    {
        InverseRotations.Add(UPlateKinematics::CalculatePlateRotation(Plate, DeltaTime).Inverse());
    }

    // Copia del estado anterior. Imprescindible: la adveccion lee el pasado mientras
    // escribe el presente, y sin copia unas celdas verian datos ya sobrescritos y otras
    // no, segun el orden de recorrido.
    const TArray<FTectonicFaceTextureData> Prev = FaceData;

    struct FFaceCounters
    {
        int32 Moved = 0;
        int32 Created = 0;
        int32 Destroyed = 0;
        int32 Collisions = 0;
    };
    TArray<FFaceCounters> Counters;
    Counters.SetNum(6);

    ParallelFor(6, [&](int32 FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];
        FFaceCounters& Count = Counters[FaceIdx];

        TArray<int32> Claimants;
        TArray<int32> SourceFace;
        TArray<int32> SourceIdx;
        Claimants.Reserve(NumPlates);
        SourceFace.Reserve(NumPlates);
        SourceIdx.Reserve(NumPlates);

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                const int32 Idx = Y * Resolution + X;
                const FVector Dir = CubeFaceMapping::PixelToDirection(
                    static_cast<ECSCubeFace>(FaceIdx), X, Y, Resolution);

                Claimants.Reset();
                SourceFace.Reset();
                SourceIdx.Reset();

                for (int32 P = 0; P < NumPlates; ++P)
                {
                    const FVector PrevDir = InverseRotations[P].RotateVector(Dir);

                    ECSCubeFace PF;
                    float PU, PV;
                    CubeFaceMapping::DirectionToFaceTexUV(PrevDir, PF, PU, PV);

                    const int32 PFaceIdx = static_cast<int32>(PF);
                    const int32 PX = FMath::Clamp(FMath::FloorToInt(PU * Resolution), 0, Resolution - 1);
                    const int32 PY = FMath::Clamp(FMath::FloorToInt(PV * Resolution), 0, Resolution - 1);
                    const int32 PIdx = PY * Resolution + PX;

                    if (Prev[PFaceIdx].PlateIDData[PIdx] == static_cast<uint8>(P))
                    {
                        Claimants.Add(P);
                        SourceFace.Add(PFaceIdx);
                        SourceIdx.Add(PIdx);
                    }
                }

                if (Claimants.Num() == 1)
                {
                    // Movimiento limpio: se arrastra todo el estado desde el origen. Si no
                    // se arrastrara el tipo de corteza, un continente cambiaria de tipo al
                    // desplazarse y se disolveria en el oceano.
                    const int32 SF = SourceFace[0];
                    const int32 SI = SourceIdx[0];

                    Face.PlateIDData[Idx]   = static_cast<uint8>(Claimants[0]);
                    Face.ElevationData[Idx] = Prev[SF].ElevationData[SI];
                    Face.CrustAgeData[Idx]  = Prev[SF].CrustAgeData[SI];
                    Face.CrustTypeData[Idx] = Prev[SF].CrustTypeData[SI];
                    ++Count.Moved;
                }
                else if (Claimants.Num() == 0)
                {
                    // RIFT. Nadie ocupaba este punto: dos placas se han separado y aqui
                    // aflora manto. Corteza oceanica nueva, edad 0, y elevacion de dorsal
                    // (una dorsal esta ~1300 m por encima de la llanura abisal porque la
                    // corteza recien formada esta caliente y flota mas).
                    //
                    // Se asigna a la placa que estaba aqui antes: la corteza nueva se
                    // suelda al borde de la placa que se aleja, que es lo que ocurre.
                    const uint8 PreviousOwner = Prev[FaceIdx].PlateIDData[Idx];
                    Face.PlateIDData[Idx]   = (PreviousOwner < NumPlates) ? PreviousOwner : 0;
                    Face.CrustTypeData[Idx] = 0;
                    Face.CrustAgeData[Idx]  = 0.0f;
                    Face.ElevationData[Idx] = -2500.0f;
                    ++Count.Created;
                }
                else
                {
                    // COLISION. Gana una placa y el resto subducen.
                    ++Count.Collisions;

                    int32 Winner = 0;
                    for (int32 C = 1; C < Claimants.Num(); ++C)
                    {
                        const int32 WF = SourceFace[Winner], WI = SourceIdx[Winner];
                        const int32 CF = SourceFace[C],      CI = SourceIdx[C];

                        const bool bWinnerContinental = (Prev[WF].CrustTypeData[WI] == 1);
                        const bool bChallengerContinental = (Prev[CF].CrustTypeData[CI] == 1);

                        if (bChallengerContinental != bWinnerContinental)
                        {
                            // Continental sobre oceanica: la oceanica es mas densa y
                            // subduce. Por eso los continentes persisten miles de millones
                            // de anos mientras el fondo oceanico se recicla entero.
                            if (bChallengerContinental)
                            {
                                Winner = C;
                            }
                        }
                        else if (!bWinnerContinental)
                        {
                            // Oceanica contra oceanica: subduce la MAS VIEJA, que se ha
                            // enfriado y es mas densa. Gana la mas joven.
                            if (Prev[CF].CrustAgeData[CI] < Prev[WF].CrustAgeData[WI])
                            {
                                Winner = C;
                            }
                        }
                        else
                        {
                            // Continental contra continental: ninguna subduce, las dos
                            // flotan. Se queda la mas alta, aproximacion barata a que el
                            // material se apila. El relieve de la colision en si lo
                            // produce el termino de frontera de Step().
                            if (Prev[CF].ElevationData[CI] > Prev[WF].ElevationData[WI])
                            {
                                Winner = C;
                            }
                        }
                    }

                    const int32 WF = SourceFace[Winner];
                    const int32 WI = SourceIdx[Winner];

                    Face.PlateIDData[Idx]   = static_cast<uint8>(Claimants[Winner]);
                    Face.ElevationData[Idx] = Prev[WF].ElevationData[WI];
                    Face.CrustAgeData[Idx]  = Prev[WF].CrustAgeData[WI];
                    Face.CrustTypeData[Idx] = Prev[WF].CrustTypeData[WI];

                    // Cada perdedor es una celda de corteza que desaparece: es la
                    // contraparte de la creacion en dorsales que faltaba (el TODO de
                    // BoundaryInteractions.cpp:609).
                    Count.Destroyed += Claimants.Num() - 1;
                }

                // La velocidad depende de donde esta el punto AHORA y de quien lo posee
                // ahora, asi que se recalcula tras resolver la propiedad. Si se dejara la
                // del paso anterior, la deteccion de convergencia de Step() estaria usando
                // la velocidad de una placa que ya no esta aqui.
                const int32 OwnerId = static_cast<int32>(Face.PlateIDData[Idx]);
                if (Plates.IsValidIndex(OwnerId))
                {
                    const FVector AngularVel =
                        Plates[OwnerId].EulerPole.GetSafeNormal() * Plates[OwnerId].AngularVelocity;
                    const FVector Velocity3D = FVector::CrossProduct(AngularVel, Dir);

                    FVector TangentU, TangentV, FaceNormal;
                    CubeFaceMapping::GetFaceAxes(static_cast<ECSCubeFace>(FaceIdx), TangentU, TangentV, FaceNormal);

                    Face.VelocityData[Idx] = FVector2f(
                        static_cast<float>(FVector::DotProduct(Velocity3D, TangentU)),
                        static_cast<float>(FVector::DotProduct(Velocity3D, TangentV)));
                }
            }
        }
    });

    for (const FFaceCounters& C : Counters)
    {
        AdvectionStats.CellsMoved     += C.Moved;
        AdvectionStats.CellsCreated   += C.Created;
        AdvectionStats.CellsDestroyed += C.Destroyed;
        AdvectionStats.CollisionCells += C.Collisions;
    }
    AdvectionStats.AdvectionCount++;
    AdvectionStats.AdvectedTime += DeltaTime;
}

void URasterizedTectonics::Step(const FPlateMovementParams& Params)
{
    if (!bIsInitialized)
    {
        UE_LOG(LogRasterizedTectonics, Warning, TEXT("Step called but system not initialized"));
        return;
    }

    // Simulación CPU simplificada
    // TODO: Implementar versión GPU con compute shaders
    
    const float DeltaTimeScaled = Params.DeltaTime * Params.TimeScale;

    // ============================================================
    // MOVIMIENTO DE PLACAS (ROADMAP.md F1)
    //
    // No se advecta en cada paso, y no es una optimizacion sino lo correcto. Con los
    // valores por defecto la placa mas rapida gira ~8e-5 rad por paso, mientras que un
    // pixel abarca ~6.1e-3 rad a Resolution=256: seria 1/76 de pixel. Advectar ahi no
    // movería nada y cada remuestreo introduce difusion numerica, asi que hacerlo 76
    // veces en vez de una emborrona el campo de placas a cambio de nada.
    //
    // Se acumula tiempo hasta que la placa mas rapida recorreria un pixel, y entonces se
    // advecta de una vez con el dt acumulado.
    // ============================================================
    PendingAdvectionTime += DeltaTimeScaled;

    if (PlateSystem)
    {
        float MaxAngularSpeed = 0.0f;
        for (const FTectonicPlate& Plate : PlateSystem->GetPlates())
        {
            MaxAngularSpeed = FMath::Max(MaxAngularSpeed, FMath::Abs(Plate.AngularVelocity));
        }

        const float PixelAngle = GetPixelAngularSize();
        if (MaxAngularSpeed > KINDA_SMALL_NUMBER && PixelAngle > 0.0f)
        {
            if (MaxAngularSpeed * PendingAdvectionTime >= PixelAngle)
            {
                AdvectPlateField(PendingAdvectionTime);
                PendingAdvectionTime = 0.0f;
            }
        }
    }

    // Procesar cada cara
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        FTectonicFaceTextureData& Face = FaceData[FaceIdx];
        
        // Actualizar edad de corteza
        for (int32 i = 0; i < Face.CrustAgeData.Num(); ++i)
        {
            Face.CrustAgeData[i] += DeltaTimeScaled;
        }
        
        // Detectar y procesar bordes de placa (simplificado)
        for (int32 Y = 1; Y < Resolution - 1; ++Y)
        {
            for (int32 X = 1; X < Resolution - 1; ++X)
            {
                const int32 Idx = GetLinearIndex(X, Y);
                const uint8 CurrentPlateID = Face.PlateIDData[Idx];
                
                // Verificar vecinos
                int32 Offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                bool bAtBoundary = false;
                float ConvergenceSum = 0.0f;
                
                for (int32 i = 0; i < 4; ++i)
                {
                    const int32 NeighborIdx = GetLinearIndex(X + Offsets[i][0], Y + Offsets[i][1]);
                    if (Face.PlateIDData[NeighborIdx] != CurrentPlateID)
                    {
                        bAtBoundary = true;
                        
                        // Calcular convergencia aproximada basada en velocidades
                        FVector2f Dir(static_cast<float>(Offsets[i][0]), static_cast<float>(Offsets[i][1]));
                        Dir.Normalize();
                        
                        FVector2f RelVel = Face.VelocityData[Idx] - Face.VelocityData[NeighborIdx];
                        float Convergence = -FVector2f::DotProduct(RelVel, Dir);
                        ConvergenceSum += Convergence;
                    }
                }
                
                if (bAtBoundary)
                {
                    // Aplicar cambios de elevación basados en convergencia/divergencia
                    if (ConvergenceSum > 0.0f) // Convergente
                    {
                        float ElevationChange = ConvergenceSum * Params.OrogenyFactor * DeltaTimeScaled;
                        Face.ElevationData[Idx] = FMath::Min(Face.ElevationData[Idx] + ElevationChange, 12000.0f);
                    }
                    else if (ConvergenceSum < 0.0f) // Divergente
                    {
                        // En zonas divergentes, crear nueva corteza oceánica
                        if (Face.CrustTypeData[Idx] == 0) // Ya es oceánica
                        {
                            Face.ElevationData[Idx] = Params.OceanicBaseElevation + 1300.0f; // Dorsal
                            Face.CrustAgeData[Idx] = 0.0f; // Nueva corteza
                        }
                    }
                }
            }
        }
    }

    // Relajación difusiva: sin esto, la elevación en celdas de frontera (incrementada
    // arriba) crece cada paso hasta el tope de 12000m mientras las celdas vecinas no
    // afectadas se quedan en la base, formando paredes casi verticales de una celda de
    // ancho. Se mezcla solo una fracción (Params.DiffusionRate) hacia el valor
    // suavizado localmente en vez de reemplazarlo por completo: aplicado cada paso
    // durante miles de pasos, un reemplazo completo aplana el planeta entero.
    if (Params.DiffusionRate > 0.0f)
    {
        const float Kernel[3][3] = {
            { 1.0f/16.0f, 2.0f/16.0f, 1.0f/16.0f },
            { 2.0f/16.0f, 4.0f/16.0f, 2.0f/16.0f },
            { 1.0f/16.0f, 2.0f/16.0f, 1.0f/16.0f }
        };

        // Igual que en SmoothElevation: instantanea previa para que el resultado no
        // dependa del orden de las caras, y vecinos que cruzan costuras.
        TArray<TArray<float>> Snapshot;
        Snapshot.SetNum(6);
        for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
            Snapshot[FaceIdx] = FaceData[FaceIdx].ElevationData;
        }

        for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
            const ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
            TArray<float>& ElevData = FaceData[FaceIdx].ElevationData;
            TArray<float> TempData;
            TempData.SetNumUninitialized(ElevData.Num());

            for (int32 Y = 0; Y < Resolution; ++Y)
            {
                for (int32 X = 0; X < Resolution; ++X)
                {
                    float Sum = 0.0f;
                    for (int32 KY = -1; KY <= 1; ++KY)
                    {
                        for (int32 KX = -1; KX <= 1; ++KX)
                        {
                            Sum += SampleNeighborElevation(Snapshot, Face, X, Y, KX, KY) * Kernel[KY + 1][KX + 1];
                        }
                    }

                    const int32 Idx = GetLinearIndex(X, Y);
                    TempData[Idx] = FMath::Lerp(ElevData[Idx], Sum, Params.DiffusionRate);
                }
            }

            ElevData = MoveTemp(TempData);
        }
    }

    TotalSimulationTime += DeltaTimeScaled;
    StepCount++;

    if (StepCount % 100 == 0)
    {
        UE_LOG(LogRasterizedTectonics, Log, TEXT("Step %d, SimTime: %.2f Ma"), StepCount, TotalSimulationTime);
    }
}

void URasterizedTectonics::SyncFromGPU()
{
    // No-op en versión CPU
    // TODO: Implementar cuando tengamos recursos GPU
}

void URasterizedTectonics::SyncToGPU()
{
    // No-op en versión CPU
    // TODO: Implementar cuando tengamos recursos GPU
}

float URasterizedTectonics::GetElevationAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    if (!bIsInitialized || !IsValidCoord(X, Y))
    {
        return 0.0f;
    }

    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx < 0 || FaceIdx >= 6 || !FaceData[FaceIdx].bIsValid)
    {
        return 0.0f;
    }
    
    return FaceData[FaceIdx].ElevationData[GetLinearIndex(X, Y)];
}

float URasterizedTectonics::GetElevationBilinear(ECSCubeFace Face, float U, float V) const
{
    if (!bIsInitialized)
    {
        return 0.0f;
    }

    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx < 0 || FaceIdx >= 6 || !FaceData[FaceIdx].bIsValid)
    {
        return 0.0f;
    }

    // Convertir UV [0,1] a coordenadas de píxel con precisión float
    const float PixelX = U * (Resolution - 1);
    const float PixelY = V * (Resolution - 1);
    
    // Obtener los 4 píxeles vecinos
    const int32 X0 = FMath::Clamp(static_cast<int32>(PixelX), 0, Resolution - 1);
    const int32 Y0 = FMath::Clamp(static_cast<int32>(PixelY), 0, Resolution - 1);
    const int32 X1 = FMath::Clamp(X0 + 1, 0, Resolution - 1);
    const int32 Y1 = FMath::Clamp(Y0 + 1, 0, Resolution - 1);
    
    // Fracciones para interpolación
    const float FracX = PixelX - X0;
    const float FracY = PixelY - Y0;
    
    // Obtener elevaciones de los 4 vecinos
    const TArray<float>& ElevData = FaceData[FaceIdx].ElevationData;
    const float E00 = ElevData[GetLinearIndex(X0, Y0)];
    const float E10 = ElevData[GetLinearIndex(X1, Y0)];
    const float E01 = ElevData[GetLinearIndex(X0, Y1)];
    const float E11 = ElevData[GetLinearIndex(X1, Y1)];
    
    // Interpolación bilineal
    const float E0 = FMath::Lerp(E00, E10, FracX);  // Interpolación en X para Y0
    const float E1 = FMath::Lerp(E01, E11, FracX);  // Interpolación en X para Y1
    return FMath::Lerp(E0, E1, FracY);              // Interpolación final en Y
}

void URasterizedTectonics::SetElevationAt(ECSCubeFace Face, int32 X, int32 Y, float Elevation)
{
    if (!bIsInitialized || !IsValidCoord(X, Y))
    {
        return;
    }

    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx < 0 || FaceIdx >= 6 || !FaceData[FaceIdx].bIsValid)
    {
        return;
    }
    
    FaceData[FaceIdx].ElevationData[GetLinearIndex(X, Y)] = Elevation;
}

void URasterizedTectonics::SetElevationData(ECSCubeFace Face, const TArray<float>& InData)
{
    if (!bIsInitialized)
    {
        return;
    }

    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx < 0 || FaceIdx >= 6 || !FaceData[FaceIdx].bIsValid)
    {
        return;
    }

    if (InData.Num() != Resolution * Resolution)
    {
        return;
    }

    FaceData[FaceIdx].ElevationData = InData;
}

int32 URasterizedTectonics::GetPlateIDAt(ECSCubeFace Face, int32 X, int32 Y) const
{
    if (!bIsInitialized || !IsValidCoord(X, Y))
    {
        return -1;
    }

    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx < 0 || FaceIdx >= 6 || !FaceData[FaceIdx].bIsValid)
    {
        return -1;
    }
    
    return FaceData[FaceIdx].PlateIDData[GetLinearIndex(X, Y)];
}

bool URasterizedTectonics::GetNeighborPixel(ECSCubeFace Face, int32 X, int32 Y, int32 DX, int32 DY,
                                            ECSCubeFace& OutFace, int32& OutX, int32& OutY) const
{
    const int32 NewX = X + DX;
    const int32 NewY = Y + DY;

    if (NewX >= 0 && NewX < Resolution && NewY >= 0 && NewY < Resolution)
    {
        OutFace = Face;
        OutX = NewX;
        OutY = NewY;
        return true;
    }

    if (Resolution <= 0)
    {
        return false;
    }

    // Se sale del cuadrado UV a proposito y se reproyecta: la cara vecina y la rotacion
    // relativa entre ambas salen solas. Ver CubeFaceMapping.h.
    const float U = (static_cast<float>(NewX) + 0.5f) / static_cast<float>(Resolution) * 2.0f - 1.0f;
    const float V = (static_cast<float>(NewY) + 0.5f) / static_cast<float>(Resolution) * 2.0f - 1.0f;

    const FVector Dir = CubeFaceMapping::FaceUVToCubePoint(Face, U, V).GetSafeNormal();
    if (Dir.IsNearlyZero())
    {
        return false;
    }

    float NU, NV;
    CubeFaceMapping::DirectionToFaceUV(Dir, OutFace, NU, NV);

    OutX = FMath::Clamp(FMath::FloorToInt((NU + 1.0f) * 0.5f * Resolution), 0, Resolution - 1);
    OutY = FMath::Clamp(FMath::FloorToInt((NV + 1.0f) * 0.5f * Resolution), 0, Resolution - 1);
    return true;
}

float URasterizedTectonics::SampleNeighborElevation(const TArray<TArray<float>>& AllFaces,
                                                    ECSCubeFace Face, int32 X, int32 Y,
                                                    int32 DX, int32 DY) const
{
    ECSCubeFace NFace;
    int32 NX, NY;
    if (!GetNeighborPixel(Face, X, Y, DX, DY, NFace, NX, NY))
    {
        return AllFaces[static_cast<int32>(Face)][GetLinearIndex(X, Y)];
    }

    const int32 NIdx = static_cast<int32>(NFace);
    if (!AllFaces.IsValidIndex(NIdx) || AllFaces[NIdx].Num() != Resolution * Resolution)
    {
        return AllFaces[static_cast<int32>(Face)][GetLinearIndex(X, Y)];
    }

    return AllFaces[NIdx][NY * Resolution + NX];
}

void URasterizedTectonics::SmoothElevation(int32 Iterations)
{
    if (!bIsInitialized)
    {
        return;
    }

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Smoothing elevation with %d iterations"), Iterations);

    // Kernel gaussiano 3x3 (normalizado)
    const float Kernel[3][3] = {
        { 1.0f/16.0f, 2.0f/16.0f, 1.0f/16.0f },
        { 2.0f/16.0f, 4.0f/16.0f, 2.0f/16.0f },
        { 1.0f/16.0f, 2.0f/16.0f, 1.0f/16.0f }
    };

    for (int32 Iter = 0; Iter < Iterations; ++Iter)
    {
        // Instantanea de las 6 caras: la convolucion tiene que leer el estado ANTERIOR
        // tambien al cruzar a otra cara, o el resultado dependeria del orden en que se
        // procesan las caras.
        TArray<TArray<float>> Snapshot;
        Snapshot.SetNum(6);
        for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
            Snapshot[FaceIdx] = FaceData[FaceIdx].ElevationData;
        }

        for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
            if (!FaceData[FaceIdx].bIsValid)
            {
                continue;
            }

            const ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
            TArray<float>& ElevData = FaceData[FaceIdx].ElevationData;
            TArray<float> TempData;
            TempData.SetNumUninitialized(ElevData.Num());

            // Aplicar convolución
            for (int32 Y = 0; Y < Resolution; ++Y)
            {
                for (int32 X = 0; X < Resolution; ++X)
                {
                    float Sum = 0.0f;
                    
                    for (int32 KY = -1; KY <= 1; ++KY)
                    {
                        for (int32 KX = -1; KX <= 1; ++KX)
                        {
                            // Vecino real, cruzando a la cara contigua si toca. Antes se
                            // recortaba al borde de la cara, lo que sesgaba la costura.
                            Sum += SampleNeighborElevation(Snapshot, Face, X, Y, KX, KY) * Kernel[KY + 1][KX + 1];
                        }
                    }
                    
                    TempData[GetLinearIndex(X, Y)] = Sum;
                }
            }

            // Copiar resultado
            ElevData = MoveTemp(TempData);
        }
    }

    UE_LOG(LogRasterizedTectonics, Log, TEXT("Elevation smoothing complete"));
}

const TArray<float>& URasterizedTectonics::GetElevationData(ECSCubeFace Face) const
{
    static TArray<float> EmptyArray;
    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx >= 0 && FaceIdx < 6 && FaceData[FaceIdx].bIsValid)
    {
        return FaceData[FaceIdx].ElevationData;
    }
    return EmptyArray;
}

const TArray<uint8>& URasterizedTectonics::GetPlateIDData(ECSCubeFace Face) const
{
    static TArray<uint8> EmptyArray;
    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx >= 0 && FaceIdx < 6 && FaceData[FaceIdx].bIsValid)
    {
        return FaceData[FaceIdx].PlateIDData;
    }
    return EmptyArray;
}

const FTectonicFaceTextureData* URasterizedTectonics::GetFaceData(ECSCubeFace Face) const
{
    const int32 FaceIdx = static_cast<int32>(Face);
    if (FaceIdx >= 0 && FaceIdx < 6 && FaceData[FaceIdx].bIsValid)
    {
        return &FaceData[FaceIdx];
    }
    return nullptr;
}

FString URasterizedTectonics::GetDebugInfo() const
{
    if (!bIsInitialized)
    {
        return TEXT("RasterizedTectonics: Not initialized");
    }

    return FString::Printf(
        TEXT("RasterizedTectonics:\n")
        TEXT("  Resolution: %dx%d per face\n")
        TEXT("  Total pixels: %d\n")
        TEXT("  Steps: %d\n")
        TEXT("  SimTime: %.2f Ma\n")
        TEXT("  Continental area: %.1f%%\n")
        TEXT("  Avg elevation: %.1f m"),
        Resolution, Resolution,
        Resolution * Resolution * 6,
        StepCount,
        TotalSimulationTime,
        GetTotalContinentalArea() * 100.0f,
        GetAverageElevation()
    );
}

float URasterizedTectonics::GetTotalContinentalArea() const
{
    if (!bIsInitialized)
    {
        return 0.0f;
    }

    int32 ContinentalPixels = 0;
    int32 TotalPixels = 0;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        if (!FaceData[FaceIdx].bIsValid) continue;
        
        for (uint8 CrustType : FaceData[FaceIdx].CrustTypeData)
        {
            if (CrustType == 1) // Continental
            {
                ContinentalPixels++;
            }
            TotalPixels++;
        }
    }

    return TotalPixels > 0 ? static_cast<float>(ContinentalPixels) / static_cast<float>(TotalPixels) : 0.0f;
}

float URasterizedTectonics::GetTotalOceanicArea() const
{
    return 1.0f - GetTotalContinentalArea();
}

float URasterizedTectonics::GetAverageElevation() const
{
    if (!bIsInitialized)
    {
        return 0.0f;
    }

    double TotalElevation = 0.0;
    int32 TotalPixels = 0;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        if (!FaceData[FaceIdx].bIsValid) continue;
        
        for (float Elevation : FaceData[FaceIdx].ElevationData)
        {
            TotalElevation += Elevation;
            TotalPixels++;
        }
    }

    return TotalPixels > 0 ? static_cast<float>(TotalElevation / TotalPixels) : 0.0f;
}
