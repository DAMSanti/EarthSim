// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "PlanetNaniteMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "StaticMeshDescription.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Engine/Texture2D.h"
#include "DrawDebugHelpers.h"

UPlanetNaniteMesh::UPlanetNaniteMesh()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_DuringPhysics;
}

void UPlanetNaniteMesh::BeginPlay()
{
    Super::BeginPlay();
    
    if (!bIsInitialized && PlanetRadius > 0)
    {
        InitializePlanet(PlanetRadius, BaseSubdivisions);
    }
}

void UPlanetNaniteMesh::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Limpiar patches
    for (auto& Pair : ActivePatches)
    {
        if (Pair.Value.NaniteMeshComponent.IsValid())
        {
            Pair.Value.NaniteMeshComponent->DestroyComponent();
        }
    }
    ActivePatches.Empty();
    
    // Limpiar pool
    for (UStaticMeshComponent* Comp : MeshComponentPool)
    {
        if (Comp)
        {
            Comp->DestroyComponent();
        }
    }
    MeshComponentPool.Empty();
    
    // Limpiar materiales
    PatchMaterials.Empty();
    
    Super::EndPlay(EndPlayReason);
}

void UPlanetNaniteMesh::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    
    // Sincronizar con Quadtree si hay controlador de LOD
    if (LODController)
    {
        SyncWithQuadTree();
    }
    
    // Actualizar estadísticas
    UpdateStatistics();
    
    // Debug visualization
    if (bShowPatchBounds)
    {
        for (const auto& Pair : ActivePatches)
        {
            FColor Color = Pair.Value.bIsNaniteActive ? FColor::Green : FColor::Yellow;
            DrawDebugBox(GetWorld(), Pair.Value.WorldBounds.GetCenter(), 
                        Pair.Value.WorldBounds.GetExtent(), Color, false, -1.0f, 0, 100.0f);
        }
    }
}

void UPlanetNaniteMesh::InitializePlanet(double InPlanetRadius, int32 InBaseSubdivisions)
{
    PlanetRadius = InPlanetRadius;
    BaseSubdivisions = InBaseSubdivisions;
    
    // Limpiar estado anterior
    ActivePatches.Empty();
    
    // Crear mesh base si usamos displacement
    if (NaniteConfig.RenderMode == EPlanetRenderMode::NaniteWithDisplacement ||
        NaniteConfig.RenderMode == EPlanetRenderMode::Hybrid)
    {
        if (!BasePlanetMesh)
        {
            BasePlanetMesh = GenerateBaseMesh(BaseSubdivisions);
        }
        
        if (BasePlanetMesh && !BaseMeshComponent)
        {
            BaseMeshComponent = NewObject<UStaticMeshComponent>(GetOwner());
            BaseMeshComponent->SetupAttachment(this);
            BaseMeshComponent->SetStaticMesh(BasePlanetMesh);
            BaseMeshComponent->RegisterComponent();
            
            // Configurar material si existe
            if (NaniteConfig.PlanetMaterial.IsValid())
            {
                UMaterialInterface* Mat = NaniteConfig.PlanetMaterial.LoadSynchronous();
                if (Mat)
                {
                    BaseMeshComponent->SetMaterial(0, Mat);
                }
            }
        }
    }
    
    bIsInitialized = true;
    
    UE_LOG(LogTemp, Log, TEXT("PlanetNaniteMesh initialized. Radius: %.0f, Mode: %d"), 
           PlanetRadius, static_cast<int32>(NaniteConfig.RenderMode));
}

void UPlanetNaniteMesh::SetLODController(UCubeLODController* InLODController)
{
    LODController = InLODController;
    
    if (LODController)
    {
        // Suscribirse a cambios de LOD
        LODController->OnLODChanged.AddDynamic(this, &UPlanetNaniteMesh::OnLODChanged);
    }
}

