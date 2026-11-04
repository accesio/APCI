#!/usr/bin/env python3
"""Exercise the short check_dma example using only the fake transport."""
import os
import pathlib
import subprocess
import sys
import tempfile

runner = pathlib.Path(sys.argv[1]).resolve()


def run(cwd, args, expected=0, mode="success", no_open=False):
    env = dict(os.environ, FAKE_MODE=mode)
    if no_open:
        env["EXPECT_NO_OPEN"] = "1"
    result = subprocess.run([str(runner), *args], cwd=cwd, env=env,
                            text=True, capture_output=True, timeout=10)
    assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
    return result


with tempfile.TemporaryDirectory(prefix="aioaio-simple-example-") as directory:
    cwd = pathlib.Path(directory)
    result = run(cwd, ["fake:slow"])
    assert "Captured 13 / 13 ADC results" in result.stdout, result.stdout
    assert "discarded DMA slots: 0" in result.stdout, result.stdout
    assert not list(cwd.iterdir()), "simple example wrote an unexpected file"
    run(cwd, ["--help"], no_open=True)
    run(cwd, ["one", "two"], expected=2, no_open=True)
    run(cwd, ["fake:slow"], expected=1, mode="timeout")
    run(cwd, ["fake:slow"], expected=1, mode="drop")
    run(cwd, ["fake:slow"], expected=130, mode="signal")

print("check_dma: finite capture, no file output, help/arguments, timeout, overflow, and cancellation passed")
