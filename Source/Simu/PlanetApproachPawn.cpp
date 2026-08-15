// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "PlanetApproachPawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "CubeSphere/Test/TectonicsTestActor.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"

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

    if (bShowPlanetCompass)
    {
        DrawPlanetCompass();
    }
}

void APlanetApproachPawn::DrawPlanetCompass() const
{
    if (!TargetPlanet || !Camera)
    {
        return;
    }

    const FVector CameraLocation = Camera->GetComponentLocation();
    const FVector CameraForward = Camera->GetForwardVector();
    const FVector ToPlanet = TargetPlanet->GetActorLocation() - CameraLocation;
    const float Distance = ToPlanet.Size();
    if (Distance < KINDA_SMALL_NUMBER)
    {
        return;
    }
    const FVector DirectionToPlanet = ToPlanet / Distance;

    // Flecha anclada a un punto fijo delante de la camara (no al planeta): asi
    // siempre esta en pantalla, gire la camara hacia donde gire, y su orientacion es
    // lo que indica hacia donde esta el planeta - como una aguja de brujula.
    const FVector ArrowStart = CameraLocation + CameraForward * 500.0f;
    const FVector ArrowEnd = ArrowStart + DirectionToPlanet * 300.0f;

    DrawDebugDirectionalArrow(GetWorld(), ArrowStart, ArrowEnd, 40.0f, FColor::Red, false, -1.0f, 0, 8.0f);

    if (GEngine)
    {
        const float DistanceKm = Distance / 100000.0f;
        GEngine->AddOnScreenDebugMessage(4270, 0.0f, FColor::Red,
            FString::Printf(TEXT("Planeta a %.1f km"), DistanceKm));
    }
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
    // ATectonicPlanetActor se borró el 15-08-2026 (ROADMAP.md F0): no simulaba relieve ni
    // dibujaba terreno, solo orquestaba el pipeline Nanite por parche que se abandonó.
    return UGameplayStatics::GetActorOfClass(this, ATectonicsTestActor::StaticClass());
}

float APlanetApproachPawn::GetTargetSurfaceRadius(const FVector& Direction) const
{
    if (ATectonicsTestActor* TestActor = Cast<ATectonicsTestActor>(TargetPlanet))
    {
        return TestActor->GetSurfaceRadiusAtDirection(Direction);
    }
    return 0.0f;
}
