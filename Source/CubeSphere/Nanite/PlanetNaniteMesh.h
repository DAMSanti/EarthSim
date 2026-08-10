// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "NaniteTypes.h"
#include "QuadTree/CubeSphereQuadTree.h"
#include "LOD/CubeLODController.h"
#include "PlanetNaniteMesh.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * UPlanetNaniteMesh
 * 
 * Componente que gestiona la malla Nanite del planeta.
 * Crea y actualiza patches de terreno usando Static Meshes con Nanite habilitado.
 * 
 * Estrategia de integración con Nanite:
 * 
 * 1. MALLA BASE CON DISPLACEMENT:
 *    - Se crea una esfera base subdividida (icosaedro o cubo esférico de baja resolución)
 *    - El detalle se añade via World Position Offset en el material
 *    - Heightmaps se pasan como texturas por chunk
 *    - Nanite maneja el LOD de la geometría base
 * 
 * 2. PATCHES DINÁMICOS:
 *    - Cada chunk del Quadtree puede tener su propio Static Mesh
 *    - Los meshes se generan proceduralmente cuando se necesitan
 *    - Nanite comprime y optimiza cada mesh automáticamente
 * 
 * 3. VIRTUAL TEXTURING:
 *    - Heightmaps y texturas de detalle usan Virtual Textures
 *    - Permite terreno de ultra alta resolución sin límites de memoria
 *    - Se integra con el sistema de streaming de chunks
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CUBESPHERE_API UPlanetNaniteMesh : public USceneComponent
{
    GENERATED_BODY()

public:
    UPlanetNaniteMesh();

    // UActorComponent interface
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // --- Inicialización ---

    UFUNCTION(BlueprintCallable, Category = "Planet|Nanite")
    void InitializePlanet(double InPlanetRadius, int32 BaseSubdivisions = 6);

    UFUNCTION(BlueprintCallable, Category = "Planet|Nanite")
    void SetLODController(UCubeLODController* InLODController);

    // --- Configuración ---

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Config")
    FPlanetNaniteConfig NaniteConfig;

    // Radio del planeta
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Config", meta = (ClampMin = "1000.0"))
    double PlanetRadius = 6371000.0;

    // Subdivisiones base de la esfera
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Config", meta = (ClampMin = "1", ClampMax = "10"))
    int32 BaseSubdivisions = 6;

    // --- Mesh Base ---

    // Static Mesh base del planeta (esfera con Nanite)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Mesh")
    UStaticMesh* BasePlanetMesh;

    // Generar mesh base proceduralmente
    UFUNCTION(BlueprintCallable, Category = "Planet|Mesh")
    UStaticMesh* GenerateBaseMesh(int32 Subdivisions);

    // --- Patches de Terreno ---

    // Crear/actualizar patch para un nodo del Quadtree
    UFUNCTION(BlueprintCallable, Category = "Planet|Patches")
    void UpdatePatch(const FQuadTreeNodeId& NodeId);

    // Remover patch
    UFUNCTION(BlueprintCallable, Category = "Planet|Patches")
    void RemovePatch(const FQuadTreeNodeId& NodeId);

    // Obtener datos de patch
    UFUNCTION(BlueprintCallable, Category = "Planet|Patches")
    bool GetPatchData(const FQuadTreeNodeId& NodeId, FNaniteTerrainPatch& OutPatch) const;

    // Actualizar todos los patches basándose en el Quadtree
    UFUNCTION(BlueprintCallable, Category = "Planet|Patches")
    void SyncWithQuadTree();

    // --- Heightmaps y Displacement ---

    // Establecer heightmap para un chunk
    UFUNCTION(BlueprintCallable, Category = "Planet|Heightmap")
    void SetChunkHeightmap(const FQuadTreeNodeId& NodeId, UTexture2D* Heightmap, FVector2D MinMaxHeight);

    // Generar heightmap procedural para un chunk
    UFUNCTION(BlueprintCallable, Category = "Planet|Heightmap")
    UTexture2D* GenerateProceduralHeightmap(const FQuadTreeNodeId& NodeId, int32 Resolution = 1024);

    // --- Material ---

    // Obtener instancia de material para un patch
    UFUNCTION(BlueprintCallable, Category = "Planet|Material")
    UMaterialInstanceDynamic* GetPatchMaterial(const FQuadTreeNodeId& NodeId);

    // Actualizar parámetros de material global
    UFUNCTION(BlueprintCallable, Category = "Planet|Material")
    void UpdateGlobalMaterialParameters();

    // --- Estadísticas ---

    UPROPERTY(BlueprintReadOnly, Category = "Planet|Stats")
    FPlanetNaniteStats Statistics;

    UFUNCTION(BlueprintCallable, Category = "Planet|Stats")
    void UpdateStatistics();

    // --- Debug ---

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Debug")
    bool bShowPatchBounds = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Debug")
    bool bShowNaniteOverdraw = false;

    UFUNCTION(BlueprintCallable, Category = "Planet|Debug")
    FString GetDebugString() const;

protected:
    // Referencia al controlador de LOD
    UPROPERTY()
    UCubeLODController* LODController;

    // Patches activos
    UPROPERTY()
    TMap<FQuadTreeNodeId, FNaniteTerrainPatch> ActivePatches;

    // Pool de componentes de mesh para reutilización
    UPROPERTY()
    TArray<UStaticMeshComponent*> MeshComponentPool;

    // Materiales dinámicos por patch
    UPROPERTY()
    TMap<FQuadTreeNodeId, UMaterialInstanceDynamic*> PatchMaterials;

    // Componente de mesh base
    UPROPERTY()
    UStaticMeshComponent* BaseMeshComponent;

    // Helpers
    UStaticMeshComponent* GetOrCreateMeshComponent(const FQuadTreeNodeId& NodeId);
    void ReturnMeshComponentToPool(UStaticMeshComponent* Component);
    UStaticMesh* GeneratePatchMesh(const FQuadTreeNodeId& NodeId, int32 Resolution);
    void SetupNaniteSettings(UStaticMesh* Mesh);
    FBox CalculatePatchBounds(const FQuadTreeNodeId& NodeId) const;

private:
    bool bIsInitialized = false;

    // Callback cuando cambia LOD
    UFUNCTION()
    void OnLODChanged(const FQuadTreeNodeId& NodeId, bool bWasSplit);
};
