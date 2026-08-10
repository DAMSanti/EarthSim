// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "TestPlanetActor.h"
#include "ProceduralMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

ATestPlanetActor::ATestPlanetActor()
{
    PrimaryActorTick.bCanEverTick = true;

    // Crear componente de mesh
    MeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PlanetMesh"));
    RootComponent = MeshComponent;
    
    MeshComponent->bUseComplexAsSimpleCollision = false;
    MeshComponent->SetCastShadow(true);
    
    // No asignar material aquí, se hará en runtime o por el usuario
    PlanetMaterial = nullptr;
}

void ATestPlanetActor::BeginPlay()
{
    Super::BeginPlay();
    
    // Inicializar sistemas usando NewObject para UObjects
    Grid = NewObject<UCubeSphereGrid>(this);
    Grid->Initialize(GridResolution, PlanetRadius);
    
    Metrics = NewObject<UCubeSphereMetrics>(this);
    Metrics->Initialize(Grid);
    
    if (bRunFlowSimulation)
    {
        FlowSim = NewObject<USimpleFlowSimulation>(this);
        FlowSim->Initialize(Grid, Metrics);
        
        // Inicializar con un punto caliente en el centro de una cara
        int32 CenterCell = (GridResolution / 2) * GridResolution + (GridResolution / 2);
        FlowSim->SetInitialCondition(ECSCubeFace::PositiveZ, CenterCell, 100.0f);
    }
    
    // Generar mesh inicial
    RegenerateMesh();
    
    // Ejecutar tests al iniciar
    RunTests();
    
    UE_LOG(LogTemp, Log, TEXT("TestPlanetActor initialized. Radius: %.1f, Resolution: %d"), 
           PlanetRadius, GridResolution);
}

void ATestPlanetActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    // Actualizar simulación si está activa
    if (bRunFlowSimulation && FlowSim != nullptr)
    {
        FlowSim->Step(DeltaTime);
        SimulationTime += DeltaTime;
        
        // Regenerar mesh para mostrar cambios (cada 0.1s para no saturar)
        static float LastUpdate = 0.0f;
        if (SimulationTime - LastUpdate > 0.1f)
        {
            RegenerateMesh();
            LastUpdate = SimulationTime;
        }
    }
}

void ATestPlanetActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    
    // Inicializar Grid y Metrics para el editor (sin simulación)
    if (!Grid)
    {
        Grid = NewObject<UCubeSphereGrid>(this);
    }
    Grid->Initialize(GridResolution, PlanetRadius);
    
    if (!Metrics)
    {
        Metrics = NewObject<UCubeSphereMetrics>(this);
    }
    Metrics->Initialize(Grid);
    
    // Generar mesh para preview en editor
    RegenerateMesh();
    
    UE_LOG(LogTemp, Log, TEXT("OnConstruction: Generated mesh with radius %.1f"), PlanetRadius);
}

