// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "TectonicConfigWidget.h"
#include "TectonicPlateSystem.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/ComboBoxString.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"

void UTectonicConfigWidget::NativeConstruct()
{
    Super::NativeConstruct();
    
    InitializeDefaults();
    
    // Vincular callbacks de sliders
    if (PlateCountSlider)
    {
        PlateCountSlider->OnValueChanged.AddDynamic(this, &UTectonicConfigWidget::OnPlateCountChanged);
    }
    
    if (OceanicRatioSlider)
    {
        OceanicRatioSlider->OnValueChanged.AddDynamic(this, &UTectonicConfigWidget::OnOceanicRatioChanged);
    }
    
    if (MinVelocitySlider)
    {
        MinVelocitySlider->OnValueChanged.AddDynamic(this, &UTectonicConfigWidget::OnMinVelocityChanged);
    }
    
    if (MaxVelocitySlider)
    {
        MaxVelocitySlider->OnValueChanged.AddDynamic(this, &UTectonicConfigWidget::OnMaxVelocityChanged);
    }
    
    if (SimulationSpeedSlider)
    {
        SimulationSpeedSlider->OnValueChanged.AddDynamic(this, &UTectonicConfigWidget::OnSimulationSpeedChanged);
    }
    
    if (RandomSeedSlider)
    {
        RandomSeedSlider->OnValueChanged.AddDynamic(this, &UTectonicConfigWidget::OnRandomSeedChanged);
    }
    
    // Vincular combo de distribución
    if (DistributionMethodCombo)
    {
        DistributionMethodCombo->OnSelectionChanged.AddDynamic(this, &UTectonicConfigWidget::OnDistributionMethodChanged);
        
        // Agregar opciones
        DistributionMethodCombo->AddOption(TEXT("Fibonacci (Uniforme)"));
        DistributionMethodCombo->AddOption(TEXT("Aleatorio"));
        DistributionMethodCombo->AddOption(TEXT("Uniforme Grid"));
        DistributionMethodCombo->SetSelectedIndex(0);
    }
    
    // Vincular botones
    if (GenerateButton)
    {
        GenerateButton->OnClicked.AddDynamic(this, &UTectonicConfigWidget::OnGenerateClicked);
    }
    
    if (ResetButton)
    {
        ResetButton->OnClicked.AddDynamic(this, &UTectonicConfigWidget::OnResetClicked);
    }
    
    UpdateAllLabels();
}

void UTectonicConfigWidget::BindPlateSystem(UTectonicPlateSystem* InPlateSystem)
{
    PlateSystem = InPlateSystem;
}

FPlateGenerationConfig UTectonicConfigWidget::GetCurrentConfig() const
{
    return CurrentConfig;
}

void UTectonicConfigWidget::InitializeDefaults()
{
    // Configuración por defecto
    CurrentConfig.NumPlates = 12;
    CurrentConfig.DistributionMethod = ECentroidDistribution::Fibonacci;
    CurrentConfig.OceanicRatio = 0.7f;
    CurrentConfig.MinAngularVelocity = 0.0005f;
    CurrentConfig.MaxAngularVelocity = 0.005f;
    CurrentConfig.RandomSeed = 42;
    CurrentConfig.bUseFixedSeed = false;
    
    // Aplicar a sliders
    if (PlateCountSlider)
    {
        PlateCountSlider->SetMinValue(5.0f);
        PlateCountSlider->SetMaxValue(50.0f);
        PlateCountSlider->SetValue(static_cast<float>(CurrentConfig.NumPlates));
    }
    
    if (OceanicRatioSlider)
    {
        OceanicRatioSlider->SetMinValue(0.0f);
        OceanicRatioSlider->SetMaxValue(1.0f);
        OceanicRatioSlider->SetValue(CurrentConfig.OceanicRatio);
    }
    
    if (MinVelocitySlider)
    {
        MinVelocitySlider->SetMinValue(0.0001f);
        MinVelocitySlider->SetMaxValue(0.01f);
        MinVelocitySlider->SetValue(CurrentConfig.MinAngularVelocity);
    }
    
    if (MaxVelocitySlider)
    {
        MaxVelocitySlider->SetMinValue(0.001f);
        MaxVelocitySlider->SetMaxValue(0.02f);
        MaxVelocitySlider->SetValue(CurrentConfig.MaxAngularVelocity);
    }
    
    if (SimulationSpeedSlider)
    {
        SimulationSpeedSlider->SetMinValue(0.1f);
        SimulationSpeedSlider->SetMaxValue(10.0f);
        SimulationSpeedSlider->SetValue(1.0f);
    }
    
    if (RandomSeedSlider)
    {
        RandomSeedSlider->SetMinValue(0.0f);
        RandomSeedSlider->SetMaxValue(9999.0f);
        RandomSeedSlider->SetValue(static_cast<float>(CurrentConfig.RandomSeed));
    }
}