UStaticMesh* UPlanetNaniteMesh::GenerateBaseMesh(int32 Subdivisions)
{
    // Crear un mesh esférico usando cubo esférico normalizado
    // Este será el mesh base que Nanite optimizará
    
    UStaticMesh* NewMesh = NewObject<UStaticMesh>(GetTransientPackage());
    
#if WITH_EDITOR
    // Solo disponible en editor - en runtime usar mesh pre-generado
    FMeshDescription MeshDesc;
    FStaticMeshAttributes Attributes(MeshDesc);
    Attributes.Register();
    
    // Obtener atributos
    TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
    
    // Reservar espacio para polygon groups
    MeshDesc.ReserveNewPolygonGroups(6);
    
    // Resolución del grid por cara
    int32 Resolution = 1 << Subdivisions;  // 2^Subdivisions
    int32 VerticesPerFace = (Resolution + 1) * (Resolution + 1);
    int32 TrianglesPerFace = Resolution * Resolution * 2;
    
    // Reservar memoria
    MeshDesc.ReserveNewVertices(VerticesPerFace * 6);
    MeshDesc.ReserveNewVertexInstances(TrianglesPerFace * 3 * 6);
    MeshDesc.ReserveNewPolygons(TrianglesPerFace * 6);
    MeshDesc.ReserveNewEdges(TrianglesPerFace * 3 * 6);
    
    // Para cada cara del cubo
    for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
    {
        FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();
        
        // Generar vértices para esta cara
        TArray<FVertexID> FaceVertices;
        FaceVertices.SetNum(VerticesPerFace);
        
        for (int32 Y = 0; Y <= Resolution; ++Y)
        {
            for (int32 X = 0; X <= Resolution; ++X)
            {
                // Coordenadas en cara [-1, 1]
                float FaceX = (float(X) / Resolution) * 2.0f - 1.0f;
                float FaceY = (float(Y) / Resolution) * 2.0f - 1.0f;
                
                // Convertir a punto en cubo
                FVector CubePoint;
                switch (FaceIndex)
                {
                case 0: CubePoint = FVector(1.0f, FaceX, FaceY); break;  // +X
                case 1: CubePoint = FVector(-1.0f, -FaceX, FaceY); break;  // -X
                case 2: CubePoint = FVector(-FaceX, 1.0f, FaceY); break;  // +Y
                case 3: CubePoint = FVector(FaceX, -1.0f, FaceY); break;  // -Y
                case 4: CubePoint = FVector(-FaceX, -FaceY, 1.0f); break;  // +Z
                case 5: CubePoint = FVector(-FaceX, FaceY, -1.0f); break;  // -Z
                }
                
                // Normalizar para proyectar a esfera
                FVector SpherePoint = CubePoint.GetSafeNormal() * PlanetRadius;
                
                FVertexID VertexID = MeshDesc.CreateVertex();
                FaceVertices[Y * (Resolution + 1) + X] = VertexID;
                VertexPositions[VertexID] = FVector3f(SpherePoint);
            }
        }
        
        // Crear triángulos
        for (int32 Y = 0; Y < Resolution; ++Y)
        {
            for (int32 X = 0; X < Resolution; ++X)
            {
                int32 I0 = Y * (Resolution + 1) + X;
                int32 I1 = Y * (Resolution + 1) + X + 1;
                int32 I2 = (Y + 1) * (Resolution + 1) + X;
                int32 I3 = (Y + 1) * (Resolution + 1) + X + 1;
                
                // Coordenadas UV
                FVector2f UV0(float(X) / Resolution, float(Y) / Resolution);
                FVector2f UV1(float(X + 1) / Resolution, float(Y) / Resolution);
                FVector2f UV2(float(X) / Resolution, float(Y + 1) / Resolution);
                FVector2f UV3(float(X + 1) / Resolution, float(Y + 1) / Resolution);
                
                // Triángulo 1
                {
                    TArray<FVertexInstanceID> Instances;
                    Instances.SetNum(3);
                    
                    Instances[0] = MeshDesc.CreateVertexInstance(FaceVertices[I0]);
                    Instances[1] = MeshDesc.CreateVertexInstance(FaceVertices[I2]);
                    Instances[2] = MeshDesc.CreateVertexInstance(FaceVertices[I1]);
                    
                    // Calcular normal del triángulo
                    FVector3f Normal = FVector3f(VertexPositions[FaceVertices[I0]]).GetSafeNormal();
                    VertexInstanceNormals[Instances[0]] = Normal;
                    VertexInstanceNormals[Instances[1]] = FVector3f(VertexPositions[FaceVertices[I2]]).GetSafeNormal();
                    VertexInstanceNormals[Instances[2]] = FVector3f(VertexPositions[FaceVertices[I1]]).GetSafeNormal();
                    
                    VertexInstanceUVs.Set(Instances[0], 0, UV0);
                    VertexInstanceUVs.Set(Instances[1], 0, UV2);
                    VertexInstanceUVs.Set(Instances[2], 0, UV1);
                    
                    MeshDesc.CreatePolygon(PolyGroup, Instances);
                }
                
                // Triángulo 2
                {
                    TArray<FVertexInstanceID> Instances;
                    Instances.SetNum(3);
                    
                    Instances[0] = MeshDesc.CreateVertexInstance(FaceVertices[I1]);
                    Instances[1] = MeshDesc.CreateVertexInstance(FaceVertices[I2]);
                    Instances[2] = MeshDesc.CreateVertexInstance(FaceVertices[I3]);
                    
                    VertexInstanceNormals[Instances[0]] = FVector3f(VertexPositions[FaceVertices[I1]]).GetSafeNormal();
                    VertexInstanceNormals[Instances[1]] = FVector3f(VertexPositions[FaceVertices[I2]]).GetSafeNormal();
                    VertexInstanceNormals[Instances[2]] = FVector3f(VertexPositions[FaceVertices[I3]]).GetSafeNormal();
                    
                    VertexInstanceUVs.Set(Instances[0], 0, UV1);
                    VertexInstanceUVs.Set(Instances[1], 0, UV2);
                    VertexInstanceUVs.Set(Instances[2], 0, UV3);
                    
                    MeshDesc.CreatePolygon(PolyGroup, Instances);
                }
            }
        }
    }
    
    // Construir el mesh
    TArray<const FMeshDescription*> MeshDescriptions;
    MeshDescriptions.Add(&MeshDesc);
    NewMesh->BuildFromMeshDescriptions(MeshDescriptions);
    
    // Configurar Nanite
    SetupNaniteSettings(NewMesh);
#endif
    
    return NewMesh;
}

