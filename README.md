# VR Performance Capture Plugin

Runtime performance measurement plugin for Unreal Engine VR projects.

This plugin records frame-level performance during real gameplay or VR interaction and can also measure selected C++ code sections. It is intended for experiments where performance should be captured in the actual scene instead of a separate benchmark map.

The plugin can be controlled from Blueprint or C++ and exports CSV files to the project’s `Saved` directory.

---

## Features

The plugin records:

* Frame time in milliseconds
* FPS
* Hitch detection
* Viewport resolution
* Screen percentage
* VR pixel density console variable
* Dynamic resolution mode
* VSync state
* `t.MaxFPS`
* HMD tracking state
* HMD system name
* HMD position and orientation
* Actor count
* Primitive component count
* Niagara component count
* Active Niagara component count
* Estimated active Niagara particle count
* Physical memory usage
* Custom marker labels
* Named C++ scope timings

The plugin writes two CSV files:

```text
Saved/Profiling/VRPerfCapture/<CaptureName>_<Timestamp>.csv
Saved/Profiling/VRPerfCapture/<CaptureName>_<Timestamp>_code.csv
```

The first file contains frame-level VR performance data.

The second file contains measured C++ code sections.

---

## Installation

Copy the plugin into your project:

```text
YourProject/
  Plugins/
    VRPerfCapture/
```

Then:

1. Close Unreal Editor.
2. Right-click your `.uproject` file.
3. Select **Generate Visual Studio project files**.
4. Open the project in Visual Studio or Rider.
5. Build the project.
6. Start Unreal Editor.
7. Enable the plugin if it is not already enabled.

---

## Recommended build for measurement

For final measurements, use a packaged build instead of Play-in-Editor.

Recommended configuration:

```text
Build configuration: Shipping
Run outside the editor
Disable VSync
Use fixed resolution
Use fixed VR runtime / headset refresh rate
Use the same interaction task for every run
```

Example launch command:

```bat
MyProject.exe -unattended -NoSplash -NoVSync -USEALLAVAILABLECORES
```

For final numbers, use the plugin CSV output from a normal non-traced run.

For deeper diagnosis, you can additionally run Unreal Insights, but do not use the traced run as your final performance result because tracing adds overhead.

---

# Blueprint Usage

## Starting a capture

Use the Blueprint node:

```text
Start VR Performance Capture
```

Recommended settings:

```text
Capture Name: Run01_FilteringTask
Duration Seconds: 0
Sample Interval Seconds: 0
Auto Discover Niagara Components: true
Write Csv On Stop: true
```

Meaning:

```text
Duration Seconds = 0
```

The capture continues until you stop it manually.

```text
Sample Interval Seconds = 0
```

The plugin samples every frame.

For longer sessions, use a lower sampling rate:

```text
0.05 = 20 samples per second
0.10 = 10 samples per second
0.50 = 2 samples per second
```

For short VR interaction tasks, every-frame sampling is recommended.

---

## Stopping a capture

Use the Blueprint node:

```text
Stop VR Performance Capture
```

This stops the current capture and writes the CSV files if `Write Csv On Stop` was enabled.

The returned summary contains:

```text
Num Samples
Duration Seconds
Average FPS
Average Frame Time
Median Frame Time
95th Percentile Frame Time
99th Percentile Frame Time
Worst Frame Time
1% Low FPS
Hitch Count
Average Estimated Niagara Particles
Max Estimated Niagara Particles
Average Measured Code Time Per Sample
Max Measured Code Time In Sample
Slowest Code Scope Name
CSV Path
Code CSV Path
```

---

## Adding markers during interaction

Use:

```text
Set VR Performance Marker
```

Markers are written into the CSV and are useful for identifying interaction phases.

Example markers:

```text
Selecting trajectory
Filtering trajectories
Opening detail view
Changing visualization mode
Spawning particles
```

Example Blueprint flow:

```text
Controller Button Pressed
    → Start VR Performance Capture
        Capture Name = Run01_TrajectorySelection
        Duration Seconds = 0
        Sample Interval Seconds = 0
        Auto Discover Niagara Components = true
        Write Csv On Stop = true

User starts selecting
    → Set VR Performance Marker = Selecting trajectory

User opens detail view
    → Set VR Performance Marker = Opening detail view

Task finished
    → Stop VR Performance Capture
```

