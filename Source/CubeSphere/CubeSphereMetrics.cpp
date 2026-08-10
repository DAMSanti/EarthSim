// CubeSphereMetrics.cpp
// Implementación del sistema de corrección métrica
// Sprint 1.2 - Factores de escala como texturas GPU

#include "CubeSphereMetrics.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

UCubeSphereMetrics::UCubeSphereMetrics()
    : Grid(nullptr)
    , AreaFactorTexture(nullptr)
    , MetricGradientTexture(nullptr)
    , AreaFactorCubeMap(nullptr)
{
}

void UCubeSphereMetrics::Initialize(UCubeSphereGrid* InGrid)
{
    Grid = InGrid;
    
    if (!Grid)
    {
        UE_LOG(LogTemp, Error, TEXT("CubeSphereMetrics: Grid is null"));
        return;
    }
    
    UE_LOG(LogTemp, Log, TEXT("CubeSphereMetrics initialized for grid resolution %d"), 
           Grid->GetResolution());
}

// ============================================================
// CÁLCULO DEL JACOBIANO
// ============================================================

float UCubeSphereMetrics::CalculateJacobianDeterminant(ECSCubeFace Face, float U, float V) const
{
    // Convertir UV [0,1] a coordenadas del cubo [-1, 1]
    float X = U * 2.0f - 1.0f;
    float Y = V * 2.0f - 1.0f;
    
    // La proyección del cubo a la esfera es: P_sphere = normalize(P_cube)
    // El Jacobiano de esta transformación tiene un determinante que
    // depende de la distancia al centro de la cara.
    //
    // Para un punto (x, y, 1) en la cara +Z del cubo:
    // P_sphere = (x, y, 1) / sqrt(x² + y² + 1)
    //
    // El determinante del Jacobiano es:
    // det(J) = 1 / (1 + x² + y²)^(3/2)
    
    float R2 = X * X + Y * Y;
    float Denominator = FMath::Pow(1.0f + R2, 1.5f);
    
    // Evitar división por cero (no debería ocurrir)
    if (Denominator < KINDA_SMALL_NUMBER)
    {
        return 1.0f;
    }
    
    return 1.0f / Denominator;
}

float UCubeSphereMetrics::GetAreaFactor(ECSCubeFace Face, FVector2D UV) const
{
    return CalculateJacobianDeterminant(Face, UV.X, UV.Y);
}

void UCubeSphereMetrics::GetJacobian(ECSCubeFace Face, FVector2D UV, FMatrix2x2& OutJacobian) const
{
    float X = UV.X * 2.0f - 1.0f;
    float Y = UV.Y * 2.0f - 1.0f;
    
    float R2 = X * X + Y * Y;
    float R = FMath::Sqrt(1.0f + R2);
    float R3 = R * R * R;
    float R5 = R3 * R * R;
    
    // Derivadas parciales de la proyección
    // Estos coeficientes describen cómo se estira/comprime el espacio
    float dPx_dx = (1.0f + Y * Y) / R3;
    float dPx_dy = -X * Y / R3;
    float dPy_dx = -X * Y / R3;
    float dPy_dy = (1.0f + X * X) / R3;
    
    // Construir la matriz usando el constructor
    OutJacobian = FMatrix2x2(dPx_dx, dPx_dy, dPy_dx, dPy_dy);
}

// ============================================================
// GENERACIÓN DE TEXTURAS
// ============================================================

