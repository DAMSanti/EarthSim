// Copyright (c) 2024 Simu Project. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TectonicTypes.h"
#include "TectonicConfigWidget.generated.h"

class UTectonicPlateSystem;
class USlider;
class UTextBlock;
class UComboBoxString;
class UButton;
class UCheckBox;

/**
 * UTectonicConfigWidget
 * 
 * Widget de configuración para parámetros iniciales de simulación tectónica.
 * Permite ajustar número de placas, distribución, velocidad, etc.
 */
UCLASS()
class SIMU_API UTectonicConfigWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // --- Inicialización ---
    
    virtual void NativeConstruct() override;
    
    /**
     * Vincular el sistema de placas para aplicar cambios
     */
    UFUNCTION(BlueprintCallable, Category = "Tectonic UI")
    void BindPlateSystem(UTectonicPlateSystem* InPlateSystem);

    // --- Getters de configuración ---
    
    UFUNCTION(BlueprintCallable, Category = "Tectonic UI")
    FPlateGenerationConfig GetCurrentConfig() const;

protected:
    // --- Widgets UI (bindear en Blueprint) ---
    
    /** Slider para número de placas (5-50) */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    USlider* PlateCountSlider;
    
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UTextBlock* PlateCountText;
    
    /** Combo para método de distribución */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UComboBoxString* DistributionMethodCombo;
    
    /** Slider para proporción oceánica (0-100%) */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    USlider* OceanicRatioSlider;
    
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UTextBlock* OceanicRatioText;
    
    /** Slider para velocidad mínima angular */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    USlider* MinVelocitySlider;
    
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UTextBlock* MinVelocityText;
    
    /** Slider para velocidad máxima angular */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    USlider* MaxVelocitySlider;
    
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UTextBlock* MaxVelocityText;
    
    /** Slider para velocidad de simulación */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    USlider* SimulationSpeedSlider;
    
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UTextBlock* SimulationSpeedText;
    
    /** Seed aleatorio */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    USlider* RandomSeedSlider;
    
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UTextBlock* RandomSeedText;
    
    /** Checkbox para usar seed fijo */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UCheckBox* UseFixedSeedCheckbox;
    
    /** Botones de acción */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UButton* GenerateButton;
    
    UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
    UButton* ResetButton;
    
    // --- Callbacks de UI ---
    
    UFUNCTION()
    void OnPlateCountChanged(float Value);
    
    UFUNCTION()
    void OnOceanicRatioChanged(float Value);
    
    UFUNCTION()
    void OnMinVelocityChanged(float Value);
    
    UFUNCTION()
    void OnMaxVelocityChanged(float Value);
    
    UFUNCTION()
    void OnSimulationSpeedChanged(float Value);
    
    UFUNCTION()
    void OnRandomSeedChanged(float Value);
    
    UFUNCTION()
    void OnDistributionMethodChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
    
    UFUNCTION()
    void OnGenerateClicked();
    
    UFUNCTION()
    void OnResetClicked();

private:
    // Sistema de placas vinculado
    UPROPERTY()
    UTectonicPlateSystem* PlateSystem;
    
    // Configuración actual
    UPROPERTY()
    FPlateGenerationConfig CurrentConfig;
    
    // Inicializar valores por defecto
    void InitializeDefaults();
    
    // Actualizar textos de los sliders
    void UpdateAllLabels();
};