void UTectonicConfigWidget::UpdateAllLabels()
{
    if (PlateCountText)
    {
        PlateCountText->SetText(FText::FromString(FString::Printf(TEXT("Placas: %d"), CurrentConfig.NumPlates)));
    }
    
    if (OceanicRatioText)
    {
        OceanicRatioText->SetText(FText::FromString(FString::Printf(TEXT("Oceánicas: %.0f%%"), CurrentConfig.OceanicRatio * 100.0f)));
    }
    
    if (MinVelocityText)
    {
        MinVelocityText->SetText(FText::FromString(FString::Printf(TEXT("Vel. Mín: %.4f rad/s"), CurrentConfig.MinAngularVelocity)));
    }
    
    if (MaxVelocityText)
    {
        MaxVelocityText->SetText(FText::FromString(FString::Printf(TEXT("Vel. Máx: %.4f rad/s"), CurrentConfig.MaxAngularVelocity)));
    }
    
    if (SimulationSpeedText)
    {
        float Speed = SimulationSpeedSlider ? SimulationSpeedSlider->GetValue() : 1.0f;
        SimulationSpeedText->SetText(FText::FromString(FString::Printf(TEXT("Velocidad: %.1fx"), Speed)));
    }
    
    if (RandomSeedText)
    {
        RandomSeedText->SetText(FText::FromString(FString::Printf(TEXT("Seed: %d"), CurrentConfig.RandomSeed)));
    }
}

void UTectonicConfigWidget::OnPlateCountChanged(float Value)
{
    CurrentConfig.NumPlates = FMath::RoundToInt(Value);
    
    if (PlateCountText)
    {
        PlateCountText->SetText(FText::FromString(FString::Printf(TEXT("Placas: %d"), CurrentConfig.NumPlates)));
    }
}

void UTectonicConfigWidget::OnOceanicRatioChanged(float Value)
{
    CurrentConfig.OceanicRatio = Value;
    
    if (OceanicRatioText)
    {
        OceanicRatioText->SetText(FText::FromString(FString::Printf(TEXT("Oceánicas: %.0f%%"), Value * 100.0f)));
    }
}

void UTectonicConfigWidget::OnMinVelocityChanged(float Value)
{
    CurrentConfig.MinAngularVelocity = Value;
    
    // Asegurar que min <= max
    if (MaxVelocitySlider && Value > MaxVelocitySlider->GetValue())
    {
        MaxVelocitySlider->SetValue(Value);
    }
    
    if (MinVelocityText)
    {
        MinVelocityText->SetText(FText::FromString(FString::Printf(TEXT("Vel. Mín: %.4f rad/s"), Value)));
    }
}

void UTectonicConfigWidget::OnMaxVelocityChanged(float Value)
{
    CurrentConfig.MaxAngularVelocity = Value;
    
    // Asegurar que max >= min
    if (MinVelocitySlider && Value < MinVelocitySlider->GetValue())
    {
        MinVelocitySlider->SetValue(Value);
    }
    
    if (MaxVelocityText)
    {
        MaxVelocityText->SetText(FText::FromString(FString::Printf(TEXT("Vel. Máx: %.4f rad/s"), Value)));
    }
}

void UTectonicConfigWidget::OnSimulationSpeedChanged(float Value)
{
    if (SimulationSpeedText)
    {
        SimulationSpeedText->SetText(FText::FromString(FString::Printf(TEXT("Velocidad: %.1fx"), Value)));
    }
    
    // TODO: Aplicar velocidad al time dilation del mundo
}

void UTectonicConfigWidget::OnRandomSeedChanged(float Value)
{
    CurrentConfig.RandomSeed = FMath::RoundToInt(Value);
    
    if (RandomSeedText)
    {
        RandomSeedText->SetText(FText::FromString(FString::Printf(TEXT("Seed: %d"), CurrentConfig.RandomSeed)));
    }
}

void UTectonicConfigWidget::OnDistributionMethodChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
    if (SelectedItem.Contains(TEXT("Fibonacci")))
    {
        CurrentConfig.DistributionMethod = ECentroidDistribution::Fibonacci;
    }
    else if (SelectedItem.Contains(TEXT("Aleatorio")))
    {
        CurrentConfig.DistributionMethod = ECentroidDistribution::Random;
    }
    else if (SelectedItem.Contains(TEXT("Uniforme")))
    {
        CurrentConfig.DistributionMethod = ECentroidDistribution::Uniform;
    }
}

void UTectonicConfigWidget::OnGenerateClicked()
{
    if (PlateSystem)
    {
        // Usar seed fijo si está marcado
        if (UseFixedSeedCheckbox && UseFixedSeedCheckbox->IsChecked())
        {
            CurrentConfig.bUseFixedSeed = true;
        }
        else
        {
            CurrentConfig.bUseFixedSeed = false;
            CurrentConfig.RandomSeed = FMath::RandRange(0, 9999);
            
            if (RandomSeedSlider)
            {
                RandomSeedSlider->SetValue(static_cast<float>(CurrentConfig.RandomSeed));
            }
        }
        
        // Regenerar placas con nueva configuración
        // PlateSystem->Initialize(Grid, CurrentConfig);
        // PlateSystem->GeneratePlates();
        
        UE_LOG(LogTemp, Log, TEXT("Generando placas: %d placas, método: %d, oceánico: %.1f%%"),
               CurrentConfig.NumPlates,
               static_cast<int32>(CurrentConfig.DistributionMethod),
               CurrentConfig.OceanicRatio * 100.0f);
    }
}

void UTectonicConfigWidget::OnResetClicked()
{
    InitializeDefaults();
    UpdateAllLabels();
    
    if (DistributionMethodCombo)
    {
        DistributionMethodCombo->SetSelectedIndex(0);
    }
    
    if (UseFixedSeedCheckbox)
    {
        UseFixedSeedCheckbox->SetIsChecked(false);
    }
}
