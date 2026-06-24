#include "VRPerfCaptureSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GenericPlatform/GenericPlatformMemory.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFilemanager.h"
#include "IXRTrackingSystem.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "Stats/Stats.h"

static FString GetVRPerfCaptureOutputDirectory()
{
    // In packaged builds this is usually the packaged project's Saved directory.
    FString Directory = FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("Profiling"),
        TEXT("VRPerfCapture")
    );

    IFileManager::Get().MakeDirectory(*Directory, true);

    // Fallback for cases where the packaged location is not writable.
    if (!FPaths::DirectoryExists(Directory))
    {
        Directory = FPaths::Combine(
            FPlatformProcess::UserDir(),
            TEXT("VRPerfCapture"),
            FApp::GetProjectName()
        );

        IFileManager::Get().MakeDirectory(*Directory, true);
    }

    return Directory;
}

static void WriteVRPerfCaptureStatus(const FString& Message)
{
    const FString Directory = GetVRPerfCaptureOutputDirectory();
    const FString StatusPath = FPaths::Combine(Directory, TEXT("VRPerfCapture_Status.txt"));

    const FString Line = FString::Printf(
        TEXT("[%s] %s\n"),
        *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")),
        *Message
    );

    FFileHelper::SaveStringToFile(
        Line,
        *StatusPath,
        FFileHelper::EEncodingOptions::AutoDetect,
        &IFileManager::Get(),
        FILEWRITE_Append
    );
}

void UVRPerfCaptureSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UVRPerfCaptureSubsystem::Deinitialize()
{
    if (bCapturing)
    {
        StopCapture();
    }

    Samples.Reset();
    ExplicitNiagaraComponents.Reset();
    CurrentCodeStats.Reset();
    ActiveManualScopes.Reset();

    Super::Deinitialize();
}

TStatId UVRPerfCaptureSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UVRPerfCaptureSubsystem, STATGROUP_Tickables);
}

bool UVRPerfCaptureSubsystem::IsTickable() const
{
    return !HasAnyFlags(RF_ClassDefaultObject);
}

ETickableTickType UVRPerfCaptureSubsystem::GetTickableTickType() const
{
    return ETickableTickType::Conditional;
}

void UVRPerfCaptureSubsystem::Tick(float DeltaTime)
{
    if (!bCapturing)
    {
        return;
    }

    ElapsedCaptureSeconds += DeltaTime;
    SecondsSinceLastSample += DeltaTime;

    const bool bSampleEveryFrame = RequestedSampleIntervalSeconds <= 0.0f;
    const bool bTimeForSample = bSampleEveryFrame || SecondsSinceLastSample >= RequestedSampleIntervalSeconds;

    if (bTimeForSample)
    {
        Samples.Add(BuildSample(DeltaTime));
        SecondsSinceLastSample = 0.0f;
    }

    if (RequestedDurationSeconds > 0.0f && ElapsedCaptureSeconds >= RequestedDurationSeconds)
    {
        StopCapture();
    }
}

void UVRPerfCaptureSubsystem::StartCapture(
    const FString& CaptureName,
    float DurationSeconds,
    float SampleIntervalSeconds,
    bool bAutoDiscoverNiagaraComponents,
    bool bWriteCsvOnStop
)
{
    Samples.Reset();
    CurrentCodeStats.Reset();
    ActiveManualScopes.Reset();

    bCapturing = true;
    bAutoDiscoverNiagara = bAutoDiscoverNiagaraComponents;
    bSaveCsvWhenStopped = bWriteCsvOnStop;

    CurrentCaptureName = CaptureName.IsEmpty() ? TEXT("VRPerfCapture") : CaptureName;
    CurrentMarker.Empty();
    LastCsvPath.Empty();
    LastCodeCsvPath.Empty();

    RequestedDurationSeconds = FMath::Max(0.0f, DurationSeconds);
    RequestedSampleIntervalSeconds = FMath::Max(0.0f, SampleIntervalSeconds);

    ElapsedCaptureSeconds = 0.0f;
    SecondsSinceLastSample = 0.0f;
    CaptureStartPlatformSeconds = FPlatformTime::Seconds();
}