void UPlanetNaniteMesh::SetupNaniteSettings(UStaticMesh* Mesh)
{
    if (!Mesh)
    {
        return;
    }
    
#if WITH_EDITOR
    // Habilitar Nanite
    Mesh->NaniteSettings.bEnabled = true;
    
    // Configurar fallback
    Mesh->NaniteSettings.FallbackPercentTriangles = 1.0f;  // 100% para fallback
    Mesh->NaniteSettings.FallbackRelativeError = 0.0f;
    
    // Configuración de posición (importante para planetas grandes)
    Mesh->NaniteSettings.bExplicitTangents = false;
    
    // Shadow settings
    if (NaniteConfig.bCastNaniteShadows)
    {
        // Nanite shadows se configuran por componente, no por mesh
    }
    
    UE_LOG(LogTemp, Log, TEXT("Nanite settings configured for mesh"));
#endif
}

void UPlanetNaniteMesh::UpdatePatch(const FQuadTreeNodeId& NodeId)
{
    FNaniteTerrainPatch& Patch = ActivePatches.FindOrAdd(NodeId);
    Patch.NodeId = NodeId;
    Patch.WorldBounds = CalculatePatchBounds(NodeId);
    
    // Obtener o crear componente de mesh
    UStaticMeshComponent* MeshComp = GetOrCreateMeshComponent(NodeId);
    if (!MeshComp)
    {
        return;
    }
    
    Patch.NaniteMeshComponent = MeshComp;
    
    // Generar mesh para este patch si no existe
    if (!MeshComp->GetStaticMesh())
    {
        // La resolución del mesh depende del nivel del nodo
        int32 PatchResolution = FMath::Clamp(32 >> NodeId.Level, 4, 32);
        UStaticMesh* PatchMesh = GeneratePatchMesh(NodeId, PatchResolution);
        
        if (PatchMesh)
        {
            MeshComp->SetStaticMesh(PatchMesh);
            Patch.bIsNaniteActive = true;
        }
    }
    
    // Configurar material
    UMaterialInstanceDynamic* PatchMat = GetPatchMaterial(NodeId);
    if (PatchMat)
    {
        MeshComp->SetMaterial(0, PatchMat);
    }
    
    MeshComp->SetVisibility(true);
}

