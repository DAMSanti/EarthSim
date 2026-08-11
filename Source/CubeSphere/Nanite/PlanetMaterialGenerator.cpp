// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "PlanetMaterialGenerator.h"

#if WITH_EDITOR

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"

namespace
{
    constexpr const TCHAR* PlanetMaterialPackagePath = TEXT("/Game/Materials/M_PlanetDisplacement");
    constexpr const TCHAR* PlanetMaterialAssetName = TEXT("M_PlanetDisplacement");
}

UMaterialInterface* GetOrCreatePlanetDisplacementMaterial()
{
    if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, PlanetMaterialPackagePath))
    {
        return Existing;
    }

    UPackage* Package = CreatePackage(PlanetMaterialPackagePath);
    if (!Package)
    {
        return nullptr;
    }

    UMaterial* NewMaterial = NewObject<UMaterial>(Package, FName(PlanetMaterialAssetName), RF_Public | RF_Standalone);
    if (!NewMaterial)
    {
        return nullptr;
    }

    // Heightmap: R16F en [0,1], escrito por PlanetNaniteMesh::GenerateProceduralHeightmap
    UMaterialExpressionTextureSampleParameter2D* HeightmapExpr =
        Cast<UMaterialExpressionTextureSampleParameter2D>(
            UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -700, -100));
    if (HeightmapExpr)
    {
        HeightmapExpr->ParameterName = TEXT("Heightmap");
        HeightmapExpr->SamplerType = SAMPLERTYPE_LinearGrayscale;
    }

    // HeightmapMinMax: X=Min, Y=Max, en metros (elevacion real de la simulacion)
    UMaterialExpressionVectorParameter* MinMaxExpr =
        Cast<UMaterialExpressionVectorParameter>(
            UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionVectorParameter::StaticClass(), -700, 100));
    if (MinMaxExpr)
    {
        MinMaxExpr->ParameterName = TEXT("HeightmapMinMax");
        MinMaxExpr->DefaultValue = FLinearColor(-3800.0f, 12000.0f, 0.0f, 0.0f);
    }

    // Elevacion real (metros) = Lerp(Min, Max, HeightmapSample)
    UMaterialExpressionLinearInterpolate* LerpExpr =
        Cast<UMaterialExpressionLinearInterpolate>(
            UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionLinearInterpolate::StaticClass(), -450, 0));

    // Metros -> cm (unidades de Unreal)
    UMaterialExpressionMultiply* ToCmExpr =
        Cast<UMaterialExpressionMultiply>(
            UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionMultiply::StaticClass(), -200, 0));
    if (ToCmExpr)
    {
        ToCmExpr->ConstB = 100.0f;
    }

    // Color base simple para diferenciar visualmente por altura hasta que exista
    // texturizado real (splat maps, Fase F8 del roadmap original)
    UMaterialExpressionConstant3Vector* BaseColorExpr =
        Cast<UMaterialExpressionConstant3Vector>(
            UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionConstant3Vector::StaticClass(), -450, 250));
    if (BaseColorExpr)
    {
        BaseColorExpr->Constant = FLinearColor(0.35f, 0.35f, 0.33f);
    }

    if (HeightmapExpr && MinMaxExpr && LerpExpr && ToCmExpr && BaseColorExpr)
    {
        UMaterialEditingLibrary::ConnectMaterialExpressions(MinMaxExpr, TEXT("R"), LerpExpr, TEXT("A"));
        UMaterialEditingLibrary::ConnectMaterialExpressions(MinMaxExpr, TEXT("G"), LerpExpr, TEXT("B"));
        UMaterialEditingLibrary::ConnectMaterialExpressions(HeightmapExpr, TEXT(""), LerpExpr, TEXT("Alpha"));
        UMaterialEditingLibrary::ConnectMaterialExpressions(LerpExpr, TEXT(""), ToCmExpr, TEXT("A"));

        UMaterialEditingLibrary::ConnectMaterialProperty(ToCmExpr, TEXT(""), MP_Displacement);
        UMaterialEditingLibrary::ConnectMaterialProperty(BaseColorExpr, TEXT(""), MP_BaseColor);
    }

    NewMaterial->SetShadingModel(MSM_DefaultLit);
    NewMaterial->TwoSided = false;

    UMaterialEditingLibrary::RecompileMaterial(NewMaterial);

    FAssetRegistryModule::AssetCreated(NewMaterial);
    Package->MarkPackageDirty();

    const FString PackageFileName = FPackageName::LongPackageNameToFilename(PlanetMaterialPackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(Package, NewMaterial, *PackageFileName, SaveArgs);

    return NewMaterial;
}

#endif // WITH_EDITOR
