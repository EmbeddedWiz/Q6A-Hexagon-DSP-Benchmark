#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 R.A.
"""Check the report's structural assumptions and Markdown hygiene.

This is intentionally a small, dependency-free check. It verifies that the
paths named by the report exist and that the Markdown files do not contain
zero-width characters or malformed ATX headings. It does not try to validate
every prose claim; the run logs and SHA-256 files do that.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

REQUIRED = [
    "README.md",
    "LICENSE",
    "docs/architecture.md",
    "docs/benchmark-methodology.md",
    "docs/decisions-log.md",
    "docs/design-decisions.md",
    "docs/engineering-findings.md",
    "docs/report-details.md",
    "docs/energy-and-power.md",
    "docs/fastrpc-result-contract.md",
    "docs/results/README.md",
    "docs/scope-review.md",
    "docs/toolchain-and-build.md",
    "docs/validation.md",
    "docs/workloads/black-scholes.md",
    "docs/workloads/ecc.md",
    "docs/workloads/motor-control.md",
    "docs/workloads/radar.md",
    "docs/rt-report.md",
    "config.example",
    "dsp/idl/foc_bench.idl",
    "dsp/idl/hvx_bench.idl",
    "dsp/fastrpc/foc_bench_skel_impl.cpp",
    "dsp/fastrpc/hvx_bench_impl.cpp",
    "dsp/fastrpc/hvx_vector_add.c",
    "dsp/fastrpc/hvx_cycle_benchmark.c",
    "host/foc_runner.cpp",
    "tools/rt_analyze.cpp",
    "scripts/build_foc_bench.sh",
    "scripts/run_foc_suite.sh",
    "scripts/summarize_foc.py",
    "scripts/collect_environment.sh",
    "scripts/build_and_run_hvx.sh",
    "validation/foc/foc_reference.cpp",
    "validation/foc/foc_reference.h",
    "validation/foc/foc_vectors.cpp",
    "validation/foc/foc_vectors.h",
    "workloads/motor_control/foc_fixed.cpp",
    "workloads/motor_control/foc_fixed.h",
    "workloads/motor_control/motor_control_fixed.cpp",
    "workloads/motor_control/motor_control_fixed.h",
]

MARKDOWN = [
    "README.md",
    "docs/rt-report.md",
    "docs/architecture.md",
    "docs/benchmark-methodology.md",
    "docs/decisions-log.md",
    "docs/design-decisions.md",
    "docs/engineering-findings.md",
    "docs/report-details.md",
    "docs/energy-and-power.md",
    "docs/fastrpc-result-contract.md",
    "docs/results/README.md",
    "docs/scope-review.md",
    "docs/toolchain-and-build.md",
    "docs/validation.md",
    "docs/workloads/black-scholes.md",
    "docs/workloads/ecc.md",
    "docs/workloads/motor-control.md",
    "docs/workloads/radar.md",
]

ZERO_WIDTH = {
    "\u200b": "ZERO WIDTH SPACE",
    "\u200c": "ZERO WIDTH NON-JOINER",
    "\u200d": "ZERO WIDTH JOINER",
    "\ufeff": "ZERO WIDTH NO-BREAK SPACE / BOM",
}


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.cwd()
    failures: list[str] = []

    for relative in REQUIRED:
        path = root / relative
        if not path.exists():
            failures.append(f"missing required path: {relative}")
        elif (os.name == "posix" and relative.endswith(".sh") and
              not path.stat().st_mode & 0o111):
            failures.append(f"script is not executable: {relative}")

    for relative in MARKDOWN:
        path = root / relative
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8")
        for char, name in ZERO_WIDTH.items():
            if char in text:
                failures.append(f"{relative}: contains {name}")
        in_fence = False
        for number, line in enumerate(text.splitlines(), 1):
            stripped = line.lstrip()
            if stripped.startswith("```"):
                in_fence = not in_fence
                continue
            if in_fence:
                continue
            if stripped.startswith("#"):
                hashes = len(stripped) - len(stripped.lstrip("#"))
                rest = stripped[hashes:]
                if hashes > 6:
                    failures.append(
                        f"{relative}:{number}: heading has more than six #"
                    )
                if rest and not rest.startswith(" "):
                    failures.append(
                        f"{relative}:{number}: heading # must be followed by space"
                    )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print(f"check_repo: OK ({len(REQUIRED)} paths, {len(MARKDOWN)} Markdown files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
