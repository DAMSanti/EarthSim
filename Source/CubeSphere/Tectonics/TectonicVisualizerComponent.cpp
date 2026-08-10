// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "TectonicVisualizerComponent.h"
#include "TectonicPlateSystem.h"
#include "PlateKinematics.h"
#include "../Nanite/PlanetNaniteMesh.h"
#include "../CubeSphereGrid.h"
#include "../CubeSphereTypes.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"
#include "ProceduralMeshComponent.h"

UTectonicVisualizerComponent::UTectonicVisualizerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.016f; // ~60 FPS

    // Paleta de colores por defecto (se expandirá según número de placas)
    PlateColorPalette = {
        FLinearColor(0.9f, 0.2f, 0.2f, 1.0f),  // Rojo
        FLinearColor(0.2f, 0.7f, 0.9f, 1.0f),  // Cyan
        FLinearColor(0.2f, 0.9f, 0.3f, 1.0f),  // Verde
        FLinearColor(0.9f, 0.7f, 0.1f, 1.0f),  // Amarillo
        FLinearColor(0.7f, 0.2f, 0.9f, 1.0f),  // Púrpura
        FLinearColor(0.9f, 0.5f, 0.2f, 1.0f),  // Naranja
        FLinearColor(0.3f, 0.3f, 0.9f, 1.0f),  // Azul
        FLinearColor(0.9f, 0.3f, 0.7f, 1.0f),  // Rosa
        FLinearColor(0.5f, 0.9f, 0.5f, 1.0f),  // Verde claro
        FLinearColor(0.9f, 0.9f, 0.3f, 1.0f),  // Amarillo claro
        FLinearColor(0.4f, 0.7f, 0.7f, 1.0f),  // Teal
        FLinearColor(0.8f, 0.4f, 0.4f, 1.0f),  // Rojo claro
    };
}

void UTectonicVisualizerComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UTectonicVisualizerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bIsInitialized)
    {
        return;
    }

    TimeSinceLastUpdate += DeltaTime;
    
    if (TimeSinceLastUpdate >= UpdateInterval)
    {
        TimeSinceLastUpdate = 0.0f;
        UpdateVisualization();
    }

    // Debug draws cada frame
    if (bShowVelocityVectors)
    {
        DrawVelocityVectors();
    }

    if (bShowPlateBoundaries)
    {
        DrawPlateBoundaries();
    }
}

void UTectonicVisualizerComponent::Initialize(UCubeSphereGrid* InGrid, UTectonicPlateSystem* InPlateSystem, UPlanetNaniteMesh* InNaniteMesh)
{
    if (!InGrid || !InPlateSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("TectonicVisualizer: Invalid initialization parameters"));
        return;
    }

    Grid = InGrid;
    PlateSystem = InPlateSystem;
    NaniteMesh = InNaniteMesh;

    // Crear sistema de cinemática si se necesita para cálculos avanzados
    // Por ahora usamos directamente PlateSystem para obtener datos

    // Generar paleta de colores para el número de placas
    int32 NumPlates = PlateSystem->GetAllPlates().Num();
    if (NumPlates > PlateColorPalette.Num())
    {
        GenerateDefaultPalette(NumPlates);
    }

    // Crear ProceduralMesh para visualización de placas si no existe
    if (!VisualizationMesh)
    {
        AActor* Owner = GetOwner();
        if (Owner)
        {
            VisualizationMesh = NewObject<UProceduralMeshComponent>(Owner);
            if (VisualizationMesh)
            {
                VisualizationMesh->SetupAttachment(Owner->GetRootComponent());
                VisualizationMesh->RegisterComponent();
                VisualizationMesh->bUseComplexAsSimpleCollision = false;
                VisualizationMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                
                // Generar mesh de visualización
                GenerateVisualizationMesh();
            }
        }
    }

    // Generar texturas de visualización
    GenerateVisualizationTextures();

    bIsInitialized = true;

    UE_LOG(LogTemp, Log, TEXT("TectonicVisualizer: Initialized with %d plates, texture resolution %d"), 
           NumPlates, TextureResolution);
}

