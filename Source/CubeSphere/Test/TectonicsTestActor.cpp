// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "TectonicsTestActor.h"
#include "../CubeSphereGrid.h"
#include "../CubeFaceMapping.h"
#include "TectonicTypes.h"
#include "TectonicPlateSystem.h"
#include "PlateKinematics.h"
#include "BoundaryInteractions.h"
#include "RasterizedTectonics.h"
#include "SphericalVoronoi.h"
#include "TectonicSaveGame.h"
#include "ProceduralMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ATectonicsTestActor::ATectonicsTestActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    // Crear componente de malla procedural
    PlanetMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PlanetMesh"));
    RootComponent = PlanetMesh;
    
    // Configuración del mesh
    PlanetMesh->bUseAsyncCooking = true;
    PlanetMesh->SetCastShadow(true);  // Habilitar sombras en el mesh

    // Crear pivot para rotación del sol
    SunPivot = CreateDefaultSubobject<USceneComponent>(TEXT("SunPivot"));
    SunPivot->SetupAttachment(RootComponent);
    
    // Crear luz direccional (sol)
    SunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("SunLight"));
    SunLight->SetupAttachment(SunPivot);
    SunLight->SetRelativeRotation(FRotator(-45.0f, 0.0f, 0.0f));  // Ángulo inicial del sol
    SunLight->Intensity = 10.0f;
    SunLight->LightColor = FColor(255, 243, 217);  // Color cálido del sol
    SunLight->bEnableLightShaftBloom = false;
    SunLight->CastShadows = true;
    SunLight->CastDynamicShadows = true;
    SunLight->ForwardShadingPriority = 0;  // Prioridad más alta (luz principal)

    // Crear luz de relleno (fill light) - ilumina el lado oscuro suavemente
    // Usamos otro DirectionalLight en lugar de SkyLight para evitar el warning
    FillLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("FillLight"));
    FillLight->SetupAttachment(RootComponent);
    FillLight->SetRelativeRotation(FRotator(45.0f, 180.0f, 0.0f));  // Opuesto al sol
    FillLight->Intensity = 2.0f;  // Más suave que el sol
    FillLight->LightColor = FColor(150, 180, 220);  // Azulado (luz del cielo)
    FillLight->CastShadows = false;  // Sin sombras para la luz de relleno
    FillLight->ForwardShadingPriority = 1;  // Prioridad menor (luz secundaria)

    // Generar colores por defecto
    GenerateDefaultPlateColors();
}

void ATectonicsTestActor::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoStart)
    {
        InitializeSystems();
    }
}

void ATectonicsTestActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ShutdownSystems();
    Super::EndPlay(EndPlayReason);
}

void ATectonicsTestActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Manejar input
    HandleInput();

    // Actualizar órbita del sol
    if (bEnableSun)
    {
        UpdateSunOrbit(DeltaTime);
    }

    // Ejecutar simulación si está activa
    if (bSimulationRunning && bSystemsInitialized)
    {
        StepSimulation(DeltaTime * TimeScale);
    }

    // Actualizar visualización
    if (bSystemsInitialized)
    {
        UpdateMeshColors();

        if (bShowVelocityVectors)
        {
            DrawVelocityDebug();
        }

        if (bShowPlateBoundaries)
        {
            DrawBoundaryDebug();
        }

        if (bShowDebugInfo)
        {
            DrawScreenDebugInfo();
        }
    }
}