UTexture2D* UCubeSphereMetrics::GenerateAreaFactorTexture()
{
    if (!Grid)
    {
        UE_LOG(LogTemp, Error, TEXT("Cannot generate texture: Grid is null"));
        return nullptr;
    }
    
    int32 Resolution = Grid->GetResolution();
    
    // Crear textura con 6 caras apiladas verticalmente
    // Formato: R32F (un float por píxel)
    int32 TextureWidth = Resolution;
    int32 TextureHeight = Resolution * 6;  // 6 caras apiladas
    
    AreaFactorTexture = UTexture2D::CreateTransient(
        TextureWidth, 
        TextureHeight, 
        PF_R32_FLOAT
    );
    
    if (!AreaFactorTexture)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create area factor texture"));
        return nullptr;
    }
    
    // Configurar textura
    AreaFactorTexture->CompressionSettings = TC_HDR;
    AreaFactorTexture->SRGB = false;
    AreaFactorTexture->Filter = TF_Bilinear;
    AreaFactorTexture->AddressX = TA_Clamp;
    AreaFactorTexture->AddressY = TA_Clamp;
    
    // Generar datos
    TArray<float> FactorData;
    FactorData.SetNum(TextureWidth * TextureHeight);
    
    float MinFactor = MAX_FLT;
    float MaxFactor = -MAX_FLT;
    float SumFactors = 0.0f;
    
    for (int32 FaceIdx = 0; FaceIdx < 6; FaceIdx++)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        int32 FaceOffset = FaceIdx * Resolution * Resolution;
        
        for (int32 V = 0; V < Resolution; V++)
        {
            for (int32 U = 0; U < Resolution; U++)
            {
                // UV del centro del píxel
                float UNorm = (U + 0.5f) / Resolution;
                float VNorm = (V + 0.5f) / Resolution;
                
                float Factor = GetAreaFactor(Face, FVector2D(UNorm, VNorm));
                
                int32 PixelIndex = FaceOffset + V * Resolution + U;
                FactorData[PixelIndex] = Factor;
                
                MinFactor = FMath::Min(MinFactor, Factor);
                MaxFactor = FMath::Max(MaxFactor, Factor);
                SumFactors += Factor;
            }
        }
    }
    
    // Log de estadísticas
    float AvgFactor = SumFactors / (TextureWidth * TextureHeight);
    UE_LOG(LogTemp, Log, TEXT("Area Factor Texture Stats:"));
    UE_LOG(LogTemp, Log, TEXT("  Min: %.4f, Max: %.4f, Avg: %.4f"), MinFactor, MaxFactor, AvgFactor);
    UE_LOG(LogTemp, Log, TEXT("  Ratio Max/Min: %.4f"), MaxFactor / MinFactor);
    
    // Copiar datos a la textura
    FTexture2DMipMap& Mip = AreaFactorTexture->GetPlatformData()->Mips[0];
    void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(TextureData, FactorData.GetData(), FactorData.Num() * sizeof(float));
    Mip.BulkData.Unlock();
    
    AreaFactorTexture->UpdateResource();
    
    return AreaFactorTexture;
}

UTexture2D* UCubeSphereMetrics::GenerateMetricGradientTexture()
{
    if (!Grid)
    {
        return nullptr;
    }
    
    int32 Resolution = Grid->GetResolution();
    int32 TextureWidth = Resolution;
    int32 TextureHeight = Resolution * 6;
    
    // Formato RG32F: dos floats por píxel (gradiente en U y V)
    MetricGradientTexture = UTexture2D::CreateTransient(
        TextureWidth,
        TextureHeight,
        PF_G32R32F
    );
    
    if (!MetricGradientTexture)
    {
        return nullptr;
    }
    
    MetricGradientTexture->CompressionSettings = TC_HDR;
    MetricGradientTexture->SRGB = false;
    
    // Generar gradientes usando diferencias finitas
    TArray<FVector2D> GradientData;
    GradientData.SetNum(TextureWidth * TextureHeight);
    
    float DeltaUV = 1.0f / Resolution;
    
    for (int32 FaceIdx = 0; FaceIdx < 6; FaceIdx++)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        int32 FaceOffset = FaceIdx * Resolution * Resolution;
        
        for (int32 V = 0; V < Resolution; V++)
        {
            for (int32 U = 0; U < Resolution; U++)
            {
                float UNorm = (U + 0.5f) / Resolution;
                float VNorm = (V + 0.5f) / Resolution;
                
                // Diferencias finitas centrales
                float FactorCenter = GetAreaFactor(Face, FVector2D(UNorm, VNorm));
                
                float FactorPlusU = GetAreaFactor(Face, FVector2D(
                    FMath::Min(UNorm + DeltaUV, 1.0f), VNorm));
                float FactorMinusU = GetAreaFactor(Face, FVector2D(
                    FMath::Max(UNorm - DeltaUV, 0.0f), VNorm));
                    
                float FactorPlusV = GetAreaFactor(Face, FVector2D(
                    UNorm, FMath::Min(VNorm + DeltaUV, 1.0f)));
                float FactorMinusV = GetAreaFactor(Face, FVector2D(
                    UNorm, FMath::Max(VNorm - DeltaUV, 0.0f)));
                
                float GradU = (FactorPlusU - FactorMinusU) / (2.0f * DeltaUV);
                float GradV = (FactorPlusV - FactorMinusV) / (2.0f * DeltaUV);
                
                int32 PixelIndex = FaceOffset + V * Resolution + U;
                GradientData[PixelIndex] = FVector2D(GradU, GradV);
            }
        }
    }
    
    // Copiar a textura
    FTexture2DMipMap& Mip = MetricGradientTexture->GetPlatformData()->Mips[0];
    void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(TextureData, GradientData.GetData(), GradientData.Num() * sizeof(FVector2D));
    Mip.BulkData.Unlock();
    
    MetricGradientTexture->UpdateResource();
    
    return MetricGradientTexture;
}