---

## Registering specific Niagara components

By default, the plugin can auto-discover Niagara components in the world.

For better control, you can manually register important Niagara systems with:

```text
Register VR Performance Niagara Component
```

This is useful if you only want to measure specific particle systems rather than every Niagara component in the level.

Recommended setup:

```text
BeginPlay of Niagara Actor
    → Register VR Performance Niagara Component
```

You can still keep `Auto Discover Niagara Components` enabled if you want global particle system statistics.

---

# C++ Usage

The plugin can measure named C++ code sections using scope timers.

Include the timer header:

```cpp
#include "VRPerfCaptureScopedTimer.h"
```

Then wrap code sections with:

```cpp
VRPERF_SCOPE(this, "ScopeName");
```

The timer starts when the macro is reached and stops automatically when the current C++ scope ends.

---

## Basic C++ example

```cpp
#include "VRPerfCaptureScopedTimer.h"

void UTrajectoryRetrievalComponent::UpdateRetrieval()
{
    VRPERF_SCOPE(this, "Retrieval/UpdateRetrieval");

    CollectVisibleTrajectories();
    ComputeSimilarity();
    UpdateVisualization();
}
```

This records the total time spent in `UpdateRetrieval`.

---

## Measuring nested sections

```cpp
#include "VRPerfCaptureScopedTimer.h"

void UTrajectoryRetrievalComponent::UpdateRetrieval()
{
    VRPERF_SCOPE(this, "Retrieval/UpdateRetrieval");

    {
        VRPERF_SCOPE(this, "Retrieval/CollectVisibleTrajectories");
        CollectVisibleTrajectories();
    }

    {
        VRPERF_SCOPE(this, "Retrieval/ComputeSimilarity");
        ComputeSimilarity();
    }

    {
        VRPERF_SCOPE(this, "Retrieval/UpdateVisualization");
        UpdateVisualization();
    }
}
```

This allows you to see both the total retrieval cost and the cost of each sub-step.

---

## Measuring plain C++ helper code

If your code is not inside a `UObject`, `AActor`, or `UActorComponent`, pass the subsystem directly.

```cpp
#include "VRPerfCaptureScopedTimer.h"
#include "VRPerfCaptureSubsystem.h"

void FTrajectorySearchHelper::RunSearch(UWorld* World)
{
    UVRPerfCaptureSubsystem* PerfSubsystem = World
        ? World->GetSubsystem<UVRPerfCaptureSubsystem>()
        : nullptr;

    VRPERF_SCOPE_SUBSYSTEM(PerfSubsystem, "TrajectorySearchHelper/RunSearch");

    // Expensive C++ code here.
}
```

---

## Dynamic scope names

Use this when the scope name is generated dynamically:

```cpp
FName ScopeName = FName(TEXT("Retrieval/ConditionA"));
VRPERF_SCOPE_NAME(this, ScopeName);
```

Prefer static names for most measurements because they are easier to aggregate later.

---

## Manual begin/end timing

Manual timing is available for advanced cases where the measured work does not fit into a single C++ scope.

```cpp
UVRPerfCaptureSubsystem* PerfSubsystem = GetWorld()->GetSubsystem<UVRPerfCaptureSubsystem>();

const int32 Handle = PerfSubsystem->BeginCodeScope(TEXT("AsyncTask/LongRunningPart"));

// Work to measure.

PerfSubsystem->EndCodeScope(Handle);
```

For normal function-level measurement, prefer `VRPERF_SCOPE(...)`.

---

# Example measurement setup for a VR task

A typical experimental capture could look like this:

```text
1. Start the packaged application.
2. Put on the headset.
3. Wait 20–30 seconds for warm-up.
4. Press controller button to start capture.
5. Perform the VR task.
6. Use markers for task phases.
7. Press controller button to stop capture.
8. Repeat 5–10 times per condition.
```

Example task phases:

