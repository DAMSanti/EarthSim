// CubeSphereVisualizerComponent.cpp
// Implementación del visualizador
// Sprint 1.2 - Integración visual con UE5

#include "CubeSphereVisualizerComponent.h"
#include "Kismet/GameplayStatics.h"

UCubeSphereVisualizerComponent::UCubeSphereVisualizerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UCubeSphereVisualizerComponent::BeginPlay()
{
    Super::BeginPlay();
    
    // Obtener subsistema
    UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(this);
    if (GameInstance)
    {
        CubeSphereSubsystem = GameInstance->GetSubsystem<UCubeSphereSubsystem>();
    }
    
    // Crear componente de malla procedural
    MeshComponent = NewObject<UProceduralMeshComponent>(GetOwner());
    MeshComponent->RegisterComponent();
    MeshComponent->AttachToComponent(GetOwner()->GetRootComponent(), 
                                     FAttachmentTransformRules::KeepRelativeTransform);
    
    // Generar malla inicial si el planeta está inicializado
    if (CubeSphereSubsystem && CubeSphereSubsystem->IsPlanetInitialized())
    {
        GenerateMesh();
    }
    else if (CubeSphereSubsystem)
    {
        // Suscribirse al evento de inicialización
        CubeSphereSubsystem->OnPlanetInitialized.AddDynamic(this, &UCubeSphereVisualizerComponent::GenerateMesh);
    }
    
    if (bAutoUpdate)
    {
        SetComponentTickEnabled(true);
    }
}

void UCubeSphereVisualizerComponent::TickComponent(float DeltaTime, ELevelTick TickType, 
                                                    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    
    if (bAutoUpdate && MeshComponent)
    {
        UpdateColors();
    }
}

void UCubeSphereVisualizerComponent::GenerateMesh()
{
    if (!CubeSphereSubsystem || !CubeSphereSubsystem->IsPlanetInitialized())
    {
        UE_LOG(LogTemp, Warning, TEXT("CubeSphereVisualizer: Planet not initialized"));
        return;
    }
    
    UCubeSphereGrid* Grid = CubeSphereSubsystem->GetGrid();
    if (!Grid)
    {
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("Generating CubeSphere mesh at resolution %d"), VisualResolution);
    
    // Limpiar datos anteriores
    Vertices.Empty();
    Triangles.Empty();
    Normals.Empty();
    UVs.Empty();
    VertexColors.Empty();
    Tangents.Empty();
    VertexToCellMap.Empty();
    
    // Estimar tamaño
    int32 VerticesPerFace = (VisualResolution + 1) * (VisualResolution + 1);
    int32 TrianglesPerFace = VisualResolution * VisualResolution * 6;  // 2 triángulos por quad, 3 índices cada uno
    
    Vertices.Reserve(VerticesPerFace * 6);
    Triangles.Reserve(TrianglesPerFace * 6);
    Normals.Reserve(VerticesPerFace * 6);
    UVs.Reserve(VerticesPerFace * 6);
    VertexColors.Reserve(VerticesPerFace * 6);
    VertexToCellMap.Reserve(VerticesPerFace * 6);
    
    // Generar cada cara
    int32 VertexOffset = 0;
    for (int32 FaceIdx = 0; FaceIdx < 6; FaceIdx++)
    {
        GenerateFaceMesh(static_cast<ECSCubeFace>(FaceIdx), VertexOffset);
    }
    
    // Crear la malla
    MeshComponent->ClearAllMeshSections();
    MeshComponent->CreateMeshSection_LinearColor(
        0,
        Vertices,
        Triangles,
        Normals,
        UVs,
        TArray<FLinearColor>(),  // Usamos VertexColors en su lugar
        Tangents,
        true  // Create collision
    );
    
    // Aplicar colores
    UpdateColors();
    
    UE_LOG(LogTemp, Log, TEXT("Generated mesh with %d vertices, %d triangles"), 
           Vertices.Num(), Triangles.Num() / 3);
}

