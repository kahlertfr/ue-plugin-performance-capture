#pragma once

#include "CoreMinimal.h"
#include "VRPerfCaptureTypes.generated.h"

USTRUCT(BlueprintType)
struct FVRPerfCodeScopeStats
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    FName ScopeName = NAME_None;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    int32 Calls = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    double TotalMs = 0.0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    double MinMs = 0.0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    double MaxMs = 0.0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    double AverageMs = 0.0;
};

USTRUCT(BlueprintType)
struct FVRPerfCaptureSample
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    double TimestampSeconds = 0.0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int64 FrameIndex = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float DeltaTimeMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float FPS = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    bool bIsHitch = false;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int32 ViewportWidth = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int32 ViewportHeight = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float ScreenPercentage = -1.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float VRPixelDensity = -1.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int32 DynamicResolutionMode = -1;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int32 VSync = -1;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float MaxFPS = -1.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    bool bHMDEnabled = false;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    FString HMDDeviceName;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    FVector HMDPosition = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    FRotator HMDOrientation = FRotator::ZeroRotator;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int32 ActorCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int32 PrimitiveComponentCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int32 NiagaraComponentCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int32 ActiveNiagaraComponentCount = 0;

    // -1 means unavailable for at least one sampled Niagara component.
    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int32 EstimatedActiveNiagaraParticles = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float UsedPhysicalMemoryMB = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float PeakUsedPhysicalMemoryMB = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    int32 CodeScopeCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    int32 CodeCallCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    double MeasuredCodeTotalMs = 0.0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    FName SlowestCodeScopeName = NAME_None;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    double SlowestCodeScopeMaxMs = 0.0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    TArray<FVRPerfCodeScopeStats> CodeScopes;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    FString Marker;
};

USTRUCT(BlueprintType)
struct FVRPerfCaptureSummary
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int32 NumSamples = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float DurationSeconds = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float AverageFPS = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float AverageFrameTimeMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float MedianFrameTimeMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float P95FrameTimeMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float P99FrameTimeMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float WorstFrameTimeMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float OnePercentLowFPS = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int32 HitchCount = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    float AverageEstimatedNiagaraParticles = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    int32 MaxEstimatedNiagaraParticles = 0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    double AverageMeasuredCodeMsPerSample = 0.0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    double MaxMeasuredCodeMsInSample = 0.0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    FName SlowestCodeScopeName = NAME_None;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    double SlowestCodeScopeMaxMs = 0.0;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance")
    FString CsvPath;

    UPROPERTY(BlueprintReadOnly, Category="VR Performance|Code")
    FString CodeCsvPath;
};