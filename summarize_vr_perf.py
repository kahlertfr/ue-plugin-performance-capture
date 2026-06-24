#!/usr/bin/env python3
"""
summarize_vr_perf.py

Summarizes CSV files produced by the VRPerfCapture Unreal Engine plugin.

Expected input files:
    <CaptureName>_<YYYYMMDD>_<HHMMSS>.csv
    <CaptureName>_<YYYYMMDD>_<HHMMSS>_code.csv

Outputs:
    summary_runs.csv
    summary_conditions.csv
    summary_markers.csv
    summary_code_scopes.csv
    summary_code_scopes_by_condition.csv

Plots:
    frame_time_boxplot_by_capture.png
    frame_time_p95_by_capture.png
    fps_1pct_low_by_capture.png
    hitches_by_capture.png
    niagara_particles_by_capture.png
    frame_time_timeseries_overview.png
    top_code_scopes_total_ms.png
    top_code_scopes_p95_ms.png
"""

from __future__ import annotations

import argparse
import math
import re
from pathlib import Path
from typing import Optional, Dict, List, Tuple

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


FRAME_FILE_RE = re.compile(
    r"^(?P<capture>.+)_(?P<date>\d{8})_(?P<time>\d{6})(?P<code>_code)?\.csv$"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Summarize VRPerfCapture CSV files and generate tables/plots."
    )
    parser.add_argument(
        "--input",
        required=True,
        help="Input directory containing VRPerfCapture CSV files.",
    )
    parser.add_argument(
        "--output",
        required=True,
        help="Output directory for summary tables and plots.",
    )
    parser.add_argument(
        "--refresh-rate",
        type=float,
        default=90.0,
        help="Target headset refresh rate in Hz. Used to compute frame budget.",
    )
    parser.add_argument(
        "--drop-first-sec",
        type=float,
        default=0.0,
        help="Drop the first N seconds of each capture before summarizing.",
    )
    parser.add_argument(
        "--condition-regex",
        default=None,
        help=(
            "Optional regex for extracting condition from CaptureName. "
            "Use a named group (?P<condition>...). "
            "Example: '^(?P<condition>.+?)_Run\\d+'"
        ),
    )
    parser.add_argument(
        "--recursive",
        action="store_true",
        help="Search input directory recursively.",
    )
    return parser.parse_args()


def parse_capture_filename(path: Path) -> Optional[Dict[str, str]]:
    match = FRAME_FILE_RE.match(path.name)
    if not match:
        return None

    return {
        "capture_name": match.group("capture"),
        "date": match.group("date"),
        "time": match.group("time"),
        "is_code": bool(match.group("code")),
        "base_id": f"{match.group('capture')}_{match.group('date')}_{match.group('time')}",
    }


def infer_condition(capture_name: str, condition_regex: Optional[str]) -> str:
    if condition_regex:
        match = re.search(condition_regex, capture_name)
        if match and "condition" in match.groupdict():
            return match.group("condition")

    # Common naming patterns:
    #   Filtering_Run01
    #   Run01_Filtering
    #   Filtering_Task_Run03
    name = re.sub(r"(^Run\d+[_-]*)|([_-]*Run\d+$)", "", capture_name, flags=re.IGNORECASE)
    name = re.sub(r"[_-]*Trial\d+$", "", name, flags=re.IGNORECASE)
    name = name.strip("_- ")

    return name if name else capture_name


def extract_sample_count_from_capture_name(capture_name: str) -> float:
    """
    Extracts the leading sample count from capture names such as:

        1.000.000_Run01
        500.000_Run02
        1000000_Run03
        1_000_000_Run01

    Returns NaN if no leading number is found.
    """
    if not isinstance(capture_name, str):
        return np.nan

    match = re.match(r"^\s*(?P<count>[0-9][0-9._,]*)", capture_name)
    if not match:
        return np.nan

    raw = match.group("count")

    # Interpret dot, underscore, comma as thousands separators.
    normalized = raw.replace(".", "").replace("_", "").replace(",", "")

    try:
        return float(normalized)
    except ValueError:
        return np.nan


def safe_numeric(df: pd.DataFrame, columns: List[str]) -> pd.DataFrame:
    for col in columns:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")
    return df


def percentile(series: pd.Series, q: float) -> float:
    clean = pd.to_numeric(series, errors="coerce").dropna()
    if clean.empty:
        return np.nan
    return float(clean.quantile(q))


def fps_from_ms(frame_time_ms: float) -> float:
    if frame_time_ms is None or np.isnan(frame_time_ms) or frame_time_ms <= 0:
        return np.nan
    return 1000.0 / frame_time_ms