void UPlanetNaniteMesh::RemovePatch(const FQuadTreeNodeId& NodeId)
{
    FNaniteTerrainPatch* Patch = ActivePatches.Find(NodeId);
    if (!Patch)
    {
        return;
    }
    
    if (Patch->NaniteMeshComponent.IsValid())
    {
        ReturnMeshComponentToPool(Patch->NaniteMeshComponent.Get());
    }
    
    ActivePatches.Remove(NodeId);
    PatchMaterials.Remove(NodeId);
}

bool UPlanetNaniteMesh::GetPatchData(const FQuadTreeNodeId& NodeId, FNaniteTerrainPatch& OutPatch) const
{
    const FNaniteTerrainPatch* Patch = ActivePatches.Find(NodeId);
    if (Patch)
    {
        OutPatch = *Patch;
        return true;
    }
    return false;
}

void UPlanetNaniteMesh::SyncWithQuadTree()
{
    if (!LODController)
    {
        return;
    }
    
    const FCubeSphereQuadTree& QuadTree = LODController->GetQuadTree();
    TArray<FQuadTreeNodeId> CurrentLeaves = QuadTree.GetAllLeaves();
    
    // Convertir a set para búsqueda rápida
    TSet<FQuadTreeNodeId> LeafSet(CurrentLeaves);
    
    // Encontrar patches que ya no son necesarios
    TArray<FQuadTreeNodeId> PatchesToRemove;
    for (const auto& Pair : ActivePatches)
    {
        if (!LeafSet.Contains(Pair.Key))
        {
            PatchesToRemove.Add(Pair.Key);
        }
    }
    
    // Remover patches obsoletos
    for (const FQuadTreeNodeId& NodeId : PatchesToRemove)
    {
        RemovePatch(NodeId);
    }
    
    // Agregar/actualizar patches necesarios
    for (const FQuadTreeNodeId& NodeId : CurrentLeaves)
    {
        if (!ActivePatches.Contains(NodeId))
        {
            UpdatePatch(NodeId);
        }
    }
}

void UPlanetNaniteMesh::SetChunkHeightmap(const FQuadTreeNodeId& NodeId, UTexture2D* Heightmap, FVector2D MinMaxHeight)
{
    FNaniteTerrainPatch* Patch = ActivePatches.Find(NodeId);
    if (!Patch)
    {
        return;
    }
    
    Patch->HeightmapTexture = Heightmap;
    Patch->HeightmapMinMax = MinMaxHeight;
    
    // Actualizar material con heightmap
    UMaterialInstanceDynamic* Mat = GetPatchMaterial(NodeId);
    if (Mat && Heightmap)
    {
        Mat->SetTextureParameterValue(TEXT("Heightmap"), Heightmap);
        Mat->SetVectorParameterValue(TEXT("HeightmapMinMax"), 
            FLinearColor(MinMaxHeight.X, MinMaxHeight.Y, 0, 0));
    }
}