void UTectonicVisualizerComponent::GenerateDefaultPalette(int32 NumPlates)
{
    PlateColorPalette.Empty();
    PlateColorPalette.Reserve(NumPlates);

    // Generar colores usando HSV para distribución uniforme
    for (int32 i = 0; i < NumPlates; ++i)
    {
        float Hue = (float)i / (float)NumPlates;
        float Saturation = 0.7f + FMath::FRand() * 0.3f;
        float Value = 0.7f + FMath::FRand() * 0.3f;

        // Convertir HSV a RGB
        FLinearColor Color = FLinearColor::MakeFromHSV8(
            (uint8)(Hue * 255),
            (uint8)(Saturation * 255),
            (uint8)(Value * 255)
        );
        PlateColorPalette.Add(Color);
    }
}

void UTectonicVisualizerComponent::GenerateVisualizationTextures()
{
    // Crear 6 texturas, una por cara del cubo
    PlateIDTextures.Empty();
    PlateIDTextures.Reserve(6);

    for (int32 Face = 0; Face < 6; ++Face)
    {
        UTexture2D* Texture = CreatePlateIDTexture(Face);
        PlateIDTextures.Add(Texture);
    }

    // Aplicar al material si tenemos NaniteMesh
    if (NaniteMesh)
    {
        ApplyTexturesToMaterial();
    }
}

UTexture2D* UTectonicVisualizerComponent::CreatePlateIDTexture(int32 FaceIndex)
{
    // Crear textura R8G8B8A8
    UTexture2D* Texture = UTexture2D::CreateTransient(TextureResolution, TextureResolution, PF_B8G8R8A8);
    
    if (!Texture)
    {
        UE_LOG(LogTemp, Error, TEXT("TectonicVisualizer: Failed to create texture for face %d"), FaceIndex);
        return nullptr;
    }

    // Configurar filtrado y wrapping
    Texture->Filter = TF_Bilinear;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    Texture->SRGB = false;

    // Rellenar con datos de placa
    UpdatePlateIDTexture(FaceIndex);

    return Texture;
}

void UTectonicVisualizerComponent::UpdatePlateIDTexture(int32 FaceIndex)
{
    if (!PlateIDTextures.IsValidIndex(FaceIndex) || !PlateIDTextures[FaceIndex])
    {
        return;
    }

    UTexture2D* Texture = PlateIDTextures[FaceIndex];
    ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIndex);

    // Bloquear textura para escritura
    FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FColor* Pixels = static_cast<FColor*>(Data);

    const TArray<FTectonicPlate>& Plates = PlateSystem->GetAllPlates();

    // Llenar píxeles según el modo de visualización
    for (int32 Y = 0; Y < TextureResolution; ++Y)
    {
        for (int32 X = 0; X < TextureResolution; ++X)
        {
            // Obtener ID de placa para esta celda
            int32 CellX = X * Grid->GetResolution() / TextureResolution;
            int32 CellY = Y * Grid->GetResolution() / TextureResolution;
            
            int32 PlateID = PlateSystem->GetPlateIDAt(Face, CellX, CellY);
            
            FLinearColor Color;

            switch (VisualMode)
            {
                case ETectonicVisualMode::PlateID:
                    Color = GetPlateColor(PlateID);
                    break;

                case ETectonicVisualMode::CrustType:
                    if (PlateID >= 0 && PlateID < Plates.Num())
                    {
                        Color = (Plates[PlateID].CrustType == ECrustType::Oceanic) ? OceanicColor : ContinentalColor;
                    }
                    else
                    {
                        Color = FLinearColor::Black;
                    }
                    break;

                case ETectonicVisualMode::Velocity:
                    if (PlateID >= 0 && PlateID < Plates.Num())
                    {
                        // Normalizar velocidad para color (0 = azul, max = rojo)
                        float Speed = FMath::Abs(Plates[PlateID].AngularVelocity);
                        float NormalizedSpeed = FMath::Clamp(Speed / 0.01f, 0.0f, 1.0f);
                        Color = FLinearColor::LerpUsingHSV(FLinearColor::Blue, FLinearColor::Red, NormalizedSpeed);
                    }
                    else
                    {
                        Color = FLinearColor::Black;
                    }
                    break;

                case ETectonicVisualMode::Age:
                    if (PlateID >= 0 && PlateID < Plates.Num())
                    {
                        // Edad: joven = azul, viejo = marrón
                        float NormalizedAge = FMath::Clamp(Plates[PlateID].Age / 200.0f, 0.0f, 1.0f);
                        Color = FLinearColor::LerpUsingHSV(
                            FLinearColor(0.2f, 0.4f, 0.9f),  // Joven - azul
                            FLinearColor(0.6f, 0.3f, 0.1f),  // Viejo - marrón
                            NormalizedAge
                        );
                    }
                    else
                    {
                        Color = FLinearColor::Black;
                    }
                    break;

                default:
                    Color = GetPlateColor(PlateID);
                    break;
            }

            // Convertir a FColor y escribir
            int32 PixelIndex = Y * TextureResolution + X;
            Pixels[PixelIndex] = Color.ToFColor(false);
        }
    }

    Mip.BulkData.Unlock();
    Texture->UpdateResource();
}