def load_frame_files(
    input_dir: Path,
    recursive: bool,
    condition_regex: Optional[str],
    drop_first_sec: float,
) -> Tuple[pd.DataFrame, Dict[str, Path]]:
    pattern = "**/*.csv" if recursive else "*.csv"
    files = sorted(input_dir.glob(pattern))

    frames = []
    code_files_by_base_id: Dict[str, Path] = {}

    for file in files:
        info = parse_capture_filename(file)
        if not info:
            continue

        if info["is_code"]:
            code_files_by_base_id[info["base_id"]] = file
            continue

        try:
            df = pd.read_csv(file)
        except Exception as exc:
            print(f"Warning: could not read frame CSV {file}: {exc}")
            continue

        if df.empty:
            print(f"Warning: empty frame CSV skipped: {file}")
            continue

        df["CaptureName"] = info["capture_name"]
        df["CaptureBaseId"] = info["base_id"]
        df["CaptureDate"] = info["date"]
        df["CaptureTime"] = info["time"]
        df["Condition"] = infer_condition(info["capture_name"], condition_regex)
        df["SampleCount"] = extract_sample_count_from_capture_name(info["capture_name"])

        numeric_columns = [
            "TimestampSeconds",
            "FrameIndex",
            "DeltaTimeMs",
            "FPS",
            "bIsHitch",
            "ViewportWidth",
            "ViewportHeight",
            "ScreenPercentage",
            "VRPixelDensity",
            "DynamicResolutionMode",
            "VSync",
            "MaxFPS",
            "bHMDEnabled",
            "ActorCount",
            "PrimitiveComponentCount",
            "NiagaraComponentCount",
            "ActiveNiagaraComponentCount",
            "EstimatedActiveNiagaraParticles",
            "UsedPhysicalMemoryMB",
            "PeakUsedPhysicalMemoryMB",
            "CodeScopeCount",
            "CodeCallCount",
            "MeasuredCodeTotalMs",
            "SlowestCodeScopeMaxMs",
        ]
        df = safe_numeric(df, numeric_columns)

        if drop_first_sec > 0 and "TimestampSeconds" in df.columns:
            df = df[df["TimestampSeconds"] >= drop_first_sec].copy()

        frames.append(df)

    if not frames:
        return pd.DataFrame(), code_files_by_base_id

    return pd.concat(frames, ignore_index=True), code_files_by_base_id


def load_code_files(
    code_files_by_base_id: Dict[str, Path],
    frame_df: pd.DataFrame,
    condition_regex: Optional[str],
    drop_first_sec: float,
) -> pd.DataFrame:
    if not code_files_by_base_id:
        return pd.DataFrame()

    capture_lookup = (
        frame_df[["CaptureBaseId", "CaptureName", "Condition"]]
        .drop_duplicates()
        .set_index("CaptureBaseId")
        .to_dict("index")
        if not frame_df.empty
        else {}
    )

    code_frames = []

    for base_id, file in sorted(code_files_by_base_id.items()):
        info = parse_capture_filename(file)
        if not info:
            continue

        try:
            df = pd.read_csv(file)
        except Exception as exc:
            print(f"Warning: could not read code CSV {file}: {exc}")
            continue

        if df.empty:
            continue

        capture_name = capture_lookup.get(base_id, {}).get("CaptureName", info["capture_name"])
        condition = capture_lookup.get(base_id, {}).get(
            "Condition", infer_condition(capture_name, condition_regex)
        )

        df["CodeFile"] = file.name
        df["CodePath"] = str(file)
        df["CaptureName"] = capture_name
        df["CaptureBaseId"] = base_id
        df["Condition"] = condition

        numeric_columns = [
            "TimestampSeconds",
            "FrameIndex",
            "Calls",
            "TotalMs",
            "AverageMs",
            "MinMs",
            "MaxMs",
        ]
        df = safe_numeric(df, numeric_columns)

        if drop_first_sec > 0 and "TimestampSeconds" in df.columns:
            df = df[df["TimestampSeconds"] >= drop_first_sec].copy()

        code_frames.append(df)

    if not code_frames:
        return pd.DataFrame()

    return pd.concat(code_frames, ignore_index=True)