```text
Marker: Idle before task
Marker: Drawing query trajectory
Marker: Searching similar trajectories
Marker: Viewing result set
Marker: Filtering result set
Marker: Inspecting selected trajectory
```

---

# Recommended reporting metrics

For frame-level VR performance, report:

```text
Mean frame time
Median frame time
95th percentile frame time
99th percentile frame time
Worst frame time
Average FPS
1% low FPS
Hitch count
Average Niagara particles
Maximum Niagara particles
Average active Niagara systems
Viewport resolution
Screen percentage
VR pixel density
Memory usage
```

For C++ code performance, report per scope:

```text
Call count
Mean duration
Median duration
95th percentile duration
99th percentile duration
Maximum duration
Percentage of frame budget
```

For VR at 90 Hz, the frame budget is approximately:

```text
1000 / 90 = 11.11 ms
```

For VR at 72 Hz:

```text
1000 / 72 = 13.89 ms
```

For VR at 120 Hz:

```text
1000 / 120 = 8.33 ms
```

Example interpretation:

```text
Retrieval/ComputeSimilarity took 3.4 ms on average.
At 90 Hz, this is approximately 30.6% of the 11.11 ms frame budget.
```

---

# CSV files

## Frame CSV

The main CSV contains one row per sampled frame or sample interval.

Important columns:

```text
TimestampSeconds
FrameIndex
DeltaTimeMs
FPS
bIsHitch
ViewportWidth
ViewportHeight
ScreenPercentage
VRPixelDensity
DynamicResolutionMode
VSync
MaxFPS
bHMDEnabled
HMDDeviceName
HMDPositionX
HMDPositionY
HMDPositionZ
HMDPitch
HMDYaw
HMDRoll
ActorCount
PrimitiveComponentCount
NiagaraComponentCount
ActiveNiagaraComponentCount
EstimatedActiveNiagaraParticles
UsedPhysicalMemoryMB
PeakUsedPhysicalMemoryMB
CodeScopeCount
CodeCallCount
MeasuredCodeTotalMs
SlowestCodeScopeName
SlowestCodeScopeMaxMs
Marker
```

## Code CSV

The code CSV contains measured C++ scopes.

Important columns:

```text
TimestampSeconds
FrameIndex
ScopeName
Calls
TotalMs
AverageMs
MinMs
MaxMs
Marker
```

Each row describes one measured scope within a sampled time window.

---

# Notes on Niagara particle counts

The plugin attempts to estimate active Niagara particle counts.

This is useful for relative comparisons, for example:

```text
Condition A used fewer particles than Condition B.
Particle count increased during the filtering phase.
Particle count spikes coincide with frame-time spikes.
```

However, exact counts may not always be available, especially for GPU-simulated Niagara systems. If exact particle counts are required, add explicit counters inside the Niagara systems or expose particle counts through custom gameplay code.

A value of `-1` means that a particle count was unavailable.

---

# Notes on measurement overhead

The plugin itself has some overhead.

To reduce overhead:

```text
Use a sample interval such as 0.05 or 0.1 for long captures.
Disable Niagara auto-discovery and manually register important Niagara components.
Avoid measuring extremely small C++ scopes thousands of times per frame.
Use packaged builds for final results.
Avoid visible stat overlays during final measurements.
```

Recommended approach:

```text
Use every-frame sampling for short controlled tasks.
Use interval sampling for long exploratory sessions.
Use C++ scope timers only around meaningful functions or algorithmic blocks.
```

---

# Recommended final measurement protocol

For each condition:

```text
1 warm-up run, discarded
5–10 measured runs
Same headset
Same runtime
Same refresh rate
Same resolution / pixel density
Same scene
Same interaction task
Same starting position
Same visualization settings
Same particle effects setting
```

Report the median across runs and include variability, for example standard deviation or interquartile range.

---


# Analyzing Captures with Python

The plugin includes an optional Python analysis script:

