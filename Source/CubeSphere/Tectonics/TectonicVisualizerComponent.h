// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TectonicTypes.h"
#include "TectonicVisualizerComponent.generated.h"

class UTectonicPlateSystem;
class UPlanetNaniteMesh;
class UCubeSphereGrid;
class UPlateKinematics;
class UMaterialInstanceDynamic;
class UTexture2D;
class UProceduralMeshComponent;

/**
 * Modo de visualización de placas
 */
UENUM(BlueprintType)
enum class ETectonicVisualMode : uint8
{
    PlateID         UMETA(DisplayName = "ID de Placa"),      // Color por ID
    CrustType       UMETA(DisplayName = "Tipo de Corteza"),  // Oceánica vs Continental
    Velocity        UMETA(DisplayName = "Velocidad"),        // Magnitud de velocidad
    BoundaryType    UMETA(DisplayName = "Límites"),          // Convergente/Divergente/Transform
    Age             UMETA(DisplayName = "Edad"),             // Edad de la corteza
    Stress          UMETA(DisplayName = "Estrés")            // Acumulación de estrés
};

/**
 * UTectonicVisualizerComponent
 * 
 * Componente que visualiza las placas tectónicas usando Nanite.
 * Genera texturas de IDs de placa y las pasa al material del planeta.
 * 
 * Integración con PlanetNaniteMesh:
 * - Genera Texture2DArray con IDs de placa por cara del cubo
 * - Actualiza materiales dinámicos con colores de placa
 * - Dibuja vectores de velocidad y límites de placa
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class CUBESPHERE_API UTectonicVisualizerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UTectonicVisualizerComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // --- Inicialización ---

    /**
     * Inicializar con los sistemas necesarios
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics|Visualization")
    void Initialize(UCubeSphereGrid* InGrid, UTectonicPlateSystem* InPlateSystem, UPlanetNaniteMesh* InNaniteMesh);

    /**
     * Generar texturas de visualización
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics|Visualization")
    void GenerateVisualizationTextures();

    /**
     * Actualizar visualización (llamar después de Step de simulación)
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonics|Visualization")
    void UpdateVisualization();

    // --- Configuración ---

    /** Modo de visualización actual */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Config")
    ETectonicVisualMode VisualMode = ETectonicVisualMode::PlateID;

    /** Mostrar vectores de velocidad */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Config")
    bool bShowVelocityVectors = false;

    /** Mostrar límites de placa */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Config")
    bool bShowPlateBoundaries = true;

    /** Escala de vectores de velocidad para visualización */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Config", meta = (ClampMin = "1.0", ClampMax = "1000000.0"))
    float VelocityVectorScale = 100000.0f;

    /** Resolución de la textura de IDs (por cara) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Config", meta = (ClampMin = "64", ClampMax = "4096"))
    int32 TextureResolution = 512;

    /** Ancho de línea de límites */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Config", meta = (ClampMin = "0.0", ClampMax = "10.0"))
    float BoundaryLineWidth = 2.0f;

    /** Resolución del mesh de visualización */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Config", meta = (ClampMin = "8", ClampMax = "128"))
    int32 VisualizationMeshResolution = 64;

    /** Offset de altura del mesh de visualización (para que no z-fight) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Config")
    float VisualizationHeightOffset = 1000.0f;

    // --- Colores ---

    /** Paleta de colores para placas */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Colors")
    TArray<FLinearColor> PlateColorPalette;

    /** Color para corteza oceánica */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Colors")
    FLinearColor OceanicColor = FLinearColor(0.1f, 0.3f, 0.6f, 1.0f);

    /** Color para corteza continental */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Colors")
    FLinearColor ContinentalColor = FLinearColor(0.4f, 0.6f, 0.2f, 1.0f);

    /** Color para límite convergente */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Colors")
    FLinearColor ConvergentColor = FLinearColor(1.0f, 0.2f, 0.1f, 1.0f);

    /** Color para límite divergente */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Colors")
    FLinearColor DivergentColor = FLinearColor(0.1f, 0.8f, 0.2f, 1.0f);

    /** Color para límite transformante */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tectonics|Colors")
    FLinearColor TransformColor = FLinearColor(1.0f, 0.8f, 0.0f, 1.0f);

    // --- Acceso a Texturas ---

    /** Obtener textura de IDs de placa */
    UFUNCTION(BlueprintCallable, Category = "Tectonics|Visualization")
    UTexture2D* GetPlateIDTexture(int32 FaceIndex) const;

    /** Obtener color de placa por ID */
    UFUNCTION(BlueprintCallable, Category = "Tectonics|Visualization")
    FLinearColor GetPlateColor(int32 PlateID) const;

protected:
    // Referencias a sistemas
    UPROPERTY()
    UCubeSphereGrid* Grid;

    UPROPERTY()
    UTectonicPlateSystem* PlateSystem;

    UPROPERTY()
    UPlanetNaniteMesh* NaniteMesh;

    // Texturas de IDs por cara (6 texturas)
    UPROPERTY()
    TArray<UTexture2D*> PlateIDTextures;

    // Material dinámico para overlay de placas
    UPROPERTY()
    UMaterialInstanceDynamic* PlateOverlayMaterial;

    // Generar paleta de colores por defecto
    void GenerateDefaultPalette(int32 NumPlates);

    // Crear textura de IDs para una cara
    UTexture2D* CreatePlateIDTexture(int32 FaceIndex);

    // Actualizar textura con IDs actuales
    void UpdatePlateIDTexture(int32 FaceIndex);

    // Dibujar vectores de velocidad como debug lines
    void DrawVelocityVectors();

    // Dibujar límites de placa
    void DrawPlateBoundaries();

    // Aplicar texturas al material de Nanite
    void ApplyTexturesToMaterial();

    // Generar mesh de visualización con vertex colors
    void GenerateVisualizationMesh();

    // Actualizar colores del mesh de visualización
    void UpdateVisualizationMeshColors();

private:
    bool bIsInitialized = false;
    float TimeSinceLastUpdate = 0.0f;
    float UpdateInterval = 0.1f; // Actualizar cada 100ms

    // Mesh procedural para visualización de placas
    UPROPERTY()
    UProceduralMeshComponent* VisualizationMesh;

    // Datos del mesh de visualización
    TArray<FVector> MeshVertices;
    TArray<int32> MeshTriangles;
    TArray<FVector> MeshNormals;
    TArray<FVector2D> MeshUVs;
    TArray<FLinearColor> MeshColors;
};