FVRPerfCaptureSummary UVRPerfCaptureSubsystem::StopCapture()
{
    if (!bCapturing)
    {
        return BuildSummary(LastCsvPath, LastCodeCsvPath);
    }

    bCapturing = false;

    if (bSaveCsvWhenStopped)
    {
        LastCsvPath = SaveCsv();
    }

    return BuildSummary(LastCsvPath, LastCodeCsvPath);
}

void UVRPerfCaptureSubsystem::SetMarker(const FString& Marker)
{
    CurrentMarker = Marker;
}

void UVRPerfCaptureSubsystem::ClearMarker()
{
    CurrentMarker.Empty();
}

void UVRPerfCaptureSubsystem::RegisterNiagaraComponent(UNiagaraComponent* NiagaraComponent)
{
    if (!IsValid(NiagaraComponent))
    {
        return;
    }

    ExplicitNiagaraComponents.AddUnique(NiagaraComponent);
}

void UVRPerfCaptureSubsystem::UnregisterNiagaraComponent(UNiagaraComponent* NiagaraComponent)
{
    ExplicitNiagaraComponents.Remove(NiagaraComponent);
}

void UVRPerfCaptureSubsystem::ClearRegisteredNiagaraComponents()
{
    ExplicitNiagaraComponents.Reset();
}

int32 UVRPerfCaptureSubsystem::BeginCodeScope(FName ScopeName)
{
    if (ScopeName.IsNone())
    {
        return INDEX_NONE;
    }

    FScopeLock Lock(&ManualScopeCriticalSection);

    const int32 Handle = NextManualScopeHandle++;
    FVRPerfManualScope ManualScope;
    ManualScope.ScopeName = ScopeName;
    ManualScope.StartCycles = FPlatformTime::Cycles64();

    ActiveManualScopes.Add(Handle, ManualScope);
    return Handle;
}

void UVRPerfCaptureSubsystem::EndCodeScope(int32 ScopeHandle)
{
    if (ScopeHandle == INDEX_NONE)
    {
        return;
    }

    FVRPerfManualScope ManualScope;
    bool bFound = false;

    {
        FScopeLock Lock(&ManualScopeCriticalSection);

        if (FVRPerfManualScope* Existing = ActiveManualScopes.Find(ScopeHandle))
        {
            ManualScope = *Existing;
            ActiveManualScopes.Remove(ScopeHandle);
            bFound = true;
        }
    }

    if (!bFound)
    {
        return;
    }

    const uint64 EndCycles = FPlatformTime::Cycles64();
    const double DurationMs = CyclesToMilliseconds(EndCycles - ManualScope.StartCycles);

    RecordCodeScopeDuration(ManualScope.ScopeName, DurationMs);
}

void UVRPerfCaptureSubsystem::RecordCodeScopeDuration(FName ScopeName, double DurationMs)
{
    if (!bCapturing || ScopeName.IsNone() || DurationMs < 0.0)
    {
        return;
    }

    FScopeLock Lock(&CodeStatsCriticalSection);

    FVRPerfCodeScopeStats& Stats = CurrentCodeStats.FindOrAdd(ScopeName);

    if (Stats.Calls == 0)
    {
        Stats.ScopeName = ScopeName;
        Stats.MinMs = DurationMs;
        Stats.MaxMs = DurationMs;
        Stats.TotalMs = DurationMs;
        Stats.Calls = 1;
        Stats.AverageMs = DurationMs;
        return;
    }

    Stats.Calls += 1;
    Stats.TotalMs += DurationMs;
    Stats.MinMs = FMath::Min(Stats.MinMs, DurationMs);
    Stats.MaxMs = FMath::Max(Stats.MaxMs, DurationMs);
    Stats.AverageMs = Stats.TotalMs / static_cast<double>(Stats.Calls);
}

FVRPerfCaptureSample UVRPerfCaptureSubsystem::CaptureSingleSample(float DeltaTimeOverride)
{
    const float DeltaTime = DeltaTimeOverride >= 0.0f
        ? DeltaTimeOverride
        : static_cast<float>(FApp::GetDeltaTime());

    FVRPerfCaptureSample Sample = BuildSample(DeltaTime);
    Samples.Add(Sample);
    return Sample;
}

