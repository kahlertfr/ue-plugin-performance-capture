#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "VRPerfCaptureTypes.h"
#include "VRPerfCaptureSubsystem.generated.h"

class UNiagaraComponent;

UCLASS(BlueprintType)
class VRPERFCAPTURE_API UVRPerfCaptureSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override;
    virtual ETickableTickType GetTickableTickType() const override;

    UFUNCTION(BlueprintCallable, Category="VR Performance")
    void StartCapture(
        const FString& CaptureName,
        float DurationSeconds = 0.0f,
        float SampleIntervalSeconds = 0.0f,
        bool bAutoDiscoverNiagaraComponents = true,
        bool bWriteCsvOnStop = true
    );

    UFUNCTION(BlueprintCallable, Category="VR Performance")
    FVRPerfCaptureSummary StopCapture();

    UFUNCTION(BlueprintCallable, Category="VR Performance")
    bool IsCapturing() const { return bCapturing; }

    UFUNCTION(BlueprintCallable, Category="VR Performance")
    void SetMarker(const FString& Marker);

    UFUNCTION(BlueprintCallable, Category="VR Performance")
    void ClearMarker();

    UFUNCTION(BlueprintCallable, Category="VR Performance")
    void RegisterNiagaraComponent(UNiagaraComponent* NiagaraComponent);

    UFUNCTION(BlueprintCallable, Category="VR Performance")
    void UnregisterNiagaraComponent(UNiagaraComponent* NiagaraComponent);

    UFUNCTION(BlueprintCallable, Category="VR Performance")
    void ClearRegisteredNiagaraComponents();

    UFUNCTION(BlueprintCallable, Category="VR Performance")
    const TArray<FVRPerfCaptureSample>& GetSamples() const { return Samples; }

    UFUNCTION(BlueprintCallable, Category="VR Performance")
    FVRPerfCaptureSummary GetCurrentSummary() const;

    UFUNCTION(BlueprintCallable, Category="VR Performance")
    FString SaveCsv();

    UFUNCTION(BlueprintCallable, Category="VR Performance")
    FVRPerfCaptureSample CaptureSingleSample(float DeltaTimeOverride = -1.0f);

    UFUNCTION(BlueprintCallable, Category="VR Performance|Code")
    int32 BeginCodeScope(FName ScopeName);

    UFUNCTION(BlueprintCallable, Category="VR Performance|Code")
    void EndCodeScope(int32 ScopeHandle);

    UFUNCTION(BlueprintCallable, Category="VR Performance|Code")
    void RecordCodeScopeDuration(FName ScopeName, double DurationMs);

private:
    struct FVRPerfManualScope
    {
        FName ScopeName = NAME_None;
        uint64 StartCycles = 0;
    };

    FVRPerfCaptureSample BuildSample(float DeltaTime);
    FVRPerfCaptureSummary BuildSummary(const FString& CsvPath = TEXT(""), const FString& CodeCsvPath = TEXT("")) const;

    void GatherNiagaraStats(
        int32& OutNiagaraComponentCount,
        int32& OutActiveNiagaraComponentCount,
        int32& OutEstimatedParticleCount
    ) const;

    TArray<FVRPerfCodeScopeStats> ConsumeCurrentCodeStats();

    FString SaveFrameCsv(const FString& BaseFilePath) const;
    FString SaveCodeCsv(const FString& BaseFilePath) const;

    static int32 TryGetNiagaraActiveParticleCount(UNiagaraComponent* Component);
    static FString EscapeCsv(const FString& In);
    static float GetConsoleVariableFloat(const TCHAR* Name, float DefaultValue);
    static int32 GetConsoleVariableInt(const TCHAR* Name, int32 DefaultValue);
    static double CyclesToMilliseconds(uint64 Cycles);

private:
    UPROPERTY()
    bool bCapturing = false;

    UPROPERTY()
    bool bAutoDiscoverNiagara = true;

    UPROPERTY()
    bool bSaveCsvWhenStopped = true;

    UPROPERTY()
    FString CurrentCaptureName;

    UPROPERTY()
    FString CurrentMarker;

    UPROPERTY()
    FString LastCsvPath;

    UPROPERTY()
    FString LastCodeCsvPath;

    UPROPERTY()
    float RequestedDurationSeconds = 0.0f;

    UPROPERTY()
    float RequestedSampleIntervalSeconds = 0.0f;

    UPROPERTY()
    float ElapsedCaptureSeconds = 0.0f;

    UPROPERTY()
    float SecondsSinceLastSample = 0.0f;

    UPROPERTY()
    TArray<TObjectPtr<UNiagaraComponent>> ExplicitNiagaraComponents;

    UPROPERTY()
    TArray<FVRPerfCaptureSample> Samples;

    double CaptureStartPlatformSeconds = 0.0;

    mutable FCriticalSection CodeStatsCriticalSection;
    TMap<FName, FVRPerfCodeScopeStats> CurrentCodeStats;

    FCriticalSection ManualScopeCriticalSection;
    int32 NextManualScopeHandle = 1;
    TMap<int32, FVRPerfManualScope> ActiveManualScopes;
};