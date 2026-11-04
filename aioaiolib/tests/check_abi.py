#!/usr/bin/env python3
"""Inspect the production library without loading it or touching devices."""
import pathlib
import re
import subprocess
import sys

library, map_file = map(pathlib.Path, sys.argv[1:])
dynamic = subprocess.check_output(["readelf", "-d", str(library)], text=True)
assert "Library soname: [libaioaio.so.1]" in dynamic, dynamic
assert not re.search(r"\((?:RPATH|RUNPATH)\)", dynamic), dynamic

symbol_output = subprocess.check_output(
    ["nm", "-D", "--defined-only", str(library)], text=True
)
symbols = {line.split()[-1].split("@")[0] for line in symbol_output.splitlines()}
symbols.discard("AIOAIO_1.0")
expected = set(re.findall(r"^\s+((?:AIO|BRD|ADC|DAC|PIO)_\w+);", map_file.read_text(), re.M))
assert symbols == expected, f"missing={expected - symbols}; unexpected={symbols - expected}"
for link in ("libaioaio.so", "libaioaio.so.1"):
    path = library.parent / link
    assert path.is_symlink() and path.resolve() == library.resolve(), path
print(f"ABI: SONAME, library links, and {len(symbols)} public C symbols verified")
