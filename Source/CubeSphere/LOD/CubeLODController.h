// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "QuadTree/CubeSphereQuadTree.h"
#include "CubeSphere/CubeSphereGrid.h"
#include "CubeLODController.generated.h"

/**
 * Modos de selección de LOD
 */
UENUM(BlueprintType)
enum class ELODSelectionMode : uint8
{
    // LOD basado en distancia euclidiana simple
    DistanceBased,
    
    // LOD basado en error de pantalla (screen-space error)
    ScreenSpaceError,
    
    // LOD basado en curvatura del horizonte (para planetas)
    HorizonCurvature,
    
    // Híbrido: combina distancia y error de pantalla
    Hybrid
};

/**
 * Configuración avanzada de LOD
 */
USTRUCT(BlueprintType)
struct FAdvancedLODConfig
{
    GENERATED_BODY()

    // Modo de selección de LOD
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
    ELODSelectionMode SelectionMode = ELODSelectionMode::Hybrid;

    // FOV de la cámara (para cálculo de screen-space error)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "10.0", ClampMax = "170.0"))
    float FieldOfView = 90.0f;

    // Resolución de pantalla para cálculos de error
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
    FIntPoint ScreenResolution = FIntPoint(1920, 1080);

    // Error máximo permitido en píxeles
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "0.5", ClampMax = "16.0"))
    float MaxPixelError = 2.0f;

    // Factor de histéresis para evitar flickering (%)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD", meta = (ClampMin = "0.0", ClampMax = "0.5"))
    float HysteresisPercent = 0.15f;

    // Distancia mínima para forzar LOD máximo
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
    float MinDistanceForMaxLOD = 100.0f;

    // Altura sobre superficie para considerar "en órbita"
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
    float OrbitalAltitudeThreshold = 100000.0f;

    // Usar frustum culling
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Culling")
    bool bUseFrustumCulling = true;

    // Usar horizon culling (ocultar detrás del planeta)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Culling")
    bool bUseHorizonCulling = true;

    // Radio de seguridad para horizon culling (evitar pop-in)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Culling", meta = (ClampMin = "1.0", ClampMax = "1.5"))
    float HorizonCullingMargin = 1.05f;

    // Número máximo de splits por frame (para suavizar transiciones)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "1", ClampMax = "64"))
    int32 MaxSplitsPerFrame = 8;

    // Número máximo de collapses por frame
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (ClampMin = "1", ClampMax = "64"))
    int32 MaxCollapsesPerFrame = 16;

    // Priorizar nodos visibles
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
    bool bPrioritizeVisibleNodes = true;
};

/**
 * Estadísticas de LOD en tiempo real
 */
USTRUCT(BlueprintType)
struct FLODStatistics
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 TotalNodes = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 VisibleLeaves = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 CulledNodes = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 SplitsThisFrame = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 CollapsesThisFrame = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    int32 MaxActiveDepth = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    float AverageLeafScreenSize = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Stats")
    double UpdateTimeMs = 0.0;
};

// Delegate para notificar cambios de LOD
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLODChanged, const FQuadTreeNodeId&, NodeId, bool, bWasSplit);

/**
 * UCubeLODController
 * 
 * Componente que controla el LOD dinámico del planeta basado en la cámara.
 * Se encarga de decidir cuándo subdividir o colapsar nodos del Quadtree.
 * 
 * Características:
 * - Múltiples modos de selección de LOD
 * - Screen-space error para precisión visual
 * - Horizon culling para planetas
 * - Control de presupuesto por frame
 * - Histéresis para evitar flickering
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CUBESPHERE_API UCubeLODController : public UActorComponent
{
    GENERATED_BODY()

public:
    UCubeLODController();

    // UActorComponent interface
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // Inicializar con referencia al Quadtree
    UFUNCTION(BlueprintCallable, Category = "LOD")
    void Initialize(double PlanetRadius);

    // Forzar actualización de LOD
    UFUNCTION(BlueprintCallable, Category = "LOD")
    void ForceUpdate();

    // --- Configuración ---

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD|Config")
    FAdvancedLODConfig LODConfig;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD|Config")
    FQuadTreeLODConfig DistanceConfig;

    // --- Estado ---

    UPROPERTY(BlueprintReadOnly, Category = "LOD|Stats")
    FLODStatistics Statistics;

    // Cámara a usar (si es null, usa la cámara del jugador)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LOD")
    AActor* OverrideCameraActor;

    // --- Eventos ---

    UPROPERTY(BlueprintAssignable, Category = "LOD|Events")
    FOnLODChanged OnLODChanged;

    // --- Acceso al Quadtree ---

    FCubeSphereQuadTree& GetQuadTree() { return QuadTree; }
    const FCubeSphereQuadTree& GetQuadTree() const { return QuadTree; }

    // --- Consultas ---

    UFUNCTION(BlueprintCallable, Category = "LOD")
    bool IsNodeVisible(const FQuadTreeNodeId& NodeId) const;

    UFUNCTION(BlueprintCallable, Category = "LOD")
    float GetNodeScreenSize(const FQuadTreeNodeId& NodeId) const;

    UFUNCTION(BlueprintCallable, Category = "LOD")
    int32 GetRecommendedLOD(const FVector& WorldPosition) const;

protected:
    // Core del sistema LOD
    void UpdateLOD();
    
    // Calcular error de pantalla para un nodo
    float CalculateScreenSpaceError(const FQuadTreeNodeId& NodeId, const FVector& CameraPos, float FOV) const;
    
    // Verificar si un nodo está en el frustum
    bool IsInFrustum(const FQuadTreeNodeId& NodeId) const;
    
    // Verificar si un nodo está oculto por el horizonte
    bool IsOccludedByHorizon(const FQuadTreeNodeId& NodeId, const FVector& CameraPos) const;

    // Decidir si un nodo debe subdividirse
    bool ShouldSplit(const FQuadTreeNodeId& NodeId, const FVector& CameraPos) const;
    
    // Decidir si un nodo padre debe colapsarse
    bool ShouldCollapse(const FQuadTreeNodeId& ParentId, const FVector& CameraPos) const;

    // Procesar subdivisiones con presupuesto
    void ProcessSplits(const FVector& CameraPos);
    
    // Procesar collapses con presupuesto
    void ProcessCollapses(const FVector& CameraPos);

    // Obtener posición de cámara actual
    FVector GetCameraPosition() const;
    FVector GetCameraForward() const;
    
    // Actualizar estadísticas
    void UpdateStatistics();

private:
    FCubeSphereQuadTree QuadTree;
    double PlanetRadius;
    
    // Caché de cámara para culling por cono (más barato que un frustum completo de 6
    // planos, y no requiere construir matrices de vista/proyección aquí)
    FVector CachedCameraForward = FVector::ForwardVector;
    float CachedHalfFOVRad = FMath::DegreesToRadians(45.0f);
    bool bFrustumValid;

    // Estado anterior para histéresis
    TMap<FQuadTreeNodeId, float> PreviousDistances;

    // Colas de operaciones pendientes
    TArray<FQuadTreeNodeId> PendingSplits;
    TArray<FQuadTreeNodeId> PendingCollapses;

    // Helpers
    void CollectSplitCandidates(const FVector& CameraPos);
    void CollectCollapseCandidates(const FVector& CameraPos);
    void SortByPriority(TArray<FQuadTreeNodeId>& Nodes, const FVector& CameraPos, bool bCloserFirst);
};
