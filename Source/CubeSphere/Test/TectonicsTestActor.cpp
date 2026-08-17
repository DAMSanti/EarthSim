// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "TectonicsTestActor.h"
#include "../CubeSphereGrid.h"
#include "../CubeFaceMapping.h"
#include "../Visualization/PlanetFieldRegistry.h"
#include "../Visualization/PlanetFieldMaterial.h"
#include "../Climate/PlanetClimate.h"
#include "../Hydrology/PlanetHydrology.h"
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
        // PASO FIJO, desacoplado del framerate (ver SimulationStepsPerSecond). El dt
        // simulado no depende ya de lo que tarde el frame, asi que la fisica es
        // reproducible y el coste esta acotado por diseno.
        SimAccumulator += DeltaTime;
        const float SimInterval = 1.0f / FMath::Max(SimulationStepsPerSecond, 0.1f);

        // Tope de pasos por frame: si el frame va lento no se intenta recuperar el tiempo
        // perdido a cualquier precio, porque eso realimenta la espiral de la muerte de M1.
        // El tiempo simulado se queda atras, que es preferible a bloquear el frame.
        const int32 MaxStepsPerFrame = 3;
        int32 StepsThisFrame = 0;

        const double SimStart = FPlatformTime::Seconds();
        while (SimAccumulator >= SimInterval && StepsThisFrame < MaxStepsPerFrame)
        {
            StepSimulation(SimulationStepMa * TimeScale);
            SimAccumulator -= SimInterval;
            ++StepsThisFrame;
        }
        if (StepsThisFrame >= MaxStepsPerFrame)
        {
            SimAccumulator = 0.0f;
        }

        if (StepsThisFrame > 0)
        {
            const double SimMs = (FPlatformTime::Seconds() - SimStart) * 1000.0;
            // Media movil: el coste por paso varia mucho (la adveccion no entra siempre),
            // y un valor instantaneo en el HUD seria ilegible.
            AvgSimStepMs = FMath::Lerp(AvgSimStepMs, SimMs, 0.1);
        }
    }

    // Actualizar visualización
    if (bSystemsInitialized)
    {
        // Limitador de refresco: reconstruir 100.000 vertices y resubir la malla entera
        // 60 veces por segundo era el coste dominante del frame, y no aporta nada cuando
        // la simulacion avanza en millones de anos.
        MeshUpdateAccumulator += DeltaTime;
        const float MeshInterval = (MeshUpdateHz > 0.0f) ? (1.0f / MeshUpdateHz) : 0.0f;

        if (bForceMeshColorUpdate || MeshUpdateAccumulator >= MeshInterval)
        {
            MeshUpdateAccumulator = 0.0f;
            bForceMeshColorUpdate = false;

            const double MeshStart = FPlatformTime::Seconds();
            UpdateMeshColors();
            AvgMeshUpdateMs = FMath::Lerp(AvgMeshUpdateMs, (FPlatformTime::Seconds() - MeshStart) * 1000.0, 0.1);
        }

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

    // 3.15 Clima diagnostico (ROADMAP.md F3)
    Climate = NewObject<UPlanetClimate>(this, TEXT("Climate"));
    Climate->Initialize(RasterizedTectonics, CubeSphereGrid);
    Climate->Recompute(FClimateParams());
    UE_LOG(LogTemp, Log, TEXT("  - Clima diagnostico inicializado"));

    // 3.16 Drenaje (ROADMAP.md F4)
    Hydrology = NewObject<UPlanetHydrology>(this, TEXT("Hydrology"));
    Hydrology->Initialize(RasterizedTectonics, Climate);
    Hydrology->Recompute();
    UE_LOG(LogTemp, Log, TEXT("  - Drenaje inicializado: %d celdas de cauce"),
        Hydrology->GetStats().ChannelCells);

    // 3.2 Registro de campos de diagnóstico (ROADMAP.md F0.5)
    FieldRegistry = NewObject<UPlanetFieldRegistry>(this, TEXT("FieldRegistry"));
    RegisterSimulationFields();

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

    if (FieldRegistry)
    {
        FieldRegistry->Reset();
        FieldRegistry = nullptr;
    }

    Climate = nullptr;
    Hydrology = nullptr;

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

        // Desde F1 el campo de IDs cambia cuando hay adveccion, asi que los espejos en
        // float del visor hay que rehacerlos o mostraria un mapa de placas congelado -
        // justo la comprobacion principal de esta fase. Se compara el contador en vez de
        // refrescar cada paso porque la adveccion ocurre cada ~76 pasos, no en todos.
        // El ID y el tipo de corteza solo cambian al advectar, pero el grosor y la altura
        // sobre el nivel del mar cambian en CADA paso (orogenia, difusion, nivel del mar),
        // asi que hay que rehacer los espejos aunque no haya habido adveccion. Se hace
        // solo si el visor esta mostrando uno de esos dos campos, para no pagar el
        // recorrido cuando no se esta mirando.
        const int32 AdvectionCount = RasterizedTectonics->GetAdvectionStats().AdvectionCount;

        // El modo depuracion (T) devuelve antes de tocar AdvectionCount -no pasa por
        // AdvectPlateField-, asi que esa condicion nunca dispara aqui y el visor se
        // quedaria congelado en el reparto inicial aunque PlateIDData si este cambiando.
        // Al ser una prueba de geometria pura (sin fisica real detras) no hay coste que
        // cuidar: se refresca en cada paso mientras el modo este activo.
        bool bNeedsRefresh = (AdvectionCount != LastSeenAdvectionCount)
                           || RasterizedTectonics->IsDebugFakeRotationOnly();

        if (!bNeedsRefresh && FieldRegistry)
        {
            if (const FPlanetScalarField* Active = FieldRegistry->GetActiveField())
            {
                bNeedsRefresh = (Active->Id == FName(TEXT("CrustThickness")))
                             || (Active->Id == FName(TEXT("AboveSeaLevel")));
            }
        }

        if (bNeedsRefresh)
        {
            LastSeenAdvectionCount = AdvectionCount;
            RefreshCategoricalFieldCaches();
        }
    }

    SimulationTime += DeltaTime;
    SimulationSteps++;

    // El clima depende del relieve, que cambia despacio, asi que no hace falta cada paso.
    if (Climate && Climate->IsInitialized() &&
        ClimateUpdateIntervalSteps > 0 && (SimulationSteps % ClimateUpdateIntervalSteps) == 0)
    {
        Climate->Recompute(FClimateParams());

        // El drenaje va detras del clima y en el mismo ritmo: depende del relieve y de la
        // lluvia, y recalcularlo antes que el clima usaria la lluvia del ciclo anterior.
        if (Hydrology && Hydrology->IsInitialized())
        {
            Hydrology->Recompute();
        }
    }

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

    FString Info = FString::Printf(
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

    // F1F Fase A (17-08-2026): solo diagnostico, no mueve nada -ver ANEXO.md. Par de
    // empuje de dorsal y de tiron de losa de esta placa, sin calibrar en escala absoluta
    // todavia: lo que se comprueba aqui es la PROPORCION entre placas (Forsyth & Uyeda
    // 1975 - las que tienen mas margen convergente deben salir con mas tiron de losa).
    if (RasterizedTectonics)
    {
        TArray<FVector> RidgePush, SlabPull;
        TArray<int32> ConvTouching, ConvSubducting;
        RasterizedTectonics->ComputePlateDrivingTorques(RidgePush, SlabPull, ConvTouching, ConvSubducting);
        if (RidgePush.IsValidIndex(PlateIndex) && SlabPull.IsValidIndex(PlateIndex))
        {
            Info += FString::Printf(
                TEXT("\n  F1F empuje dorsal: %.3e (eje %s)\n")
                TEXT("  F1F tiron de losa: %.3e (eje %s)\n")
                TEXT("  F1F convergentes: %d tocan, %d subduce esta placa"),
                RidgePush[PlateIndex].Size(), *RidgePush[PlateIndex].GetSafeNormal().ToCompactString(),
                SlabPull[PlateIndex].Size(), *SlabPull[PlateIndex].GetSafeNormal().ToCompactString(),
                ConvTouching[PlateIndex], ConvSubducting[PlateIndex]
            );
        }
    }

    return Info;
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

    // Tras cargar, el estado del ráster es otro: hay que rehacer los espejos en float de
    // los campos categóricos y recalcular los rangos automáticos, o la leyenda y los
    // colores seguirían describiendo la partida anterior.
    RefreshCategoricalFieldCaches();
    if (FieldRegistry)
    {
        FieldRegistry->RefreshRanges();
    }

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

    bool bCategoricalColor = true;
    if (FieldRegistry && FieldRegistry->GetActiveField())
    {
        bCategoricalColor = (FieldRegistry->GetActiveField()->Palette == EPlanetFieldPalette::Categorical);
    }

    // ============================================================
    // VERTICES PROPIOS POR CELDA, SIN COMPARTIR CON LA VECINA (17-08-2026)
    //
    // Antes esto era una rejilla de (Resolution+1)^2 vertices COMPARTIDOS entre hasta 4
    // celdas, con un solo color cada uno. El rasterizador interpola ese color entre los 3
    // vertices de cada triangulo de todas formas -asi que en cualquier frontera de placa,
    // un triangulo con una esquina en la placa 2 y otra en la placa 4 pintaba un degradado
    // suave entre medias, con un tono intermedio que no representa ninguna placa real.
    // Reportado por el usuario viendo el campo "ID de placa" en el visor.
    //
    // Arreglo: cada celda tiene sus 4 vertices propios. Ver SampleMeshCell() para el porque
    // eso basta para que un campo categorico salga plano sin perder el aspecto suave de los
    // campos continuos (elevacion, temperatura...).
    // ============================================================
    for (int32 Face = 0; Face < 6; ++Face)
    {
        ECSCubeFace CubeFace = static_cast<ECSCubeFace>(Face);

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                FVector CellPos[4];
                FLinearColor CellColor[4];
                SampleMeshCell(CubeFace, X, Y, Resolution, Radius, bCategoricalColor, CellPos, CellColor);

                const int32 BaseVertex = MeshVertices.Num();
                for (int32 C = 0; C < 4; ++C)
                {
                    MeshVertices.Add(CellPos[C]);
                    MeshNormals.Add(FVector::ZeroVector);  // Se calculará después
                    MeshColors.Add(CellColor[C].ToFColor(true));
                }

                MeshUVs.Add(FVector2D(static_cast<float>(X) / Resolution, static_cast<float>(Y) / Resolution));
                MeshUVs.Add(FVector2D(static_cast<float>(X + 1) / Resolution, static_cast<float>(Y) / Resolution));
                MeshUVs.Add(FVector2D(static_cast<float>(X) / Resolution, static_cast<float>(Y + 1) / Resolution));
                MeshUVs.Add(FVector2D(static_cast<float>(X + 1) / Resolution, static_cast<float>(Y + 1) / Resolution));

                // Winding dextrogiro uniforme en las 6 caras (ver CubeFaceMapping.h,
                // ROADMAP.md F0) -mismo orden V0,V2,V1 / V1,V2,V3 que tenia la rejilla
                // compartida, ahora sobre los 4 vertices propios de esta celda.
                MeshTriangles.Add(BaseVertex + 0);
                MeshTriangles.Add(BaseVertex + 2);
                MeshTriangles.Add(BaseVertex + 1);

                MeshTriangles.Add(BaseVertex + 1);
                MeshTriangles.Add(BaseVertex + 2);
                MeshTriangles.Add(BaseVertex + 3);
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
        
        ApplyPlanetMaterial();

        UE_LOG(LogTemp, Log, TEXT("Mesh creado con %d vertices y %d colores"), 
            MeshVertices.Num(), MeshColors.Num());
    }
}

