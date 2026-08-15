// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "RasterizedTectonics.h"
#include "TectonicPlateSystem.h"
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
    const float Radius = Grid->GetRadius();

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
                const FVector SpherePos = SphereDir * Radius;

                // Buscar placa más cercana
                int32 ClosestPlateID = 0;
                float MinDist = TNumericLimits<float>::Max();
                bool bIsContinental = false;
                FVector ClosestEulerPole = FVector::UpVector;
                float ClosestAngularVelocity = 0.0f;

                for (const FTectonicPlate& Plate : Plates)
                {
                    const float Dist = FVector::Dist(SpherePos, Plate.Centroid);
                    if (Dist < MinDist)
                    {
                        MinDist = Dist;
                        ClosestPlateID = Plate.PlateID;
                        bIsContinental = (Plate.CrustType == ECrustType::Continental);
                        ClosestEulerPole = Plate.EulerPole;
                        ClosestAngularVelocity = Plate.AngularVelocity;
                    }
                }

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

        for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
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
                            int32 SampleX = FMath::Clamp(X + KX, 0, Resolution - 1);
                            int32 SampleY = FMath::Clamp(Y + KY, 0, Resolution - 1);
                            Sum += ElevData[GetLinearIndex(SampleX, SampleY)] * Kernel[KY + 1][KX + 1];
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
        // Procesar cada cara
        for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
            if (!FaceData[FaceIdx].bIsValid)
            {
                continue;
            }

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
                            // Clamp a los bordes (mirror boundary)
                            int32 SampleX = FMath::Clamp(X + KX, 0, Resolution - 1);
                            int32 SampleY = FMath::Clamp(Y + KY, 0, Resolution - 1);
                            
                            Sum += ElevData[GetLinearIndex(SampleX, SampleY)] * Kernel[KY + 1][KX + 1];
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