UTexture2D* UPlanetNaniteMesh::GenerateProceduralHeightmap(const FQuadTreeNodeId& NodeId, int32 Resolution)
{
    // Crear textura procedural de altura
    // En producción, esto cargaría datos reales de elevación
    
    UTexture2D* Heightmap = UTexture2D::CreateTransient(Resolution, Resolution, PF_R16F);
    if (!Heightmap)
    {
        return nullptr;
    }
    
    // Configurar para Virtual Texturing si está habilitado
    if (NaniteConfig.bUseVirtualTexturing)
    {
        Heightmap->VirtualTextureStreaming = true;
    }
    
    // Generar datos de altura procedurales (simplex noise, etc.)
    // Por ahora, datos de placeholder
    FTexture2DMipMap& Mip = Heightmap->GetPlatformData()->Mips[0];
    Mip.BulkData.Lock(LOCK_READ_WRITE);
    
    void* RawData = Mip.BulkData.Realloc(Resolution * Resolution * sizeof(uint16));
    uint16* Data = reinterpret_cast<uint16*>(RawData);
    
    for (int32 Y = 0; Y < Resolution; ++Y)
    {
        for (int32 X = 0; X < Resolution; ++X)
        {
            // Simple placeholder: variación suave
            float Value = FMath::Sin(X * 0.1f) * FMath::Cos(Y * 0.1f) * 0.5f + 0.5f;
            Data[Y * Resolution + X] = static_cast<uint16>(Value * 65535.0f);
        }
    }
    
    Mip.BulkData.Unlock();
    Heightmap->UpdateResource();
    
    return Heightmap;
}

UMaterialInstanceDynamic* UPlanetNaniteMesh::GetPatchMaterial(const FQuadTreeNodeId& NodeId)
{
    // Buscar material existente
    if (UMaterialInstanceDynamic** Existing = PatchMaterials.Find(NodeId))
    {
        return *Existing;
    }
    
    // Crear nuevo material dinámico
    UMaterialInterface* BaseMat = NaniteConfig.PlanetMaterial.IsValid() 
        ? NaniteConfig.PlanetMaterial.LoadSynchronous() 
        : nullptr;
    
    if (!BaseMat)
    {
        // Usar material por defecto si no hay uno configurado
        return nullptr;
    }
    
    UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BaseMat, this);
    if (DynMat)
    {
        // Configurar parámetros por defecto
        DynMat->SetScalarParameterValue(TEXT("PlanetRadius"), PlanetRadius);
        DynMat->SetScalarParameterValue(TEXT("MaxDisplacement"), NaniteConfig.MaxDisplacementScale);
        
        // Parámetros de chunk
        DynMat->SetScalarParameterValue(TEXT("ChunkLevel"), static_cast<float>(NodeId.Level));
        
        PatchMaterials.Add(NodeId, DynMat);
    }
    
    return DynMat;
}

void UPlanetNaniteMesh::UpdateGlobalMaterialParameters()
{
    for (auto& Pair : PatchMaterials)
    {
        if (Pair.Value)
        {
            Pair.Value->SetScalarParameterValue(TEXT("PlanetRadius"), PlanetRadius);
            Pair.Value->SetScalarParameterValue(TEXT("MaxDisplacement"), NaniteConfig.MaxDisplacementScale);
        }
    }
}

void UPlanetNaniteMesh::UpdateStatistics()
{
    Statistics.ActiveNanitePatches = 0;
    Statistics.NaniteTriangles = 0;
    Statistics.FallbackPatches = 0;
    Statistics.CulledPatches = 0;
    Statistics.GPUMemoryBytes = 0;
    
    for (const auto& Pair : ActivePatches)
    {
        const FNaniteTerrainPatch& Patch = Pair.Value;
        
        if (!Patch.NaniteMeshComponent.IsValid())
        {
            continue;
        }
        
        if (Patch.bIsNaniteActive)
        {
            Statistics.ActiveNanitePatches++;
            
            // Estimar triángulos (depende del mesh)
            if (UStaticMesh* Mesh = Patch.NaniteMeshComponent->GetStaticMesh())
            {
                if (Mesh->GetRenderData())
                {
                    for (const FStaticMeshLODResources& LOD : Mesh->GetRenderData()->LODResources)
                    {
                        Statistics.NaniteTriangles += LOD.GetNumTriangles();
                    }
                }
            }
        }
        else
        {
            Statistics.FallbackPatches++;
        }
        
        if (!Patch.NaniteMeshComponent->IsVisible())
        {
            Statistics.CulledPatches++;
        }
    }
}

