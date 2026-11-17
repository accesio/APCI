#!/usr/bin/env python3
"""Run sample binaries linked only to API stubs, never the hardware library."""
import os
import subprocess
import sys


def run(program, case, arguments=(), expected=0, device=None):
    env = dict(os.environ, DAC_SAMPLE_CASE=case)
    if device:
        env["DAC_SAMPLE_DEVICE"] = device
    result = subprocess.run([program, *arguments], env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
    assert result.returncode == expected, (program, case, result.returncode,
                                           result.stdout, result.stderr)
    return result.stdout + result.stderr


for program in sys.argv[1:]:
    assert "Usage:" in run(program, "no_open", ["--help"])
    run(program, "no_open", ["--bogus"], expected=2)
    run(program, "no_open", ["/dev/one", "/dev/two"], expected=2)
    output = run(program, "success")
    assert "Host uploads: 12345 scalar DAC samples" in output
    assert "not measured physical playback" in output
    run(program, "success", ["/dev/apci/explicit_fds_1"], device="/dev/apci/explicit_fds_1")
    output = run(program, "open_failure", ["/dev/apci/missing"], expected=1, device="/dev/apci/missing")
    assert "apci.ko" in output
    assert "ADC-only" in run(program, "unsupported", expected=1)
    assert "DAC_StartWaveform" in run(program, "start_failure", expected=1)
    assert "DAC playback" in run(program, "underrun", expected=1)
    run(program, "interrupt", expected=128 + 2)
print("DAC samples: modes, waveform layout, exact paths, errors, cleanup and interruption passed")