void ATectonicsTestActor::InitializeSystems()
{
    if (bSystemsInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("TectonicsTestActor: Sistemas ya inicializados"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("TectonicsTestActor: Inicializando sistemas tectónicos..."));

    // Establecer semilla
    FMath::SRandInit(RandomSeed);

    // 1. Crear CubeSphereGrid
    CubeSphereGrid = NewObject<UCubeSphereGrid>(this, TEXT("CubeSphereGrid"));
    CubeSphereGrid->Initialize(GridResolution, VisualRadius);
    UE_LOG(LogTemp, Log, TEXT("  - CubeSphereGrid creado (Resolución: %d)"), GridResolution);

    // 2. Crear Sistema de Placas con configuración
    FPlateGenerationConfig Config;
    Config.NumPlates = NumPlates;
    Config.RandomSeed = RandomSeed;
    Config.ContinentalFraction = 0.3f;
    
    PlateSystem = NewObject<UTectonicPlateSystem>(this, TEXT("PlateSystem"));
    PlateSystem->Initialize(CubeSphereGrid, Config);
    PlateSystem->GeneratePlates();
    UE_LOG(LogTemp, Log, TEXT("  - PlateSystem creado (%d placas)"), NumPlates);

    // 3. Crear Sistema Rasterizado
    RasterizedTectonics = NewObject<URasterizedTectonics>(this, TEXT("RasterizedTectonics"));
    RasterizedTectonics->Initialize(CubeSphereGrid, PlateSystem, RasterResolution);
    UE_LOG(LogTemp, Log, TEXT("  - RasterizedTectonics inicializado (Resolución: %d)"), RasterResolution);

    // 3.5 Suavizar elevación para transiciones más orgánicas
    if (ElevationSmoothingIterations > 0)
    {
        RasterizedTectonics->SmoothElevation(ElevationSmoothingIterations);
        UE_LOG(LogTemp, Log, TEXT("  - Elevación suavizada (%d iteraciones)"), ElevationSmoothingIterations);
    }

    // 4. Crear malla visual
    if (bShowPlanetMesh)
    {
        // Log para depurar estado de RasterizedTectonics
        UE_LOG(LogTemp, Log, TEXT("  - RasterizedTectonics IsInitialized: %s"), 
            RasterizedTectonics->IsInitialized() ? TEXT("TRUE") : TEXT("FALSE"));
        
        // Verificar elevación de muestra
        if (RasterizedTectonics->IsInitialized())
        {
            float SampleElevation = RasterizedTectonics->GetElevationAt(ECSCubeFace::PositiveX, 0, 0);
            UE_LOG(LogTemp, Log, TEXT("  - Elevación de muestra (0,0): %.2f metros"), SampleElevation);
        }
        
        CreatePlanetMesh();
        UE_LOG(LogTemp, Log, TEXT("  - Malla del planeta creada"));
    }

    bSystemsInitialized = true;
    SimulationTime = 0.0f;
    SimulationSteps = 0;

    UE_LOG(LogTemp, Log, TEXT("TectonicsTestActor: ¡Sistemas inicializados correctamente!"));

    // Mostrar info inicial
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, 
            FString::Printf(TEXT("Tectónica iniciada: %d placas, Grid %dx%d"), 
                NumPlates, GridResolution, GridResolution));
    }
}

void ATectonicsTestActor::ShutdownSystems()
{
    if (!bSystemsInitialized)
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("TectonicsTestActor: Liberando sistemas..."));

    if (RasterizedTectonics)
    {
        RasterizedTectonics->ReleaseResources();
        RasterizedTectonics = nullptr;
    }

    BoundaryInteractions = nullptr;
    Kinematics = nullptr;
    PlateSystem = nullptr;
    CubeSphereGrid = nullptr;

    // Limpiar malla
    if (PlanetMesh)
    {
        PlanetMesh->ClearAllMeshSections();
    }

    MeshVertices.Empty();
    MeshTriangles.Empty();
    MeshNormals.Empty();
    MeshColors.Empty();
    MeshUVs.Empty();

    bSystemsInitialized = false;

    UE_LOG(LogTemp, Log, TEXT("TectonicsTestActor: Sistemas liberados"));
}

void ATectonicsTestActor::RestartSimulation()
{
    ShutdownSystems();
    
    // Cambiar semilla para nueva simulación
    RandomSeed = FMath::Rand();
    
    InitializeSystems();

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, 
            TEXT("Simulación reiniciada con nueva semilla"));
    }
}

void ATectonicsTestActor::ToggleSimulation()
{
    bSimulationRunning = !bSimulationRunning;

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, 
            bSimulationRunning ? FColor::Green : FColor::Red, 
            bSimulationRunning ? TEXT("Simulación REANUDADA") : TEXT("Simulación PAUSADA"));
    }
}

void ATectonicsTestActor::StepSimulation(float DeltaTime)
{
    if (!bSystemsInitialized || DeltaTime <= 0.0f)
    {
        return;
    }

    // 1. Avanzar el sistema de placas
    if (PlateSystem)
    {
        PlateSystem->Step(DeltaTime);
    }

    // 2. Actualizar sistema rasterizado
    if (RasterizedTectonics)
    {
        FPlateMovementParams Params;
        Params.DeltaTime = DeltaTime;
        Params.TimeScale = 1.0f;
        Params.OrogenyFactor = 0.1f;
        Params.SpreadingFactor = 0.05f;
        Params.SubductionRate = 0.02f;

        RasterizedTectonics->Step(Params);
    }

    SimulationTime += DeltaTime;
    SimulationSteps++;

    // 3. Refrescar la malla visual periódicamente para reflejar la elevación actual
    // (por defecto la malla se genera una vez y queda congelada, ver comentario en el header)
    if (bShowPlanetMesh && MeshRegenerationIntervalSteps > 0 && SimulationSteps % MeshRegenerationIntervalSteps == 0)
    {
        RegeneratePlanetMesh();
    }
}

