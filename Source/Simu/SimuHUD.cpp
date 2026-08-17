// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "SimuHUD.h"
#include "PlanetApproachPawn.h"
#include "Engine/Canvas.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "CubeSphere/Test/TectonicsTestActor.h"

void ASimuHUD::DrawScreenArrow(const FVector2D& Origin, const FVector2D& Dir, const FLinearColor& Color)
{
    if (!Canvas || Dir.IsNearlyZero())
    {
        return;
    }

    const FVector2D D = Dir.GetSafeNormal();
    const FVector2D Tip = Origin + D * CompassArrowLength;

    // Cuerpo
    Canvas->K2_DrawLine(Origin, Tip, CompassThickness, Color);

    // Punta: dos segmentos a +/-150 grados respecto a la direccion de avance
    const float HeadLength = CompassArrowLength * 0.3f;
    const FVector2D Perp(-D.Y, D.X);
    const FVector2D BackDir = -D * 0.866f;   // cos(30)

    Canvas->K2_DrawLine(Tip, Tip + (BackDir + Perp * 0.5f) * HeadLength, CompassThickness, Color);
    Canvas->K2_DrawLine(Tip, Tip + (BackDir - Perp * 0.5f) * HeadLength, CompassThickness, Color);
}

void ASimuHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!Canvas || !PlayerOwner)
    {
        return;
    }

    // Independiente del pawn y de sus condiciones de brujula (orbita, fuera de encuadre):
    // el texto de depuracion de tectonica se dibuja siempre que haya un actor que lo pida.
    DrawTectonicsDebugText();

    APlanetApproachPawn* Pawn = Cast<APlanetApproachPawn>(PlayerOwner->GetPawn());
    if (!Pawn || !Pawn->bShowPlanetCompass)
    {
        return;
    }

    AActor* Planet = Pawn->TargetPlanet;
    if (!Planet)
    {
        // Sin planeta que señalar, decirlo explicitamente: es un fallo de montaje del
        // nivel (falta el actor), y a escala planetaria es facil confundirlo con "el
        // planeta esta fuera de encuadre".
        DrawText(TEXT("Sin planeta en el nivel: coloca un TectonicsTestActor en (0,0,0)"),
            FLinearColor::Red, 40.0f, 40.0f);
        return;
    }

    // En orbita el planeta esta siempre centrado por construccion, asi que una flecha
    // que apunte a el no aporta nada y solo estorba la lectura de los campos.
    if (Pawn->bOrbitMode)
    {
        return;
    }

    const FVector PlanetCenter = Planet->GetActorLocation();

    const FVector ScreenPos = Project(PlanetCenter);
    const float ViewW = Canvas->ClipX;
    const float ViewH = Canvas->ClipY;
    const FVector2D ViewCenter(ViewW * 0.5f, ViewH * 0.5f);

    // Project() devuelve Z<0 cuando el punto queda detras de la camara, y en ese caso
    // X e Y no significan nada util (estan reflejados). Hay que tratarlo aparte.
    const bool bBehind = ScreenPos.Z <= 0.0f;
    const bool bOnScreen = !bBehind
        && ScreenPos.X >= 0.0f && ScreenPos.X <= ViewW
        && ScreenPos.Y >= 0.0f && ScreenPos.Y <= ViewH;

    if (bOnScreen)
    {
        // Visible: no hace falta brujula.
        return;
    }

    FVector2D Dir;
    if (bBehind)
    {
        // Detras: se invierte la proyeccion para obtener un sentido coherente en pantalla.
        Dir = FVector2D(ViewCenter.X - ScreenPos.X, ViewCenter.Y - ScreenPos.Y);
        if (Dir.IsNearlyZero())
        {
            Dir = FVector2D(0.0f, 1.0f);
        }
    }
    else
    {
        Dir = FVector2D(ScreenPos.X, ScreenPos.Y) - ViewCenter;
    }

    Dir = Dir.GetSafeNormal();

    // Anclada cerca del borde en la direccion del planeta, no en el centro: en el centro
    // taparia justo la zona que se esta mirando.
    const float Radius = FMath::Min(ViewW, ViewH) * 0.5f - CompassScreenMargin;
    const FVector2D Origin = ViewCenter + Dir * FMath::Max(Radius, 0.0f) - Dir * CompassArrowLength;

    DrawScreenArrow(Origin, Dir, FLinearColor::Red);

    const float DistanceKm = FVector::Dist(PlanetCenter, Pawn->GetActorLocation()) / 100000.0f;
    DrawText(FString::Printf(TEXT("Planeta a %.0f km"), DistanceKm),
        FLinearColor::Red, Origin.X, Origin.Y - 24.0f);
}

ATectonicsTestActor* ASimuHUD::FindTectonicsActor()
{
    if (CachedTectonicsActor)
    {
        return CachedTectonicsActor;
    }

    if (UWorld* World = GetWorld())
    {
        for (TActorIterator<ATectonicsTestActor> It(World); It; ++It)
        {
            CachedTectonicsActor = *It;
            break;
        }
    }

    return CachedTectonicsActor;
}

void ASimuHUD::DrawTectonicsDebugText()
{
    ATectonicsTestActor* Tectonics = FindTectonicsActor();
    if (!Tectonics)
    {
        return;
    }

    const float Margin = 12.0f;
    const float Scale = TectonicsTextScale;

    const FString& DebugText = Tectonics->GetHUDDebugText();
    if (!DebugText.IsEmpty())
    {
        DrawText(DebugText, FLinearColor::White, Margin, Margin, nullptr, Scale);
    }

    const FString& KeyLegendText = Tectonics->GetHUDKeyLegendText();
    if (!KeyLegendText.IsEmpty())
    {
        float TextW = 0.0f, TextH = 0.0f;
        GetTextSize(KeyLegendText, TextW, TextH, nullptr, Scale);
        DrawText(KeyLegendText, FLinearColor(0.65f, 0.85f, 1.0f), Canvas->ClipX - TextW - Margin, Margin, nullptr, Scale);
    }

    const FString& CostText = Tectonics->GetHUDCostText();
    if (!CostText.IsEmpty())
    {
        float TextW = 0.0f, TextH = 0.0f;
        GetTextSize(CostText, TextW, TextH, nullptr, Scale);
        DrawText(CostText, FLinearColor(1.0f, 0.9f, 0.4f),
            (Canvas->ClipX - TextW) * 0.5f, Canvas->ClipY - TextH - Margin, nullptr, Scale);
    }
}
