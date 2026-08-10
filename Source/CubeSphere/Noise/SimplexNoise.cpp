// Copyright (c) 2024 Simu Project. All Rights Reserved.

#include "SimplexNoise.h"

// ============================================================
// STATIC MEMBER INITIALIZATION
// ============================================================

bool FSimplexNoise::bInitialized = false;
int32 FSimplexNoise::Perm[512];
int32 FSimplexNoise::Perm12[512];

// Gradientes 3D - 12 direcciones hacia los bordes de un cubo
const float FSimplexNoise::Grad3[12][3] = {
    {1,1,0}, {-1,1,0}, {1,-1,0}, {-1,-1,0},
    {1,0,1}, {-1,0,1}, {1,0,-1}, {-1,0,-1},
    {0,1,1}, {0,-1,1}, {0,1,-1}, {0,-1,-1}
};

// ============================================================
// INITIALIZATION
// ============================================================

void FSimplexNoise::Initialize(int32 Seed)
{
    // Generar permutación base
    TArray<int32> P;
    P.SetNum(256);
    for (int32 i = 0; i < 256; ++i)
    {
        P[i] = i;
    }
    
    // Shuffle con la semilla
    FRandomStream RandStream(Seed);
    for (int32 i = 255; i > 0; --i)
    {
        int32 j = RandStream.RandRange(0, i);
        Swap(P[i], P[j]);
    }
    
    // Duplicar para evitar buffer overflow
    for (int32 i = 0; i < 512; ++i)
    {
        Perm[i] = P[i & 255];
        Perm12[i] = Perm[i] % 12;
    }
    
    bInitialized = true;
}

// ============================================================
// HELPERS
// ============================================================

int32 FSimplexNoise::FastFloor(float X)
{
    int32 Xi = static_cast<int32>(X);
    return X < Xi ? Xi - 1 : Xi;
}

float FSimplexNoise::Dot(const float* G, float X, float Y, float Z)
{
    return G[0] * X + G[1] * Y + G[2] * Z;
}

// ============================================================
// SIMPLEX NOISE 3D
// ============================================================

float FSimplexNoise::Noise3D(float X, float Y, float Z)
{
    if (!bInitialized)
    {
        Initialize(12345);
    }

    // Skewing/Unskewing factors for 3D
    const float F3 = 1.0f / 3.0f;
    const float G3 = 1.0f / 6.0f;

    // Skew the input space to determine which simplex cell we're in
    float S = (X + Y + Z) * F3;
    int32 I = FastFloor(X + S);
    int32 J = FastFloor(Y + S);
    int32 K = FastFloor(Z + S);

    float T = (I + J + K) * G3;
    float X0 = X - (I - T);
    float Y0 = Y - (J - T);
    float Z0 = Z - (K - T);

    // Determine which simplex we're in
    int32 I1, J1, K1, I2, J2, K2;
    
    if (X0 >= Y0)
    {
        if (Y0 >= Z0)
        {
            I1 = 1; J1 = 0; K1 = 0;
            I2 = 1; J2 = 1; K2 = 0;
        }
        else if (X0 >= Z0)
        {
            I1 = 1; J1 = 0; K1 = 0;
            I2 = 1; J2 = 0; K2 = 1;
        }
        else
        {
            I1 = 0; J1 = 0; K1 = 1;
            I2 = 1; J2 = 0; K2 = 1;
        }
    }
    else
    {
        if (Y0 < Z0)
        {
            I1 = 0; J1 = 0; K1 = 1;
            I2 = 0; J2 = 1; K2 = 1;
        }
        else if (X0 < Z0)
        {
            I1 = 0; J1 = 1; K1 = 0;
            I2 = 0; J2 = 1; K2 = 1;
        }
        else
        {
            I1 = 0; J1 = 1; K1 = 0;
            I2 = 1; J2 = 1; K2 = 0;
        }
    }

    // Offsets for remaining corners
    float X1 = X0 - I1 + G3;
    float Y1 = Y0 - J1 + G3;
    float Z1 = Z0 - K1 + G3;
    float X2 = X0 - I2 + 2.0f * G3;
    float Y2 = Y0 - J2 + 2.0f * G3;
    float Z2 = Z0 - K2 + 2.0f * G3;
    float X3 = X0 - 1.0f + 3.0f * G3;
    float Y3 = Y0 - 1.0f + 3.0f * G3;
    float Z3 = Z0 - 1.0f + 3.0f * G3;

    // Hash coordinates for gradient indices
    int32 II = I & 255;
    int32 JJ = J & 255;
    int32 KK = K & 255;

    // Calculate contributions from each corner
    float N0, N1, N2, N3;

    float T0 = 0.6f - X0*X0 - Y0*Y0 - Z0*Z0;
    if (T0 < 0)
    {
        N0 = 0.0f;
    }
    else
    {
        T0 *= T0;
        int32 Gi0 = Perm12[II + Perm[JJ + Perm[KK]]];
        N0 = T0 * T0 * Dot(Grad3[Gi0], X0, Y0, Z0);
    }

    float T1 = 0.6f - X1*X1 - Y1*Y1 - Z1*Z1;
    if (T1 < 0)
    {
        N1 = 0.0f;
    }
    else
    {
        T1 *= T1;
        int32 Gi1 = Perm12[II + I1 + Perm[JJ + J1 + Perm[KK + K1]]];
        N1 = T1 * T1 * Dot(Grad3[Gi1], X1, Y1, Z1);
    }

    float T2 = 0.6f - X2*X2 - Y2*Y2 - Z2*Z2;
    if (T2 < 0)
    {
        N2 = 0.0f;
    }
    else
    {
        T2 *= T2;
        int32 Gi2 = Perm12[II + I2 + Perm[JJ + J2 + Perm[KK + K2]]];
        N2 = T2 * T2 * Dot(Grad3[Gi2], X2, Y2, Z2);
    }

    float T3 = 0.6f - X3*X3 - Y3*Y3 - Z3*Z3;
    if (T3 < 0)
    {
        N3 = 0.0f;
    }
    else
    {
        T3 *= T3;
        int32 Gi3 = Perm12[II + 1 + Perm[JJ + 1 + Perm[KK + 1]]];
        N3 = T3 * T3 * Dot(Grad3[Gi3], X3, Y3, Z3);
    }

    // Scale to [-1, 1]
    return 32.0f * (N0 + N1 + N2 + N3);
}