def summarize_runs(frame_df: pd.DataFrame, refresh_rate: float) -> pd.DataFrame:
    if frame_df.empty:
        return pd.DataFrame()

    frame_budget_ms = 1000.0 / refresh_rate

    rows = []

    for capture_name, group in frame_df.groupby("CaptureName", sort=True):
        dt = group["DeltaTimeMs"].dropna()

        if dt.empty:
            continue

        duration = (
            group["TimestampSeconds"].max() - group["TimestampSeconds"].min()
            if "TimestampSeconds" in group.columns
            else np.nan
        )

        p50 = percentile(dt, 0.50)
        p95 = percentile(dt, 0.95)
        p99 = percentile(dt, 0.99)
        p999 = percentile(dt, 0.999)

        mean_frame_time = float(dt.mean())
        worst_frame_time = float(dt.max())

        row = {
            "Condition": group["Condition"].iloc[0],
            "CaptureName": capture_name,
            "SampleCount": most_common_value(group, "SampleCount"),
            "Samples": int(len(group)),
            "DurationSeconds": duration,
            "MeanFrameTimeMs": mean_frame_time,
            "MedianFrameTimeMs": p50,
            "P95FrameTimeMs": p95,
            "P99FrameTimeMs": p99,
            "P999FrameTimeMs": p999,
            "WorstFrameTimeMs": worst_frame_time,
            "FPSFromMeanFrameTime": fps_from_ms(mean_frame_time),
            "OnePercentLowFPS": fps_from_ms(p99),
            "PointOnePercentLowFPS": fps_from_ms(p999),
            "FramesOverBudget": int((dt > frame_budget_ms).sum()),
            "PercentFramesOverBudget": float((dt > frame_budget_ms).mean() * 100.0),
            "HitchCount": int(group["bIsHitch"].fillna(0).astype(int).sum())
            if "bIsHitch" in group.columns
            else np.nan,
            "MeanMeasuredCodeMs": group["MeasuredCodeTotalMs"].mean()
            if "MeasuredCodeTotalMs" in group.columns
            else np.nan,
            "MaxMeasuredCodeMs": group["MeasuredCodeTotalMs"].max()
            if "MeasuredCodeTotalMs" in group.columns
            else np.nan,
            "MeanNiagaraParticles": group["EstimatedActiveNiagaraParticles"]
            .replace(-1, np.nan)
            .mean()
            if "EstimatedActiveNiagaraParticles" in group.columns
            else np.nan,
            "MaxNiagaraParticles": group["EstimatedActiveNiagaraParticles"]
            .replace(-1, np.nan)
            .max()
            if "EstimatedActiveNiagaraParticles" in group.columns
            else np.nan,
            "MeanActiveNiagaraComponents": group["ActiveNiagaraComponentCount"].mean()
            if "ActiveNiagaraComponentCount" in group.columns
            else np.nan,
            "MaxActiveNiagaraComponents": group["ActiveNiagaraComponentCount"].max()
            if "ActiveNiagaraComponentCount" in group.columns
            else np.nan,
            "MeanUsedPhysicalMemoryMB": group["UsedPhysicalMemoryMB"].mean()
            if "UsedPhysicalMemoryMB" in group.columns
            else np.nan,
            "MaxUsedPhysicalMemoryMB": group["UsedPhysicalMemoryMB"].max()
            if "UsedPhysicalMemoryMB" in group.columns
            else np.nan,
            "ViewportWidth": most_common_value(group, "ViewportWidth"),
            "ViewportHeight": most_common_value(group, "ViewportHeight"),
            "ScreenPercentage": most_common_value(group, "ScreenPercentage"),
            "VRPixelDensity": most_common_value(group, "VRPixelDensity"),
            "HMDDeviceName": most_common_value(group, "HMDDeviceName"),
        }

        rows.append(row)

    return pd.DataFrame(rows)


def summarize_conditions(run_summary: pd.DataFrame) -> pd.DataFrame:
    if run_summary.empty:
        return pd.DataFrame()

    metrics = [
        "MeanFrameTimeMs",
        "MedianFrameTimeMs",
        "P95FrameTimeMs",
        "P99FrameTimeMs",
        "WorstFrameTimeMs",
        "FPSFromMeanFrameTime",
        "OnePercentLowFPS",
        "PercentFramesOverBudget",
        "HitchCount",
        "MeanMeasuredCodeMs",
        "MeanNiagaraParticles",
        "MaxNiagaraParticles",
        "MeanUsedPhysicalMemoryMB",
    ]

    rows = []

    for condition, group in run_summary.groupby("Condition", sort=True):
        row = {
            "Condition": condition,
            "Runs": int(len(group)),
        }

        for metric in metrics:
            if metric not in group.columns:
                continue

            values = pd.to_numeric(group[metric], errors="coerce").dropna()
            if values.empty:
                row[f"{metric}_Mean"] = np.nan
                row[f"{metric}_Std"] = np.nan
                row[f"{metric}_Median"] = np.nan
                continue

            row[f"{metric}_Mean"] = float(values.mean())
            row[f"{metric}_Std"] = float(values.std(ddof=1)) if len(values) > 1 else 0.0
            row[f"{metric}_Median"] = float(values.median())

        rows.append(row)

    return pd.DataFrame(rows)