void UTectonicVisualizerComponent::UpdateVisualization()
{
    if (!bIsInitialized)
    {
        return;
    }

    // Actualizar todas las texturas
    for (int32 Face = 0; Face < 6; ++Face)
    {
        UpdatePlateIDTexture(Face);
    }

    // Actualizar materiales
    if (NaniteMesh)
    {
        ApplyTexturesToMaterial();
    }
}

UTexture2D* UTectonicVisualizerComponent::GetPlateIDTexture(int32 FaceIndex) const
{
    if (PlateIDTextures.IsValidIndex(FaceIndex))
    {
        return PlateIDTextures[FaceIndex];
    }
    return nullptr;
}

FLinearColor UTectonicVisualizerComponent::GetPlateColor(int32 PlateID) const
{
    if (PlateID >= 0 && PlateID < PlateColorPalette.Num())
    {
        return PlateColorPalette[PlateID];
    }
    return FLinearColor::Black;
}

void UTectonicVisualizerComponent::DrawVelocityVectors()
{
    if (!Grid || !PlateSystem)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const TArray<FTectonicPlate>& Plates = PlateSystem->GetAllPlates();

    // Dibujar un vector de velocidad cada N celdas
    const int32 SampleInterval = FMath::Max(1, Grid->GetResolution() / 16);

    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);

        for (int32 Y = 0; Y < Grid->GetResolution(); Y += SampleInterval)
        {
            for (int32 X = 0; X < Grid->GetResolution(); X += SampleInterval)
            {
                int32 PlateID = PlateSystem->GetPlateIDAt(Face, X, Y);
                
                if (PlateID < 0 || PlateID >= Plates.Num())
                {
                    continue;
                }

                // Obtener posición 3D usando CellToPoint
                FCubeSphereCell Cell;
                Cell.Face = Face;
                Cell.U = X;
                Cell.V = Y;
                FVector Position = Grid->CellToPoint(Cell);
                
                // Calcular velocidad
                FVector Velocity = Plates[PlateID].GetVelocityAtPoint(Position.GetSafeNormal(), Grid->GetPlanetRadius());
                
                // Escalar para visualización
                FVector ArrowEnd = Position + Velocity * VelocityVectorScale;

                // Dibujar flecha
                FColor Color = GetPlateColor(PlateID).ToFColor(false);
                DrawDebugDirectionalArrow(World, Position, ArrowEnd, 
                    VelocityVectorScale * 0.1f, Color, false, -1.0f, 0, 1000.0f);
            }
        }
    }
}

void UTectonicVisualizerComponent::DrawPlateBoundaries()
{
    if (!Grid || !PlateSystem)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const int32 Resolution = Grid->GetResolution();
    const float Radius = Grid->GetPlanetRadius();

    // Dibujar límites donde cambia el ID de placa
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);

        for (int32 Y = 0; Y < Resolution - 1; ++Y)
        {
            for (int32 X = 0; X < Resolution - 1; ++X)
            {
                int32 PlateID = PlateSystem->GetPlateIDAt(Face, X, Y);
                int32 RightID = PlateSystem->GetPlateIDAt(Face, X + 1, Y);
                int32 DownID = PlateSystem->GetPlateIDAt(Face, X, Y + 1);

                FCubeSphereCell CurrentCell;
                CurrentCell.Face = Face;
                CurrentCell.U = X;
                CurrentCell.V = Y;
                FVector CurrentPos = Grid->CellToPoint(CurrentCell);

                // Límite horizontal
                if (PlateID != RightID)
                {
                    FCubeSphereCell RightCell;
                    RightCell.Face = Face;
                    RightCell.U = X + 1;
                    RightCell.V = Y;
                    FVector RightPos = Grid->CellToPoint(RightCell);
                    FVector MidPoint = (CurrentPos + RightPos) * 0.5f;
                    
                    // Elevar ligeramente para que se vea sobre la superficie
                    MidPoint = MidPoint.GetSafeNormal() * (Radius + 1000.0f);
                    FVector Start = CurrentPos.GetSafeNormal() * (Radius + 1000.0f);
                    FVector End = RightPos.GetSafeNormal() * (Radius + 1000.0f);

                    DrawDebugLine(World, Start, End, ConvergentColor.ToFColor(false), 
                        false, -1.0f, 0, BoundaryLineWidth * 500.0f);
                }

                // Límite vertical
                if (PlateID != DownID)
                {
                    FCubeSphereCell DownCell;
                    DownCell.Face = Face;
                    DownCell.U = X;
                    DownCell.V = Y + 1;
                    FVector DownPos = Grid->CellToPoint(DownCell);
                    FVector Start = CurrentPos.GetSafeNormal() * (Radius + 1000.0f);
                    FVector End = DownPos.GetSafeNormal() * (Radius + 1000.0f);

                    DrawDebugLine(World, Start, End, ConvergentColor.ToFColor(false), 
                        false, -1.0f, 0, BoundaryLineWidth * 500.0f);
                }
            }
        }
    }
}