```text
summarize_vr_perf.py
````

The script reads the CSV files created by the plugin and generates summary tables and plots for reports, papers, or thesis documentation.

The expected input directory is:

```text
Saved/Profiling/VRPerfCapture/
```

The script expects files with the plugin’s default naming pattern:

```text
<CaptureName>_<YYYYMMDD>_<HHMMSS>.csv
<CaptureName>_<YYYYMMDD>_<HHMMSS>_code.csv
```

The first file contains frame-level performance data.

The second file contains measured C++ code scope timings.

---

## Installing Python dependencies

Install the required Python packages:

```bash
pip install pandas numpy matplotlib
```

---

## Running the script

Example:

```bash
python summarize_vr_perf.py \
  --input "C:/Path/To/YourProject/Saved/Profiling/VRPerfCapture" \
  --output "C:/Path/To/PerfReport" \
  --refresh-rate 90
```

On Windows Command Prompt:

```bat
python summarize_vr_perf.py ^
  --input "C:\Path\To\YourProject\Saved\Profiling\VRPerfCapture" ^
  --output "C:\Path\To\PerfReport" ^
  --refresh-rate 90
```

Set `--refresh-rate` to the headset refresh rate used during the experiment.

Common values:

```text
72 Hz  -> 13.89 ms frame budget
80 Hz  -> 12.50 ms frame budget
90 Hz  -> 11.11 ms frame budget
120 Hz ->  8.33 ms frame budget
```

The script uses this value to compute how many frames exceeded the VR frame budget.

---

## Optional: Dropping warm-up time

If the first seconds of a capture include loading, scene stabilization, or the user getting ready, exclude them:

```bash
python summarize_vr_perf.py \
  --input "Saved/Profiling/VRPerfCapture" \
  --output "PerfReport" \
  --refresh-rate 90 \
  --drop-first-sec 5
```

This removes the first five seconds of each capture before calculating summaries.

---

## Recommended capture naming

Use consistent capture names so the script can group runs by condition.

Recommended pattern:

```text
Baseline_Run01
Baseline_Run02
Baseline_Run03

ParticlesHigh_Run01
ParticlesHigh_Run02
ParticlesHigh_Run03

SemanticFiltering_Run01
SemanticFiltering_Run02
SemanticFiltering_Run03
```

The script automatically infers conditions such as:

```text
Baseline
ParticlesHigh
SemanticFiltering
```

from these names.

---

## Custom condition grouping

For custom naming conventions, use `--condition-regex`.

Example:

```bash
python summarize_vr_perf.py \
  --input "Saved/Profiling/VRPerfCapture" \
  --output "PerfReport" \
  --refresh-rate 90 \
  --condition-regex "^(?P<condition>.+?)_Run\d+"
```

The regex must contain a named group:

```text
(?P<condition>...)
```

Example capture names:

```text
ConditionA_Participant01_Run01
ConditionA_Participant01_Run02
ConditionB_Participant01_Run01
```

Possible regex:

```text
^(?P<condition>Condition[A-Z])
```

This groups the captures into:

```text
ConditionA
ConditionB
```

---

## Recursive search

If capture files are stored in subfolders, use:

```bash
python summarize_vr_perf.py \
  --input "Saved/Profiling/VRPerfCapture" \
  --output "PerfReport" \
  --refresh-rate 90 \
  --recursive
