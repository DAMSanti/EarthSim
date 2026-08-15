// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "PlanetApproachPawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "CubeSphere/Test/TectonicsTestActor.h"
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

    SyncOrbitStateFromTransform();
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

    EnsureMouseCaptured();
    HandleModeToggle();

    if (bOrbitMode && TargetPlanet)
    {
        HandleOrbit(DeltaTime);
    }
    else
    {
        HandleLook(DeltaTime);
        HandleMovement(DeltaTime);
    }

    ApplyHeightClamp();

    // La brujula se dibuja ahora en ASimuHUD, en 2D. Ver SimuHUD.h para el porque.
}

void APlanetApproachPawn::EnsureMouseCaptured()
{
    if (bInputModeConfigured)
    {
        return;
    }

    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC)
    {
        // La posesion puede completarse despues de BeginPlay; se reintenta cada frame
        // hasta conseguirlo en vez de darlo por perdido.
        return;
    }

    FInputModeGameOnly InputMode;
    InputMode.SetConsumeCaptureMouseDown(true);
    PC->SetInputMode(InputMode);
    PC->SetShowMouseCursor(false);

    bInputModeConfigured = true;

    UE_LOG(LogTemp, Log, TEXT("PlanetApproachPawn: raton capturado (modo de entrada = solo juego)"));
}

void APlanetApproachPawn::SyncOrbitStateFromTransform()
{
    if (!TargetPlanet)
    {
        bOrbitStateValid = false;
        return;
    }

    const FVector ToPawn = GetActorLocation() - TargetPlanet->GetActorLocation();
    OrbitDistance = ToPawn.Size();

    if (OrbitDistance < KINDA_SMALL_NUMBER)
    {
        // Degenerado (camara en el centro): se elige una posicion arbitraria pero valida
        // en vez de dejar angulos indefinidos.
        OrbitDistance = FMath::Max(GetTargetSurfaceRadius(FVector::UpVector) * 2.0, 1.0);
        OrbitLongitudeDeg = 0.0;
        OrbitLatitudeDeg = 0.0;
    }
    else
    {
        const FVector Dir = ToPawn / OrbitDistance;
        OrbitLatitudeDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Dir.Z, -1.0, 1.0)));
        OrbitLongitudeDeg = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
    }

    bOrbitStateValid = true;
}

void APlanetApproachPawn::HandleModeToggle()
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC || !PC->InputEnabled())
    {
        return;
    }

    if (!PC->WasInputKeyJustPressed(EKeys::O))
    {
        return;
    }

    bOrbitMode = !bOrbitMode;

    // Al entrar en orbita hay que leer la posicion actual, o la camara saltaria al
    // ultimo punto orbital conocido. Al salir no hace falta nada: el modo libre parte
    // de la transform, que ya es la correcta.
    if (bOrbitMode)
    {
        SyncOrbitStateFromTransform();
    }

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
            bOrbitMode ? TEXT("Camara: ORBITA (raton gira, W/S o rueda acerca)")
                       : TEXT("Camara: LIBRE (WASD + QE, raton mira)"));
    }
}

void APlanetApproachPawn::HandleOrbit(float DeltaTime)
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!PC || !PC->InputEnabled())
    {
        return;
    }

    if (!bOrbitStateValid)
    {
        SyncOrbitStateFromTransform();
    }

    const FVector Center = TargetPlanet->GetActorLocation();

    // --- Giro ---------------------------------------------------------------
    float DX = 0.0f, DY = 0.0f;
    PC->GetInputMouseDelta(DX, DY);

    // A/D y flechas como alternativa al raton, util para giros finos y para grabar
    // recorridos repetibles.
    float KeyboardYaw = 0.0f;
    float KeyboardPitch = 0.0f;
    if (PC->IsInputKeyDown(EKeys::A) || PC->IsInputKeyDown(EKeys::Left))  KeyboardYaw -= 1.0f;
    if (PC->IsInputKeyDown(EKeys::D) || PC->IsInputKeyDown(EKeys::Right)) KeyboardYaw += 1.0f;
    if (PC->IsInputKeyDown(EKeys::Up))    KeyboardPitch += 1.0f;
    if (PC->IsInputKeyDown(EKeys::Down))  KeyboardPitch -= 1.0f;

    const float KeyboardOrbitDegPerSec = 45.0f;
    OrbitLongitudeDeg += DX * OrbitSensitivity + KeyboardYaw * KeyboardOrbitDegPerSec * DeltaTime;
    OrbitLatitudeDeg  += DY * OrbitSensitivity + KeyboardPitch * KeyboardOrbitDegPerSec * DeltaTime;

    // Se recorta en vez de envolver: al pasar por el polo, la longitud se invierte de
    // golpe y la camara da un tirón desconcertante. 89 grados deja ver el polo de sobra.
    OrbitLatitudeDeg = FMath::Clamp(OrbitLatitudeDeg, -89.0, 89.0);
    OrbitLongitudeDeg = FMath::Fmod(OrbitLongitudeDeg, 360.0);

    // --- Acercar / alejar ---------------------------------------------------
    const FVector CurrentDir = GetActorLocation() - Center;
    const float SurfaceRadius = GetTargetSurfaceRadius(
        CurrentDir.IsNearlyZero() ? FVector::UpVector : CurrentDir.GetSafeNormal());

    float ZoomInput = 0.0f;
    if (PC->IsInputKeyDown(EKeys::W) || PC->IsInputKeyDown(EKeys::E)) ZoomInput -= 1.0f;
    if (PC->IsInputKeyDown(EKeys::S) || PC->IsInputKeyDown(EKeys::Q)) ZoomInput += 1.0f;

    // La rueda da un paso discreto equivalente a ~0.3 s de zoom continuo.
    float WheelSteps = 0.0f;
    if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp))   WheelSteps -= 1.0f;
    if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown)) WheelSteps += 1.0f;

    if (!FMath::IsNearlyZero(ZoomInput) || !FMath::IsNearlyZero(WheelSteps))
    {
        const double Altitude = FMath::Max(OrbitDistance - SurfaceRadius, static_cast<double>(SurfaceClearance));
        const double Delta = Altitude * OrbitZoomRate * (ZoomInput * DeltaTime + WheelSteps * 0.3);
        OrbitDistance += Delta;
    }

    OrbitDistance = FMath::Max(OrbitDistance, static_cast<double>(SurfaceRadius + SurfaceClearance));

    // --- Recolocar ----------------------------------------------------------
    const double LatRad = FMath::DegreesToRadians(OrbitLatitudeDeg);
    const double LonRad = FMath::DegreesToRadians(OrbitLongitudeDeg);
    const double CosLat = FMath::Cos(LatRad);

    const FVector Dir(
        CosLat * FMath::Cos(LonRad),
        CosLat * FMath::Sin(LonRad),
        FMath::Sin(LatRad));

    SetActorLocation(Center + Dir * OrbitDistance);

    // Mirar siempre al centro. Se fija la rotacion en absoluto en vez de acumular
    // giros: acumular introduce roll y la camara acaba escorada.
    SetActorRotation((-Dir).Rotation());
    if (Camera)
    {
        Camera->SetRelativeRotation(FRotator::ZeroRotator);
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

        // En orbita, la distancia es estado propio: si no se resincroniza aqui, el
        // siguiente frame volveria a colocar la camara donde ya sabemos que no cabe, y
        // se quedaria vibrando contra el suelo. HandleOrbit ya recorta con el radio de
        // la direccion ANTERIOR, pero al girar se puede entrar en una zona mas alta.
        if (bOrbitMode)
        {
            OrbitDistance = MinAllowedDistance;
        }
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