FVRPerfCaptureSample UVRPerfCaptureSubsystem::BuildSample(float DeltaTime)
{
    FVRPerfCaptureSample Sample;

    Sample.TimestampSeconds = FPlatformTime::Seconds() - CaptureStartPlatformSeconds;
    Sample.FrameIndex = static_cast<int64>(GFrameCounter);

    Sample.DeltaTimeMs = DeltaTime * 1000.0f;
    Sample.FPS = DeltaTime > SMALL_NUMBER ? 1.0f / DeltaTime : 0.0f;

    constexpr float HitchThresholdMs = 20.0f;
    Sample.bIsHitch = Sample.DeltaTimeMs >= HitchThresholdMs;

    Sample.ScreenPercentage = GetConsoleVariableFloat(TEXT("r.ScreenPercentage"), -1.0f);
    Sample.VRPixelDensity = GetConsoleVariableFloat(TEXT("vr.PixelDensity"), -1.0f);
    Sample.DynamicResolutionMode = GetConsoleVariableInt(TEXT("r.DynamicRes.OperationMode"), -1);
    Sample.VSync = GetConsoleVariableInt(TEXT("r.VSync"), -1);
    Sample.MaxFPS = GetConsoleVariableFloat(TEXT("t.MaxFPS"), -1.0f);

    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        const FIntPoint ViewportSize = GEngine->GameViewport->Viewport->GetSizeXY();
        Sample.ViewportWidth = ViewportSize.X;
        Sample.ViewportHeight = ViewportSize.Y;
    }

    Sample.bHMDEnabled = false;
	Sample.HMDDeviceName = TEXT("None");
	Sample.HMDPosition = FVector::ZeroVector;
	Sample.HMDOrientation = FRotator::ZeroRotator;

	if (GEngine && GEngine->XRSystem.IsValid())
	{
	    TSharedPtr<IXRTrackingSystem, ESPMode::ThreadSafe> XRSystem = GEngine->XRSystem;

	    Sample.bHMDEnabled =
	        XRSystem->IsHeadTrackingAllowed() &&
	        XRSystem->IsTracking(IXRTrackingSystem::HMDDeviceId);

	    Sample.HMDDeviceName = XRSystem->GetSystemName().ToString();

	    FQuat HMDOrientationQuat = FQuat::Identity;
	    FVector HMDPosition = FVector::ZeroVector;

	    if (XRSystem->GetCurrentPose(
	        IXRTrackingSystem::HMDDeviceId,
	        HMDOrientationQuat,
	        HMDPosition))
	    {
	        Sample.HMDPosition = HMDPosition;
	        Sample.HMDOrientation = HMDOrientationQuat.Rotator();
	    }
	}

    const UWorld* World = GetWorld();
    if (World)
    {
        for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
        {
            AActor* Actor = *ActorIt;
            if (!IsValid(Actor))
            {
                continue;
            }

            ++Sample.ActorCount;

            TArray<UPrimitiveComponent*> PrimitiveComponents;
            Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
            Sample.PrimitiveComponentCount += PrimitiveComponents.Num();
        }
    }

    GatherNiagaraStats(
        Sample.NiagaraComponentCount,
        Sample.ActiveNiagaraComponentCount,
        Sample.EstimatedActiveNiagaraParticles
    );

    const FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
    Sample.UsedPhysicalMemoryMB = static_cast<float>(MemoryStats.UsedPhysical) / (1024.0f * 1024.0f);
    Sample.PeakUsedPhysicalMemoryMB = static_cast<float>(MemoryStats.PeakUsedPhysical) / (1024.0f * 1024.0f);

    Sample.CodeScopes = ConsumeCurrentCodeStats();
    Sample.CodeScopeCount = Sample.CodeScopes.Num();

    for (const FVRPerfCodeScopeStats& CodeScope : Sample.CodeScopes)
    {
        Sample.CodeCallCount += CodeScope.Calls;
        Sample.MeasuredCodeTotalMs += CodeScope.TotalMs;

        if (CodeScope.MaxMs > Sample.SlowestCodeScopeMaxMs)
        {
            Sample.SlowestCodeScopeMaxMs = CodeScope.MaxMs;
            Sample.SlowestCodeScopeName = CodeScope.ScopeName;
        }
    }

    Sample.Marker = CurrentMarker;

    return Sample;
}

