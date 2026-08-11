// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "PlanetApproachPawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "CubeSphere/Test/TectonicsTestActor.h"
#include "CubeSphere/Tectonics/TectonicPlanetActor.h"

APlanetApproachPawn::APlanetApproachPawn()
{
    PrimaryActorTick.bCanEverTick = true;

    PawnRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PawnRoot"));
    RootComponent = PawnRoot;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(PawnRoot);

    AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void APlanetApproachPawn::BeginPlay()
{
    Super::BeginPlay();

    if (!TargetPlanet)
    {
        TargetPlanet = FindTargetPlanet();
    }

    CurrentSpeed = MinSpeed;
}

void APlanetApproachPawn::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!TargetPlanet)
    {
        TargetPlanet = FindTargetPlanet();
    }

    if (TargetPlanet)
    {
        const FVector ToPawn = GetActorLocation() - TargetPlanet->GetActorLocation();
        const float DistanceFromCenter = FMath::Max(ToPawn.Size(), 1.0f);
        const FVector Direction = ToPawn / DistanceFromCenter;

        const float SurfaceRadius = GetTargetSurfaceRadius(Direction);
        const float Altitude = FMath::Max(DistanceFromCenter - SurfaceRadius, 0.0f);

        // Interpolacion logaritmica: la altitud varia en varios ordenes de magnitud
        // (metros a miles de km), una interpolacion lineal dejaria la mayoria del
        // rango util comprimido en un puñado de frames de movimiento.
        const float LogAlt = FMath::LogX(10.0f, FMath::Max(Altitude, 1.0f));
        const float LogMin = FMath::LogX(10.0f, FMath::Max(SurfaceClearance, 1.0f));
        const float LogMax = FMath::LogX(10.0f, FMath::Max(MaxSpeedAltitude, SurfaceClearance + 1.0f));
        const float T = FMath::Clamp((LogAlt - LogMin) / FMath::Max(LogMax - LogMin, KINDA_SMALL_NUMBER), 0.0f, 1.0f);
        CurrentSpeed = FMath::Lerp(MinSpeed, MaxSpeed, T);
    }
    else
    {
        CurrentSpeed = MinSpeed;
    }

    HandleLook(DeltaTime);
    HandleMovement(DeltaTime);
    ApplyHeightClamp();
}

void APlanetApproachPawn::HandleLook(float DeltaTime)
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC || !PC->InputEnabled())
    {
        return;
    }

    float DX = 0.0f, DY = 0.0f;
    PC->GetInputMouseDelta(DX, DY);

    if (!FMath::IsNearlyZero(DX))
    {
        AddActorLocalRotation(FRotator(0.0f, DX * MouseLookSpeed, 0.0f));
    }

    if (!FMath::IsNearlyZero(DY) && Camera)
    {
        FRotator CamRot = Camera->GetRelativeRotation();
        CamRot.Pitch = FMath::Clamp(CamRot.Pitch - DY * MouseLookSpeed, -89.0f, 89.0f);
        Camera->SetRelativeRotation(CamRot);
    }
}

void APlanetApproachPawn::HandleMovement(float DeltaTime)
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC || !PC->InputEnabled())
    {
        return;
    }

    FVector MoveDir = FVector::ZeroVector;
    if (PC->IsInputKeyDown(EKeys::W)) MoveDir += GetActorForwardVector();
    if (PC->IsInputKeyDown(EKeys::S)) MoveDir -= GetActorForwardVector();
    if (PC->IsInputKeyDown(EKeys::D)) MoveDir += GetActorRightVector();
    if (PC->IsInputKeyDown(EKeys::A)) MoveDir -= GetActorRightVector();
    if (PC->IsInputKeyDown(EKeys::E)) MoveDir += GetActorUpVector();
    if (PC->IsInputKeyDown(EKeys::Q)) MoveDir -= GetActorUpVector();

    if (!MoveDir.IsNearlyZero())
    {
        MoveDir.Normalize();
        AddActorWorldOffset(MoveDir * CurrentSpeed * DeltaTime, false);
    }
}

void APlanetApproachPawn::ApplyHeightClamp()
{
    if (!TargetPlanet)
    {
        return;
    }

    const FVector ToPawn = GetActorLocation() - TargetPlanet->GetActorLocation();
    const float DistanceFromCenter = ToPawn.Size();
    if (DistanceFromCenter < KINDA_SMALL_NUMBER)
    {
        return;
    }

    const FVector Direction = ToPawn / DistanceFromCenter;
    const float SurfaceRadius = GetTargetSurfaceRadius(Direction);
    const float MinAllowedDistance = SurfaceRadius + SurfaceClearance;

    if (DistanceFromCenter < MinAllowedDistance)
    {
        SetActorLocation(TargetPlanet->GetActorLocation() + Direction * MinAllowedDistance);
    }
}

AActor* APlanetApproachPawn::FindTargetPlanet() const
{
    if (AActor* Found = UGameplayStatics::GetActorOfClass(this, ATectonicsTestActor::StaticClass()))
    {
        return Found;
    }
    return UGameplayStatics::GetActorOfClass(this, ATectonicPlanetActor::StaticClass());
}

float APlanetApproachPawn::GetTargetSurfaceRadius(const FVector& Direction) const
{
    if (ATectonicsTestActor* TestActor = Cast<ATectonicsTestActor>(TargetPlanet))
    {
        return TestActor->GetSurfaceRadiusAtDirection(Direction);
    }
    if (ATectonicPlanetActor* PlanetActor = Cast<ATectonicPlanetActor>(TargetPlanet))
    {
        // Sin datos de elevacion por direccion en esta clase - aproximacion esferica.
        // Razonable: el desplazamiento Nanite es pequeño relativo al radio del planeta.
        return PlanetActor->PlanetRadius;
    }
    return 0.0f;
}