void UCubeSphereVisualizerComponent::GenerateFaceMesh(ECSCubeFace Face, int32& VertexOffset)
{
    UCubeSphereGrid* Grid = CubeSphereSubsystem->GetGrid();
    float PlanetRadius = Grid->GetPlanetRadius() * VisualScale;
    
    int32 StartVertex = VertexOffset;
    
    // Generar vértices
    for (int32 V = 0; V <= VisualResolution; V++)
    {
        for (int32 U = 0; U <= VisualResolution; U++)
        {
            // UV normalizado
            float UNorm = (float)U / VisualResolution;
            float VNorm = (float)V / VisualResolution;
            
            // Convertir a punto en el cubo [-1, 1]
            float LocalU = UNorm * 2.0f - 1.0f;
            float LocalV = VNorm * 2.0f - 1.0f;
            
            // Obtener ejes de la cara
            FVector AxisU, AxisV, Normal;
            switch (Face)
            {
                case ECSCubeFace::PositiveX:
                    Normal = FVector(1, 0, 0);
                    AxisU = FVector(0, 1, 0);
                    AxisV = FVector(0, 0, 1);
                    break;
                case ECSCubeFace::NegativeX:
                    Normal = FVector(-1, 0, 0);
                    AxisU = FVector(0, -1, 0);
                    AxisV = FVector(0, 0, 1);
                    break;
                case ECSCubeFace::PositiveY:
                    Normal = FVector(0, 1, 0);
                    AxisU = FVector(-1, 0, 0);
                    AxisV = FVector(0, 0, 1);
                    break;
                case ECSCubeFace::NegativeY:
                    Normal = FVector(0, -1, 0);
                    AxisU = FVector(1, 0, 0);
                    AxisV = FVector(0, 0, 1);
                    break;
                case ECSCubeFace::PositiveZ:
                    Normal = FVector(0, 0, 1);
                    AxisU = FVector(1, 0, 0);
                    AxisV = FVector(0, 1, 0);
                    break;
                case ECSCubeFace::NegativeZ:
                default:
                    Normal = FVector(0, 0, -1);
                    AxisU = FVector(1, 0, 0);
                    AxisV = FVector(0, -1, 0);
                    break;
            }
            
            // Punto en el cubo
            FVector CubePoint = Normal + AxisU * LocalU + AxisV * LocalV;
            
            // Proyectar a esfera
            FVector SpherePoint = CubePoint.GetSafeNormal() * PlanetRadius;
            FVector SphereNormal = CubePoint.GetSafeNormal();
            
            Vertices.Add(SpherePoint);
            Normals.Add(SphereNormal);
            UVs.Add(FVector2D(UNorm, VNorm));
            
            // Mapeo de vértice a celda (aproximado)
            int32 CellU = FMath::Clamp(FMath::FloorToInt(UNorm * Grid->GetResolution()), 
                                        0, Grid->GetResolution() - 1);
            int32 CellV = FMath::Clamp(FMath::FloorToInt(VNorm * Grid->GetResolution()), 
                                        0, Grid->GetResolution() - 1);
            VertexToCellMap.Add(FCubeSphereCell(Face, CellU, CellV));
            
            // Tangente
            Tangents.Add(FProcMeshTangent(AxisU, false));
            
            VertexOffset++;
        }
    }
    
    // Generar triángulos
    int32 RowSize = VisualResolution + 1;
    for (int32 V = 0; V < VisualResolution; V++)
    {
        for (int32 U = 0; U < VisualResolution; U++)
        {
            int32 BottomLeft = StartVertex + V * RowSize + U;
            int32 BottomRight = BottomLeft + 1;
            int32 TopLeft = BottomLeft + RowSize;
            int32 TopRight = TopLeft + 1;
            
            // Primer triángulo
            Triangles.Add(BottomLeft);
            Triangles.Add(TopLeft);
            Triangles.Add(TopRight);
            
            // Segundo triángulo
            Triangles.Add(BottomLeft);
            Triangles.Add(TopRight);
            Triangles.Add(BottomRight);
        }
    }
}

void UCubeSphereVisualizerComponent::UpdateColors()
{
    if (!MeshComponent || Vertices.Num() == 0)
    {
        return;
    }
    
    VertexColors.SetNum(Vertices.Num());
    
    for (int32 i = 0; i < Vertices.Num(); i++)
    {
        VertexColors[i] = GetVertexColor(VertexToCellMap[i]);
    }
    
    // Actualizar la malla con los nuevos colores
    // Nota: En producción usaríamos un método más eficiente
    MeshComponent->UpdateMeshSection_LinearColor(
        0,
        Vertices,
        Normals,
        UVs,
        TArray<FLinearColor>(),
        Tangents
    );
}