void ATestPlanetActor::RegenerateMesh()
{
    if (Grid == nullptr)
    {
        UE_LOG(LogTemp, Warning, TEXT("RegenerateMesh: Grid is null!"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("RegenerateMesh: Generating mesh with radius %.1f, resolution %d"), 
           PlanetRadius, GridResolution);
    
    // Limpiar mesh existente
    MeshComponent->ClearAllMeshSections();
    
    // Generar mesh para cada cara
    for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
    {
        GenerateFaceMesh(static_cast<ECSCubeFace>(FaceIndex), FaceIndex);
    }
    
    UE_LOG(LogTemp, Log, TEXT("RegenerateMesh: Done! 6 faces generated."));
}

void ATestPlanetActor::GenerateFaceMesh(ECSCubeFace Face, int32 SectionIndex)
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> Colors;
    
    int32 Resolution = GridResolution;
    int32 VertexCount = (Resolution + 1) * (Resolution + 1);
    
    Vertices.Reserve(VertexCount);
    Normals.Reserve(VertexCount);
    UVs.Reserve(VertexCount);
    Colors.Reserve(VertexCount);
    Triangles.Reserve(Resolution * Resolution * 6);
    
    // Generar vértices
    for (int32 Y = 0; Y <= Resolution; ++Y)
    {
        for (int32 X = 0; X <= Resolution; ++X)
        {
            // Coordenadas en cara [-1, 1]
            float FaceX = (float(X) / Resolution) * 2.0f - 1.0f;
            float FaceY = (float(Y) / Resolution) * 2.0f - 1.0f;
            
            // Convertir a punto en cubo
            FVector CubePoint;
            switch (Face)
            {
            case ECSCubeFace::PositiveX: CubePoint = FVector(1.0f, FaceX, FaceY); break;
            case ECSCubeFace::NegativeX: CubePoint = FVector(-1.0f, -FaceX, FaceY); break;
            case ECSCubeFace::PositiveY: CubePoint = FVector(-FaceX, 1.0f, FaceY); break;
            case ECSCubeFace::NegativeY: CubePoint = FVector(FaceX, -1.0f, FaceY); break;
            case ECSCubeFace::PositiveZ: CubePoint = FVector(-FaceX, -FaceY, 1.0f); break;
            case ECSCubeFace::NegativeZ: CubePoint = FVector(-FaceX, FaceY, -1.0f); break;
            }
            
            // Normalizar para proyectar a esfera
            FVector Normal = CubePoint.GetSafeNormal();
            FVector SpherePoint = Normal * PlanetRadius;
            
            Vertices.Add(SpherePoint);
            Normals.Add(Normal);
            UVs.Add(FVector2D(float(X) / Resolution, float(Y) / Resolution));
            
            // Color basado en modo de visualización
            FLinearColor VertexColor;
            
            if (bShowAreaFactor && Metrics != nullptr)
            {
                // Calcular factor de área para este punto
                float AreaFactor = Metrics->GetAreaFactor(Face, FVector2D(FaceX, FaceY));
                VertexColor = GetAreaFactorColor(AreaFactor);
            }
            else if (bRunFlowSimulation && FlowSim != nullptr)
            {
                // Color basado en datos de simulación
                int32 CellX = FMath::Clamp(X, 0, Resolution - 1);
                int32 CellY = FMath::Clamp(Y, 0, Resolution - 1);
                int32 CellIndex = CellY * Resolution + CellX;
                float Value = FlowSim->GetCellValue(Face, CellIndex);
                VertexColor = GetSimulationColor(Value);
            }
            else if (bColorByFace)
            {
                VertexColor = GetFaceColor(Face);
            }
            else
            {
                VertexColor = FLinearColor::White;
            }
            
            Colors.Add(VertexColor);
        }
    }
    
    // Generar triángulos
    for (int32 Y = 0; Y < Resolution; ++Y)
    {
        for (int32 X = 0; X < Resolution; ++X)
        {
            int32 I0 = Y * (Resolution + 1) + X;
            int32 I1 = Y * (Resolution + 1) + X + 1;
            int32 I2 = (Y + 1) * (Resolution + 1) + X;
            int32 I3 = (Y + 1) * (Resolution + 1) + X + 1;
            
            // Triángulo 1
            Triangles.Add(I0);
            Triangles.Add(I2);
            Triangles.Add(I1);
            
            // Triángulo 2
            Triangles.Add(I1);
            Triangles.Add(I2);
            Triangles.Add(I3);
        }
    }
    
    // Crear sección de mesh
    MeshComponent->CreateMeshSection_LinearColor(
        SectionIndex,
        Vertices,
        Triangles,
        Normals,
        UVs,
        Colors,
        TArray<FProcMeshTangent>(),
        true  // Create collision
    );
    
    // Aplicar material
    if (PlanetMaterial)
    {
        MeshComponent->SetMaterial(SectionIndex, PlanetMaterial);
    }
    
    // Configurar wireframe si está habilitado
    if (bShowWireframe)
    {
        MeshComponent->SetMaterial(SectionIndex, nullptr);
        MeshComponent->SetRenderCustomDepth(true);
    }
}

FLinearColor ATestPlanetActor::GetFaceColor(ECSCubeFace Face) const
{
    switch (Face)
    {
    case ECSCubeFace::PositiveX: return FLinearColor(1.0f, 0.2f, 0.2f);  // Rojo
    case ECSCubeFace::NegativeX: return FLinearColor(0.2f, 1.0f, 1.0f);  // Cyan
    case ECSCubeFace::PositiveY: return FLinearColor(0.2f, 1.0f, 0.2f);  // Verde
    case ECSCubeFace::NegativeY: return FLinearColor(1.0f, 0.2f, 1.0f);  // Magenta
    case ECSCubeFace::PositiveZ: return FLinearColor(0.2f, 0.2f, 1.0f);  // Azul
    case ECSCubeFace::NegativeZ: return FLinearColor(1.0f, 1.0f, 0.2f);  // Amarillo
    default: return FLinearColor::White;
    }
}

FLinearColor ATestPlanetActor::GetAreaFactorColor(float AreaFactor) const
{
    // El factor de área varía de ~0.65 (esquinas) a 1.0 (centro)
    // Mapear a gradiente: azul (bajo) -> verde (medio) -> rojo (alto)
    
    float Normalized = (AreaFactor - 0.5f) / 0.5f;  // Mapear [0.5, 1.0] a [0, 1]
    Normalized = FMath::Clamp(Normalized, 0.0f, 1.0f);
    
    if (Normalized < 0.5f)
    {
        float T = Normalized * 2.0f;
        return FLinearColor::LerpUsingHSV(FLinearColor::Blue, FLinearColor::Green, T);
    }
    else
    {
        float T = (Normalized - 0.5f) * 2.0f;
        return FLinearColor::LerpUsingHSV(FLinearColor::Green, FLinearColor::Red, T);
    }
}

FLinearColor ATestPlanetActor::GetSimulationColor(float Value) const
{
    // Mapear valor de simulación [0, 100] a gradiente de calor
    float Normalized = FMath::Clamp(Value / 100.0f, 0.0f, 1.0f);
    
    // Gradiente: negro -> azul -> cyan -> verde -> amarillo -> rojo -> blanco
    if (Normalized < 0.2f)
    {
        return FLinearColor::LerpUsingHSV(FLinearColor::Black, FLinearColor::Blue, Normalized * 5.0f);
    }
    else if (Normalized < 0.4f)
    {
        return FLinearColor::LerpUsingHSV(FLinearColor::Blue, FLinearColor::Green, (Normalized - 0.2f) * 5.0f);
    }
    else if (Normalized < 0.6f)
    {
        return FLinearColor::LerpUsingHSV(FLinearColor::Green, FLinearColor::Yellow, (Normalized - 0.4f) * 5.0f);
    }
    else if (Normalized < 0.8f)
    {
        return FLinearColor::LerpUsingHSV(FLinearColor::Yellow, FLinearColor::Red, (Normalized - 0.6f) * 5.0f);
    }
    else
    {
        return FLinearColor::LerpUsingHSV(FLinearColor::Red, FLinearColor::White, (Normalized - 0.8f) * 5.0f);
    }
}

void ATestPlanetActor::RunTests()
{
    UE_LOG(LogTemp, Log, TEXT("========== CUBESPHERE TESTS =========="));
    
    if (Grid == nullptr)
    {
        UE_LOG(LogTemp, Error, TEXT("Grid not initialized!"));
        return;
    }
    
    // Test 1: Conversión de coordenadas roundtrip
    {
        FVector TestPoint(1.0f, 0.5f, 0.3f);
        TestPoint.Normalize();
        
        // PointToCell requiere punto normalizado
        FCubeSphereCell Cell = Grid->PointToCell(TestPoint);
        
        // CellToPoint devuelve punto YA escalado al radio del planeta
        FVector Recovered = Grid->CellToPoint(Cell);
        
        // Escalar TestPoint al radio para comparar (CellToPoint ya incluye el radio)
        FVector ScaledTest = TestPoint * PlanetRadius;
        
        float Error = FVector::Dist(ScaledTest, Recovered);
        UE_LOG(LogTemp, Log, TEXT("Test 1 - Coordinate Roundtrip: Error = %.4f units"), Error);
        
        if (Error < PlanetRadius * 0.1f)  // Dentro de 10% del radio (tolerancia para celdas discretas)
        {
            UE_LOG(LogTemp, Log, TEXT("  PASS"));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("  FAIL - Error too high"));
        }
    }
    
    // Test 2: Validación de área total
    if (Metrics != nullptr)
    {
        float TotalArea = Metrics->ValidateTotalArea();
        float ExpectedArea = 4.0f * PI * PlanetRadius * PlanetRadius;
        float AreaError = FMath::Abs(TotalArea - ExpectedArea) / ExpectedArea * 100.0f;
        
        UE_LOG(LogTemp, Log, TEXT("Test 2 - Total Area Validation:"));
        UE_LOG(LogTemp, Log, TEXT("  Expected: %.2f, Got: %.2f, Error: %.2f%%"), 
               ExpectedArea, TotalArea, AreaError);
        
        if (AreaError < 5.0f)  // Menos de 5% de error
        {
            UE_LOG(LogTemp, Log, TEXT("  PASS"));
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("  FAIL - Area error too high"));
        }
    }
    
    // Test 3: Vecinos cross-face
    {
        // Celda en el borde de una cara
        int32 EdgeU = GridResolution - 1;  // Última columna
        int32 EdgeV = GridResolution / 2;  // Mitad de la fila
        
        FCubeSphereCell EdgeCell;
        EdgeCell.Face = ECSCubeFace::PositiveX;
        EdgeCell.U = EdgeU;
        EdgeCell.V = EdgeV;
        
        TArray<FCubeSphereCell> Neighbors = Grid->GetAllNeighbors(EdgeCell);
        
        bool HasCrossFace = false;
        for (const FCubeSphereCell& N : Neighbors)
        {
            if (N.Face != ECSCubeFace::PositiveX)
            {
                HasCrossFace = true;
                break;
            }
        }
        
        UE_LOG(LogTemp, Log, TEXT("Test 3 - Cross-Face Neighbors: %s"), 
               HasCrossFace ? TEXT("Found") : TEXT("Not Found"));
        UE_LOG(LogTemp, Log, TEXT("  %s"), HasCrossFace ? TEXT("PASS") : TEXT("FAIL"));
    }
    
    UE_LOG(LogTemp, Log, TEXT("========================================"));
}