def summarize_markers(frame_df: pd.DataFrame) -> pd.DataFrame:
    if frame_df.empty or "Marker" not in frame_df.columns:
        return pd.DataFrame()

    df = frame_df.copy()
    df["Marker"] = df["Marker"].fillna("").replace("", "Unmarked")

    rows = []

    for keys, group in df.groupby(["Condition", "CaptureName", "Marker"], sort=True):
        condition, capture_name, marker = keys
        dt = group["DeltaTimeMs"].dropna()
        if dt.empty:
            continue

        rows.append(
            {
                "Condition": condition,
                "CaptureName": capture_name,
                "Marker": marker,
                "Samples": int(len(group)),
                "MeanFrameTimeMs": float(dt.mean()),
                "MedianFrameTimeMs": percentile(dt, 0.50),
                "P95FrameTimeMs": percentile(dt, 0.95),
                "P99FrameTimeMs": percentile(dt, 0.99),
                "WorstFrameTimeMs": float(dt.max()),
                "FPSFromMeanFrameTime": fps_from_ms(float(dt.mean())),
                "MeanMeasuredCodeMs": group["MeasuredCodeTotalMs"].mean()
                if "MeasuredCodeTotalMs" in group.columns
                else np.nan,
                "MeanNiagaraParticles": group["EstimatedActiveNiagaraParticles"]
                .replace(-1, np.nan)
                .mean()
                if "EstimatedActiveNiagaraParticles" in group.columns
                else np.nan,
            }
        )

    return pd.DataFrame(rows)

def compute_marker_intervals(frame_df: pd.DataFrame) -> pd.DataFrame:
    """
    Computes time intervals between marker changes.

    Each row describes one continuous marker segment:
        CaptureName, Marker, StartSeconds, EndSeconds, DurationSeconds
    """
    if frame_df.empty or "Marker" not in frame_df.columns or "TimestampSeconds" not in frame_df.columns:
        return pd.DataFrame()

    rows = []

    df = frame_df.copy()
    df["Marker"] = df["Marker"].fillna("").replace("", "Unmarked")

    for capture_name, group in df.groupby("CaptureName", sort=True):
        group = group.sort_values("TimestampSeconds").reset_index(drop=True)

        if group.empty:
            continue

        condition = group["Condition"].iloc[0] if "Condition" in group.columns else ""
        current_marker = group.loc[0, "Marker"]
        start_time = float(group.loc[0, "TimestampSeconds"])
        start_frame = int(group.loc[0, "FrameIndex"]) if "FrameIndex" in group.columns else 0

        for i in range(1, len(group)):
            marker = group.loc[i, "Marker"]

            if marker != current_marker:
                end_time = float(group.loc[i - 1, "TimestampSeconds"])
                end_frame = int(group.loc[i - 1, "FrameIndex"]) if "FrameIndex" in group.columns else i - 1

                rows.append(
                    {
                        "Condition": condition,
                        "CaptureName": capture_name,
                        "Marker": current_marker,
                        "StartSeconds": start_time,
                        "EndSeconds": end_time,
                        "DurationSeconds": max(0.0, end_time - start_time),
                        "StartFrame": start_frame,
                        "EndFrame": end_frame,
                        "Frames": max(0, end_frame - start_frame + 1),
                    }
                )

                current_marker = marker
                start_time = float(group.loc[i, "TimestampSeconds"])
                start_frame = int(group.loc[i, "FrameIndex"]) if "FrameIndex" in group.columns else i

        end_time = float(group.loc[len(group) - 1, "TimestampSeconds"])
        end_frame = int(group.loc[len(group) - 1, "FrameIndex"]) if "FrameIndex" in group.columns else len(group) - 1

        rows.append(
            {
                "Condition": condition,
                "CaptureName": capture_name,
                "Marker": current_marker,
                "StartSeconds": start_time,
                "EndSeconds": end_time,
                "DurationSeconds": max(0.0, end_time - start_time),
                "StartFrame": start_frame,
                "EndFrame": end_frame,
                "Frames": max(0, end_frame - start_frame + 1),
            }
        )

    return pd.DataFrame(rows)


def summarize_marker_durations(marker_intervals: pd.DataFrame) -> pd.DataFrame:
    if marker_intervals.empty:
        return pd.DataFrame()

    return (
        marker_intervals
        .groupby(["Condition", "CaptureName", "Marker"], as_index=False)
        .agg(
            Occurrences=("Marker", "count"),
            TotalDurationSeconds=("DurationSeconds", "sum"),
            MeanDurationSeconds=("DurationSeconds", "mean"),
            MedianDurationSeconds=("DurationSeconds", "median"),
            MaxDurationSeconds=("DurationSeconds", "max"),
            TotalFrames=("Frames", "sum"),
        )
        .sort_values(["Condition", "CaptureName", "Marker"])
    )