TArray<FVRPerfCodeScopeStats> UVRPerfCaptureSubsystem::ConsumeCurrentCodeStats()
{
    FScopeLock Lock(&CodeStatsCriticalSection);

    TArray<FVRPerfCodeScopeStats> Result;
    CurrentCodeStats.GenerateValueArray(Result);
    CurrentCodeStats.Reset();

    Result.Sort([](const FVRPerfCodeScopeStats& A, const FVRPerfCodeScopeStats& B)
    {
        return A.TotalMs > B.TotalMs;
    });

    return Result;
}

void UVRPerfCaptureSubsystem::GatherNiagaraStats(
    int32& OutNiagaraComponentCount,
    int32& OutActiveNiagaraComponentCount,
    int32& OutEstimatedParticleCount
) const
{
    OutNiagaraComponentCount = 0;
    OutActiveNiagaraComponentCount = 0;
    OutEstimatedParticleCount = 0;

    bool bHadUnavailableParticleCount = false;

    TArray<UNiagaraComponent*> ComponentsToMeasure;

    for (UNiagaraComponent* Component : ExplicitNiagaraComponents)
    {
        if (IsValid(Component))
        {
            ComponentsToMeasure.AddUnique(Component);
        }
    }

    if (bAutoDiscoverNiagara)
    {
        const UWorld* World = GetWorld();
        if (World)
        {
            for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
            {
                AActor* Actor = *ActorIt;
                if (!IsValid(Actor))
                {
                    continue;
                }

                TArray<UNiagaraComponent*> NiagaraComponents;
                Actor->GetComponents<UNiagaraComponent>(NiagaraComponents);

                for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
                {
                    if (IsValid(NiagaraComponent))
                    {
                        ComponentsToMeasure.AddUnique(NiagaraComponent);
                    }
                }
            }
        }
    }

    for (UNiagaraComponent* Component : ComponentsToMeasure)
    {
        if (!IsValid(Component) || !Component->IsRegistered())
        {
            continue;
        }

        ++OutNiagaraComponentCount;

        if (Component->IsActive())
        {
            ++OutActiveNiagaraComponentCount;
        }

        const int32 ParticleCount = TryGetNiagaraActiveParticleCount(Component);
        if (ParticleCount >= 0)
        {
            OutEstimatedParticleCount += ParticleCount;
        }
        else
        {
            bHadUnavailableParticleCount = true;
        }
    }

    if (bHadUnavailableParticleCount && OutEstimatedParticleCount == 0)
    {
        OutEstimatedParticleCount = -1;
    }
}

int32 UVRPerfCaptureSubsystem::TryGetNiagaraActiveParticleCount(UNiagaraComponent* Component)
{
    if (!IsValid(Component))
    {
        return 0;
    }

    UFunction* Function = Component->FindFunction(TEXT("GetNumActiveParticles"));
    if (!Function)
    {
        return -1;
    }

    struct FGetNumActiveParticlesParams
    {
        int32 ReturnValue = 0;
    };

    FGetNumActiveParticlesParams Params;
    Component->ProcessEvent(Function, &Params);

    return Params.ReturnValue;
}

FVRPerfCaptureSummary UVRPerfCaptureSubsystem::GetCurrentSummary() const
{
    return BuildSummary(LastCsvPath, LastCodeCsvPath);
}