void UTectonicVisualizerComponent::ApplyTexturesToMaterial()
{
    // Actualizar los colores del mesh de visualización
    if (VisualizationMesh && bIsInitialized)
    {
        UpdateVisualizationMeshColors();
    }

    // Si hay material overlay configurado, actualizar texturas
    if (PlateOverlayMaterial)
    {
        for (int32 i = 0; i < 6 && i < PlateIDTextures.Num(); ++i)
        {
            if (PlateIDTextures[i])
            {
                FString ParamName = FString::Printf(TEXT("PlateIDTexture_%d"), i);
                PlateOverlayMaterial->SetTextureParameterValue(FName(*ParamName), PlateIDTextures[i]);
            }
        }
    }
}

void UTectonicVisualizerComponent::GenerateVisualizationMesh()
{
    if (!VisualizationMesh || !Grid)
    {
        return;
    }

    MeshVertices.Empty();
    MeshTriangles.Empty();
    MeshNormals.Empty();
    MeshUVs.Empty();
    MeshColors.Empty();

    // Usar la resolución visual (ajustable para rendimiento)
    int32 MeshResolution = FMath::Clamp(VisualizationMeshResolution, 8, 128);
    float Radius = Grid->GetPlanetRadius() + VisualizationHeightOffset;

    int32 VerticesPerFace = (MeshResolution + 1) * (MeshResolution + 1);
    MeshVertices.Reserve(VerticesPerFace * 6);
    MeshNormals.Reserve(VerticesPerFace * 6);
    MeshUVs.Reserve(VerticesPerFace * 6);
    MeshColors.Reserve(VerticesPerFace * 6);
    MeshTriangles.Reserve(MeshResolution * MeshResolution * 6 * 6);

    // Generar mesh para cada cara
    int32 BaseVertex = 0;
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        
        // Generar vértices
        for (int32 V = 0; V <= MeshResolution; ++V)
        {
            for (int32 U = 0; U <= MeshResolution; ++U)
            {
                float UNorm = (float)U / MeshResolution;
                float VNorm = (float)V / MeshResolution;

                // Convertir a punto en cubo [-1, 1]
                float LocalU = UNorm * 2.0f - 1.0f;
                float LocalV = VNorm * 2.0f - 1.0f;

                // Obtener ejes de la cara
                FVector CubePoint;
                switch (FaceIdx)
                {
                    case 0: CubePoint = FVector(1.0f, LocalU, LocalV); break;   // +X
                    case 1: CubePoint = FVector(-1.0f, -LocalU, LocalV); break; // -X
                    case 2: CubePoint = FVector(-LocalU, 1.0f, LocalV); break;  // +Y
                    case 3: CubePoint = FVector(LocalU, -1.0f, LocalV); break;  // -Y
                    case 4: CubePoint = FVector(-LocalU, -LocalV, 1.0f); break; // +Z
                    case 5: CubePoint = FVector(-LocalU, LocalV, -1.0f); break; // -Z
                }

                // Normalizar para proyectar a esfera
                FVector Normal = CubePoint.GetSafeNormal();
                FVector Position = Normal * Radius;

                MeshVertices.Add(Position);
                MeshNormals.Add(Normal);
                MeshUVs.Add(FVector2D(UNorm, VNorm));

                // Color inicial (se actualizará después)
                MeshColors.Add(FLinearColor::White);
            }
        }

        // Generar triángulos
        for (int32 V = 0; V < MeshResolution; ++V)
        {
            for (int32 U = 0; U < MeshResolution; ++U)
            {
                int32 I0 = BaseVertex + V * (MeshResolution + 1) + U;
                int32 I1 = I0 + 1;
                int32 I2 = I0 + MeshResolution + 1;
                int32 I3 = I2 + 1;

                // Triángulo 1
                MeshTriangles.Add(I0);
                MeshTriangles.Add(I2);
                MeshTriangles.Add(I1);

                // Triángulo 2
                MeshTriangles.Add(I1);
                MeshTriangles.Add(I2);
                MeshTriangles.Add(I3);
            }
        }

        BaseVertex += VerticesPerFace;
    }

    // Actualizar colores según placas
    UpdateVisualizationMeshColors();

    // Crear la sección de mesh
    VisualizationMesh->ClearAllMeshSections();
    VisualizationMesh->CreateMeshSection_LinearColor(
        0,
        MeshVertices,
        MeshTriangles,
        MeshNormals,
        MeshUVs,
        MeshColors,
        TArray<FProcMeshTangent>(),
        false  // Sin colisión
    );

    UE_LOG(LogTemp, Log, TEXT("TectonicVisualizer: Generated visualization mesh with %d vertices"), 
           MeshVertices.Num());
}