def summarize_code_scopes(code_df: pd.DataFrame) -> pd.DataFrame:
    if code_df.empty or "ScopeName" not in code_df.columns:
        return pd.DataFrame()

    rows = []

    for keys, group in code_df.groupby(["Condition", "CaptureName", "ScopeName"], sort=True):
        condition, capture_name, scope_name = keys

        total_ms = pd.to_numeric(group["TotalMs"], errors="coerce").dropna()
        max_ms = pd.to_numeric(group["MaxMs"], errors="coerce").dropna()
        calls = pd.to_numeric(group["Calls"], errors="coerce").fillna(0)

        rows.append(
            {
                "Condition": condition,
                "CaptureName": capture_name,
                "ScopeName": scope_name,
                "SamplesWithScope": int(len(group)),
                "TotalCalls": int(calls.sum()),
                "TotalMs": float(total_ms.sum()) if not total_ms.empty else np.nan,
                "MeanTotalMsPerSample": float(total_ms.mean()) if not total_ms.empty else np.nan,
                "MedianTotalMsPerSample": percentile(total_ms, 0.50),
                "P95TotalMsPerSample": percentile(total_ms, 0.95),
                "P99TotalMsPerSample": percentile(total_ms, 0.99),
                "MaxSingleSampleTotalMs": float(total_ms.max()) if not total_ms.empty else np.nan,
                "MeanMaxMs": float(max_ms.mean()) if not max_ms.empty else np.nan,
                "P95MaxMs": percentile(max_ms, 0.95),
                "WorstMaxMs": float(max_ms.max()) if not max_ms.empty else np.nan,
            }
        )

    return pd.DataFrame(rows)


def summarize_code_by_condition(code_scope_summary: pd.DataFrame) -> pd.DataFrame:
    if code_scope_summary.empty:
        return pd.DataFrame()

    rows = []

    for keys, group in code_scope_summary.groupby(["Condition", "ScopeName"], sort=True):
        condition, scope_name = keys

        rows.append(
            {
                "Condition": condition,
                "ScopeName": scope_name,
                "RunsWithScope": int(group["CaptureName"].nunique()),
                "TotalCalls": int(group["TotalCalls"].sum()),
                "TotalMs": float(group["TotalMs"].sum()),
                "MeanTotalMsPerSample_MeanAcrossRuns": float(
                    group["MeanTotalMsPerSample"].mean()
                ),
                "P95TotalMsPerSample_MeanAcrossRuns": float(
                    group["P95TotalMsPerSample"].mean()
                ),
                "WorstMaxMsAcrossRuns": float(group["WorstMaxMs"].max()),
            }
        )

    return pd.DataFrame(rows)


def most_common_value(df: pd.DataFrame, column: str):
    if column not in df.columns:
        return np.nan
    values = df[column].dropna()
    if values.empty:
        return np.nan
    return values.mode().iloc[0]


def write_table(df: pd.DataFrame, output_dir: Path, filename: str) -> None:
    if df.empty:
        return
    df.to_csv(output_dir / filename, index=False)


def save_plot(path: Path) -> None:
    plt.tight_layout()
    plt.savefig(path, dpi=200)
    plt.close()


def plot_frame_time_boxplot(frame_df: pd.DataFrame, output_dir: Path) -> None:
    """
    Creates a frame-time boxplot ordered by sample count if the capture names
    start with values such as 1.000.000_, 500.000_, or 1000000_.
    """
    if frame_df.empty:
        return

    required = ["CaptureName", "DeltaTimeMs"]
    if not all(col in frame_df.columns for col in required):
        return

    df = frame_df.copy()

    if "SampleCount" not in df.columns:
        df["SampleCount"] = df["CaptureName"].apply(extract_sample_count_from_capture_name)

    capture_info = (
        df.groupby("CaptureName", as_index=False)
        .agg(
            SampleCount=("SampleCount", "first"),
            MedianFrameTimeMs=("DeltaTimeMs", "median"),
        )
    )

    # Captures with parsed sample counts come first, ordered ascending.
    # Captures without sample counts come after, ordered by name.
    capture_info["HasSampleCount"] = capture_info["SampleCount"].notna()

    capture_info = capture_info.sort_values(
        by=["HasSampleCount", "SampleCount", "CaptureName"],
        ascending=[False, True, True],
    )

    captures = capture_info["CaptureName"].tolist()

    data = []
    labels = []

    for cap in captures:
        values = pd.to_numeric(
            df.loc[df["CaptureName"] == cap, "DeltaTimeMs"],
            errors="coerce",
        ).dropna().values

        if len(values) == 0:
            continue

        sample_count = capture_info.loc[
            capture_info["CaptureName"] == cap,
            "SampleCount",
        ].iloc[0]

        if pd.notna(sample_count):
            label = f"{int(sample_count):,}".replace(",", ".")
        else:
            label = cap

        data.append(values)
        labels.append(label)

    if not data:
        return

    plt.figure(figsize=(max(8, len(labels) * 0.65), 6))

    # Matplotlib 3.11+ uses tick_labels instead of labels.
    plt.boxplot(data, tick_labels=labels, showfliers=False)

    plt.xticks(rotation=45, ha="right")
    plt.xlabel("Sample count")
    plt.ylabel("Frame time (ms)")
    plt.title("Frame-time distribution ordered by sample count")
    plt.grid(axis="y", alpha=0.3)

    save_plot(output_dir / "frame_time_boxplot_by_capture.png")

    # Duplicate with a clearer name
    plt.figure(figsize=(max(8, len(labels) * 0.65), 6))
    plt.boxplot(data, tick_labels=labels, showfliers=False)
    plt.xticks(rotation=45, ha="right")
    plt.xlabel("Sample count")
    plt.ylabel("Frame time (ms)")
    plt.title("Frame-time distribution ordered by sample count")
    plt.grid(axis="y", alpha=0.3)
    save_plot(output_dir / "frame_time_boxplot_by_sample_count.png")


