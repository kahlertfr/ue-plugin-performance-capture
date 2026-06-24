#pragma once

#include "CoreMinimal.h"

class UVRPerfCaptureSubsystem;

class VRPERFCAPTURE_API FVRPerfScopedCodeTimer
{
public:
    FVRPerfScopedCodeTimer(const UObject* WorldContextObject, FName InScopeName);
    FVRPerfScopedCodeTimer(UVRPerfCaptureSubsystem* InSubsystem, FName InScopeName);
    ~FVRPerfScopedCodeTimer();

    FVRPerfScopedCodeTimer(const FVRPerfScopedCodeTimer&) = delete;
    FVRPerfScopedCodeTimer& operator=(const FVRPerfScopedCodeTimer&) = delete;

private:
    TWeakObjectPtr<UVRPerfCaptureSubsystem> Subsystem;
    FName ScopeName = NAME_None;
    uint64 StartCycles = 0;
};

#define VRPERF_CONCAT_INNER(A, B) A##B
#define VRPERF_CONCAT(A, B) VRPERF_CONCAT_INNER(A, B)

// Usage: VRPERF_SCOPE(this, "TrajectoryRetrieval/SearchIndex");
#define VRPERF_SCOPE(WorldContextObject, ScopeNameLiteral) \
    FVRPerfScopedCodeTimer VRPERF_CONCAT(VRPerfScopedTimer_, __LINE__)(WorldContextObject, FName(TEXT(ScopeNameLiteral)))

// Usage with dynamic FName: VRPERF_SCOPE_NAME(this, DynamicScopeName);
#define VRPERF_SCOPE_NAME(WorldContextObject, ScopeFName) \
    FVRPerfScopedCodeTimer VRPERF_CONCAT(VRPerfScopedTimer_, __LINE__)(WorldContextObject, ScopeFName)

// Usage if you already have the subsystem pointer.
#define VRPERF_SCOPE_SUBSYSTEM(SubsystemPointer, ScopeNameLiteral) \
    FVRPerfScopedCodeTimer VRPERF_CONCAT(VRPerfScopedTimer_, __LINE__)(SubsystemPointer, FName(TEXT(ScopeNameLiteral)))