FString ATectonicsTestActor::GetPlateInfo(int32 PlateIndex) const
{
    if (!PlateSystem || PlateIndex < 0 || PlateIndex >= PlateSystem->GetNumPlates())
    {
        return TEXT("Placa no válida");
    }

    FTectonicPlate Plate = PlateSystem->GetPlate(PlateIndex);

    return FString::Printf(
        TEXT("Placa %d:\n")
        TEXT("  Tipo: %s\n")
        TEXT("  Celdas: %d\n")
        TEXT("  Vel. Angular: %.4f rad/Ma\n")
        TEXT("  Polo Euler: (%.2f, %.2f, %.2f)\n")
        TEXT("  Densidad: %.0f kg/m³\n")
        TEXT("  Espesor: %.1f km"),
        PlateIndex,
        Plate.CrustType == ECrustType::Continental ? TEXT("Continental") : 
            (Plate.CrustType == ECrustType::Oceanic ? TEXT("Oceánica") : TEXT("Transicional")),
        Plate.CellCount,
        Plate.AngularVelocity,
        Plate.EulerPole.X, Plate.EulerPole.Y, Plate.EulerPole.Z,
        Plate.Density,
        Plate.Thickness
    );
}

FString ATectonicsTestActor::GetGlobalStats() const
{
    if (!bSystemsInitialized)
    {
        return TEXT("Sistemas no inicializados");
    }

    float AvgElevation = 0.0f;
    if (RasterizedTectonics)
    {
        AvgElevation = RasterizedTectonics->GetAverageElevation();
    }

    return FString::Printf(
        TEXT("=== ESTADÍSTICAS GLOBALES ===\n")
        TEXT("Tiempo simulado: %.2f Ma\n")
        TEXT("Pasos: %d\n")
        TEXT("Placas: %d\n")
        TEXT("Elevación promedio: %.0f m\n")
        TEXT("Escala tiempo: %.1fx"),
        SimulationTime,
        SimulationSteps,
        PlateSystem ? PlateSystem->GetNumPlates() : 0,
        AvgElevation,
        TimeScale
    );
}

float ATectonicsTestActor::GetSurfaceRadiusAtDirection(const FVector& Direction) const
{
    if (!RasterizedTectonics || !RasterizedTectonics->IsInitialized())
    {
        return VisualRadius;
    }

    // Conversion unificada (CubeFaceMapping.h, ROADMAP.md F0). Antes esto era un bloque
    // de 20 lineas copiado tambien en CreatePlanetMesh y UpdateMeshColors: mantener las
    // tres copias sincronizadas a mano es exactamente lo que fallo en los polos.
    ECSCubeFace DominantFace;
    float TexU, TexV;
    CubeFaceMapping::DirectionToFaceTexUV(Direction.GetSafeNormal(), DominantFace, TexU, TexV);

    float Elevation = RasterizedTectonics->GetElevationBilinear(DominantFace, TexU, TexV);

    float ElevationOffset = Elevation * 100.0f * ElevationScale;
    return VisualRadius + ElevationOffset;
}

bool ATectonicsTestActor::SaveSimulation(const FString& SlotName)
{
    if (!bSystemsInitialized || !PlateSystem || !RasterizedTectonics)
    {
        return false;
    }

    UTectonicSaveGame* Save = Cast<UTectonicSaveGame>(UGameplayStatics::CreateSaveGameObject(UTectonicSaveGame::StaticClass()));
    if (!Save)
    {
        return false;
    }

    Save->RandomSeed = RandomSeed;
    Save->NumPlates = NumPlates;
    Save->GridResolution = GridResolution;
    Save->RasterResolution = RasterResolution;
    Save->VisualRadius = VisualRadius;
    Save->Plates = PlateSystem->GetAllPlates();
    Save->SimulationTime = SimulationTime;
    Save->SimulationSteps = SimulationSteps;

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        Save->GetElevationArray(Face) = RasterizedTectonics->GetElevationData(Face);
    }

    return UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
}

bool ATectonicsTestActor::LoadSimulation(const FString& SlotName)
{
    if (!UGameplayStatics::DoesSaveGameExist(SlotName, 0))
    {
        return false;
    }

    UTectonicSaveGame* Save = Cast<UTectonicSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
    if (!Save)
    {
        return false;
    }

    // Reconstruir la topología con los mismos parámetros que al guardar (misma semilla
    // fija -> misma asignación de placas por celda, ver comentario en TectonicSaveGame.h)
    ShutdownSystems();

    GridResolution = Save->GridResolution;
    RasterResolution = Save->RasterResolution;
    VisualRadius = Save->VisualRadius;
    NumPlates = Save->NumPlates;
    RandomSeed = Save->RandomSeed;

    InitializeSystems();

    if (!bSystemsInitialized || !PlateSystem || !RasterizedTectonics)
    {
        return false;
    }

    // Sobreescribir con el estado evolucionado guardado
    PlateSystem->RestorePlateState(Save->Plates);
    PlateSystem->SetTotalSimulationTime(Save->SimulationTime);

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        RasterizedTectonics->SetElevationData(Face, Save->GetElevationArray(Face));
    }

    SimulationTime = Save->SimulationTime;
    SimulationSteps = Save->SimulationSteps;

    RegeneratePlanetMesh();

    return true;
}

