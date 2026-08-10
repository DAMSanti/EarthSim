// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "BoundaryInteractions.h"
#include "TectonicPlateSystem.h"
#include "../CubeSphereGrid.h"

UBoundaryInteractions::UBoundaryInteractions()
{
}

void UBoundaryInteractions::Initialize(UCubeSphereGrid* InGrid, UTectonicPlateSystem* InPlateSystem)
{
    if (!InGrid || !InPlateSystem)
    {
        UE_LOG(LogTemp, Error, TEXT("BoundaryInteractions::Initialize - Invalid parameters!"));
        return;
    }

    Grid = InGrid;
    PlateSystem = InPlateSystem;

    int32 Resolution = Grid->GetResolution();

    // Inicializar mapas de tasa de elevación
    ElevationRateMaps.SetNum(6);
    for (int32 Face = 0; Face < 6; ++Face)
    {
        ElevationRateMaps[Face].SetNumZeroed(Resolution * Resolution);
    }

    bIsInitialized = true;

    UE_LOG(LogTemp, Log, TEXT("BoundaryInteractions initialized"));
}

void UBoundaryInteractions::ProcessAllBoundaries(float DeltaTime)
{
    if (!bIsInitialized)
    {
        return;
    }

    // Limpiar datos anteriores
    ClearInteractionData();

    // Identificar tipos de límites basados en velocidades
    IdentifyBoundaryTypes();

    // Procesar cada tipo de límite
    ProcessConvergentBoundaries(DeltaTime);
    ProcessDivergentBoundaries(DeltaTime);
    ProcessTransformBoundaries(DeltaTime);

    // Procesar vulcanismo
    ProcessVolcanism(DeltaTime);

    // Aplicar cambios de elevación
    ApplyElevationChanges(DeltaTime);
}

void UBoundaryInteractions::ClearInteractionData()
{
    ConvergentInteractions.Empty();
    DivergentInteractions.Empty();
    TransformInteractions.Empty();
    VolcanicActivities.Empty();

    // Resetear mapas de elevación
    for (int32 Face = 0; Face < 6; ++Face)
    {
        for (int32 i = 0; i < ElevationRateMaps[Face].Num(); ++i)
        {
            ElevationRateMaps[Face][i] = 0.0f;
        }
    }
}

