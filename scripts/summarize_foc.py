#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 R.A.
"""Summarize one run produced by scripts/run_foc_suite.sh.

Reads the JSON lines emitted by the host runner and writes summary.json plus a
Markdown table. The summary keeps both the raw records and their spreads; the
raw logs remain the source of truth.
"""

from __future__ import annotations

import json
import statistics
import sys
from pathlib import Path


def read_json_lines(path: Path) -> list[dict]:
    if not path.exists():
        return []
    records = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return records


def spread(values: list[float]) -> dict:
    if not values:
        return {
            "samples": [],
            "min": None,
            "median": None,
            "mean": None,
            "max": None,
        }
    return {
        "samples": values,
        "min": min(values),
        "median": statistics.median(values),
        "mean": statistics.fmean(values),
        "max": max(values),
    }


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: summarize_foc.py <run-directory>", file=sys.stderr)
        return 2

    run = Path(sys.argv[1])
    validation = read_json_lines(run / "validation.log")
    validation_pi = read_json_lines(run / "validation-pi.log")
    dsp = read_json_lines(run / "bench-dsp.log")
    cpu = read_json_lines(run / "bench-cpu.log")
    hvx = read_json_lines(run / "hvx.log")
    pi = read_json_lines(run / "pi.log")
    echo = read_json_lines(run / "echo.log")

    dsp_cycles = [r["median_cycles_per_step"] for r in dsp]
    dsp_ns = [r["median_ns_per_step"] for r in dsp]
    dsp_all_cycles = [
        value for r in dsp for value in r.get("per_step_cycles", [])
    ]
    dsp_all_ns = [
        value for r in dsp for value in r.get("per_step_ns", [])
    ]
    dsp_ns_no_correction = [
        r["median_ns_per_step_no_correction"] for r in dsp
        if "median_ns_per_step_no_correction" in r
    ]
    dsp_wall_us = [r["rpc_wall_us"] for r in dsp]
    dsp_preflight_us = [
        r["preflight_wall_us"] for r in dsp if "preflight_wall_us" in r
    ]
    dsp_preflight_fraction = [
        100.0 * r["preflight_fraction"] for r in dsp
        if "preflight_fraction" in r
    ]
    dsp_clock = [
        r["effective_clock_hz"] / 1.0e6 for r in dsp
        if "effective_clock_hz" in r
    ]
    dsp_clock_no_correction = [
        r["effective_clock_hz_no_correction"] / 1.0e6 for r in dsp
        if "effective_clock_hz_no_correction" in r
    ]
    cpu_ns = [r["median_ns_per_step"] for r in cpu]
    checksums = sorted({r.get("checksum") for r in dsp + cpu})

    scalar_hvx = [
        r["scalar"]["median_cycles_per_iteration"]
        for r in hvx if "scalar" in r
    ]
    vector_hvx = [
        r["hvx"]["median_cycles_per_iteration"]
        for r in hvx if "hvx" in r
    ]
    hvx_speedups = [
        r["speedup_cycles"] for r in hvx if "speedup_cycles" in r
    ]
    vector_hvx_multiacc = [
        r["hvx_multiacc"]["median_cycles_per_iteration"]
        for r in hvx if "hvx_multiacc" in r
    ]
    hvx_multiacc_speedups = [
        r["speedup_cycles_multiacc"] for r in hvx
        if "speedup_cycles_multiacc" in r
    ]

    summary: dict = {
        "run": run.name,
        "echo": echo[0] if echo else None,
        "validation": validation[0] if validation else None,
        "validation_pi": validation_pi[0] if validation_pi else None,
        "pi": pi[0] if pi else None,
        "dsp": {
            "cycles_per_step": spread(dsp_cycles),
            "all_repetition_cycles_per_step": spread(dsp_all_cycles),
            "ns_per_step": spread(dsp_ns),
            "all_repetition_ns_per_step": spread(dsp_all_ns),
            "ns_per_step_no_correction": spread(dsp_ns_no_correction),
            "rpc_wall_us": spread(dsp_wall_us),
            "preflight_wall_us": spread(dsp_preflight_us),
            "preflight_fraction": spread(dsp_preflight_fraction),
            "effective_clock_hz": spread(dsp_clock),
            "effective_clock_hz_no_correction": spread(
                dsp_clock_no_correction
            ),
            "checksums": checksums,
            "statuses": [r.get("status") for r in dsp],
            "raw_records": dsp,
        },
        "cpu": {
            "ns_per_step": spread(cpu_ns),
            "checksums": sorted({r.get("checksum") for r in cpu}),
            "raw_records": cpu,
        },
        "hvx": {
            "scalar_cycles_per_iteration": spread(scalar_hvx),
            "hvx_cycles_per_iteration": spread(vector_hvx),
            "hvx_multiacc_cycles_per_iteration": spread(vector_hvx_multiacc),
            "speedup_cycles": spread(hvx_speedups),
            "speedup_cycles_multiacc": spread(hvx_multiacc_speedups),
            "checksums_match": all(
                r.get("checksum_match", False) for r in hvx
            ) if hvx else False,
            "results": [r.get("result") for r in hvx],
            "raw_records": hvx,
        },
    }

    if dsp_ns and cpu_ns:
        dsp_median = statistics.median(dsp_ns)
        cpu_median = statistics.median(cpu_ns)
        process_ratios = [d / c for d in dsp_ns for c in cpu_ns]
        summary["ratio"] = {
            "dsp_over_cpu": dsp_median / cpu_median,
            "min": min(process_ratios),
            "max": max(process_ratios),
            "description": (
                "Cross-process median DSP wall-derived ns/step divided by "
                "cross-process median CPU ns/step; min/max pair each DSP "
                "process median with each CPU process median"
            ),
        }

    (run / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")

    def fmt(value: float | None, digits: int = 3) -> str:
        if value is None:
            return "n/a"
        return f"{value:.{digits}f}"

    lines = [
        "# FOC run summary",
        "",
        f"- Run: `{run.name}`",
        f"- Echo: {summary['echo']['result'] if summary['echo'] else 'missing'}",
    ]
    if validation:
        v = validation[0]
        lines += [
            f"- Validation: {v['result']} — {v['dsp_cases']} DSP steps, "
            f"{v['exact_case_mismatches']} exact mismatches, "
            f"{v['fp32_max_abs_lsb']:.3f} LSB max FP32 error",
        ]
    if validation_pi:
        vp = validation_pi[0]
        lines += [
            f"- PI clamp test: {vp['result']} — {vp['cases']} cases, "
            f"{vp['exact_case_mismatches']} exact mismatches, "
            f"{vp['fp32_max_abs_lsb']:.3f} LSB max FP32 error",
        ]
    if pi:
        p = pi[0]
        clamp = p.get("clamp", {})
        long_run = p.get("long", {})
        lines += [
            f"- PI stress: {p.get('result')} — saturation "
            f"d/q={clamp.get('saturated_d')}/{clamp.get('saturated_q')}, "
            f"recovery d/q={clamp.get('recovered_d')}/{clamp.get('recovered_q')}, "
            f"reset={clamp.get('reset_ok')}/{clamp.get('reset_flag_ok')}/"
            f"{clamp.get('configure_reset_ok')}, long "
            f"{long_run.get('steps')} steps, "
            f"{long_run.get('exact_mismatches')} exact mismatches, "
            f"{fmt(long_run.get('fp32_max_abs_lsb'))} LSB max FP32 error",
        ]
    if hvx:
        lines += [
            f"- HVX: {', '.join(str(r.get('result')) for r in hvx)} — "
            f"scalar median {fmt(summary['hvx']['scalar_cycles_per_iteration']['median'])} "
            f"cycles/iteration, HVX median "
            f"{fmt(summary['hvx']['hvx_cycles_per_iteration']['median'])} "
            f"cycles/iteration, speedup "
            f"{fmt(summary['hvx']['speedup_cycles']['median'])}x; "
            f"4-accumulator HVX median "
            f"{fmt(summary['hvx']['hvx_multiacc_cycles_per_iteration']['median'])} "
            f"cycles/iteration, speedup "
            f"{fmt(summary['hvx']['speedup_cycles_multiacc']['median'])}x",
        ]
    lines += [
        "",
        "| Metric | Min | Median | Mean | Max |",
        "|---|---:|---:|---:|---:|",
    ]
    for label, key in (("DSP process-median cycles/step", "cycles_per_step"),
                       ("DSP all 15 cycles/step",
                        "all_repetition_cycles_per_step"),
                       ("DSP process-median ns/step", "ns_per_step"),
                       ("DSP all 15 ns/step", "all_repetition_ns_per_step"),
                       ("DSP ns/step, no preflight subtraction",
                        "ns_per_step_no_correction"),
                       ("DSP RPC wall (us)", "rpc_wall_us"),
                       ("DSP effective clock (MHz)", "effective_clock_hz"),
                       ("DSP clock no correction (MHz)",
                        "effective_clock_hz_no_correction"),
                       ("Preflight share of wall (%)", "preflight_fraction")):
        s = summary["dsp"][key]
        lines.append(
            f"| {label} | {fmt(s['min'])} | {fmt(s['median'])} | "
            f"{fmt(s['mean'])} | {fmt(s['max'])} |"
        )
    s = summary["cpu"]["ns_per_step"]
    lines.append(
        f"| CPU ns/step | {fmt(s['min'])} | {fmt(s['median'])} | "
        f"{fmt(s['mean'])} | {fmt(s['max'])} |"
    )
    if "ratio" in summary:
        ratio = summary["ratio"]
        lines += [
            "",
            f"DSP/CPU time ratio: **{ratio['dsp_over_cpu']:.2f}x** "
            f"(process-median envelope {ratio['min']:.2f}–{ratio['max']:.2f}x)",
        ]
    lines += ["", f"Checksums: {', '.join(str(c) for c in checksums)}"]

    (run / "summary.md").write_text("\n".join(lines) + "\n")
    print(f"wrote {run / 'summary.json'} and {run / 'summary.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