void ATectonicsTestActor::RegeneratePlanetMesh()
{
    if (PlanetMesh)
    {
        PlanetMesh->ClearAllMeshSections();
    }
    
    MeshVertices.Empty();
    MeshTriangles.Empty();
    MeshNormals.Empty();
    MeshColors.Empty();
    MeshUVs.Empty();

    if (bSystemsInitialized && bShowPlanetMesh)
    {
        CreatePlanetMesh();
    }
}

void ATectonicsTestActor::GenerateDefaultPlateColors()
{
    PlateColors.Empty();
    
    // Colores distintivos para hasta 20 placas
    PlateColors.Add(FLinearColor(0.8f, 0.2f, 0.2f, 1.0f));  // Rojo
    PlateColors.Add(FLinearColor(0.2f, 0.6f, 0.8f, 1.0f));  // Azul
    PlateColors.Add(FLinearColor(0.2f, 0.8f, 0.2f, 1.0f));  // Verde
    PlateColors.Add(FLinearColor(0.9f, 0.7f, 0.1f, 1.0f));  // Amarillo
    PlateColors.Add(FLinearColor(0.7f, 0.2f, 0.7f, 1.0f));  // Magenta
    PlateColors.Add(FLinearColor(0.1f, 0.8f, 0.8f, 1.0f));  // Cyan
    PlateColors.Add(FLinearColor(0.9f, 0.5f, 0.1f, 1.0f));  // Naranja
    PlateColors.Add(FLinearColor(0.5f, 0.3f, 0.7f, 1.0f));  // Púrpura
    PlateColors.Add(FLinearColor(0.6f, 0.4f, 0.2f, 1.0f));  // Marrón
    PlateColors.Add(FLinearColor(0.8f, 0.8f, 0.3f, 1.0f));  // Lima
    PlateColors.Add(FLinearColor(0.3f, 0.5f, 0.3f, 1.0f));  // Verde oscuro
    PlateColors.Add(FLinearColor(0.8f, 0.4f, 0.6f, 1.0f));  // Rosa
    PlateColors.Add(FLinearColor(0.4f, 0.4f, 0.8f, 1.0f));  // Azul claro
    PlateColors.Add(FLinearColor(0.7f, 0.7f, 0.5f, 1.0f));  // Beige
    PlateColors.Add(FLinearColor(0.5f, 0.2f, 0.2f, 1.0f));  // Rojo oscuro
    PlateColors.Add(FLinearColor(0.2f, 0.4f, 0.5f, 1.0f));  // Teal
    PlateColors.Add(FLinearColor(0.6f, 0.6f, 0.2f, 1.0f));  // Oliva
    PlateColors.Add(FLinearColor(0.4f, 0.2f, 0.4f, 1.0f));  // Púrpura oscuro
    PlateColors.Add(FLinearColor(0.3f, 0.6f, 0.4f, 1.0f));  // Verde agua
    PlateColors.Add(FLinearColor(0.7f, 0.3f, 0.3f, 1.0f));  // Coral
}

