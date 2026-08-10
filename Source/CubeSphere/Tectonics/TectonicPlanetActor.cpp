// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "TectonicPlanetActor.h"
#include "TectonicPlateSystem.h"
#include "TectonicVisualizerComponent.h"
#include "../CubeSphereGrid.h"
#include "../Nanite/PlanetNaniteMesh.h"

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

    if (!bIsInitialized || !bAutoSimulate)
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
    while (AccumulatedTime >= StepInterval)
    {
        StepSimulation(StepInterval);
        AccumulatedTime -= StepInterval;
    }
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

    // Generar placas
    GeneratePlates();

    bIsInitialized = true;

    UE_LOG(LogTemp, Log, TEXT("TectonicPlanet: Initialization complete"));
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