FString UPlanetNaniteMesh::GetDebugString() const
{
    return FString::Printf(
        TEXT("PlanetNaniteMesh:\n")
        TEXT("  Mode: %d\n")
        TEXT("  Active Patches: %d\n")
        TEXT("  Nanite Triangles: %lld\n")
        TEXT("  Fallback: %d, Culled: %d"),
        static_cast<int32>(NaniteConfig.RenderMode),
        Statistics.ActiveNanitePatches,
        Statistics.NaniteTriangles,
        Statistics.FallbackPatches,
        Statistics.CulledPatches
    );
}

UStaticMeshComponent* UPlanetNaniteMesh::GetOrCreateMeshComponent(const FQuadTreeNodeId& NodeId)
{
    // Intentar reutilizar del pool
    if (MeshComponentPool.Num() > 0)
    {
        UStaticMeshComponent* PooledComp = MeshComponentPool.Pop();
        if (PooledComp)
        {
            PooledComp->SetVisibility(true);
            return PooledComp;
        }
    }
    
    // Crear nuevo componente
    UStaticMeshComponent* NewComp = NewObject<UStaticMeshComponent>(GetOwner());
    NewComp->SetupAttachment(this);
    NewComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);  // Colisión manejada por separado
    NewComp->bCastDynamicShadow = NaniteConfig.bCastNaniteShadows;
    NewComp->RegisterComponent();
    
    return NewComp;
}

void UPlanetNaniteMesh::ReturnMeshComponentToPool(UStaticMeshComponent* Component)
{
    if (!Component)
    {
        return;
    }
    
    Component->SetStaticMesh(nullptr);
    Component->SetVisibility(false);
    
    // Limitar tamaño del pool
    const int32 MaxPoolSize = 64;
    if (MeshComponentPool.Num() < MaxPoolSize)
    {
        MeshComponentPool.Add(Component);
    }
    else
    {
        Component->DestroyComponent();
    }
}