void ATectonicsTestActor::CreatePlanetMesh()
{
    if (!CubeSphereGrid || !PlateSystem)
    {
        return;
    }

    MeshVertices.Empty();
    MeshTriangles.Empty();
    MeshNormals.Empty();
    MeshColors.Empty();
    MeshUVs.Empty();

    const int32 Resolution = GridResolution;
    const float Radius = VisualRadius;

    // Generar vértices para cada cara del cubo usando proyección directa
    for (int32 Face = 0; Face < 6; ++Face)
    {
        const int32 BaseVertex = MeshVertices.Num();
        ECSCubeFace CubeFace = static_cast<ECSCubeFace>(Face);

        // Generar vértices de esta cara
        for (int32 Y = 0; Y <= Resolution; ++Y)
        {
            for (int32 X = 0; X <= Resolution; ++X)
            {
                // Coordenadas UV normalizadas [-1, 1]
                float U = (static_cast<float>(X) / Resolution) * 2.0f - 1.0f;
                float V = (static_cast<float>(Y) / Resolution) * 2.0f - 1.0f;

                // Proyección del cubo a esfera (DEBE coincidir exactamente con RasterizedTectonics)
                FVector CubePos = CubeFaceMapping::FaceUVToCubePoint(CubeFace, U, V);
                
                // Normalizar para proyectar a esfera
                FVector Normal = CubePos.GetSafeNormal();
                
                // Obtener elevación si está disponible
                float Elevation = 0.0f;
                if (bShowElevation && RasterizedTectonics && RasterizedTectonics->IsInitialized())
                {
                    // Para garantizar continuidad en los bordes, usar la cara dominante
                    // basada en la posición esférica, no la cara actual del loop
                    // Conversión unificada, ver CubeFaceMapping.h (ROADMAP.md F0).
                    ECSCubeFace DominantFace;
                    float TexU, TexV;
                    CubeFaceMapping::DirectionToFaceTexUV(Normal, DominantFace, TexU, TexV);
                    Elevation = RasterizedTectonics->GetElevationBilinear(DominantFace, TexU, TexV);
                }
                
                // Elevación en metros reales -> cm (unidades de Unreal), con exageración
                // directa. Independiente del radio del planeta (antes escalaba con
                // Radius, lo que rompía la exageración al usar el radio real de la Tierra).
                float ElevationOffset = Elevation * 100.0f * ElevationScale;
                FVector Position = Normal * (Radius + ElevationOffset);

                MeshVertices.Add(Position);
                MeshNormals.Add(FVector::ZeroVector);  // Se calculará después
                MeshUVs.Add(FVector2D((U + 1.0f) * 0.5f, (V + 1.0f) * 0.5f));

                // Color basado en placa - usar coordenadas de celda válidas
                int32 CellX = FMath::Clamp(X, 0, Resolution - 1);
                int32 CellY = FMath::Clamp(Y, 0, Resolution - 1);
                int32 PlateID = PlateSystem->GetPlateIDAt(CubeFace, CellX, CellY);
                FLinearColor Color = GetPlateColor(PlateID);
                MeshColors.Add(Color.ToFColor(true));
            }
        }

        // Generar triángulos de esta cara
        // Algunas caras necesitan winding invertido debido al mapeo UV
        bool bFlipWinding = (CubeFace == ECSCubeFace::PositiveZ) || 
                            (CubeFace == ECSCubeFace::NegativeZ);
        
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                int32 V0 = BaseVertex + Y * (Resolution + 1) + X;
                int32 V1 = V0 + 1;
                int32 V2 = V0 + (Resolution + 1);
                int32 V3 = V2 + 1;

                if (bFlipWinding)
                {
                    // Winding invertido para PositiveZ
                    MeshTriangles.Add(V0);
                    MeshTriangles.Add(V1);
                    MeshTriangles.Add(V2);

                    MeshTriangles.Add(V1);
                    MeshTriangles.Add(V3);
                    MeshTriangles.Add(V2);
                }
                else
                {
                    // Winding normal
                    MeshTriangles.Add(V0);
                    MeshTriangles.Add(V2);
                    MeshTriangles.Add(V1);

                    MeshTriangles.Add(V1);
                    MeshTriangles.Add(V2);
                    MeshTriangles.Add(V3);
                }
            }
        }
    }

    // Usar normales esféricas (dirección radial) para evitar discontinuidades
    // Las normales basadas en triángulos causan costuras en los bordes de las caras del cubo
    for (int32 i = 0; i < MeshVertices.Num(); ++i)
    {
        MeshNormals[i] = MeshVertices[i].GetSafeNormal();
    }

    // Crear la sección de malla
    if (PlanetMesh && MeshVertices.Num() > 0)
    {
        PlanetMesh->CreateMeshSection(
            0,
            MeshVertices,
            MeshTriangles,
            MeshNormals,
            MeshUVs,
            MeshColors,
            TArray<FProcMeshTangent>(),
            false  // No crear colisión, es muy pesado
        );
        
        // Intentar cargar material custom del proyecto
        UMaterial* VertexColorMat = LoadObject<UMaterial>(nullptr,
            TEXT("/Game/Materials/M_VertexColor.M_VertexColor"));
        
        if (VertexColorMat)
        {
            PlanetMesh->SetMaterial(0, VertexColorMat);
            UE_LOG(LogTemp, Log, TEXT("Material M_VertexColor aplicado"));
            UE_LOG(LogTemp, Log, TEXT("IMPORTANTE: Asegurate que el material sea LIT (no Unlit) para que funcione la iluminacion"));
        }
        else
        {
            // Si no existe, dar instrucciones claras
            UE_LOG(LogTemp, Warning, TEXT("=== CREAR MATERIAL M_VertexColor ==="));
            UE_LOG(LogTemp, Warning, TEXT("1. Content Browser > Add > Material"));
            UE_LOG(LogTemp, Warning, TEXT("2. Nombrar: M_VertexColor en Content/Materials/"));
            UE_LOG(LogTemp, Warning, TEXT("3. Abrir material, añadir VertexColor node"));
            UE_LOG(LogTemp, Warning, TEXT("4. Conectar VertexColor RGB -> Base Color"));
            UE_LOG(LogTemp, Warning, TEXT("5. Shading Model = Default Lit (NO Unlit)"));
            UE_LOG(LogTemp, Warning, TEXT("6. Two Sided = True"));
            UE_LOG(LogTemp, Warning, TEXT("7. Guardar y reiniciar Play"));
        }
        
        UE_LOG(LogTemp, Log, TEXT("Mesh creado con %d vertices y %d colores"), 
            MeshVertices.Num(), MeshColors.Num());
    }
}