```

---

## Output files

The script creates CSV summary tables:

```text
summary_runs.csv
summary_conditions.csv
summary_markers.csv
summary_code_scopes.csv
summary_code_scopes_by_condition.csv
```

It also creates plots:

```text
frame_time_boxplot_by_capture.png
frame_time_p95_by_capture.png
fps_1pct_low_by_capture.png
hitches_by_capture.png
niagara_particles_by_capture.png
frame_time_timeseries_overview.png
top_code_scopes_total_ms.png
top_code_scopes_p95_ms.png
```

---

## Summary tables

### summary_runs.csv

Contains one row per capture run.

Useful columns:

```text
CaptureName
Condition
DurationSeconds
MeanFrameTimeMs
MedianFrameTimeMs
P95FrameTimeMs
P99FrameTimeMs
WorstFrameTimeMs
FPSFromMeanFrameTime
OnePercentLowFPS
FramesOverBudget
PercentFramesOverBudget
HitchCount
MeanMeasuredCodeMs
MaxMeasuredCodeMs
MeanNiagaraParticles
MaxNiagaraParticles
MeanUsedPhysicalMemoryMB
```

Use this file to compare individual runs.

---

### summary_conditions.csv

Aggregates multiple runs of the same condition.

Useful for comparing conditions such as:

```text
Baseline
ParticlesHigh
SemanticFiltering
```

This is usually the most useful table for a thesis or paper because it summarizes repeated measurements.

---

### summary_markers.csv

Groups performance by marker labels set during the capture.

Example markers:

```text
Idle
Selecting trajectory
Filtering trajectories
Opening detail view
Spawning particles
```

This table is useful when a capture contains several interaction phases and you want to compare them separately.

---

### summary_code_scopes.csv

Summarizes measured C++ scopes per capture.

Example scopes:

```text
Retrieval/UpdateRetrieval
Retrieval/ComputeSimilarity
Retrieval/UpdateVisualization
TrajectorySearchHelper/RunSearch
```

Useful columns:

```text
ScopeName
TotalCalls
TotalMs
MeanTotalMsPerSample
P95TotalMsPerSample
P99TotalMsPerSample
MaxSingleSampleTotalMs
WorstMaxMs
```

Use this file to identify which C++ functions or algorithmic blocks are most expensive.

---

### summary_code_scopes_by_condition.csv

Aggregates C++ scope timings across all runs of the same condition.

This is useful for statements such as:

```text
Semantic filtering increased the average retrieval cost from 1.8 ms to 3.4 ms.
Particle-heavy rendering did not affect retrieval cost but increased frame-time percentiles.
```

---

## Plots

### Frame-time distribution

```text
frame_time_boxplot_by_capture.png
```

Shows the distribution of frame times for each capture. This is useful for detecting unstable runs or outliers.

---

### P95 frame time

```text
frame_time_p95_by_capture.png
```

Shows the 95th percentile frame time for each capture.

For VR, this is often more meaningful than average FPS because it captures short performance drops.

---

### 1% low FPS

```text
fps_1pct_low_by_capture.png
```

Shows 1% low FPS for each capture.

Lower values indicate unstable performance or frame-time spikes.

---

### Hitch count

```text
hitches_by_capture.png
```

Shows how many hitches occurred during each capture.

The plugin marks hitches using a frame-time threshold. Adjust the plugin threshold if your experiment uses a different definition.

---

### Niagara particles

```text
niagara_particles_by_capture.png
```

Shows the average estimated number of active Niagara particles per capture.

This helps relate particle-heavy scenes to frame-time increases.

---

### C++ code scope plots

```text
top_code_scopes_total_ms.png
top_code_scopes_p95_ms.png
```

These plots show which measured C++ scopes consumed the most total time and which scopes had the highest P95 timing.

Use them to identify expensive algorithmic sections such as retrieval, filtering, sorting, clustering, or visualization updates.

---

## Recommended workflow

A typical analysis workflow is:

```text
1. Run the VR task several times per condition.
2. Save all plugin-generated CSV files in Saved/Profiling/VRPerfCapture.
3. Run summarize_vr_perf.py.
4. Inspect summary_runs.csv for unstable or invalid runs.
5. Use summary_conditions.csv for condition-level comparisons.
6. Use summary_markers.csv for interaction-phase analysis.
7. Use summary_code_scopes.csv to identify expensive C++ functions.
8. Use the generated plots in reports or presentations.
```

---

## Recommended metrics for reporting

For frame-level VR performance, report:

```text
Mean frame time
Median frame time
P95 frame time
P99 frame time
Worst frame time
1% low FPS
Frames over budget
Percent frames over budget
Hitch count
```

For C++ code performance, report:

```text
Total calls
Mean duration
P95 duration
P99 duration
Worst duration
Percentage of VR frame budget
```

Example interpretation:

```text
At 90 Hz, the frame budget is 11.11 ms.
A C++ retrieval scope with a P95 duration of 3.0 ms uses approximately 27% of the available frame budget.
```

---

## Notes

The script does not modify the original capture CSV files.

All generated summaries and plots are written to the selected output directory.

For final measurements, use captures from packaged builds rather than Play-in-Editor.

```
```