FVRPerfCaptureSummary UVRPerfCaptureSubsystem::BuildSummary(const FString& CsvPath, const FString& CodeCsvPath) const
{
    FVRPerfCaptureSummary Summary;
    Summary.NumSamples = Samples.Num();
    Summary.CsvPath = CsvPath;
    Summary.CodeCsvPath = CodeCsvPath;

    if (Samples.Num() == 0)
    {
        return Summary;
    }

    Summary.DurationSeconds = static_cast<float>(
        Samples.Last().TimestampSeconds - Samples[0].TimestampSeconds
    );

    TArray<float> FrameTimes;
    FrameTimes.Reserve(Samples.Num());

    double SumFrameTime = 0.0;
    double SumFPS = 0.0;
    double SumParticles = 0.0;
    double SumMeasuredCodeMs = 0.0;

    int32 ValidParticleSamples = 0;

    for (const FVRPerfCaptureSample& Sample : Samples)
    {
        FrameTimes.Add(Sample.DeltaTimeMs);
        SumFrameTime += Sample.DeltaTimeMs;
        SumFPS += Sample.FPS;

        Summary.WorstFrameTimeMs = FMath::Max(Summary.WorstFrameTimeMs, Sample.DeltaTimeMs);

        if (Sample.bIsHitch)
        {
            ++Summary.HitchCount;
        }

        if (Sample.EstimatedActiveNiagaraParticles >= 0)
        {
            SumParticles += Sample.EstimatedActiveNiagaraParticles;
            ++ValidParticleSamples;
            Summary.MaxEstimatedNiagaraParticles = FMath::Max(
                Summary.MaxEstimatedNiagaraParticles,
                Sample.EstimatedActiveNiagaraParticles
            );
        }

        SumMeasuredCodeMs += Sample.MeasuredCodeTotalMs;
        Summary.MaxMeasuredCodeMsInSample = FMath::Max(
            Summary.MaxMeasuredCodeMsInSample,
            Sample.MeasuredCodeTotalMs
        );

        if (Sample.SlowestCodeScopeMaxMs > Summary.SlowestCodeScopeMaxMs)
        {
            Summary.SlowestCodeScopeMaxMs = Sample.SlowestCodeScopeMaxMs;
            Summary.SlowestCodeScopeName = Sample.SlowestCodeScopeName;
        }
    }

    FrameTimes.Sort();

    auto Percentile = [&FrameTimes](float P) -> float
    {
        if (FrameTimes.Num() == 0)
        {
            return 0.0f;
        }

        const float IndexFloat = P * static_cast<float>(FrameTimes.Num() - 1);
        const int32 Index = FMath::Clamp(FMath::RoundToInt(IndexFloat), 0, FrameTimes.Num() - 1);
        return FrameTimes[Index];
    };

    Summary.AverageFrameTimeMs = static_cast<float>(SumFrameTime / Samples.Num());
    Summary.AverageFPS = static_cast<float>(SumFPS / Samples.Num());
    Summary.MedianFrameTimeMs = Percentile(0.50f);
    Summary.P95FrameTimeMs = Percentile(0.95f);
    Summary.P99FrameTimeMs = Percentile(0.99f);

    Summary.OnePercentLowFPS = Summary.P99FrameTimeMs > SMALL_NUMBER
        ? 1000.0f / Summary.P99FrameTimeMs
        : 0.0f;

    Summary.AverageEstimatedNiagaraParticles = ValidParticleSamples > 0
        ? static_cast<float>(SumParticles / ValidParticleSamples)
        : -1.0f;

    Summary.AverageMeasuredCodeMsPerSample = SumMeasuredCodeMs / static_cast<double>(Samples.Num());

    return Summary;
}


FString UVRPerfCaptureSubsystem::SaveCsv()
{
    const FString SafeName = CurrentCaptureName
        .Replace(TEXT(" "), TEXT("_"))
        .Replace(TEXT(":"), TEXT("-"))
        .Replace(TEXT("/"), TEXT("-"))
        .Replace(TEXT("\\"), TEXT("-"));

    const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
    const FString Directory = GetVRPerfCaptureOutputDirectory();

    const FString BaseFilePath = FPaths::Combine(
        Directory,
        FString::Printf(TEXT("%s_%s"), *SafeName, *Timestamp)
    );

    LastCsvPath = SaveFrameCsv(BaseFilePath);
    LastCodeCsvPath = SaveCodeCsv(BaseFilePath);

    if (LastCsvPath.IsEmpty())
    {
        WriteVRPerfCaptureStatus(
            FString::Printf(
                TEXT("ERROR: Failed to write frame CSV. Target base path: '%s'"),
                *BaseFilePath
            )
        );
    }

    if (LastCodeCsvPath.IsEmpty())
    {
        WriteVRPerfCaptureStatus(
            FString::Printf(
                TEXT("WARNING: Failed to write code CSV or no code data existed. Target base path: '%s'"),
                *BaseFilePath
            )
        );
    }

    return LastCsvPath;
}