void ATectonicsTestActor::UpdateMeshColors()
{
    if (!PlanetMesh || !bSystemsInitialized || MeshVertices.Num() == 0)
    {
        return;
    }

    // Solo actualizar cada ciertos frames para mejorar rendimiento
    static int32 FrameCounter = 0;
    FrameCounter++;
    if (FrameCounter % 10 != 0) return;

    const int32 Resolution = GridResolution;
    const float Radius = VisualRadius;
    int32 VertexIndex = 0;

    for (int32 Face = 0; Face < 6; ++Face)
    {
        ECSCubeFace CubeFace = static_cast<ECSCubeFace>(Face);

        for (int32 Y = 0; Y <= Resolution; ++Y)
        {
            for (int32 X = 0; X <= Resolution; ++X)
            {
                if (VertexIndex >= MeshColors.Num())
                {
                    break;
                }

                int32 CellX = FMath::Min(X, Resolution - 1);
                int32 CellY = FMath::Min(Y, Resolution - 1);

                int32 PlateID = 0;
                float Elevation = 0.0f;

                // Calcular posición esférica para este vértice
                float U = (static_cast<float>(X) / Resolution) * 2.0f - 1.0f;
                float V = (static_cast<float>(Y) / Resolution) * 2.0f - 1.0f;
                
                FVector CubePos;
                switch (CubeFace)
                {
                    case ECSCubeFace::PositiveX: CubePos = FVector(1.0f, U, V); break;
                    case ECSCubeFace::NegativeX: CubePos = FVector(-1.0f, -U, V); break;
                    case ECSCubeFace::PositiveY: CubePos = FVector(-U, 1.0f, V); break;
                    case ECSCubeFace::NegativeY: CubePos = FVector(U, -1.0f, V); break;
                    case ECSCubeFace::PositiveZ: CubePos = FVector(U, -V, 1.0f); break;
                    case ECSCubeFace::NegativeZ: CubePos = FVector(U, V, -1.0f); break;
                    default: CubePos = FVector(1.0f, U, V); break;
                }
                FVector Normal = CubePos.GetSafeNormal();

                if (RasterizedTectonics && RasterizedTectonics->IsInitialized())
                {
                    // Usar cara dominante para garantizar continuidad en bordes
                    FVector AbsNormal = Normal.GetAbs();
                    ECSCubeFace DominantFace;
                    float FaceU, FaceV;
                    
                    if (AbsNormal.X >= AbsNormal.Y && AbsNormal.X >= AbsNormal.Z)
                    {
                        DominantFace = Normal.X >= 0 ? ECSCubeFace::PositiveX : ECSCubeFace::NegativeX;
                        float Scale = 1.0f / AbsNormal.X;
                        if (Normal.X >= 0) {
                            FaceU = Normal.Y * Scale;
                            FaceV = Normal.Z * Scale;
                        } else {
                            FaceU = -Normal.Y * Scale;
                            FaceV = Normal.Z * Scale;
                        }
                    }
                    else if (AbsNormal.Y >= AbsNormal.X && AbsNormal.Y >= AbsNormal.Z)
                    {
                        DominantFace = Normal.Y >= 0 ? ECSCubeFace::PositiveY : ECSCubeFace::NegativeY;
                        float Scale = 1.0f / AbsNormal.Y;
                        if (Normal.Y >= 0) {
                            FaceU = -Normal.X * Scale;
                            FaceV = Normal.Z * Scale;
                        } else {
                            FaceU = Normal.X * Scale;
                            FaceV = Normal.Z * Scale;
                        }
                    }
                    else
                    {
                        DominantFace = Normal.Z >= 0 ? ECSCubeFace::PositiveZ : ECSCubeFace::NegativeZ;
                        float Scale = 1.0f / AbsNormal.Z;
                        if (Normal.Z >= 0) {
                            FaceU = Normal.X * Scale;
                            FaceV = -Normal.Y * Scale;
                        } else {
                            FaceU = Normal.X * Scale;
                            FaceV = Normal.Y * Scale;
                        }
                    }
                    
                    int32 TexX = FMath::Clamp(static_cast<int32>((FaceU + 1.0f) * 0.5f * RasterResolution), 0, RasterResolution - 1);
                    int32 TexY = FMath::Clamp(static_cast<int32>((FaceV + 1.0f) * 0.5f * RasterResolution), 0, RasterResolution - 1);
                    PlateID = RasterizedTectonics->GetPlateIDAt(DominantFace, TexX, TexY);
                    // Usar interpolación bilineal para elevación suave
                    float TexU = (FaceU + 1.0f) * 0.5f;
                    float TexV = (FaceV + 1.0f) * 0.5f;
                    Elevation = RasterizedTectonics->GetElevationBilinear(DominantFace, TexU, TexV);
                }
                else if (PlateSystem)
                {
                    PlateID = PlateSystem->GetPlateIDAt(CubeFace, CellX, CellY);
                }

                // Actualizar posición del vértice con la elevación
                if (bShowElevation && VertexIndex < MeshVertices.Num())
                {
                    // Ver comentario equivalente en CreatePlanetMesh(): metros reales -> cm,
                    // exageración directa, independiente del radio del planeta.
                    float ElevationOffset = Elevation * 100.0f * ElevationScale;
                    MeshVertices[VertexIndex] = Normal * (Radius + ElevationOffset);
                }

                FLinearColor Color = GetPlateColor(PlateID);

                // Modular brillo por elevación
                float ElevationFactor = FMath::GetMappedRangeValueClamped(
                    FVector2D(-5000.0f, 10000.0f), FVector2D(0.5f, 1.2f), Elevation);
                Color *= ElevationFactor;

                // Resaltar placa seleccionada
                if (HighlightedPlate >= 0)
                {
                    if (PlateID == HighlightedPlate)
                    {
                        Color = FLinearColor::White;
                    }
                    else
                    {
                        Color *= 0.3f;
                    }
                }

                MeshColors[VertexIndex] = Color.ToFColor(true);
                VertexIndex++;
            }
        }
    }

    // Recalcular normales esféricas si se actualizaron las posiciones
    if (bShowElevation)
    {
        for (int32 i = 0; i < MeshVertices.Num(); ++i)
        {
            MeshNormals[i] = MeshVertices[i].GetSafeNormal();
        }
    }

    // Actualizar la malla
    if (PlanetMesh)
    {
        PlanetMesh->UpdateMeshSection(
            0,
            MeshVertices,
            MeshNormals,
            MeshUVs,
            MeshColors,
            TArray<FProcMeshTangent>()
        );
    }
}