void ATectonicsTestActor::ApplyPlanetMaterial()
{
    if (!PlanetMesh)
    {
        return;
    }

    UMaterialInterface* Chosen = nullptr;

#if WITH_EDITOR
    if (bUnlitFieldView)
    {
        // Vista de diagnostico: el color en pantalla tiene que ser el de la paleta, sin
        // que la iluminacion lo module (ver PlanetFieldMaterial.h para el porque).
        Chosen = GetOrCreateFieldViewMaterial();
    }
#endif

    if (!Chosen)
    {
        // Vista "natural": material iluminado. El sombreado ayuda a leer la forma del
        // relieve, a costa de falsear los colores de la paleta.
        Chosen = LoadObject<UMaterial>(nullptr, TEXT("/Game/Materials/M_VertexColor.M_VertexColor"));
    }

    if (Chosen)
    {
        PlanetMesh->SetMaterial(0, Chosen);
        UE_LOG(LogTemp, Log, TEXT("Material del planeta: %s (%s)"),
            *Chosen->GetName(), bUnlitFieldView ? TEXT("diagnostico/unlit") : TEXT("iluminado"));
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Sin material para el planeta: no se pudo crear M_PlanetFieldUnlit ni cargar M_VertexColor"));
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

    bool bCategoricalColor = true;
    if (FieldRegistry && FieldRegistry->GetActiveField())
    {
        bCategoricalColor = (FieldRegistry->GetActiveField()->Palette == EPlanetFieldPalette::Categorical);
    }

    // Mismo orden de celdas que CreatePlanetMesh() -4 vertices propios por celda, ver
    // SampleMeshCell()-, asi que VertexIndex avanza en pasos de 4 y cae siempre en el
    // primer vertice de la celda que le toca.
    int32 VertexIndex = 0;

    for (int32 Face = 0; Face < 6; ++Face)
    {
        ECSCubeFace CubeFace = static_cast<ECSCubeFace>(Face);

        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                if (VertexIndex + 4 > MeshVertices.Num())
                {
                    break;
                }

                FVector CellPos[4];
                FLinearColor CellColor[4];
                SampleMeshCell(CubeFace, X, Y, Resolution, Radius, bCategoricalColor, CellPos, CellColor);

                for (int32 C = 0; C < 4; ++C)
                {
                    // Ver comentario equivalente en CreatePlanetMesh(): solo se toca la
                    // posición si hay elevación activa, igual que antes.
                    if (bShowElevation)
                    {
                        MeshVertices[VertexIndex + C] = CellPos[C];
                    }
                    MeshColors[VertexIndex + C] = CellColor[C].ToFColor(true);
                }

                VertexIndex += 4;
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

void ATectonicsTestActor::SampleMeshCell(ECSCubeFace CubeFace, int32 X, int32 Y, int32 Resolution,
                                          float Radius, bool bCategoricalColor,
                                          FVector OutPosition[4], FLinearColor OutColor[4]) const
{
    // Orden de esquinas: 0=(X,Y) 1=(X+1,Y) 2=(X,Y+1) 3=(X+1,Y+1) -coincide con el orden de
    // triangulos V0,V2,V1 / V1,V2,V3 que arman CreatePlanetMesh()/UpdateMeshColors().
    const int32 CornerGX[4] = { X, X + 1, X, X + 1 };
    const int32 CornerGY[4] = { Y, Y, Y + 1, Y + 1 };

    // Centro de la celda: el unico punto de muestreo si el color es categorico, para no
    // depender de que celda vecina "gana" una esquina que geometricamente comparten.
    const float CenterU = ((static_cast<float>(X) + 0.5f) / Resolution) * 2.0f - 1.0f;
    const float CenterV = ((static_cast<float>(Y) + 0.5f) / Resolution) * 2.0f - 1.0f;

    for (int32 C = 0; C < 4; ++C)
    {
        const float CornerU = (static_cast<float>(CornerGX[C]) / Resolution) * 2.0f - 1.0f;
        const float CornerV = (static_cast<float>(CornerGY[C]) / Resolution) * 2.0f - 1.0f;

        // Proyección del cubo a esfera (DEBE coincidir exactamente con RasterizedTectonics)
        const FVector CornerCubePos = CubeFaceMapping::FaceUVToCubePoint(CubeFace, CornerU, CornerV);
        const FVector CornerNormal = CornerCubePos.GetSafeNormal();

        // La posicion -y por tanto la elevacion del terreno- se muestrea SIEMPRE por
        // esquina, categorico o no: solo el color se aplana, la forma se queda suave.
        float Elevation = 0.0f;
        if (bShowElevation && RasterizedTectonics && RasterizedTectonics->IsInitialized())
        {
            ECSCubeFace ElevFace; float ElevU, ElevV;
            CubeFaceMapping::DirectionToFaceTexUV(CornerNormal, ElevFace, ElevU, ElevV);
            Elevation = RasterizedTectonics->GetElevationBilinear(ElevFace, ElevU, ElevV);
        }

        // Elevación en metros reales -> cm (unidades de Unreal), con exageración directa.
        // Independiente del radio del planeta.
        OutPosition[C] = CornerNormal * (Radius + Elevation * 100.0f * ElevationScale);

        // El color muestrea el centro de la celda si es categorico -las 4 esquinas caen
        // en el mismo punto del mundo y salen identicas, sin degradado posible- o la
        // propia esquina si es continuo -la costura con la celda vecina sigue siendo
        // invisible porque ambas evaluan la misma formula en la misma posicion del mundo-.
        const FVector ColorNormal = bCategoricalColor
            ? CubeFaceMapping::FaceUVToCubePoint(CubeFace, CenterU, CenterV).GetSafeNormal()
            : CornerNormal;

        int32 PlateID = 0;
        FLinearColor Color = FLinearColor::Black;
        bool bHaveFieldColor = false;

        if (RasterizedTectonics && RasterizedTectonics->IsInitialized())
        {
            // Conversion unificada (CubeFaceMapping.h, ROADMAP.md F0).
            ECSCubeFace DominantFace; float TexU, TexV;
            CubeFaceMapping::DirectionToFaceTexUV(ColorNormal, DominantFace, TexU, TexV);

            const int32 TexX = FMath::Clamp(FMath::FloorToInt(TexU * RasterResolution), 0, RasterResolution - 1);
            const int32 TexY = FMath::Clamp(FMath::FloorToInt(TexV * RasterResolution), 0, RasterResolution - 1);
            PlateID = RasterizedTectonics->GetPlateIDAt(DominantFace, TexX, TexY);

            float FieldValue = 0.0f;
            if (FieldRegistry && FieldRegistry->GetActiveField() &&
                FieldRegistry->SampleActiveBilinear(DominantFace, TexU, TexV, FieldValue))
            {
                Color = FieldRegistry->ColorForValue(FieldValue);
                bHaveFieldColor = true;
            }
        }
        else if (PlateSystem)
        {
            const int32 CellX = FMath::Clamp(CornerGX[C], 0, Resolution - 1);
            const int32 CellY = FMath::Clamp(CornerGY[C], 0, Resolution - 1);
            PlateID = PlateSystem->GetPlateIDAt(CubeFace, CellX, CellY);
        }

        if (!bHaveFieldColor)
        {
            // Sin campo disponible: se cae al coloreado por placa de siempre.
            Color = GetPlateColor(PlateID);
        }

        // Resaltar placa seleccionada.
        if (HighlightedPlate >= 0)
        {
            Color = (PlateID == HighlightedPlate) ? FLinearColor::White : Color * 0.3f;
        }

        OutColor[C] = Color;
    }
}

// ============================================================
// CAMPOS DE DIAGNOSTICO (ROADMAP.md F0.5)
// ============================================================

void ATectonicsTestActor::RefreshCategoricalFieldCaches()
{
    if (!RasterizedTectonics || !RasterizedTectonics->IsInitialized())
    {
        return;
    }

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        const FTectonicFaceTextureData* Face =
            RasterizedTectonics->GetFaceData(static_cast<ECSCubeFace>(FaceIdx));
        if (!Face)
        {
            continue;
        }

        PlateIDFieldCache[FaceIdx].SetNumUninitialized(Face->PlateIDData.Num());
        for (int32 i = 0; i < Face->PlateIDData.Num(); ++i)
        {
            PlateIDFieldCache[FaceIdx][i] = static_cast<float>(Face->PlateIDData[i]);
        }

        CrustTypeFieldCache[FaceIdx].SetNumUninitialized(Face->CrustTypeData.Num());
        for (int32 i = 0; i < Face->CrustTypeData.Num(); ++i)
        {
            CrustTypeFieldCache[FaceIdx][i] = static_cast<float>(Face->CrustTypeData[i]);
        }

        // Grosor en km, que es la unidad en la que se piensa la corteza (35 km, 70 km),
        // no en metros.
        CrustThicknessFieldCache[FaceIdx].SetNumUninitialized(Face->CrustThicknessData.Num());
        for (int32 i = 0; i < Face->CrustThicknessData.Num(); ++i)
        {
            CrustThicknessFieldCache[FaceIdx][i] = Face->CrustThicknessData[i] / 1000.0f;
        }

        // Altura referida al nivel del mar actual, que se mueve con la tectonica.
        const float CurrentSeaLevel = RasterizedTectonics->GetSeaLevel();
        AboveSeaLevelFieldCache[FaceIdx].SetNumUninitialized(Face->ElevationData.Num());
        for (int32 i = 0; i < Face->ElevationData.Num(); ++i)
        {
            AboveSeaLevelFieldCache[FaceIdx][i] = Face->ElevationData[i] - CurrentSeaLevel;
        }

        RecoveryCountFieldCache[FaceIdx].SetNumUninitialized(Face->RecoveryCountData.Num());
        for (int32 i = 0; i < Face->RecoveryCountData.Num(); ++i)
        {
            RecoveryCountFieldCache[FaceIdx][i] = static_cast<float>(Face->RecoveryCountData[i]);
        }
    }
}

void ATectonicsTestActor::RegisterSimulationFields()
{
    if (!FieldRegistry || !RasterizedTectonics)
    {
        return;
    }

    FieldRegistry->Reset();
    RefreshCategoricalFieldCaches();

    const int32 Res = RasterizedTectonics->GetTextureResolution();
    URasterizedTectonics* Raster = RasterizedTectonics;

    // --- Elevación -------------------------------------------------------------
    // Rango FIJO, no automático, y a propósito: con rango automático la escala se
    // reajusta sola a medida que crecen las montañas, así que el planeta se ve siempre
    // igual y es imposible notar que el relieve está creciendo. Con rango fijo, ver que
    // todo satura a blanco ES la señal de que el levantamiento se está desbocando.
    // El centro de la paleta Terrain (T=0.5) cae en 0 m, o sea el nivel del mar.
    {
        FPlanetScalarField Field;
        Field.Id = TEXT("Elevation");
        Field.Label = TEXT("Elevacion");
        Field.Unit = TEXT("m");
        Field.Palette = EPlanetFieldPalette::Terrain;
        Field.Resolution = Res;
        Field.bAutoRange = false;
        Field.RangeMin = -10000.0f;
        Field.RangeMax = 10000.0f;
        Field.GetFaceData = [Raster](ECSCubeFace Face) -> const TArray<float>*
        {
            return Raster->IsInitialized() ? &Raster->GetElevationData(Face) : nullptr;
        };
        FieldRegistry->RegisterField(Field);
    }

    // --- ID de placa -----------------------------------------------------------
    // El campo que hace visible el bloqueador de F1: hoy es una imagen congelada, y en
    // cuanto las placas se muevan debe verse la deriva.
    {
        FPlanetScalarField Field;
        Field.Id = TEXT("PlateID");
        Field.Label = TEXT("ID de placa");
        Field.Palette = EPlanetFieldPalette::Categorical;
        Field.Resolution = Res;
        Field.bAutoRange = false;
        ATectonicsTestActor* Self = this;
        Field.GetFaceData = [Self](ECSCubeFace Face) -> const TArray<float>*
        {
            const int32 Idx = static_cast<int32>(Face);
            return (Idx >= 0 && Idx < 6) ? &Self->PlateIDFieldCache[Idx] : nullptr;
        };
        FieldRegistry->RegisterField(Field);
    }

    // --- Edad de la corteza ----------------------------------------------------
    // En F1 este campo es la prueba de fuego del spreading: deben aparecer bandas
    // simétricas a ambos lados de las dorsales, como en los mapas reales del fondo
    // oceánico. Hoy es ruido inicial aleatorio, que ya dice algo: que no hay spreading.
    {
        FPlanetScalarField Field;
        Field.Id = TEXT("CrustAge");
        Field.Label = TEXT("Edad de corteza");
        Field.Unit = TEXT("Ma");
        Field.Palette = EPlanetFieldPalette::Sequential;
        Field.Resolution = Res;
        Field.bAutoRange = true;
        Field.GetFaceData = [Raster](ECSCubeFace Face) -> const TArray<float>*
        {
            const FTectonicFaceTextureData* Data = Raster->GetFaceData(Face);
            return Data ? &Data->CrustAgeData : nullptr;
        };
        FieldRegistry->RegisterField(Field);
    }

    // --- Tipo de corteza -------------------------------------------------------
    {
        FPlanetScalarField Field;
        Field.Id = TEXT("CrustType");
        Field.Label = TEXT("Tipo de corteza (0=oceanica, 1=continental)");
        Field.Palette = EPlanetFieldPalette::Categorical;
        Field.Resolution = Res;
        Field.bAutoRange = false;
        ATectonicsTestActor* Self = this;
        Field.GetFaceData = [Self](ECSCubeFace Face) -> const TArray<float>*
        {
            const int32 Idx = static_cast<int32>(Face);
            return (Idx >= 0 && Idx < 6) ? &Self->CrustTypeFieldCache[Idx] : nullptr;
        };
        FieldRegistry->RegisterField(Field);
    }

    // --- Grosor de corteza -----------------------------------------------------
    // El estado primario desde F2. Aqui se ve la RAIZ de las cordilleras: una montana
    // alta tiene debajo una columna gruesa, que es lo que la sostiene por flotacion.
    {
        FPlanetScalarField Field;
        Field.Id = TEXT("CrustThickness");
        Field.Label = TEXT("Grosor de corteza");
        Field.Unit = TEXT("km");
        Field.Palette = EPlanetFieldPalette::Sequential;
        Field.Resolution = Res;
        Field.bAutoRange = true;
        ATectonicsTestActor* Self = this;
        Field.GetFaceData = [Self](ECSCubeFace Face) -> const TArray<float>*
        {
            const int32 Idx = static_cast<int32>(Face);
            return (Idx >= 0 && Idx < 6) ? &Self->CrustThicknessFieldCache[Idx] : nullptr;
        };
        FieldRegistry->RegisterField(Field);
    }

    // --- Altura sobre el nivel del mar -----------------------------------------
    // Distinto de la elevacion: esta referida al nivel del mar ACTUAL, que se mueve con
    // la tectonica. Con paleta divergente centrada en cero, la costa es exactamente donde
    // cambia el color, asi que la linea de costa se lee de un vistazo en vez de haber que
    // adivinarla entre azules.
    {
        FPlanetScalarField Field;
        Field.Id = TEXT("AboveSeaLevel");
        Field.Label = TEXT("Altura sobre el nivel del mar");
        Field.Unit = TEXT("m");
        Field.Palette = EPlanetFieldPalette::Diverging;
        Field.Scale = EPlanetFieldScale::Linear;
        Field.Resolution = Res;
        Field.bAutoRange = false;
        Field.RangeMin = -8000.0f;
        Field.RangeMax = 8000.0f;
        Field.DivergingCenter = 0.0f;
        ATectonicsTestActor* Self = this;
        Field.GetFaceData = [Self](ECSCubeFace Face) -> const TArray<float>*
        {
            const int32 Idx = static_cast<int32>(Face);
            return (Idx >= 0 && Idx < 6) ? &Self->AboveSeaLevelFieldCache[Idx] : nullptr;
        };
        FieldRegistry->RegisterField(Field);
    }

    // --- Clima (ROADMAP.md F3) -------------------------------------------------
    if (Climate && Climate->IsInitialized())
    {
        UPlanetClimate* ClimateRef = Climate;

        // Temperatura: paleta DIVERGENTE centrada en 0 grados, porque el cero tiene aqui
        // significado fisico - es donde el agua se hiela - y no es un punto medio
        // arbitrario. Con una paleta secuencial la isoterma de 0 no se veria.
        {
            FPlanetScalarField Field;
            Field.Id = TEXT("Temperature");
            Field.Label = TEXT("Temperatura");
            Field.Unit = TEXT("C");
            Field.Palette = EPlanetFieldPalette::Diverging;
            Field.Resolution = Res;
            Field.bAutoRange = false;
            Field.RangeMin = -40.0f;
            Field.RangeMax = 40.0f;
            Field.DivergingCenter = 0.0f;
            Field.GetFaceData = [ClimateRef](ECSCubeFace Face) -> const TArray<float>*
            {
                return ClimateRef->IsInitialized() ? &ClimateRef->GetTemperatureData(Face) : nullptr;
            };
            FieldRegistry->RegisterField(Field);
        }

        // Precipitacion: el mapa que valida F3. Debe verse cinturon humedo ecuatorial,
        // franjas deserticas hacia los 30 grados, y sombras de lluvia detras de las
        // cordilleras.
        {
            FPlanetScalarField Field;
            Field.Id = TEXT("Precipitation");
            Field.Label = TEXT("Precipitacion");
            Field.Unit = TEXT("mm/ano");
            Field.Palette = EPlanetFieldPalette::Sequential;
            Field.Resolution = Res;
            Field.bAutoRange = false;
            Field.RangeMin = 0.0f;
            Field.RangeMax = 3000.0f;
            Field.GetFaceData = [ClimateRef](ECSCubeFace Face) -> const TArray<float>*
            {
                return ClimateRef->IsInitialized() ? &ClimateRef->GetPrecipitationData(Face) : nullptr;
            };
            FieldRegistry->RegisterField(Field);
        }
    }

    // --- Caudal acumulado (ROADMAP.md F4) --------------------------------------
    // ESTE es el mapa que valida F4, y va en ESCALA LOGARITMICA por necesidad. El caudal
    // crece varios ordenes de magnitud entre la cabecera de un arroyo y la desembocadura
    // del rio principal: en lineal el cauce principal satura y todos sus afluentes quedan
    // indistinguibles del fondo, asi que la red simplemente no se ve. En log aparecen las
    // ramificaciones, que es lo que hay que juzgar.
    if (Hydrology && Hydrology->IsInitialized())
    {
        UPlanetHydrology* HydroRef = Hydrology;

        // AREA DRENADA, no caudal, y el rango se fija a mano. Es el mapa que hace visible
        // la red.
        //
        // Con rango automatico y caudal en m3/ano no se veia nada: una sola celda de 39 km
        // ya aporta ~1,5e9 m3/ano, asi que entre una cabecera y el rio mayor hay apenas
        // una decada, y en la rampa de color toda la variacion quedaba aplastada contra el
        // extremo mientras el oceano (caudal 0) ocupaba el otro. Tierra de un color plano.
        //
        // El area drenada arranca en el area de UNA celda, que es el minimo con sentido, y
        // llega a cuencas continentales: varias decadas limpias. Fijar el minimo del rango
        // en una celda es lo que hace que una cabecera sea el color mas bajo y un rio
        // principal el mas alto, en vez de repartir la rampa entre el oceano y la tierra.
        const float CellArea = HydroRef->GetCellAreaKm2();

        FPlanetScalarField Field;
        Field.Id = TEXT("DrainageArea");
        Field.Label = TEXT("Area drenada");
        Field.Unit = TEXT("km2");
        Field.Palette = EPlanetFieldPalette::Sequential;
        Field.Scale = EPlanetFieldScale::Logarithmic;
        Field.Resolution = Res;
        Field.bAutoRange = false;
        Field.RangeMin = CellArea;
        Field.RangeMax = CellArea * 3000.0f;   // cuenca grande: ~3000 celdas aguas arriba
        Field.GetFaceData = [HydroRef](ECSCubeFace Face) -> const TArray<float>*
        {
            return HydroRef->IsInitialized() ? &HydroRef->GetDrainageAreaData(Face) : nullptr;
        };
        FieldRegistry->RegisterField(Field);
    }

    // --- Celdas congeladas (residuo real) ---------------------------------------
    // DIAGNOSTICO: cuantas veces cada celda ha caido en el residuo real de
    // AdvectPlateField -ni reclamante ni con tolerancia, ni rift- y se le ha conservado el
    // estado anterior. Desde R2.9 Fase 4 (ANEXO.md A14) ya no mide recuperaciones por
    // tolerancia -ese mecanismo se quito-, sino el trilema documentado, sobre todo en
    // fronteras transformantes. Un valor alto y persistente en el mismo sitio es una celda
    // cronicamente congelada. Rango automatico: el valor tipico es 0.
    {
        FPlanetScalarField Field;
        Field.Id = TEXT("RecoveryCount");
        Field.Label = TEXT("Celdas congeladas (residuo)");
        Field.Palette = EPlanetFieldPalette::Sequential;
        Field.Resolution = Res;
        Field.bAutoRange = true;
        ATectonicsTestActor* SelfRecovery = this;
        Field.GetFaceData = [SelfRecovery](ECSCubeFace Face) -> const TArray<float>*
        {
            const int32 Idx = static_cast<int32>(Face);
            return (Idx >= 0 && Idx < 6) ? &SelfRecovery->RecoveryCountFieldCache[Idx] : nullptr;
        };
        FieldRegistry->RegisterField(Field);
    }

    FieldRegistry->RefreshRanges();

    UE_LOG(LogTemp, Log, TEXT("  - %d campos de diagnostico registrados (F/G para conmutar)"),
        FieldRegistry->GetNumFields());
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
    
    BoundaryLimitsText = FString::Printf(TEXT("Límites: Conv=%d Div=%d Trans=%d"), Convergent, Divergent, Transform);
}

void ATectonicsTestActor::DrawScreenDebugInfo()
{
    // ============================================================
    // TRES BLOQUES, TRES SITIOS (17-08-2026)
    //
    // Antes todo esto eran varios canales de GEngine->AddOnScreenDebugMessage, que solo
    // sabe apilar arriba a la izquierda -con la pantalla cada vez mas llena, dejo de
    // leerse. Ahora se rellenan tres miembros (HUDDebugText, HUDKeyLegendText,
    // HUDCostText) que ASimuHUD::DrawHUD() dibuja posicionados: depuracion arriba
    // izquierda, teclas arriba derecha, coste una sola linea abajo centrada. Ver el
    // comentario junto a los getters en el .h para por que lo dibuja la HUD y no este
    // actor directamente.
    // ============================================================

    // DIAGNOSTICO (16-08-2026): desglose continental sumergido vs emergido, para verificar
    // en vivo si la acrecion de arco fabrica plataforma sumergida (~20 km -> ~-1.660 m por
    // Airy) en vez de tierra de verdad. Ver ANEXO.md "Acrecion de arco: restriccion por
    // placa".
    float SubmergedContinentalFrac = 0.0f, EmergedContinentalFrac = 0.0f, OceanicFrac = 0.0f;
    if (RasterizedTectonics)
    {
        RasterizedTectonics->GetContinentalBreakdown(SubmergedContinentalFrac, EmergedContinentalFrac, OceanicFrac);
    }

    // DIAGNOSTICO (17-08-2026): ver comentario junto a GetFrozenCellDiagnostics() en
    // RasterizedTectonics.h -- distingue residuo cronico (acotado) de residuo que se
    // extiende, sin tocar la logica de resolucion.
    int32 DistinctFrozenCells = 0, MaxRecoveryCount = 0;
    if (RasterizedTectonics)
    {
        RasterizedTectonics->GetFrozenCellDiagnostics(DistinctFrozenCells, MaxRecoveryCount);
    }

    HUDDebugText = FString::Printf(
        TEXT("=== TECTÓNICA ===\n")
        TEXT("Tiempo: %.2f Ma | Pasos: %d\n")
        TEXT("Estado: %s | Escala: %.1fx\n")
        TEXT("Placas: %d | Grid: %d\n")
        TEXT("DIAG tierra: emergida %.1f%% | continental sumergida %.1f%% | oceanica %.1f%%\n")
        TEXT("R2.12 segmentos: %d activos | edad media %.2f Ma | celdas %d\n")
        TEXT("Corteza: +%d creada / -%d destruida (%d advecciones)\n")
        TEXT("AUDIT reloj: sim %.2f Ma / advectado %.2f Ma (pendiente %.1f%%)\n")
        TEXT("AUDIT tiempo TIRADO: %.2f Ma en %d veces\n")
        TEXT("AUDIT respaldo material: %d de %d (%.2f%%)\n")
        TEXT("AUDIT huecos: %d recuperados por tolerancia | %d CONGELADOS acumulado (trilema) | %d distintas, max %d fallos | %d ESTA adveccion (%d convergen, %d cerca de cero) | %d resueltas por vecino cercano\n")
        TEXT("Drenaje: %d celdas de cauce | %d lagos\n")
        TEXT("%s"),
        SimulationTime,
        SimulationSteps,
        bSimulationRunning ? TEXT("EJECUTANDO") : TEXT("PAUSADO"),
        TimeScale,
        PlateSystem ? PlateSystem->GetNumPlates() : 0,
        GridResolution,
        EmergedContinentalFrac * 100.0f,
        SubmergedContinentalFrac * 100.0f,
        OceanicFrac * 100.0f,
        RasterizedTectonics ? RasterizedTectonics->GetBoundarySegments().Num() : 0,
        [this]() {
            if (!RasterizedTectonics) return 0.0f;
            const TArray<FBoundarySegment>& Segs = RasterizedTectonics->GetBoundarySegments();
            if (Segs.Num() == 0) return 0.0f;
            float Sum = 0.0f;
            for (const FBoundarySegment& S : Segs) { Sum += S.Age; }
            return Sum / Segs.Num();
        }(),
        [this]() {
            if (!RasterizedTectonics) return 0;
            int32 Sum = 0;
            for (const FBoundarySegment& S : RasterizedTectonics->GetBoundarySegments()) { Sum += S.CellCount; }
            return Sum;
        }(),
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().CellsCreated : 0,
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().CellsDestroyed : 0,
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().AdvectionCount : 0,
        // AUDITORIA 0a: el reloj. TotalSimulationTime suma el paso entero siempre, pero
        // Step() descarta el tiempo sobrante cuando hacen falta mas de 8 advecciones. Si
        // estos dos numeros divergen, las edades y las tasas estan mal escaladas.
        RasterizedTectonics ? RasterizedTectonics->GetTotalSimulationTime() : 0.0f,
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().AdvectedTime : 0.0f,
        (RasterizedTectonics && RasterizedTectonics->GetTotalSimulationTime() > 0.0f)
            ? 100.0f * (RasterizedTectonics->GetTotalSimulationTime()
                      - RasterizedTectonics->GetAdvectionStats().AdvectedTime)
                     / RasterizedTectonics->GetTotalSimulationTime()
            : 0.0f,
        // AUDITORIA 0a-bis: esto SI es tiempo perdido. La resta de arriba esta dominada por
        // el acumulador a medio llenar y no puede ver este termino.
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().DiscardedTime : 0.0f,
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().DiscardEvents : 0,
        // AUDITORIA 0b: cuantas lecturas de material caen al respaldo (lectura encadenada
        // del mundo) en vez de salir del marco propio de la placa.
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().MaterialFallbacks : 0,
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().MaterialReads : 0,
        (RasterizedTectonics && RasterizedTectonics->GetAdvectionStats().MaterialReads > 0)
            ? 100.0f * RasterizedTectonics->GetAdvectionStats().MaterialFallbacks
                     / RasterizedTectonics->GetAdvectionStats().MaterialReads
            : 0.0f,
        // R2.9 FASE 4 (17-08-2026): ya no cuenta recuperaciones por tolerancia -ese
        // mecanismo se quito, ver ANEXO.md A14- sino el trilema real: ni reclamante, ni
        // rift. Sobre todo fronteras transformantes.
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().CellsRecovered : 0,
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().CellsUnresolved : 0,
        // DIAGNOSTICO (17-08-2026): CellsUnresolved de arriba es un contador ACUMULADO que
        // nunca baja -crece aunque el area afectada este parada-. DistinctFrozenCells y
        // MaxRecoveryCount (calculados arriba) dicen si el residuo es cronico (acotado) o
        // se esta extendiendo, sin tocar la logica de resolucion.
        DistinctFrozenCells,
        MaxRecoveryCount,
        // DIAGNOSTICO (17-08-2026): las dos anteriores resultaron enganosas -tanto la
        // acumulada como "distintas" solo pueden crecer, porque cuentan historia (¿ha
        // fallado esta celda ALGUNA VEZ?), no estado actual, y una frontera que simplemente
        // se desplaza sobre el planeta barre celdas nuevas sin parar aunque el ritmo real no
        // empeore. Esta, en cambio, se SOBREESCRIBE cada adveccion -cuantas celdas fallaron
        // en la MAS RECIENTE, nada mas-, la unica de las tres que puede bajar si el ritmo
        // real mejora.
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().LastUnresolvedCells : 0,
        // DIAGNOSTICO (17-08-2026): desglose para distinguir residuo "convergente sin
        // reclamar" (posible fallo real de la busqueda) de "cerca de cero" (candidato a
        // frontera transformante, ya documentada sin modelar en ROADMAP.md F1D).
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().LastUnresolvedConverging : 0,
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().LastUnresolvedNearZero : 0,
        // Arreglo (18-08-2026): costuras transformantes completadas por vecino mas cercano
        // en vez de quedar en el residuo. Ver FindNearestOwnerWide.
        RasterizedTectonics ? RasterizedTectonics->GetAdvectionStats().LastResolvedByWideSearch : 0,
        Hydrology ? Hydrology->GetStats().ChannelCells : 0,
        Hydrology ? Hydrology->GetStats().SinkCells : 0,
        FieldRegistry ? *FieldRegistry->GetLegendText() : TEXT("(sin visor)")
    );

    // Banner del modo de depuracion, límites (si estan activos) e info de la placa
    // resaltada: se anexan al mismo bloque en vez de canales aparte.
    if (RasterizedTectonics && RasterizedTectonics->IsDebugFakeRotationOnly())
    {
        HUDDebugText += FString::Printf(
            TEXT("\n=== MODO DEPURACION CAPA 1 (T): angulo acumulado placa 0 = %.4f grados ==="),
            RasterizedTectonics->GetDebugAccumAngleDegrees(0));
    }
    else if (RasterizedTectonics && RasterizedTectonics->IsDebugAdvectionOnly())
    {
        HUDDebugText += TEXT("\n=== MODO DEPURACION CAPA 2a (Y): adveccion real, sin frontera ni isostasia ===");
    }

    if (RasterizedTectonics && RasterizedTectonics->IsUsingDynamicKinematics())
    {
        HUDDebugText += TEXT("\n=== F1F (D): cinematica DINAMICA -balance de pares, no aleatoria fija ===");
    }

    if (RasterizedTectonics && RasterizedTectonics->IsDebugAssimilationDisabled())
    {
        HUDDebugText += TEXT("\n=== F1E (I): asimilacion de islas DESACTIVADA -comparando desgaste de frontera ===");
    }

    if (bShowPlateBoundaries && !BoundaryLimitsText.IsEmpty())
    {
        HUDDebugText += TEXT("\n") + BoundaryLimitsText;
    }

    if (HighlightedPlate >= 0)
    {
        HUDDebugText += TEXT("\n") + GetPlateInfo(HighlightedPlate);
    }

    HUDKeyLegendText = FString::Printf(
        TEXT("=== TECLAS ===\n")
        TEXT("SPACE   Pausa\n")
        TEXT("R       Reiniciar\n")
        TEXT("+ / -   Velocidad\n")
        TEXT("1-8     Placa\n")
        TEXT("V       Velocidades\n")
        TEXT("B       Límites\n")
        TEXT("T       Depuracion capa 1\n")
        TEXT("Y       Depuracion capa 2a\n")
        TEXT("D       F1F cinematica dinamica\n")
        TEXT("I       F1E asimilacion on/off\n")
        TEXT("F / G   Campo\n")
        TEXT("U       %s"),
        bUnlitFieldView ? TEXT("Iluminado") : TEXT("Unlit")
    );

    HUDCostText = FString::Printf(
        TEXT("Coste: sim %.1fms@%.0fHz | malla %.1fms@%.0fHz | adv %.1f (motas %.1f) frag %.1f seg %.1f wb %.1f front %.1f dif %.1f iso %.1f ms"),
        AvgSimStepMs,
        SimulationStepsPerSecond,
        AvgMeshUpdateMs,
        MeshUpdateHz,
        RasterizedTectonics ? RasterizedTectonics->GetStepTimings().AdvectionMs : 0.0f,
        RasterizedTectonics ? RasterizedTectonics->GetStepTimings().DespeckleMs : 0.0f,
        RasterizedTectonics ? RasterizedTectonics->GetStepTimings().FragmentationMs : 0.0f,
        RasterizedTectonics ? RasterizedTectonics->GetStepTimings().SegmentsMs : 0.0f,
        RasterizedTectonics ? RasterizedTectonics->GetStepTimings().WriteBackMs : 0.0f,
        RasterizedTectonics ? RasterizedTectonics->GetStepTimings().BoundaryMs : 0.0f,
        RasterizedTectonics ? RasterizedTectonics->GetStepTimings().DiffusionMs : 0.0f,
        RasterizedTectonics ? RasterizedTectonics->GetStepTimings().IsostasyMs : 0.0f
    );
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

    // F / G - Conmutar campo de diagnostico (ROADMAP.md F0.5)
    if (FieldRegistry && FieldRegistry->GetNumFields() > 0)
    {
        const bool bNext = PC->WasInputKeyJustPressed(EKeys::F);
        const bool bPrev = PC->WasInputKeyJustPressed(EKeys::G);
        if (bNext || bPrev)
        {
            FieldRegistry->CycleActive(bNext ? 1 : -1);
            FieldRegistry->RefreshRanges();
            bForceMeshColorUpdate = true;
            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
                    FieldRegistry->GetLegendText());
            }
        }
    }

    // U - Conmutar entre vista de diagnostico (unlit) y vista iluminada
    if (PC->WasInputKeyJustPressed(EKeys::U))
    {
        bUnlitFieldView = !bUnlitFieldView;
        ApplyPlanetMaterial();
        bForceMeshColorUpdate = true;
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
                bUnlitFieldView ? TEXT("Vista: diagnostico (unlit, color = paleta)")
                                : TEXT("Vista: iluminada (sombreado, color falseado)"));
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

    // T - MODO DEPURACION POR CAPAS (17-08-2026): solo rotacion geometrica pura, sin
    // isostasia, fisica de frontera, ni segmentos. La capa 1 de la prueba por capas.
    if (PC->WasInputKeyJustPressed(EKeys::T) && RasterizedTectonics)
    {
        const bool bNewState = !RasterizedTectonics->IsDebugFakeRotationOnly();
        RasterizedTectonics->SetDebugFakeRotationOnly(bNewState);
        if (bNewState)
        {
            // Las dos capas son mutuamente excluyentes: activar una apaga la otra, para
            // no tener Step() decidiendo entre ellas por orden de comprobacion.
            RasterizedTectonics->SetDebugAdvectionOnly(false);
        }
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, bNewState ? FColor::Magenta : FColor::Green,
                bNewState
                    ? TEXT("MODO DEPURACION CAPA 1: solo rotacion geometrica pura (sin vecinos, sin fisica)")
                    : TEXT("Simulacion real reanudada"));
        }
    }

    // Y - MODO DEPURACION POR CAPAS: Capa 2a, adveccion real (AdvectPlateField) sin
    // fisica de frontera ni isostasia. Ver RasterizedTectonics.h.
    if (PC->WasInputKeyJustPressed(EKeys::Y) && RasterizedTectonics)
    {
        const bool bNewState = !RasterizedTectonics->IsDebugAdvectionOnly();
        RasterizedTectonics->SetDebugAdvectionOnly(bNewState);
        if (bNewState)
        {
            RasterizedTectonics->SetDebugFakeRotationOnly(false);
        }
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, bNewState ? FColor::Magenta : FColor::Green,
                bNewState
                    ? TEXT("MODO DEPURACION CAPA 2a: adveccion real, sin frontera ni isostasia")
                    : TEXT("Simulacion real reanudada"));
        }
    }

    // D - F1F FASE B (17-08-2026): cinematica dinamica, EulerPole/AngularVelocity desde
    // el balance de pares en vez de la asignacion aleatoria fija de siempre. Interruptor
    // independiente de T/Y -esto cambia FISICA de placas, no que capa de la advecion
    // corre, asi que puede combinarse con la simulacion real o con la Capa 2a.
    if (PC->WasInputKeyJustPressed(EKeys::D) && RasterizedTectonics)
    {
        const bool bNewState = !RasterizedTectonics->IsUsingDynamicKinematics();
        RasterizedTectonics->SetUseDynamicKinematics(bNewState);
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, bNewState ? FColor::Magenta : FColor::Green,
                bNewState
                    ? TEXT("F1F: cinematica DINAMICA (balance de pares) activada")
                    : TEXT("F1F: cinematica fija de siempre restaurada"));
        }
    }

    // I - F1E FASE A.1 (17-08-2026): asimilacion de islas huerfanas, SOLO para comparar en
    // vivo si es la causante del desgaste progresivo de frontera visto en una corrida larga
    // (segmentos R2.12 de 44 a 107 con las mismas 8 placas -ver ANEXO.md). No toca el resto
    // de F1E: las fragmentaciones por encima del umbral siguen naciendo como placa nueva.
    if (PC->WasInputKeyJustPressed(EKeys::I) && RasterizedTectonics)
    {
        const bool bNewState = !RasterizedTectonics->IsDebugAssimilationDisabled();
        RasterizedTectonics->SetDebugDisableAssimilation(bNewState);
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, bNewState ? FColor::Magenta : FColor::Green,
                bNewState
                    ? TEXT("F1E: asimilacion de islas DESACTIVADA (comparacion)")
                    : TEXT("F1E: asimilacion de islas reactivada"));
        }
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