FColor UCubeSphereVisualizerComponent::GetVertexColor(const FCubeSphereCell& Cell) const
{
    switch (VisualMode)
    {
        case ECubeSphereVisualMode::FaceColors:
            return GetFaceColor(Cell.Face).ToFColor(true);
            
        case ECubeSphereVisualMode::AreaFactor:
        {
            if (CubeSphereSubsystem && CubeSphereSubsystem->GetMetrics())
            {
                UCubeSphereGrid* Grid = CubeSphereSubsystem->GetGrid();
                float UNorm = (Cell.U + 0.5f) / Grid->GetResolution();
                float VNorm = (Cell.V + 0.5f) / Grid->GetResolution();
                float Factor = CubeSphereSubsystem->GetMetrics()->GetAreaFactor(Cell.Face, FVector2D(UNorm, VNorm));
                return AreaFactorToColor(Factor).ToFColor(true);
            }
            return FColor::White;
        }
        
        case ECubeSphereVisualMode::UVCoordinates:
        {
            if (CubeSphereSubsystem && CubeSphereSubsystem->GetGrid())
            {
                UCubeSphereGrid* Grid = CubeSphereSubsystem->GetGrid();
                float UNorm = (Cell.U + 0.5f) / Grid->GetResolution();
                float VNorm = (Cell.V + 0.5f) / Grid->GetResolution();
                return FLinearColor(UNorm, VNorm, 0.0f, 1.0f).ToFColor(true);
            }
            return FColor::White;
        }
        
        case ECubeSphereVisualMode::FlowData:
        {
            if (CubeSphereSubsystem && CubeSphereSubsystem->GetFlowSimulation())
            {
                float Water = CubeSphereSubsystem->GetFlowSimulation()->GetWaterAt(Cell);
                return FlowDataToColor(Water, 10.0f).ToFColor(true);
            }
            return FColor::Black;
        }
        
        default:
            return FColor::White;
    }
}

FLinearColor UCubeSphereVisualizerComponent::GetFaceColor(ECSCubeFace Face) const
{
    switch (Face)
    {
        case ECSCubeFace::PositiveX: return FLinearColor::Red;
        case ECSCubeFace::NegativeX: return FLinearColor(0.5f, 0.0f, 0.0f);  // Dark Red
        case ECSCubeFace::PositiveY: return FLinearColor::Green;
        case ECSCubeFace::NegativeY: return FLinearColor(0.0f, 0.5f, 0.0f);  // Dark Green
        case ECSCubeFace::PositiveZ: return FLinearColor::Blue;
        case ECSCubeFace::NegativeZ: return FLinearColor(0.0f, 0.0f, 0.5f);  // Dark Blue
        default: return FLinearColor::White;
    }
}

FLinearColor UCubeSphereVisualizerComponent::AreaFactorToColor(float Factor) const
{
    // Factor va de ~0.58 (esquinas) a 1.0 (centro)
    // Mapeamos a un gradiente de color
    
    // Normalizar al rango visual [0, 1]
    float NormFactor = (Factor - 0.5f) / 0.5f;
    NormFactor = FMath::Clamp(NormFactor, 0.0f, 1.0f);
    
    // Gradiente: Rojo (distorsión alta) -> Amarillo -> Verde (distorsión baja)
    if (NormFactor < 0.5f)
    {
        float T = NormFactor * 2.0f;
        return FLinearColor::LerpUsingHSV(FLinearColor::Red, FLinearColor::Yellow, T);
    }
    else
    {
        float T = (NormFactor - 0.5f) * 2.0f;
        return FLinearColor::LerpUsingHSV(FLinearColor::Yellow, FLinearColor::Green, T);
    }
}

FLinearColor UCubeSphereVisualizerComponent::FlowDataToColor(float Value, float MaxValue) const
{
    if (Value <= 0.0f)
    {
        return FLinearColor::Black;
    }
    
    float Norm = FMath::Clamp(Value / MaxValue, 0.0f, 1.0f);
    
    // Gradiente: Negro -> Azul -> Cyan -> Blanco
    if (Norm < 0.33f)
    {
        return FLinearColor::LerpUsingHSV(FLinearColor::Black, FLinearColor::Blue, Norm * 3.0f);
    }
    else if (Norm < 0.66f)
    {
        return FLinearColor::LerpUsingHSV(FLinearColor::Blue, FLinearColor(0, 1, 1), (Norm - 0.33f) * 3.0f);
    }
    else
    {
        return FLinearColor::LerpUsingHSV(FLinearColor(0, 1, 1), FLinearColor::White, (Norm - 0.66f) * 3.0f);
    }
}

void UCubeSphereVisualizerComponent::SetVisualMode(ECubeSphereVisualMode NewMode)
{
    VisualMode = NewMode;
    UpdateColors();
}