def plot_bar(
    df: pd.DataFrame,
    x_col: str,
    y_col: str,
    title: str,
    ylabel: str,
    output_path: Path,
    sort_desc: bool = True,
    top_n: Optional[int] = None,
) -> None:
    if df.empty or x_col not in df.columns or y_col not in df.columns:
        return

    plot_df = df[[x_col, y_col]].dropna().copy()
    if plot_df.empty:
        return

    plot_df = plot_df.sort_values(y_col, ascending=not sort_desc)
    if top_n:
        plot_df = plot_df.head(top_n)

    plt.figure(figsize=(max(8, len(plot_df) * 0.65), 6))
    plt.bar(plot_df[x_col].astype(str), plot_df[y_col])
    plt.xticks(rotation=45, ha="right")
    plt.ylabel(ylabel)
    plt.title(title)
    plt.grid(axis="y", alpha=0.3)

    save_plot(output_path)


def plot_frame_time_timeseries(frame_df: pd.DataFrame, output_dir: Path) -> None:
    if frame_df.empty or "TimestampSeconds" not in frame_df.columns:
        return

    captures = list(frame_df["CaptureName"].dropna().unique())
    if not captures:
        return

    plt.figure(figsize=(12, 6))

    for capture in captures[:12]:
        group = frame_df[frame_df["CaptureName"] == capture]
        if group.empty:
            continue
        plt.plot(group["TimestampSeconds"], group["DeltaTimeMs"], label=capture, linewidth=1)

    plt.xlabel("Time (s)")
    plt.ylabel("Frame time (ms)")
    plt.title("Frame time over time")
    plt.grid(alpha=0.3)

    if len(captures) <= 12:
        plt.legend(fontsize=8)

    save_plot(output_dir / "frame_time_timeseries_overview.png")


def plot_niagara(frame_df: pd.DataFrame, output_dir: Path) -> None:
    if frame_df.empty or "EstimatedActiveNiagaraParticles" not in frame_df.columns:
        return

    df = frame_df.copy()
    df["EstimatedActiveNiagaraParticles"] = df["EstimatedActiveNiagaraParticles"].replace(-1, np.nan)

    summary = (
        df.groupby("CaptureName", as_index=False)["EstimatedActiveNiagaraParticles"]
        .mean()
        .rename(columns={"EstimatedActiveNiagaraParticles": "MeanNiagaraParticles"})
        .dropna()
    )

    plot_bar(
        summary,
        "CaptureName",
        "MeanNiagaraParticles",
        "Mean estimated Niagara particles by capture",
        "Mean estimated active particles",
        output_dir / "niagara_particles_by_capture.png",
    )


def plot_code_scopes(code_scope_summary: pd.DataFrame, output_dir: Path) -> None:
    if code_scope_summary.empty:
        return

    by_scope = (
        code_scope_summary.groupby("ScopeName", as_index=False)
        .agg(
            TotalMs=("TotalMs", "sum"),
            P95TotalMsPerSample=("P95TotalMsPerSample", "mean"),
            WorstMaxMs=("WorstMaxMs", "max"),
            TotalCalls=("TotalCalls", "sum"),
        )
        .sort_values("TotalMs", ascending=False)
    )

    plot_bar(
        by_scope,
        "ScopeName",
        "TotalMs",
        "Top C++ scopes by total measured time",
        "Total measured time (ms)",
        output_dir / "top_code_scopes_total_ms.png",
        top_n=20,
    )

    plot_bar(
        by_scope.sort_values("P95TotalMsPerSample", ascending=False),
        "ScopeName",
        "P95TotalMsPerSample",
        "Top C++ scopes by P95 sample time",
        "P95 total time per sample (ms)",
        output_dir / "top_code_scopes_p95_ms.png",
        top_n=20,
    )

