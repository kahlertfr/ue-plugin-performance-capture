#include "VRPerfCaptureScopedTimer.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "VRPerfCaptureSubsystem.h"

FVRPerfScopedCodeTimer::FVRPerfScopedCodeTimer(const UObject* WorldContextObject, FName InScopeName)
    : ScopeName(InScopeName)
{
    StartCycles = FPlatformTime::Cycles64();

    if (!GEngine || !WorldContextObject)
    {
        return;
    }

    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    if (!World)
    {
        return;
    }

    Subsystem = World->GetSubsystem<UVRPerfCaptureSubsystem>();
}

FVRPerfScopedCodeTimer::FVRPerfScopedCodeTimer(UVRPerfCaptureSubsystem* InSubsystem, FName InScopeName)
    : Subsystem(InSubsystem)
    , ScopeName(InScopeName)
{
    StartCycles = FPlatformTime::Cycles64();
}

FVRPerfScopedCodeTimer::~FVRPerfScopedCodeTimer()
{
    const uint64 EndCycles = FPlatformTime::Cycles64();
    const uint64 DeltaCycles = EndCycles - StartCycles;

    const double DurationMs =
        static_cast<double>(DeltaCycles) * FPlatformTime::GetSecondsPerCycle64() * 1000.0;

    if (UVRPerfCaptureSubsystem* StrongSubsystem = Subsystem.Get())
    {
        StrongSubsystem->RecordCodeScopeDuration(ScopeName, DurationMs);
    }
}