void ATectonicsTestActor::DrawVelocityDebug()
{
    // Simplificado: dibujamos los polos de Euler de cada placa
    if (!PlateSystem)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FVector ActorLoc = GetActorLocation();

    const TArray<FTectonicPlate>& Plates = PlateSystem->GetAllPlates();
    for (int32 i = 0; i < Plates.Num(); ++i)
    {
        const FTectonicPlate& Plate = Plates[i];
        
        // Dibujar polo de Euler
        FVector PolePos = Plate.EulerPole.GetSafeNormal() * VisualRadius * 1.1f + ActorLoc;
        
        DrawDebugSphere(World, PolePos, 500.0f, 8, 
            GetPlateColor(i).ToFColor(true), false, -1.0f, 0, 3.0f);
        
        // Etiqueta
        DrawDebugString(World, PolePos, 
            FString::Printf(TEXT("P%d"), i), nullptr, FColor::White, 0.0f, true);
    }
}

void ATectonicsTestActor::DrawBoundaryDebug()
{
    // Simplificado: solo mostramos estadísticas de límites
    // Los límites completos se pueden visualizar con el BoundaryInteractions
    if (!PlateSystem || !GEngine)
    {
        return;
    }

    const TArray<FPlateBoundary>& Boundaries = PlateSystem->GetBoundaries();
    
    int32 Convergent = 0, Divergent = 0, Transform = 0;
    for (const FPlateBoundary& Boundary : Boundaries)
    {
        switch (Boundary.BoundaryType)
        {
            case EBoundaryType::Convergent: Convergent++; break;
            case EBoundaryType::Divergent: Divergent++; break;
            case EBoundaryType::Transform: Transform++; break;
            default: break;
        }
    }
    
    GEngine->AddOnScreenDebugMessage(3, 0.0f, FColor::Yellow,
        FString::Printf(TEXT("Límites: Conv=%d Div=%d Trans=%d"), Convergent, Divergent, Transform));
}

void ATectonicsTestActor::DrawScreenDebugInfo()
{
    if (!GEngine)
    {
        return;
    }

    // Info básica en pantalla
    FString InfoText = FString::Printf(
        TEXT("=== TECTÓNICA ===\n")
        TEXT("Tiempo: %.2f Ma | Pasos: %d\n")
        TEXT("Estado: %s | Escala: %.1fx\n")
        TEXT("Placas: %d | Grid: %d\n")
        TEXT("\n[SPACE] Pausa | [R] Reiniciar\n")
        TEXT("[+/-] Velocidad | [1-8] Placa\n")
        TEXT("[V] Velocidades | [B] Límites"),
        SimulationTime,
        SimulationSteps,
        bSimulationRunning ? TEXT("EJECUTANDO") : TEXT("PAUSADO"),
        TimeScale,
        PlateSystem ? PlateSystem->GetNumPlates() : 0,
        GridResolution
    );

    GEngine->AddOnScreenDebugMessage(1, 0.0f, FColor::White, InfoText);

    // Info de placa resaltada
    if (HighlightedPlate >= 0)
    {
        FString PlateInfo = GetPlateInfo(HighlightedPlate);
        GEngine->AddOnScreenDebugMessage(2, 0.0f, FColor::Cyan, PlateInfo);
    }
}