// ============================================================
// FRACTAL BROWNIAN MOTION (fBm)
// ============================================================

float FSimplexNoise::FractalNoise3D(float X, float Y, float Z, 
                                     int32 Octaves, 
                                     float Lacunarity, 
                                     float Persistence)
{
    float Total = 0.0f;
    float Frequency = 1.0f;
    float Amplitude = 1.0f;
    float MaxValue = 0.0f;

    for (int32 i = 0; i < Octaves; ++i)
    {
        Total += Noise3D(X * Frequency, Y * Frequency, Z * Frequency) * Amplitude;
        MaxValue += Amplitude;
        Amplitude *= Persistence;
        Frequency *= Lacunarity;
    }

    return Total / MaxValue;
}

// ============================================================
// SPHERE FRACTAL NOISE
// ============================================================

float FSimplexNoise::SphereFractalNoise(const FVector& Direction,
                                         float Frequency,
                                         int32 Octaves,
                                         float Lacunarity,
                                         float Persistence)
{
    // Usar la dirección normalizada * frecuencia como coordenadas 3D
    // Esto garantiza continuidad perfecta en toda la esfera
    FVector Pos = Direction.GetSafeNormal() * Frequency;
    return FractalNoise3D(Pos.X, Pos.Y, Pos.Z, Octaves, Lacunarity, Persistence);
}

// ============================================================
// RIDGED MULTIFRACTAL
// ============================================================

float FSimplexNoise::RidgedNoise3D(float X, float Y, float Z,
                                    int32 Octaves,
                                    float Lacunarity,
                                    float Gain)
{
    float Total = 0.0f;
    float Frequency = 1.0f;
    float Amplitude = 0.5f;
    float Weight = 1.0f;
    float MaxValue = 0.0f;

    for (int32 i = 0; i < Octaves; ++i)
    {
        // Valor absoluto crea "ridges" (crestas)
        float Signal = Noise3D(X * Frequency, Y * Frequency, Z * Frequency);
        Signal = 1.0f - FMath::Abs(Signal);
        Signal *= Signal; // Cuadrado para acentuar picos
        Signal *= Weight;
        
        // Weight reduce octavas en valles
        Weight = FMath::Clamp(Signal * Gain, 0.0f, 1.0f);
        
        Total += Signal * Amplitude;
        MaxValue += Amplitude;
        
        Frequency *= Lacunarity;
        Amplitude *= 0.5f;
    }

    return Total / MaxValue;
}

// ============================================================
// DOMAIN WARPING
// ============================================================

FVector FSimplexNoise::WarpDomain(const FVector& Position, float WarpStrength)
{
    // Usar ruido para distorsionar las coordenadas
    // Esto crea formas más orgánicas
    float WarpX = FractalNoise3D(Position.X + 100.0f, Position.Y, Position.Z, 4, 2.0f, 0.5f);
    float WarpY = FractalNoise3D(Position.X, Position.Y + 100.0f, Position.Z, 4, 2.0f, 0.5f);
    float WarpZ = FractalNoise3D(Position.X, Position.Y, Position.Z + 100.0f, 4, 2.0f, 0.5f);
    
    return Position + FVector(WarpX, WarpY, WarpZ) * WarpStrength;
}
