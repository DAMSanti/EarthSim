// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "TectonicPlanetActor.h"
#include "TectonicPlateSystem.h"
#include "TectonicVisualizerComponent.h"
#include "../CubeSphereGrid.h"
#include "../Nanite/PlanetNaniteMesh.h"
#include "../LOD/CubeLODController.h"

ATectonicPlanetActor::ATectonicPlanetActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    // Crear componente raíz
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

    // Crear componente Nanite para visualización del planeta
    NaniteMeshComponent = CreateDefaultSubobject<UPlanetNaniteMesh>(TEXT("NaniteMesh"));
    NaniteMeshComponent->SetupAttachment(RootComponent);

    // Crear componente de visualización de tectónica
    VisualizerComponent = CreateDefaultSubobject<UTectonicVisualizerComponent>(TEXT("TectonicVisualizer"));

    // Sin esto, UPlanetNaniteMesh::SyncWithQuadTree() nunca se ejecuta (ver comentario en el header)
    LODControllerComponent = CreateDefaultSubobject<UCubeLODController>(TEXT("LODController"));
}

void ATectonicPlanetActor::BeginPlay()
{
    Super::BeginPlay();

    // Inicializar automáticamente al comenzar
    InitializePlanet();
}

void ATectonicPlanetActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bIsInitialized)
    {
        return;
    }

    if (bShowDebugInfo)
    {
        DrawScreenDebugInfo();
    }

    if (!bAutoSimulate)
    {
        return;
    }

    // Actualizar configuración de visualización
    if (VisualizerComponent)
    {
        VisualizerComponent->bShowVelocityVectors = bShowVelocityVectors;
        VisualizerComponent->bShowPlateBoundaries = bShowPlateBoundaries;
    }

    // Ejecutar simulación
    AccumulatedTime += DeltaTime * SimulationSpeed;

    // Step cada cierto intervalo (10ms de tiempo simulado)
    const float StepInterval = 0.01f;

    // Espiral de la muerte (encontrada 11-08-2026, ver ROADMAP.md M1): si un frame va
    // lento, DeltaTime es grande, así que este bucle intenta compensar todo el tiempo
    // perdido de golpe - lo que tarda más, hace el siguiente frame aún más lento, etc.
    // Acotar cuántos pasos se hacen por frame rompe el ciclo: el tiempo simulado se
    // queda atrás en vez de intentar ponerse al día a cualquier precio.
    const int32 MaxStepsPerFrame = 10;
    int32 StepsThisFrame = 0;
    while (AccumulatedTime >= StepInterval && StepsThisFrame < MaxStepsPerFrame)
    {
        StepSimulation(StepInterval);
        AccumulatedTime -= StepInterval;
        StepsThisFrame++;
    }
    if (StepsThisFrame >= MaxStepsPerFrame)
    {
        // Se descarta el resto del tiempo acumulado en vez de arrastrarlo al siguiente
        // frame (que volvería a disparar el mismo problema)
        AccumulatedTime = 0.0f;
    }
}

void ATectonicPlanetActor::DrawScreenDebugInfo() const
{
    if (!GEngine)
    {
        return;
    }

    const float SimTime = PlateSystem ? PlateSystem->GetTotalSimulationTime() : 0.0f;
    const FString Msg = FString::Printf(
        TEXT("=== TECTONIC PLANET ACTOR ===\nInicializado: SI\nTiempo: %.2f Ma | Pasos: %d\nPlacas: %d | Radio: %.0f km\nAuto-simular: %s"),
        SimTime, SimulationSteps, PlateSystem ? PlateSystem->GetNumPlates() : 0,
        PlanetRadius / 100000.0f, bAutoSimulate ? TEXT("SI") : TEXT("NO"));

    GEngine->AddOnScreenDebugMessage(4269, 0.0f, FColor::Green, Msg);
}