UTextureRenderTargetCube* UCubeSphereMetrics::GenerateAreaFactorCubeMap()
{
    if (!Grid)
    {
        return nullptr;
    }
    
    int32 Resolution = Grid->GetResolution();
    
    // Crear Cube Map Render Target
    AreaFactorCubeMap = NewObject<UTextureRenderTargetCube>(this);
    AreaFactorCubeMap->Init(Resolution, PF_R32_FLOAT);
    AreaFactorCubeMap->UpdateResourceImmediate();
    
    // Nota: Para llenar un Cube Map necesitamos usar un Compute Shader
    // o renderizar cada cara por separado. Por ahora retornamos el target
    // vacío para que se llene con el shader correspondiente.
    
    UE_LOG(LogTemp, Log, TEXT("Created Area Factor CubeMap: %dx%d"), Resolution, Resolution);
    
    return AreaFactorCubeMap;
}

// ============================================================
// VALIDACIÓN
// ============================================================

float UCubeSphereMetrics::ValidateTotalArea() const
{
    if (!Grid)
    {
        return 0.0f;
    }
    
    int32 Resolution = Grid->GetResolution();
    float PlanetRadius = Grid->GetPlanetRadius();
    
    // Área teórica de la esfera
    float TheoreticalArea = 4.0f * PI * PlanetRadius * PlanetRadius;
    
    // Cada cara del cubo cubre 1/6 del área total de la esfera
    // pero las celdas no son uniformes debido a la proyección
    
    // El área de una celda en el cubo (antes de proyectar) es:
    // AreaCubo = (2/Resolution)^2 = 4/Resolution^2 (en unidades del cubo [-1,1])
    // 
    // Al proyectar a esfera, el área se multiplica por el Jacobiano
    // El Jacobiano de cubo->esfera es 1/(1+x²+y²)^1.5
    // 
    // Área total = sum de (AreaCubo * Jacobiano * R²) para todas las celdas
    
    float SummedArea = 0.0f;
    float CubeCellSize = 2.0f / Resolution;  // Tamaño de celda en el cubo [-1,1]
    float CubeCellArea = CubeCellSize * CubeCellSize;  // Área en el cubo
    
    for (int32 FaceIdx = 0; FaceIdx < 6; FaceIdx++)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);
        
        for (int32 V = 0; V < Resolution; V++)
        {
            for (int32 U = 0; U < Resolution; U++)
            {
                // Centro de la celda en coordenadas [-1, 1]
                float X = (U + 0.5f) / Resolution * 2.0f - 1.0f;
                float Y = (V + 0.5f) / Resolution * 2.0f - 1.0f;
                
                // Jacobiano: factor de escala de área cubo->esfera
                float R2 = X * X + Y * Y;
                float Jacobian = 1.0f / FMath::Pow(1.0f + R2, 1.5f);
                
                // Área de esta celda en la esfera
                // El área en la esfera = área_cubo * Jacobiano * R²
                float CellArea = CubeCellArea * Jacobian * PlanetRadius * PlanetRadius;
                SummedArea += CellArea;
            }
        }
    }
    
    // El factor de corrección necesario
    float CorrectionFactor = TheoreticalArea / SummedArea;
    
    UE_LOG(LogTemp, Log, TEXT("Area Validation:"));
    UE_LOG(LogTemp, Log, TEXT("  Theoretical: %.2f km²"), TheoreticalArea / 1e6f);
    UE_LOG(LogTemp, Log, TEXT("  Summed:      %.2f km²"), SummedArea / 1e6f);
    UE_LOG(LogTemp, Log, TEXT("  Error:       %.4f%%"), FMath::Abs(1.0f - SummedArea/TheoreticalArea) * 100.0f);
    UE_LOG(LogTemp, Log, TEXT("  Correction:  %.6f"), CorrectionFactor);
    
    return SummedArea;
}

void UCubeSphereMetrics::NormalizeAreaFactors(TArray<float>& Factors) const
{
    if (Factors.Num() == 0)
    {
        return;
    }
    
    // Calcular suma
    float Sum = 0.0f;
    for (float Factor : Factors)
    {
        Sum += Factor;
    }
    
    // Normalizar para que la suma sea igual al número de elementos
    // (promedio = 1.0)
    float NormFactor = Factors.Num() / Sum;
    
    for (float& Factor : Factors)
    {
        Factor *= NormFactor;
    }
}
