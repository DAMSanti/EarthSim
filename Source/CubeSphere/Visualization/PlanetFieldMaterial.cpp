// PlanetFieldMaterial.cpp

#include "PlanetFieldMaterial.h"

#if WITH_EDITOR

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"

namespace
{
    constexpr const TCHAR* FieldMaterialPackagePath = TEXT("/Game/Materials/M_PlanetFieldUnlit");
    constexpr const TCHAR* FieldMaterialAssetName = TEXT("M_PlanetFieldUnlit");
}

UMaterialInterface* GetOrCreateFieldViewMaterial()
{
    if (UMaterial* Existing = LoadObject<UMaterial>(nullptr, FieldMaterialPackagePath))
    {
        return Existing;
    }

    UPackage* Package = CreatePackage(FieldMaterialPackagePath);
    if (!Package)
    {
        return nullptr;
    }

    UMaterial* NewMaterial = NewObject<UMaterial>(Package, FName(FieldMaterialAssetName), RF_Public | RF_Standalone);
    if (!NewMaterial)
    {
        return nullptr;
    }

    UMaterialExpressionVertexColor* VertexColorExpr =
        Cast<UMaterialExpressionVertexColor>(
            UMaterialEditingLibrary::CreateMaterialExpression(
                NewMaterial, UMaterialExpressionVertexColor::StaticClass(), -300, 0));

    if (VertexColorExpr)
    {
        // A Emissive y no a BaseColor: en un material unlit, BaseColor no se evalúa.
        // Emissive es el único canal que sale tal cual a pantalla, que es justo lo que
        // hace falta para que el color mostrado sea el color de la paleta.
        UMaterialEditingLibrary::ConnectMaterialProperty(VertexColorExpr, TEXT(""), MP_EmissiveColor);
    }

    NewMaterial->SetShadingModel(MSM_Unlit);
    NewMaterial->TwoSided = false;

    UMaterialEditingLibrary::RecompileMaterial(NewMaterial);

    FAssetRegistryModule::AssetCreated(NewMaterial);
    Package->MarkPackageDirty();

    const FString PackageFileName = FPackageName::LongPackageNameToFilename(
        FieldMaterialPackagePath, FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    UPackage::SavePackage(Package, NewMaterial, *PackageFileName, SaveArgs);

    return NewMaterial;
}

#endif // WITH_EDITOR