void ATectonicsTestActor::HandleInput()
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC)
    {
        return;
    }

    // Verificar que el juego tiene input habilitado
    if (!PC->InputEnabled())
    {
        return;
    }

    // SPACE - Toggle simulación
    if (PC->WasInputKeyJustPressed(EKeys::SpaceBar))
    {
        ToggleSimulation();
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan, 
                FString::Printf(TEXT("Simulación: %s"), bSimulationRunning ? TEXT("EJECUTANDO") : TEXT("PAUSADA")));
        }
    }

    // R - Reiniciar
    if (PC->WasInputKeyJustPressed(EKeys::R))
    {
        RestartSimulation();
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange, TEXT("Simulación reiniciada"));
        }
    }

    // + - Acelerar (tanto numpad como teclado principal)
    if (PC->WasInputKeyJustPressed(EKeys::Add) || 
        PC->WasInputKeyJustPressed(EKeys::Equals) ||
        (PC->IsInputKeyDown(EKeys::LeftShift) && PC->WasInputKeyJustPressed(EKeys::Equals)))
    {
        TimeScale = FMath::Min(TimeScale * 2.0f, 1000.0f);
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Green, 
                FString::Printf(TEXT("Escala tiempo: %.1fx"), TimeScale));
        }
    }

    // - - Desacelerar
    if (PC->WasInputKeyJustPressed(EKeys::Subtract) || PC->WasInputKeyJustPressed(EKeys::Hyphen))
    {
        TimeScale = FMath::Max(TimeScale * 0.5f, 0.1f);
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Yellow, 
                FString::Printf(TEXT("Escala tiempo: %.1fx"), TimeScale));
        }
    }

    // V - Toggle velocidades
    if (PC->WasInputKeyJustPressed(EKeys::V))
    {
        bShowVelocityVectors = !bShowVelocityVectors;
    }

    // B - Toggle límites
    if (PC->WasInputKeyJustPressed(EKeys::B))
    {
        bShowPlateBoundaries = !bShowPlateBoundaries;
    }

    // 1-8 - Seleccionar placa
    static const FKey NumberKeys[] = {
        EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four,
        EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight
    };
    
    for (int32 i = 0; i < 8; ++i)
    {
        if (PC->WasInputKeyJustPressed(NumberKeys[i]))
        {
            if (HighlightedPlate == i)
            {
                HighlightedPlate = -1;  // Deseleccionar
            }
            else
            {
                HighlightedPlate = i;
            }
        }
    }

    // 0 - Deseleccionar
    if (PC->WasInputKeyJustPressed(EKeys::Zero))
    {
        HighlightedPlate = -1;
    }

    // K - Guardar snapshot
    if (PC->WasInputKeyJustPressed(EKeys::K))
    {
        bool bOk = SaveSimulation(TEXT("SimuSnapshot"));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, bOk ? FColor::Green : FColor::Red,
                bOk ? TEXT("Snapshot guardado") : TEXT("Error al guardar snapshot"));
        }
    }

    // L - Cargar snapshot
    if (PC->WasInputKeyJustPressed(EKeys::L))
    {
        bool bOk = LoadSimulation(TEXT("SimuSnapshot"));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, bOk ? FColor::Green : FColor::Red,
                bOk ? TEXT("Snapshot cargado") : TEXT("Error al cargar snapshot (¿existe?)"));
        }
    }
}

FLinearColor ATectonicsTestActor::GetPlateColor(int32 PlateID) const
{
    if (PlateColors.IsValidIndex(PlateID))
    {
        return PlateColors[PlateID];
    }

    // Color procedural para IDs fuera de rango
    float Hue = FMath::Fmod(PlateID * 137.5f, 360.0f) / 360.0f;
    return FLinearColor::MakeFromHSV8(
        static_cast<uint8>(Hue * 255),
        200,
        200
    );
}

void ATectonicsTestActor::UpdateSunOrbit(float DeltaTime)
{
    if (!SunPivot || !SunLight)
    {
        return;
    }

    // Actualizar ángulo de órbita
    SunOrbitAngle += SunOrbitSpeed * DeltaTime;
    if (SunOrbitAngle >= 360.0f)
    {
        SunOrbitAngle -= 360.0f;
    }

    // Rotar el pivot del sol alrededor del planeta
    // La inclinación simula las estaciones
    FRotator NewRotation(SunOrbitTilt, SunOrbitAngle, 0.0f);
    SunPivot->SetRelativeRotation(NewRotation);

    // Actualizar propiedades de la luz solar
    SunLight->SetIntensity(SunIntensity);
    SunLight->SetLightColor(SunColor.ToFColor(true));

    // Actualizar luz de relleno (ilumina desde dirección perpendicular/desde arriba)
    // Esto evita la línea divisoria que ocurre con luz opuesta
    if (FillLight)
    {
        FillLight->SetIntensity(FillIntensity);
        // La luz de relleno viene desde arriba (eje Z positivo) para simular luz del cielo
        FillLight->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
    }
}