FString UVRPerfCaptureSubsystem::SaveFrameCsv(const FString& BaseFilePath) const
{
    const FString FilePath = BaseFilePath + TEXT(".csv");

    FString Csv;
    Csv += TEXT("TimestampSeconds,FrameIndex,DeltaTimeMs,FPS,bIsHitch,ViewportWidth,ViewportHeight,ScreenPercentage,VRPixelDensity,DynamicResolutionMode,VSync,MaxFPS,bHMDEnabled,HMDDeviceName,HMDPositionX,HMDPositionY,HMDPositionZ,HMDPitch,HMDYaw,HMDRoll,ActorCount,PrimitiveComponentCount,NiagaraComponentCount,ActiveNiagaraComponentCount,EstimatedActiveNiagaraParticles,UsedPhysicalMemoryMB,PeakUsedPhysicalMemoryMB,CodeScopeCount,CodeCallCount,MeasuredCodeTotalMs,SlowestCodeScopeName,SlowestCodeScopeMaxMs,Marker\n");

    for (const FVRPerfCaptureSample& S : Samples)
    {
        Csv += FString::Printf(
            TEXT("%.6f,%lld,%.4f,%.4f,%d,%d,%d,%.4f,%.4f,%d,%d,%.4f,%d,%s,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%d,%d,%d,%d,%.4f,%.4f,%d,%d,%.6f,%s,%.6f,%s\n"),
            S.TimestampSeconds,
            S.FrameIndex,
            S.DeltaTimeMs,
            S.FPS,
            S.bIsHitch ? 1 : 0,
            S.ViewportWidth,
            S.ViewportHeight,
            S.ScreenPercentage,
            S.VRPixelDensity,
            S.DynamicResolutionMode,
            S.VSync,
            S.MaxFPS,
            S.bHMDEnabled ? 1 : 0,
            *EscapeCsv(S.HMDDeviceName),
            S.HMDPosition.X,
            S.HMDPosition.Y,
            S.HMDPosition.Z,
            S.HMDOrientation.Pitch,
            S.HMDOrientation.Yaw,
            S.HMDOrientation.Roll,
            S.ActorCount,
            S.PrimitiveComponentCount,
            S.NiagaraComponentCount,
            S.ActiveNiagaraComponentCount,
            S.EstimatedActiveNiagaraParticles,
            S.UsedPhysicalMemoryMB,
            S.PeakUsedPhysicalMemoryMB,
            S.CodeScopeCount,
            S.CodeCallCount,
            S.MeasuredCodeTotalMs,
            *EscapeCsv(S.SlowestCodeScopeName.ToString()),
            S.SlowestCodeScopeMaxMs,
            *EscapeCsv(S.Marker)
        );
    }

    return FFileHelper::SaveStringToFile(Csv, *FilePath) ? FilePath : TEXT("");
}

FString UVRPerfCaptureSubsystem::SaveCodeCsv(const FString& BaseFilePath) const
{
    const FString FilePath = BaseFilePath + TEXT("_code.csv");

    FString Csv;
    Csv += TEXT("TimestampSeconds,FrameIndex,ScopeName,Calls,TotalMs,AverageMs,MinMs,MaxMs,Marker\n");

    for (const FVRPerfCaptureSample& Sample : Samples)
    {
        for (const FVRPerfCodeScopeStats& Scope : Sample.CodeScopes)
        {
            Csv += FString::Printf(
                TEXT("%.6f,%lld,%s,%d,%.6f,%.6f,%.6f,%.6f,%s\n"),
                Sample.TimestampSeconds,
                Sample.FrameIndex,
                *EscapeCsv(Scope.ScopeName.ToString()),
                Scope.Calls,
                Scope.TotalMs,
                Scope.AverageMs,
                Scope.MinMs,
                Scope.MaxMs,
                *EscapeCsv(Sample.Marker)
            );
        }
    }

    return FFileHelper::SaveStringToFile(Csv, *FilePath) ? FilePath : TEXT("");
}

FString UVRPerfCaptureSubsystem::EscapeCsv(const FString& In)
{
    FString Out = In;
    Out.ReplaceInline(TEXT("\""), TEXT("\"\""));
    return FString::Printf(TEXT("\"%s\""), *Out);
}

float UVRPerfCaptureSubsystem::GetConsoleVariableFloat(const TCHAR* Name, float DefaultValue)
{
    IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
    return CVar ? CVar->GetFloat() : DefaultValue;
}

int32 UVRPerfCaptureSubsystem::GetConsoleVariableInt(const TCHAR* Name, int32 DefaultValue)
{
    IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
    return CVar ? CVar->GetInt() : DefaultValue;
}

double UVRPerfCaptureSubsystem::CyclesToMilliseconds(uint64 Cycles)
{
    return static_cast<double>(Cycles) * FPlatformTime::GetSecondsPerCycle64() * 1000.0;
}