void ATectonicPlanetActor::InitializePlanet()
{
    UE_LOG(LogTemp, Log, TEXT("TectonicPlanet: Initializing with radius %.0f, resolution %d"), 
           PlanetRadius, GridResolution);

    // Crear Grid
    Grid = NewObject<UCubeSphereGrid>(this);
    Grid->Initialize(GridResolution, PlanetRadius);

    // Inicializar Nanite mesh
    if (NaniteMeshComponent)
    {
        NaniteMeshComponent->InitializePlanet(PlanetRadius, 6);
    }

    // DESACTIVADO (11-08-2026, ver ROADMAP.md M1): conectar el LODController hace que
    // NaniteMeshComponent genere parches Nanite reales. Se probo dos veces - primero
    // sin limite de subdivision (congelacion total, "0.00001 FPS") y despues con
    // LODConfig.MaxSplitsPerFrame = 0 para fijar solo 6 parches raiz sin subdividir
    // (SIGUE congelado, "0.001 FPS", confirmado por el usuario). Que el segundo intento
    // tambien se cuelgue indica que el problema no es (solo) la cascada de creacion de
    // parches que arreglaba el primer fix, sino algo mas caro y aun sin diagnosticar -
    // candidato mas probable: coste de renderizado de Nanite Displacement en si mismo
    // (re-teselado por frame segun la vista), pero no se puede confirmar sin perfilar
    // con el editor abierto. Seguir iterando a ciegas con mas mitigaciones no es
    // razonable - desactivado por completo hasta que se pueda diagnosticar de verdad.
    /*
    if (LODControllerComponent && NaniteMeshComponent)
    {
        LODControllerComponent->Initialize(PlanetRadius);
        LODControllerComponent->LODConfig.MaxSplitsPerFrame = 0;
        NaniteMeshComponent->SetLODController(LODControllerComponent);
    }
    */

    // Generar placas
    GeneratePlates();

    bIsInitialized = true;

    UE_LOG(LogTemp, Log, TEXT("TectonicPlanet: Initialization complete"));
    if (GEngine)
    {
        // Este mensaje solo puede aparecer DESPUES del bloqueo sincrono de arriba
        // (construccion Nanite) - no hay forma de mostrar progreso "en vivo" mientras
        // carga sin hacer ese build asincrono (ver ROADMAP.md M1).
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Cyan, TEXT("TectonicPlanetActor: inicializacion completa"));
    }
}

void ATectonicPlanetActor::GeneratePlates()
{
    if (!Grid)
    {
        UE_LOG(LogTemp, Error, TEXT("TectonicPlanet: Grid not initialized"));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("TectonicPlanet: Generating %d plates..."), NumPlates);

    // Crear sistema de placas
    PlateSystem = NewObject<UTectonicPlateSystem>(this);
    
    FPlateGenerationConfig Config;
    Config.NumPlates = NumPlates;
    Config.DistributionMethod = ECentroidDistribution::Fibonacci;
    Config.bUseFixedSeed = false;
    Config.OceanicRatio = 0.7f;
    Config.MinAngularVelocity = 0.0005f;
    Config.MaxAngularVelocity = 0.005f;

    PlateSystem->Initialize(Grid, Config);
    
    if (PlateSystem->GeneratePlates())
    {
        UE_LOG(LogTemp, Log, TEXT("TectonicPlanet: Generated %d plates successfully"), 
               PlateSystem->GetAllPlates().Num());

        // Inicializar visualizador
        if (VisualizerComponent)
        {
            VisualizerComponent->Initialize(Grid, PlateSystem, NaniteMeshComponent);
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("TectonicPlanet: Failed to generate plates"));
    }
}

void ATectonicPlanetActor::StepSimulation(float DeltaTime)
{
    if (!PlateSystem)
    {
        return;
    }

    PlateSystem->Step(DeltaTime);
    SimulationSteps++;
}

void ATectonicPlanetActor::RegeneratePlates(int32 NewSeed)
{
    if (!Grid)
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("TectonicPlanet: Regenerating plates with seed %d"), NewSeed);

    // Recrear sistema con nueva semilla
    PlateSystem = NewObject<UTectonicPlateSystem>(this);
    
    FPlateGenerationConfig Config;
    Config.NumPlates = NumPlates;
    Config.DistributionMethod = ECentroidDistribution::Fibonacci;
    Config.bUseFixedSeed = true;
    Config.RandomSeed = NewSeed;
    Config.OceanicRatio = 0.7f;

    PlateSystem->Initialize(Grid, Config);
    
    if (PlateSystem->GeneratePlates())
    {
        // Reinicializar visualizador
        if (VisualizerComponent)
        {
            VisualizerComponent->Initialize(Grid, PlateSystem, NaniteMeshComponent);
        }

        UE_LOG(LogTemp, Log, TEXT("TectonicPlanet: Plates regenerated successfully"));
    }
}