def plot_fps_vs_niagara(frame_df: pd.DataFrame, output_dir: Path) -> None:
    """
    Scatter plot showing FPS in relation to estimated active Niagara particles.
    """
    required = ["EstimatedActiveNiagaraParticles", "FPS"]
    if frame_df.empty or not all(col in frame_df.columns for col in required):
        return

    df = frame_df.copy()
    df["EstimatedActiveNiagaraParticles"] = pd.to_numeric(
        df["EstimatedActiveNiagaraParticles"], errors="coerce"
    )
    df["FPS"] = pd.to_numeric(df["FPS"], errors="coerce")

    df = df.replace({"EstimatedActiveNiagaraParticles": {-1: np.nan}})
    df = df.dropna(subset=["EstimatedActiveNiagaraParticles", "FPS"])

    if df.empty:
        return

    plt.figure(figsize=(8, 6))
    plt.scatter(
        df["EstimatedActiveNiagaraParticles"],
        df["FPS"],
        alpha=0.35,
        s=12,
    )

    plt.xlabel("Estimated active Niagara particles")
    plt.ylabel("FPS")
    plt.title("FPS in relation to Niagara particle count")
    plt.grid(alpha=0.3)

    save_plot(output_dir / "fps_vs_niagara_particles.png")


def plot_frame_time_vs_niagara(frame_df: pd.DataFrame, output_dir: Path) -> None:
    """
    Scatter plot showing frame time in relation to estimated active Niagara particles.
    This is often more useful than FPS for VR analysis.
    """
    required = ["EstimatedActiveNiagaraParticles", "DeltaTimeMs"]
    if frame_df.empty or not all(col in frame_df.columns for col in required):
        return

    df = frame_df.copy()
    df["EstimatedActiveNiagaraParticles"] = pd.to_numeric(
        df["EstimatedActiveNiagaraParticles"], errors="coerce"
    )
    df["DeltaTimeMs"] = pd.to_numeric(df["DeltaTimeMs"], errors="coerce")

    df = df.replace({"EstimatedActiveNiagaraParticles": {-1: np.nan}})
    df = df.dropna(subset=["EstimatedActiveNiagaraParticles", "DeltaTimeMs"])

    if df.empty:
        return

    plt.figure(figsize=(8, 6))
    plt.scatter(
        df["EstimatedActiveNiagaraParticles"],
        df["DeltaTimeMs"],
        alpha=0.35,
        s=12,
    )

    plt.xlabel("Estimated active Niagara particles")
    plt.ylabel("Frame time (ms)")
    plt.title("Frame time in relation to Niagara particle count")
    plt.grid(alpha=0.3)

    save_plot(output_dir / "frame_time_vs_niagara_particles.png")


def plot_marker_duration_overview(marker_durations: pd.DataFrame, output_dir: Path) -> None:
    """
    Bar plot showing total time spent in each marker per capture.
    """
    if marker_durations.empty:
        return

    required = ["CaptureName", "Marker", "TotalDurationSeconds"]
    if not all(col in marker_durations.columns for col in required):
        return

    plot_df = marker_durations.copy()
    plot_df["Label"] = plot_df["CaptureName"].astype(str) + " | " + plot_df["Marker"].astype(str)

    plot_df = plot_df.sort_values("TotalDurationSeconds", ascending=False)

    plt.figure(figsize=(max(10, len(plot_df) * 0.35), 7))
    plt.bar(plot_df["Label"], plot_df["TotalDurationSeconds"])
    plt.xticks(rotation=60, ha="right")
    plt.ylabel("Duration (s)")
    plt.title("Time spent in markers")
    plt.grid(axis="y", alpha=0.3)

    save_plot(output_dir / "marker_duration_overview.png")


def plot_marker_timeline(marker_intervals: pd.DataFrame, output_dir: Path) -> None:
    """
    Timeline plot showing when marker intervals occurred during each capture.
    """
    if marker_intervals.empty:
        return

    required = ["CaptureName", "Marker", "StartSeconds", "DurationSeconds"]
    if not all(col in marker_intervals.columns for col in required):
        return

    captures = list(marker_intervals["CaptureName"].dropna().unique())
    if not captures:
        return

    capture_to_y = {capture: idx for idx, capture in enumerate(captures)}

    plt.figure(figsize=(12, max(5, len(captures) * 0.5)))

    for _, row in marker_intervals.iterrows():
        capture = row["CaptureName"]
        y = capture_to_y[capture]

        plt.barh(
            y=y,
            width=row["DurationSeconds"],
            left=row["StartSeconds"],
            height=0.6,
        )

        if row["DurationSeconds"] > 0.5:
            plt.text(
                row["StartSeconds"] + row["DurationSeconds"] / 2.0,
                y,
                str(row["Marker"]),
                va="center",
                ha="center",
                fontsize=8,
            )

    plt.yticks(list(capture_to_y.values()), list(capture_to_y.keys()))
    plt.xlabel("Time since capture start (s)")
    plt.ylabel("Capture")
    plt.title("Marker timeline")
    plt.grid(axis="x", alpha=0.3)

    save_plot(output_dir / "marker_timeline.png")