void ATestPlanetActor::RunMassConservationTest()
{
    if (FlowSim == nullptr)
    {
        FlowSim = NewObject<USimpleFlowSimulation>(this);
        FlowSim->Initialize(Grid, Metrics);
    }
    
    FSimulationResult Result = FlowSim->RunConservationTestWithResult(100, 0.01f);
    
    UE_LOG(LogTemp, Log, TEXT("Mass Conservation Test Results:"));
    UE_LOG(LogTemp, Log, TEXT("  Initial Mass: %.4f"), Result.InitialMass);
    UE_LOG(LogTemp, Log, TEXT("  Final Mass: %.4f"), Result.FinalMass);
    UE_LOG(LogTemp, Log, TEXT("  Conservation Error: %.6f%%"), Result.ConservationError * 100.0f);
    UE_LOG(LogTemp, Log, TEXT("  Simulation Time: %.2f ms"), Result.SimulationTimeMs);
    
    if (Result.ConservationError < 0.01f)
    {
        UE_LOG(LogTemp, Log, TEXT("  PASS - Mass conserved within 1%%"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("  FAIL - Mass conservation error > 1%%"));
    }
}

FString ATestPlanetActor::GetDebugInfo() const
{
    FString Info;
    
    Info += FString::Printf(TEXT("Planet Radius: %.1f\n"), PlanetRadius);
    Info += FString::Printf(TEXT("Grid Resolution: %d x %d\n"), GridResolution, GridResolution);
    Info += FString::Printf(TEXT("Total Cells: %d\n"), GridResolution * GridResolution * 6);
    
    if (bRunFlowSimulation && FlowSim != nullptr)
    {
        Info += FString::Printf(TEXT("Simulation Time: %.2f s\n"), SimulationTime);
        Info += FString::Printf(TEXT("Total Mass: %.4f\n"), FlowSim->GetTotalMass());
    }
    
    return Info;
}
