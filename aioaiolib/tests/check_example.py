#!/usr/bin/env python3
"""Run the actual C example against only the in-memory fake backend."""
import os
import pathlib
import subprocess
import sys
import tempfile

runner = pathlib.Path(sys.argv[1]).resolve()
base = ["--device", "fake:paired"]


def run(cwd, args, expected=0, mode="success", no_open=False, no_start=False):
    env = dict(os.environ, FAKE_MODE=mode)
    if no_open:
        env["EXPECT_NO_OPEN"] = "1"
    if no_start:
        env["EXPECT_NO_START"] = "1"
    result = subprocess.run([str(runner), *args], cwd=cwd, env=env,
                            text=True, capture_output=True, timeout=10)
    assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
    return result


with tempfile.TemporaryDirectory(prefix="aioaio-example-") as directory:
    cwd = pathlib.Path(directory)
    result = run(cwd, base + ["--benchmark", "--samples", "13"])
    assert "delivered 13 / 13 scalar ADC results" in result.stdout, result.stdout
    assert not (cwd / "samples.csv").exists(), "benchmark wrote a CSV"

    for rate in ("250000", "1000000"):
        # Rejection must happen before DMA or truncating an existing CSV.
        preserved = cwd / "preserved.csv"
        preserved.write_text("keep this capture\n")
        result = run(cwd, ["--device", "fake:slow", "--rate", rate,
                          "--csv", str(preserved)], expected=1, no_start=True)
        assert "mPCIe-ADI12-8E" in result.stderr, result.stderr
        assert rate in result.stderr and "100000" in result.stderr, result.stderr
        assert "maximum" in result.stderr, result.stderr
        assert "ADC_AcquireBlock" not in result.stderr, result.stderr
        assert "AIO_Shutdown" not in result.stderr, result.stderr
        assert preserved.read_text() == "keep this capture\n"
    result = run(cwd, ["--device", "fake:slow", "--rate", "100000",
                      "--samples", "13", "--benchmark"])
    assert "delivered 13 / 13 scalar ADC results" in result.stdout, result.stdout

    result = run(cwd, base + ["--benchmark", "--seconds", "0.00065"])
    assert "delivered 13 / 13 scalar ADC results" in result.stdout, result.stdout

    run(cwd, base + ["--samples", "13", "--csv", "capture.csv"])
    rows = (cwd / "capture.csv").read_text().splitlines()
    assert rows[0] == "sample_index,raw_word" and len(rows) == 14, rows
    for index, row in enumerate(rows[1:]):
        channel = (index // 2) % 8 + (index % 2) * 8
        word = (channel << 20) | (7 << 16) | index
        assert row == f"{index},0x{word:08x}", row

    result = run(cwd, base + ["--benchmark", "--samples", "13", "--timeout", "0.03"],
                 expected=1, mode="timeout")
    assert "Capture deadline expired" in result.stderr, result.stderr
    assert "cancelled" in result.stdout, result.stdout
    assert not (cwd / "samples.csv").exists()

    result = run(cwd, base + ["--benchmark", "--samples", "13"], expected=1, mode="drop")
    assert "discarded DMA slots: 2" in result.stdout, result.stdout
    result = run(cwd, base + ["--benchmark", "--samples", "13"], expected=130, mode="signal")
    assert "Capture interrupted by signal" in result.stderr, result.stderr

    run(cwd, base + ["--samples", "13", "--csv", "absent/subdir/file.csv"], expected=1)
    if pathlib.Path("/dev/full").exists():
        result = run(cwd, base + ["--samples", "13", "--csv", "/dev/full"], expected=1)
        assert "CSV" in result.stderr, result.stderr

    run(cwd, ["--help"], no_open=True)
    for args in (["--samples", "0"], ["--samples", "1", "--seconds", "1"],
                 ["--benchmark", "--csv", "bad.csv"], ["--rate", "nan"]):
        run(cwd, args, expected=2, no_open=True)

print("axio_benchmark: rate limits, finite/duration, CSV contents/errors, no-file benchmark, deadline, overflow, signal, and argument checks passed")