void UTectonicVisualizerComponent::UpdateVisualizationMeshColors()
{
    if (!VisualizationMesh || !Grid || !PlateSystem || MeshColors.Num() == 0)
    {
        return;
    }

    int32 MeshResolution = FMath::Clamp(VisualizationMeshResolution, 8, 128);
    int32 VerticesPerFace = (MeshResolution + 1) * (MeshResolution + 1);
    const TArray<FTectonicPlate>& Plates = PlateSystem->GetAllPlates();

    int32 VertexIndex = 0;
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        
        for (int32 V = 0; V <= MeshResolution; ++V)
        {
            for (int32 U = 0; U <= MeshResolution; ++U)
            {
                // Mapear UV del mesh a celda del grid
                int32 CellX = U * Grid->GetResolution() / MeshResolution;
                int32 CellY = V * Grid->GetResolution() / MeshResolution;
                CellX = FMath::Clamp(CellX, 0, Grid->GetResolution() - 1);
                CellY = FMath::Clamp(CellY, 0, Grid->GetResolution() - 1);
                
                int32 PlateID = PlateSystem->GetPlateIDAt(Face, CellX, CellY);

                FLinearColor Color = FLinearColor::Black;

                switch (VisualMode)
                {
                    case ETectonicVisualMode::PlateID:
                        Color = GetPlateColor(PlateID);
                        break;

                    case ETectonicVisualMode::CrustType:
                        if (PlateID >= 0 && PlateID < Plates.Num())
                        {
                            Color = (Plates[PlateID].CrustType == ECrustType::Oceanic) 
                                ? OceanicColor : ContinentalColor;
                        }
                        break;

                    case ETectonicVisualMode::Velocity:
                        if (PlateID >= 0 && PlateID < Plates.Num())
                        {
                            float Speed = FMath::Abs(Plates[PlateID].AngularVelocity);
                            float NormalizedSpeed = FMath::Clamp(Speed / 0.01f, 0.0f, 1.0f);
                            Color = FLinearColor::LerpUsingHSV(FLinearColor::Blue, FLinearColor::Red, NormalizedSpeed);
                        }
                        break;

                    case ETectonicVisualMode::Age:
                        if (PlateID >= 0 && PlateID < Plates.Num())
                        {
                            float NormalizedAge = FMath::Clamp(Plates[PlateID].Age / 200.0f, 0.0f, 1.0f);
                            Color = FLinearColor::LerpUsingHSV(
                                FLinearColor(0.2f, 0.4f, 0.9f),
                                FLinearColor(0.6f, 0.3f, 0.1f),
                                NormalizedAge
                            );
                        }
                        break;

                    default:
                        Color = GetPlateColor(PlateID);
                        break;
                }

                if (VertexIndex < MeshColors.Num())
                {
                    MeshColors[VertexIndex] = Color;
                }
                VertexIndex++;
            }
        }
    }

    // Actualizar la sección del mesh con nuevos colores
    VisualizationMesh->UpdateMeshSection_LinearColor(
        0,
        MeshVertices,
        MeshNormals,
        MeshUVs,
        MeshColors,
        TArray<FProcMeshTangent>()
    );
}
