#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VRPerfCaptureTypes.h"
#include "VRPerfCaptureBlueprintLibrary.generated.h"

class UVRPerfCaptureSubsystem;
class UNiagaraComponent;

UCLASS()
class VRPERFCAPTURE_API UVRPerfCaptureBlueprintLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category="VR Performance", meta=(WorldContext="WorldContextObject"))
    static UVRPerfCaptureSubsystem* GetVRPerfCaptureSubsystem(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category="VR Performance", meta=(WorldContext="WorldContextObject"))
    static void StartVRPerformanceCapture(
        const UObject* WorldContextObject,
        const FString& CaptureName,
        float DurationSeconds = 0.0f,
        float SampleIntervalSeconds = 0.0f,
        bool bAutoDiscoverNiagaraComponents = true,
        bool bWriteCsvOnStop = true
    );

    UFUNCTION(BlueprintCallable, Category="VR Performance", meta=(WorldContext="WorldContextObject"))
    static FVRPerfCaptureSummary StopVRPerformanceCapture(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category="VR Performance", meta=(WorldContext="WorldContextObject"))
    static void SetVRPerformanceMarker(const UObject* WorldContextObject, const FString& Marker);

    UFUNCTION(BlueprintCallable, Category="VR Performance", meta=(WorldContext="WorldContextObject"))
    static void RegisterVRPerformanceNiagaraComponent(const UObject* WorldContextObject, UNiagaraComponent* NiagaraComponent);

    UFUNCTION(BlueprintPure, Category="VR Performance", meta=(WorldContext="WorldContextObject"))
    static FVRPerfCaptureSummary GetVRPerformanceSummary(const UObject* WorldContextObject);

    UFUNCTION(BlueprintCallable, Category="VR Performance|Code", meta=(WorldContext="WorldContextObject"))
    static int32 BeginVRCodeScope(const UObject* WorldContextObject, FName ScopeName);

    UFUNCTION(BlueprintCallable, Category="VR Performance|Code", meta=(WorldContext="WorldContextObject"))
    static void EndVRCodeScope(const UObject* WorldContextObject, int32 ScopeHandle);

    UFUNCTION(BlueprintCallable, Category="VR Performance|Code", meta=(WorldContext="WorldContextObject"))
    static void RecordVRCodeDuration(const UObject* WorldContextObject, FName ScopeName, double DurationMs);
};