def create_all_plots(
    frame_df: pd.DataFrame,
    run_summary: pd.DataFrame,
    code_scope_summary: pd.DataFrame,
    marker_intervals: pd.DataFrame,
    marker_durations: pd.DataFrame,
    output_dir: Path,
) -> None:
    plot_frame_time_boxplot(frame_df, output_dir)
    plot_frame_time_timeseries(frame_df, output_dir)

    plot_bar(
        run_summary,
        "CaptureName",
        "P95FrameTimeMs",
        "P95 frame time by capture",
        "P95 frame time (ms)",
        output_dir / "frame_time_p95_by_capture.png",
    )

    plot_bar(
        run_summary,
        "CaptureName",
        "OnePercentLowFPS",
        "1% low FPS by capture",
        "1% low FPS",
        output_dir / "fps_1pct_low_by_capture.png",
        sort_desc=False,
    )

    plot_bar(
        run_summary,
        "CaptureName",
        "HitchCount",
        "Hitch count by capture",
        "Hitches",
        output_dir / "hitches_by_capture.png",
    )

    plot_niagara(frame_df, output_dir)

    # New Niagara relationship plots
    plot_fps_vs_niagara(frame_df, output_dir)
    plot_frame_time_vs_niagara(frame_df, output_dir)

    # New marker duration plots
    plot_marker_duration_overview(marker_durations, output_dir)
    plot_marker_timeline(marker_intervals, output_dir)

    plot_code_scopes(code_scope_summary, output_dir)


def print_brief_summary(run_summary: pd.DataFrame, condition_summary: pd.DataFrame) -> None:
    if run_summary.empty:
        print("No frame captures found.")
        return

    print("\n=== Runs ===")
    cols = [
        "CaptureName",
        "Condition",
        "DurationSeconds",
        "MeanFrameTimeMs",
        "P95FrameTimeMs",
        "P99FrameTimeMs",
        "OnePercentLowFPS",
        "HitchCount",
    ]
    cols = [c for c in cols if c in run_summary.columns]
    print(run_summary[cols].round(3).to_string(index=False))

    if not condition_summary.empty:
        print("\n=== Conditions ===")
        condition_cols = [
            "Condition",
            "Runs",
            "MeanFrameTimeMs_Mean",
            "P95FrameTimeMs_Mean",
            "P99FrameTimeMs_Mean",
            "OnePercentLowFPS_Mean",
            "HitchCount_Mean",
        ]
        condition_cols = [c for c in condition_cols if c in condition_summary.columns]
        print(condition_summary[condition_cols].round(3).to_string(index=False))


def main() -> None:
    args = parse_args()

    input_dir = Path(args.input)
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    if not input_dir.exists():
        raise FileNotFoundError(f"Input directory does not exist: {input_dir}")

    print(f"Reading captures from: {input_dir}")
    print(f"Writing report to: {output_dir}")

    frame_df, code_files_by_base_id = load_frame_files(
        input_dir=input_dir,
        recursive=args.recursive,
        condition_regex=args.condition_regex,
        drop_first_sec=args.drop_first_sec,
    )

    code_df = load_code_files(
        code_files_by_base_id=code_files_by_base_id,
        frame_df=frame_df,
        condition_regex=args.condition_regex,
        drop_first_sec=args.drop_first_sec,
    )

    if frame_df.empty:
        print("No frame CSV files found. Check the input directory.")
        return

    run_summary = summarize_runs(frame_df, refresh_rate=args.refresh_rate)
    condition_summary = summarize_conditions(run_summary)

    marker_summary = summarize_markers(frame_df)
    marker_intervals = compute_marker_intervals(frame_df)
    marker_durations = summarize_marker_durations(marker_intervals)

    code_scope_summary = summarize_code_scopes(code_df)
    code_condition_summary = summarize_code_by_condition(code_scope_summary)

    write_table(run_summary, output_dir, "summary_runs.csv")
    write_table(condition_summary, output_dir, "summary_conditions.csv")
    write_table(marker_summary, output_dir, "summary_markers.csv")
    write_table(marker_intervals, output_dir, "summary_marker_intervals.csv")
    write_table(marker_durations, output_dir, "summary_marker_durations.csv")

    write_table(code_scope_summary, output_dir, "summary_code_scopes.csv")
    write_table(code_condition_summary, output_dir, "summary_code_scopes_by_condition.csv")

    create_all_plots(
        frame_df=frame_df,
        run_summary=run_summary,
        code_scope_summary=code_scope_summary,
        marker_intervals=marker_intervals,
        marker_durations=marker_durations,
        output_dir=output_dir,
    )

    print_brief_summary(run_summary, condition_summary)

    print("\nCreated:")
    for file in sorted(output_dir.glob("*")):
        print(f"  {file}")


if __name__ == "__main__":
    main()