UStaticMesh* UPlanetNaniteMesh::GeneratePatchMesh(const FQuadTreeNodeId& NodeId, int32 Resolution)
{
    // Similar a GenerateBaseMesh pero solo para una sección de una cara
    UStaticMesh* PatchMesh = NewObject<UStaticMesh>(GetTransientPackage());
    
#if WITH_EDITOR
    if (!LODController)
    {
        return nullptr;
    }
    
    const FCubeFaceQuadTree& FaceTree = LODController->GetQuadTree().GetFaceQuadTree(NodeId.Face);
    FQuadTreeBounds NodeBounds = FaceTree.GetNodeBounds(NodeId);
    
    FMeshDescription MeshDesc;
    FStaticMeshAttributes Attributes(MeshDesc);
    Attributes.Register();
    
    TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
    TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = Attributes.GetVertexInstanceNormals();
    TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = Attributes.GetVertexInstanceUVs();
    
    MeshDesc.ReserveNewPolygonGroups(1);
    MeshDesc.ReserveNewVertices((Resolution + 1) * (Resolution + 1));
    MeshDesc.ReserveNewVertexInstances(Resolution * Resolution * 2 * 3);
    MeshDesc.ReserveNewPolygons(Resolution * Resolution * 2);
    
    FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();
    
    // Generar grid de vértices
    TArray<FVertexID> Vertices;
    Vertices.SetNum((Resolution + 1) * (Resolution + 1));
    
    for (int32 Y = 0; Y <= Resolution; ++Y)
    {
        for (int32 X = 0; X <= Resolution; ++X)
        {
            // Interpolar dentro de los bounds del patch
            float U = float(X) / Resolution;
            float V = float(Y) / Resolution;
            
            float FaceX = FMath::Lerp(NodeBounds.Min.X, NodeBounds.Max.X, U);
            float FaceY = FMath::Lerp(NodeBounds.Min.Y, NodeBounds.Max.Y, V);
            
            // Convertir a punto en esfera
            FVector CubePoint;
            switch (NodeId.Face)
            {
            case ECSCubeFace::PositiveX: CubePoint = FVector(1.0f, FaceX, FaceY); break;
            case ECSCubeFace::NegativeX: CubePoint = FVector(-1.0f, -FaceX, FaceY); break;
            case ECSCubeFace::PositiveY: CubePoint = FVector(-FaceX, 1.0f, FaceY); break;
            case ECSCubeFace::NegativeY: CubePoint = FVector(FaceX, -1.0f, FaceY); break;
            case ECSCubeFace::PositiveZ: CubePoint = FVector(-FaceX, -FaceY, 1.0f); break;
            case ECSCubeFace::NegativeZ: CubePoint = FVector(-FaceX, FaceY, -1.0f); break;
            }
            
            FVector SpherePoint = CubePoint.GetSafeNormal() * PlanetRadius;
            
            FVertexID VertexID = MeshDesc.CreateVertex();
            Vertices[Y * (Resolution + 1) + X] = VertexID;
            VertexPositions[VertexID] = FVector3f(SpherePoint);
        }
    }
    
    // Crear triángulos
    for (int32 Y = 0; Y < Resolution; ++Y)
    {
        for (int32 X = 0; X < Resolution; ++X)
        {
            int32 I0 = Y * (Resolution + 1) + X;
            int32 I1 = Y * (Resolution + 1) + X + 1;
            int32 I2 = (Y + 1) * (Resolution + 1) + X;
            int32 I3 = (Y + 1) * (Resolution + 1) + X + 1;
            
            FVector2f UV0(float(X) / Resolution, float(Y) / Resolution);
            FVector2f UV1(float(X + 1) / Resolution, float(Y) / Resolution);
            FVector2f UV2(float(X) / Resolution, float(Y + 1) / Resolution);
            FVector2f UV3(float(X + 1) / Resolution, float(Y + 1) / Resolution);
            
            // Triángulo 1
            {
                TArray<FVertexInstanceID> Instances;
                Instances.SetNum(3);
                
                Instances[0] = MeshDesc.CreateVertexInstance(Vertices[I0]);
                Instances[1] = MeshDesc.CreateVertexInstance(Vertices[I2]);
                Instances[2] = MeshDesc.CreateVertexInstance(Vertices[I1]);
                
                VertexInstanceNormals[Instances[0]] = FVector3f(VertexPositions[Vertices[I0]]).GetSafeNormal();
                VertexInstanceNormals[Instances[1]] = FVector3f(VertexPositions[Vertices[I2]]).GetSafeNormal();
                VertexInstanceNormals[Instances[2]] = FVector3f(VertexPositions[Vertices[I1]]).GetSafeNormal();
                
                VertexInstanceUVs.Set(Instances[0], 0, UV0);
                VertexInstanceUVs.Set(Instances[1], 0, UV2);
                VertexInstanceUVs.Set(Instances[2], 0, UV1);
                
                MeshDesc.CreatePolygon(PolyGroup, Instances);
            }
            
            // Triángulo 2
            {
                TArray<FVertexInstanceID> Instances;
                Instances.SetNum(3);
                
                Instances[0] = MeshDesc.CreateVertexInstance(Vertices[I1]);
                Instances[1] = MeshDesc.CreateVertexInstance(Vertices[I2]);
                Instances[2] = MeshDesc.CreateVertexInstance(Vertices[I3]);
                
                VertexInstanceNormals[Instances[0]] = FVector3f(VertexPositions[Vertices[I1]]).GetSafeNormal();
                VertexInstanceNormals[Instances[1]] = FVector3f(VertexPositions[Vertices[I2]]).GetSafeNormal();
                VertexInstanceNormals[Instances[2]] = FVector3f(VertexPositions[Vertices[I3]]).GetSafeNormal();
                
                VertexInstanceUVs.Set(Instances[0], 0, UV1);
                VertexInstanceUVs.Set(Instances[1], 0, UV2);
                VertexInstanceUVs.Set(Instances[2], 0, UV3);
                
                MeshDesc.CreatePolygon(PolyGroup, Instances);
            }
        }
    }
    
    TArray<const FMeshDescription*> MeshDescriptions;
    MeshDescriptions.Add(&MeshDesc);
    PatchMesh->BuildFromMeshDescriptions(MeshDescriptions);
    
    SetupNaniteSettings(PatchMesh);
#endif
    
    return PatchMesh;
}