void UBoundaryInteractions::IdentifyBoundaryTypes()
{
    if (!PlateSystem || !Grid)
    {
        return;
    }

    const TArray<FTectonicPlate>& Plates = PlateSystem->GetPlates();
    int32 Resolution = Grid->GetResolution();
    float PlanetRadius = Grid->GetRadius();

    // Iterar por todas las celdas
    for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
    {
        ECSCubeFace Face = static_cast<ECSCubeFace>(FaceIdx);

        for (int32 U = 0; U < Resolution; ++U)
        {
            for (int32 V = 0; V < Resolution; ++V)
            {
                FCubeSphereCell Cell(Face, U, V);
                int32 CurrentPlateID = PlateSystem->GetPlateIDAt(Cell);

                if (CurrentPlateID < 0 || CurrentPlateID >= Plates.Num())
                {
                    continue;
                }

                // Verificar vecinos para encontrar límites
                TArray<FCubeSphereCell> Neighbors = Grid->GetNeighbors(Cell);

                for (const FCubeSphereCell& Neighbor : Neighbors)
                {
                    int32 NeighborPlateID = PlateSystem->GetPlateIDAt(Neighbor);

                    if (NeighborPlateID < 0 || NeighborPlateID >= Plates.Num())
                    {
                        continue;
                    }

                    // Solo procesar si es un límite real (placas diferentes)
                    // y solo una vez (PlateID menor procesa)
                    if (NeighborPlateID != CurrentPlateID && CurrentPlateID < NeighborPlateID)
                    {
                        // Calcular posición 3D
                        FVector WorldPos = Grid->CellToCartesian(Cell);

                        // Obtener velocidades de ambas placas en este punto
                        const FTectonicPlate& PlateA = Plates[CurrentPlateID];
                        const FTectonicPlate& PlateB = Plates[NeighborPlateID];

                        FVector VelA = PlateA.GetVelocityAtPoint(WorldPos.GetSafeNormal(), PlanetRadius);
                        FVector VelB = PlateB.GetVelocityAtPoint(WorldPos.GetSafeNormal(), PlanetRadius);

                        FVector RelativeVel = VelA - VelB;

                        // Calcular normal del límite (perpendicular a la superficie, apuntando de A a B)
                        FVector BoundaryNormal = CalculateBoundaryNormal(Cell, CurrentPlateID, NeighborPlateID);

                        // Componente de velocidad relativa perpendicular al límite
                        float NormalComponent = FVector::DotProduct(RelativeVel, BoundaryNormal);

                        // Componente tangencial
                        FVector TangentialVel = RelativeVel - BoundaryNormal * NormalComponent;
                        float TangentialComponent = TangentialVel.Size();

                        // Determinar tipo de límite basado en la proporción de componentes
                        float TotalSpeed = RelativeVel.Size();
                        if (TotalSpeed < 0.001f)
                        {
                            continue;  // Sin movimiento significativo
                        }

                        float NormalRatio = FMath::Abs(NormalComponent) / TotalSpeed;

                        // Convergente: movimiento principalmente hacia el límite
                        if (NormalComponent < -0.3f * TotalSpeed)
                        {
                            FConvergentInteraction Conv;
                            Conv.Cell = Cell;
                            Conv.WorldPosition = WorldPos * PlanetRadius;
                            Conv.ConvergenceRate = FMath::Abs(NormalComponent);
                            Conv.SubductionType = DetermineSubductionType(CurrentPlateID, NeighborPlateID);

                            if (Conv.SubductionType == ESubductionType::ContinentalCollision)
                            {
                                Conv.OverridingPlate = CurrentPlateID;
                                Conv.SubductingPlate = -1;  // Ninguna subduce
                                Conv.UpliftRate = CalculateUpliftRate(Conv);
                            }
                            else if (Conv.SubductionType != ESubductionType::None)
                            {
                                // Determinar cuál subduce
                                if (Plates[CurrentPlateID].CrustType == ECrustType::Oceanic &&
                                    Plates[NeighborPlateID].CrustType == ECrustType::Continental)
                                {
                                    Conv.SubductingPlate = CurrentPlateID;
                                    Conv.OverridingPlate = NeighborPlateID;
                                }
                                else if (Plates[NeighborPlateID].CrustType == ECrustType::Oceanic &&
                                         Plates[CurrentPlateID].CrustType == ECrustType::Continental)
                                {
                                    Conv.SubductingPlate = NeighborPlateID;
                                    Conv.OverridingPlate = CurrentPlateID;
                                }
                                else
                                {
                                    // Ambas oceánicas - la más antigua subduce
                                    if (Plates[CurrentPlateID].Age > Plates[NeighborPlateID].Age)
                                    {
                                        Conv.SubductingPlate = CurrentPlateID;
                                        Conv.OverridingPlate = NeighborPlateID;
                                    }
                                    else
                                    {
                                        Conv.SubductingPlate = NeighborPlateID;
                                        Conv.OverridingPlate = CurrentPlateID;
                                    }
                                }

                                Conv.SubductionAngle = CalculateSubductionAngle(Conv.SubductingPlate, Conv.OverridingPlate);
                            }

                            ConvergentInteractions.Add(Conv);
                        }
                        // Divergente: movimiento principalmente alejándose del límite
                        else if (NormalComponent > 0.3f * TotalSpeed)
                        {
                            FDivergentInteraction Div;
                            Div.Cell = Cell;
                            Div.WorldPosition = WorldPos * PlanetRadius;
                            Div.PlateA = CurrentPlateID;
                            Div.PlateB = NeighborPlateID;
                            Div.SpreadingRate = NormalComponent;

                            // Determinar si es rift continental o dorsal oceánica
                            bool PlateAOceanic = Plates[CurrentPlateID].CrustType == ECrustType::Oceanic;
                            bool PlateBOceanic = Plates[NeighborPlateID].CrustType == ECrustType::Oceanic;

                            Div.bIsOceanicRidge = PlateAOceanic || PlateBOceanic;
                            Div.CrustCreationRate = CalculateSpreadingRate(Div);

                            // Dorsales oceánicas tienen elevación característica
                            if (Div.bIsOceanicRidge)
                            {
                                Div.RidgeElevation = 2500.0f * (Div.SpreadingRate / 5.0f);  // Proporcional a velocidad
                            }

                            DivergentInteractions.Add(Div);
                        }
                        // Transformante: movimiento principalmente paralelo al límite
                        else if (TangentialComponent > 0.5f * TotalSpeed)
                        {
                            FTransformInteraction Trans;
                            Trans.Cell = Cell;
                            Trans.WorldPosition = WorldPos * PlanetRadius;
                            Trans.PlateA = CurrentPlateID;
                            Trans.PlateB = NeighborPlateID;
                            Trans.SlipRate = TangentialComponent;
                            Trans.SlipDirection = TangentialVel.GetSafeNormal();
                            Trans.FrictionCoefficient = 0.6f;

                            TransformInteractions.Add(Trans);
                        }
                    }
                }
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Boundary analysis: Convergent=%d, Divergent=%d, Transform=%d"),
           ConvergentInteractions.Num(), DivergentInteractions.Num(), TransformInteractions.Num());
}

FVector UBoundaryInteractions::CalculateBoundaryNormal(const FCubeSphereCell& Cell, int32 PlateA, int32 PlateB) const
{
    if (!Grid || !PlateSystem)
    {
        return FVector::UpVector;
    }

    // Calcular el gradiente del campo de IDs de placa para obtener la normal
    FVector CellPos = Grid->CellToCartesian(Cell);
    FVector Normal = FVector::ZeroVector;

    TArray<FCubeSphereCell> Neighbors = Grid->GetNeighbors(Cell);

    for (const FCubeSphereCell& Neighbor : Neighbors)
    {
        int32 NeighborPlate = PlateSystem->GetPlateIDAt(Neighbor);
        FVector NeighborPos = Grid->CellToCartesian(Neighbor);

        FVector Direction = (NeighborPos - CellPos).GetSafeNormal();

        // Si el vecino pertenece a la otra placa, contribuye a la normal
        if (NeighborPlate == PlateB)
        {
            Normal += Direction;
        }
        else if (NeighborPlate == PlateA)
        {
            Normal -= Direction;
        }
    }

    // Proyectar la normal al plano tangente a la esfera
    FVector SurfaceNormal = CellPos.GetSafeNormal();
    Normal = Normal - SurfaceNormal * FVector::DotProduct(Normal, SurfaceNormal);

    return Normal.GetSafeNormal();
}

ESubductionType UBoundaryInteractions::DetermineSubductionType(int32 PlateAID, int32 PlateBID) const
{
    if (!PlateSystem)
    {
        return ESubductionType::None;
    }

    const TArray<FTectonicPlate>& Plates = PlateSystem->GetPlates();

    if (PlateAID < 0 || PlateAID >= Plates.Num() || PlateBID < 0 || PlateBID >= Plates.Num())
    {
        return ESubductionType::None;
    }

    const FTectonicPlate& PlateA = Plates[PlateAID];
    const FTectonicPlate& PlateB = Plates[PlateBID];

    // Continental vs Continental = Colisión (Himalaya-style)
    if (PlateA.CrustType == ECrustType::Continental && PlateB.CrustType == ECrustType::Continental)
    {
        return ESubductionType::ContinentalCollision;
    }

    // Oceánica vs Continental = Subducción oceánica
    if ((PlateA.CrustType == ECrustType::Oceanic && PlateB.CrustType == ECrustType::Continental) ||
        (PlateA.CrustType == ECrustType::Continental && PlateB.CrustType == ECrustType::Oceanic))
    {
        return ESubductionType::OceanicUnder;
    }

    // Oceánica vs Oceánica = La más antigua/densa subduce
    if (PlateA.CrustType == ECrustType::Oceanic && PlateB.CrustType == ECrustType::Oceanic)
    {
        return ESubductionType::OceanicOceanic;
    }

    // Casos mixtos
    return ESubductionType::OceanicUnder;
}

float UBoundaryInteractions::CalculateUpliftRate(const FConvergentInteraction& Interaction) const
{
    // Tasa de uplift basada en la velocidad de convergencia y tipo de interacción
    float BaseRate = Interaction.ConvergenceRate;

    if (Interaction.SubductionType == ESubductionType::ContinentalCollision)
    {
        // Colisión continental produce máxima orogenia (Himalaya)
        return BaseRate * OrogenicUpliftFactor;
    }
    else if (Interaction.SubductionType == ESubductionType::OceanicUnder)
    {
        // Subducción oceánica produce orogenia moderada (Andes)
        return BaseRate * SubductionUpliftFactor * 0.5f;
    }
    else if (Interaction.SubductionType == ESubductionType::OceanicOceanic)
    {
        // Subducción oceánica-oceánica produce arcos de islas
        return BaseRate * SubductionUpliftFactor * 0.3f;
    }

    return 0.0f;
}

float UBoundaryInteractions::CalculateSubductionAngle(int32 SubductingPlate, int32 OverridingPlate) const
{
    if (!PlateSystem)
    {
        return 45.0f;
    }

    const TArray<FTectonicPlate>& Plates = PlateSystem->GetPlates();

    if (SubductingPlate < 0 || SubductingPlate >= Plates.Num())
    {
        return 45.0f;
    }

    const FTectonicPlate& Subducting = Plates[SubductingPlate];

    // El ángulo de subducción depende de la edad/densidad de la placa que subduce
    // Corteza oceánica vieja/fría subduce más verticalmente
    // Corteza joven/caliente subduce más horizontalmente

    float BaseAngle = 45.0f;

    // Ajustar por edad (más vieja = más vertical)
    float AgeEffect = FMath::Clamp(Subducting.Age / 200.0f, 0.0f, 1.0f) * 20.0f;

    // Ajustar por densidad (más densa = más vertical)
    float DensityEffect = FMath::Clamp((Subducting.Density - 2800.0f) / 400.0f, 0.0f, 1.0f) * 15.0f;

    return FMath::Clamp(BaseAngle + AgeEffect + DensityEffect, 20.0f, 80.0f);
}

float UBoundaryInteractions::CalculateSpreadingRate(const FDivergentInteraction& Interaction) const
{
    // La tasa de creación de corteza es proporcional a la velocidad de spreading
    // y al grosor de la corteza que se crea
    float ThicknessCreated = 7.0f;  // km, corteza oceánica típica

    return Interaction.SpreadingRate * ThicknessCreated * 0.001f;  // km²/paso
}

void UBoundaryInteractions::ProcessConvergentBoundaries(float DeltaTime)
{
    if (!Grid)
    {
        return;
    }

    int32 Resolution = Grid->GetResolution();

    for (FConvergentInteraction& Conv : ConvergentInteractions)
    {
        // Actualizar profundidad de la placa subducida
        if (Conv.SubductingPlate >= 0)
        {
            Conv.SlabDepth += Conv.ConvergenceRate * FMath::Sin(FMath::DegreesToRadians(Conv.SubductionAngle)) * DeltaTime;
            Conv.SlabDepth = FMath::Clamp(Conv.SlabDepth, 0.0f, 700.0f);  // Max 700 km
        }

        // Acumular estrés
        Conv.AccumulatedStress += Conv.ConvergenceRate * DeltaTime * 0.1f;

        // Aplicar tasa de elevación al mapa
        int32 FaceIdx = static_cast<int32>(Conv.Cell.Face);
        int32 LinearIdx = Conv.Cell.V * Resolution + Conv.Cell.U;

        if (FaceIdx >= 0 && FaceIdx < 6 && LinearIdx >= 0 && LinearIdx < ElevationRateMaps[FaceIdx].Num())
        {
            ElevationRateMaps[FaceIdx][LinearIdx] += Conv.UpliftRate;
        }

        // Generar vulcanismo si hay subducción profunda
        if (Conv.SlabDepth > 100.0f && Conv.SubductionType != ESubductionType::ContinentalCollision)
        {
            GenerateSubductionVolcanism(Conv);
        }
    }
}

void UBoundaryInteractions::ProcessDivergentBoundaries(float DeltaTime)
{
    if (!Grid)
    {
        return;
    }

    int32 Resolution = Grid->GetResolution();

    for (FDivergentInteraction& Div : DivergentInteractions)
    {
        // Crear nueva corteza
        CreateNewCrust(Div.Cell, Div.SpreadingRate * DeltaTime);

        // En dorsales oceánicas, la elevación es positiva (dorsal elevada)
        // pero se hunde con la distancia (enfriamiento)
        int32 FaceIdx = static_cast<int32>(Div.Cell.Face);
        int32 LinearIdx = Div.Cell.V * Resolution + Div.Cell.U;

        if (FaceIdx >= 0 && FaceIdx < 6 && LinearIdx >= 0 && LinearIdx < ElevationRateMaps[FaceIdx].Num())
        {
            // Dorsales tienen elevación positiva respecto al fondo oceánico
            if (Div.bIsOceanicRidge)
            {
                // Pero para el modelo general, representamos la creación de corteza nueva
                ElevationRateMaps[FaceIdx][LinearIdx] += SpreadingDepthFactor * Div.SpreadingRate;
            }
            else
            {
                // Rift continental - depresión
                ElevationRateMaps[FaceIdx][LinearIdx] += SpreadingDepthFactor * Div.SpreadingRate * 2.0f;
            }
        }

        // Añadir actividad volcánica en dorsales
        if (Div.bIsOceanicRidge && FMath::FRand() < 0.1f)  // 10% chance por paso
        {
            FVolcanicActivity Volcano;
            Volcano.Cell = Div.Cell;
            Volcano.WorldPosition = Div.WorldPosition;
            Volcano.PlateID = Div.PlateA;  // Puede ser cualquiera
            Volcano.VolcanismType = EVolcanismType::MidOceanRidge;
            Volcano.Intensity = FMath::Clamp(Div.SpreadingRate / 10.0f, 0.0f, 1.0f);
            Volcano.MagmaFlux = Div.CrustCreationRate;
            Volcano.Temperature = 1200.0f;

            VolcanicActivities.Add(Volcano);
        }
    }
}

void UBoundaryInteractions::ProcessTransformBoundaries(float DeltaTime)
{
    for (FTransformInteraction& Trans : TransformInteractions)
    {
        // Acumular estrés por fricción
        Trans.AccumulatedStress += Trans.SlipRate * Trans.FrictionCoefficient * DeltaTime * 0.05f;

        // No hay cambio de elevación significativo en límites transformantes
        // (excepto pequeñas cuencas de tracción, que ignoramos por ahora)
    }
}

void UBoundaryInteractions::ProcessVolcanism(float DeltaTime)
{
    // Procesar hotspots
    ProcessHotspots(DeltaTime);

    // Actualizar volcanes existentes
    for (FVolcanicActivity& Volcano : VolcanicActivities)
    {
        Volcano.TimeSinceEruption += DeltaTime;

        // Construir edificio volcánico
        if (Volcano.Intensity > 0.1f)
        {
            Volcano.VolcanoHeight += VolcanicBuildupRate * Volcano.MagmaFlux * DeltaTime;
        }
    }
}

void UBoundaryInteractions::GenerateSubductionVolcanism(const FConvergentInteraction& Convergent)
{
    if (!Grid || !PlateSystem)
    {
        return;
    }

    // Vulcanismo sobre zona de subducción ocurre ~100-200 km sobre la placa subducida
    // Esto crea arcos volcánicos

    float DistanceFromTrench = 150.0f;  // km aproximado

    // Calcular posición del arco volcánico (sobre la placa cabalgante)
    // Simplificación: usar la misma celda pero en la placa overriding
    // En realidad debería ser desplazado, pero eso requiere geometría más compleja

    FVolcanicActivity Volcano;
    Volcano.Cell = Convergent.Cell;
    Volcano.WorldPosition = Convergent.WorldPosition;
    Volcano.PlateID = Convergent.OverridingPlate;
    Volcano.VolcanismType = EVolcanismType::SubductionArc;

    // Intensidad basada en la profundidad de la placa subducida y velocidad
    float DepthFactor = FMath::Clamp((Convergent.SlabDepth - 100.0f) / 200.0f, 0.0f, 1.0f);
    float SpeedFactor = FMath::Clamp(Convergent.ConvergenceRate / 10.0f, 0.0f, 1.0f);

    Volcano.Intensity = DepthFactor * SpeedFactor;
    Volcano.MagmaFlux = Volcano.Intensity * 0.1f;
    Volcano.Temperature = 1000.0f + Convergent.SlabDepth * 0.5f;

    // Solo añadir si es suficientemente intenso
    if (Volcano.Intensity > 0.2f)
    {
        VolcanicActivities.Add(Volcano);
    }
}

void UBoundaryInteractions::ProcessHotspots(float DeltaTime)
{
    if (!Grid || !PlateSystem)
    {
        return;
    }

    float PlanetRadius = Grid->GetRadius();
    int32 Resolution = Grid->GetResolution();
    const TArray<FTectonicPlate>& Plates = PlateSystem->GetPlates();

    for (FMantlePlume& Hotspot : Hotspots)
    {
        Hotspot.Age += DeltaTime;

        // Encontrar la celda sobre el hotspot
        FVector HotspotDir = Hotspot.FixedPosition.GetSafeNormal();

        // Convertir a celda (simplificado)
        FCubeSphereCell Cell = Grid->CartesianToCell(HotspotDir);
        int32 PlateID = PlateSystem->GetPlateIDAt(Cell);

        if (PlateID >= 0 && PlateID < Plates.Num())
        {
            FVolcanicActivity Volcano;
            Volcano.Cell = Cell;
            Volcano.WorldPosition = HotspotDir * PlanetRadius;
            Volcano.PlateID = PlateID;
            Volcano.VolcanismType = EVolcanismType::Hotspot;
            Volcano.Intensity = Hotspot.HeatFlux;
            Volcano.MagmaFlux = Hotspot.HeatFlux * 0.05f;
            Volcano.Temperature = 1300.0f;

            // Los hotspots construyen islas/montañas grandes (Hawaii, Yellowstone)
            VolcanicActivities.Add(Volcano);

            // Aplicar elevación
            int32 FaceIdx = static_cast<int32>(Cell.Face);
            int32 LinearIdx = Cell.V * Resolution + Cell.U;

            if (FaceIdx >= 0 && FaceIdx < 6 && LinearIdx >= 0 && LinearIdx < ElevationRateMaps[FaceIdx].Num())
            {
                ElevationRateMaps[FaceIdx][LinearIdx] += Hotspot.HeatFlux * VolcanicBuildupRate;
            }
        }
    }
}

void UBoundaryInteractions::CreateNewCrust(const FCubeSphereCell& Cell, float SpreadingRate)
{
    // En una implementación completa, esto modificaría las propiedades de la corteza
    // Por ahora, solo registramos que corteza nueva se está creando

    // La corteza nueva tiene:
    // - Edad = 0
    // - Tipo = Oceánica
    // - Densidad = baja (caliente)
    // - Grosor = típico oceánico (~7 km)

    // TODO: Implementar modificación de propiedades de corteza por celda
}

void UBoundaryInteractions::AddHotspot(const FVector& Position, float Radius, float HeatFlux)
{
    FMantlePlume Hotspot;
    Hotspot.FixedPosition = Position.GetSafeNormal();
    Hotspot.Radius = Radius;
    Hotspot.HeatFlux = HeatFlux;
    Hotspot.Age = 0.0f;

    Hotspots.Add(Hotspot);

    UE_LOG(LogTemp, Log, TEXT("Added hotspot at (%f, %f, %f), radius=%f, flux=%f"),
           Position.X, Position.Y, Position.Z, Radius, HeatFlux);
}

void UBoundaryInteractions::ApplyElevationChanges(float DeltaTime)
{
    // Los cambios de elevación se acumulan en ElevationRateMaps
    // El sistema que llama a ProcessAllBoundaries debe leer estos mapas
    // y aplicarlos a la capa de elevación

    // Por ahora, solo mantenemos los datos listos para ser leídos
}

TArray<float> UBoundaryInteractions::GetElevationRateMap(ECSCubeFace Face) const
{
    int32 FaceIdx = static_cast<int32>(Face);

    if (FaceIdx >= 0 && FaceIdx < 6)
    {
        return ElevationRateMaps[FaceIdx];
    }

    return TArray<float>();
}

FString UBoundaryInteractions::GetDebugInfo() const
{
    FString Info = TEXT("=== Boundary Interactions ===\n");
    Info += FString::Printf(TEXT("Convergent: %d\n"), ConvergentInteractions.Num());
    
    // Count subduction vs collision
    int32 WithSubduction = 0;
    int32 ContinentalCollisions = 0;
    for (const FConvergentInteraction& C : ConvergentInteractions)
    {
        if (C.SubductionType == ESubductionType::ContinentalCollision)
        {
            ContinentalCollisions++;
        }
        else
        {
            WithSubduction++;
        }
    }
    Info += FString::Printf(TEXT("  - With Subduction: %d\n"), WithSubduction);
    Info += FString::Printf(TEXT("  - Continental Collision: %d\n"), ContinentalCollisions);

    Info += FString::Printf(TEXT("Divergent: %d\n"), DivergentInteractions.Num());
    
    // Count oceanic ridges vs rifts
    int32 OceanicRidges = 0;
    int32 ContinentalRifts = 0;
    for (const FDivergentInteraction& D : DivergentInteractions)
    {
        if (D.bIsOceanicRidge)
        {
            OceanicRidges++;
        }
        else
        {
            ContinentalRifts++;
        }
    }
    Info += FString::Printf(TEXT("  - Oceanic Ridges: %d\n"), OceanicRidges);
    Info += FString::Printf(TEXT("  - Continental Rifts: %d\n"), ContinentalRifts);

    Info += FString::Printf(TEXT("Transform: %d\n"), TransformInteractions.Num());
    Info += FString::Printf(TEXT("Volcanic Activities: %d\n"), VolcanicActivities.Num());
    Info += FString::Printf(TEXT("Hotspots: %d\n"), Hotspots.Num());

    return Info;
}
