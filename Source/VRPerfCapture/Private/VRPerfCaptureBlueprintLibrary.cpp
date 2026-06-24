#include "VRPerfCaptureBlueprintLibrary.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "VRPerfCaptureSubsystem.h"

UVRPerfCaptureSubsystem* UVRPerfCaptureBlueprintLibrary::GetVRPerfCaptureSubsystem(const UObject* WorldContextObject)
{
    if (!GEngine || !WorldContextObject)
    {
        return nullptr;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (!World)
    {
        return nullptr;
    }

    return World->GetSubsystem<UVRPerfCaptureSubsystem>();
}

void UVRPerfCaptureBlueprintLibrary::StartVRPerformanceCapture(
    const UObject* WorldContextObject,
    const FString& CaptureName,
    float DurationSeconds,
    float SampleIntervalSeconds,
    bool bAutoDiscoverNiagaraComponents,
    bool bWriteCsvOnStop
)
{
    if (UVRPerfCaptureSubsystem* Subsystem = GetVRPerfCaptureSubsystem(WorldContextObject))
    {
        Subsystem->StartCapture(
            CaptureName,
            DurationSeconds,
            SampleIntervalSeconds,
            bAutoDiscoverNiagaraComponents,
            bWriteCsvOnStop
        );
    }
}

FVRPerfCaptureSummary UVRPerfCaptureBlueprintLibrary::StopVRPerformanceCapture(const UObject* WorldContextObject)
{
    if (UVRPerfCaptureSubsystem* Subsystem = GetVRPerfCaptureSubsystem(WorldContextObject))
    {
        return Subsystem->StopCapture();
    }

    return FVRPerfCaptureSummary();
}

void UVRPerfCaptureBlueprintLibrary::SetVRPerformanceMarker(const UObject* WorldContextObject, const FString& Marker)
{
    if (UVRPerfCaptureSubsystem* Subsystem = GetVRPerfCaptureSubsystem(WorldContextObject))
    {
        Subsystem->SetMarker(Marker);
    }
}

void UVRPerfCaptureBlueprintLibrary::RegisterVRPerformanceNiagaraComponent(
    const UObject* WorldContextObject,
    UNiagaraComponent* NiagaraComponent
)
{
    if (UVRPerfCaptureSubsystem* Subsystem = GetVRPerfCaptureSubsystem(WorldContextObject))
    {
        Subsystem->RegisterNiagaraComponent(NiagaraComponent);
    }
}

FVRPerfCaptureSummary UVRPerfCaptureBlueprintLibrary::GetVRPerformanceSummary(const UObject* WorldContextObject)
{
    if (UVRPerfCaptureSubsystem* Subsystem = GetVRPerfCaptureSubsystem(WorldContextObject))
    {
        return Subsystem->GetCurrentSummary();
    }

    return FVRPerfCaptureSummary();
}

int32 UVRPerfCaptureBlueprintLibrary::BeginVRCodeScope(const UObject* WorldContextObject, FName ScopeName)
{
    if (UVRPerfCaptureSubsystem* Subsystem = GetVRPerfCaptureSubsystem(WorldContextObject))
    {
        return Subsystem->BeginCodeScope(ScopeName);
    }

    return INDEX_NONE;
}

void UVRPerfCaptureBlueprintLibrary::EndVRCodeScope(const UObject* WorldContextObject, int32 ScopeHandle)
{
    if (UVRPerfCaptureSubsystem* Subsystem = GetVRPerfCaptureSubsystem(WorldContextObject))
    {
        Subsystem->EndCodeScope(ScopeHandle);
    }
}

void UVRPerfCaptureBlueprintLibrary::RecordVRCodeDuration(
    const UObject* WorldContextObject,
    FName ScopeName,
    double DurationMs
)
{
    if (UVRPerfCaptureSubsystem* Subsystem = GetVRPerfCaptureSubsystem(WorldContextObject))
    {
        Subsystem->RecordCodeScopeDuration(ScopeName, DurationMs);
    }
}