FBox UPlanetNaniteMesh::CalculatePatchBounds(const FQuadTreeNodeId& NodeId) const
{
    if (!LODController)
    {
        return FBox(ForceInit);
    }
    
    const FCubeFaceQuadTree& FaceTree = LODController->GetQuadTree().GetFaceQuadTree(NodeId.Face);
    FQuadTreeBounds Bounds2D = FaceTree.GetNodeBounds(NodeId);
    
    // Calcular esquinas 3D
    TArray<FVector> Corners;
    Corners.Reserve(4);
    
    TArray<FVector2D> CornerCoords = {
        Bounds2D.Min,
        FVector2D(Bounds2D.Max.X, Bounds2D.Min.Y),
        Bounds2D.Max,
        FVector2D(Bounds2D.Min.X, Bounds2D.Max.Y)
    };
    
    for (const FVector2D& Coord : CornerCoords)
    {
        FVector CubePoint;
        switch (NodeId.Face)
        {
        case ECSCubeFace::PositiveX: CubePoint = FVector(1.0f, Coord.X, Coord.Y); break;
        case ECSCubeFace::NegativeX: CubePoint = FVector(-1.0f, -Coord.X, Coord.Y); break;
        case ECSCubeFace::PositiveY: CubePoint = FVector(-Coord.X, 1.0f, Coord.Y); break;
        case ECSCubeFace::NegativeY: CubePoint = FVector(Coord.X, -1.0f, Coord.Y); break;
        case ECSCubeFace::PositiveZ: CubePoint = FVector(-Coord.X, -Coord.Y, 1.0f); break;
        case ECSCubeFace::NegativeZ: CubePoint = FVector(-Coord.X, Coord.Y, -1.0f); break;
        }
        
        Corners.Add(CubePoint.GetSafeNormal() * PlanetRadius);
    }
    
    // También añadir el centro
    FVector2D Center2D = (Bounds2D.Min + Bounds2D.Max) * 0.5;
    FVector CenterCube;
    switch (NodeId.Face)
    {
    case ECSCubeFace::PositiveX: CenterCube = FVector(1.0f, Center2D.X, Center2D.Y); break;
    case ECSCubeFace::NegativeX: CenterCube = FVector(-1.0f, -Center2D.X, Center2D.Y); break;
    case ECSCubeFace::PositiveY: CenterCube = FVector(-Center2D.X, 1.0f, Center2D.Y); break;
    case ECSCubeFace::NegativeY: CenterCube = FVector(Center2D.X, -1.0f, Center2D.Y); break;
    case ECSCubeFace::PositiveZ: CenterCube = FVector(-Center2D.X, -Center2D.Y, 1.0f); break;
    case ECSCubeFace::NegativeZ: CenterCube = FVector(-Center2D.X, Center2D.Y, -1.0f); break;
    }
    Corners.Add(CenterCube.GetSafeNormal() * PlanetRadius);
    
    // Construir bounding box
    FBox Box(ForceInit);
    for (const FVector& Corner : Corners)
    {
        Box += Corner;
    }
    
    // Expandir para incluir displacement máximo
    Box = Box.ExpandBy(NaniteConfig.MaxDisplacementScale);
    
    return Box;
}

void UPlanetNaniteMesh::OnLODChanged(const FQuadTreeNodeId& NodeId, bool bWasSplit)
{
    if (bWasSplit)
    {
        // El nodo se subdividió - remover patch del padre si existe
        RemovePatch(NodeId);
        
        // Los nuevos hijos se crearán en el siguiente SyncWithQuadTree
    }
    else
    {
        // El nodo se colapsó - remover patches de hijos
        for (uint8 i = 0; i < 4; ++i)
        {
            RemovePatch(NodeId.GetChild(i));
        }
    }
}
