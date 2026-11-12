#!/usr/bin/env python3
"""Compile the affected production methods against minimal engine test doubles.

The DLL requires Windows and ZHMModSDK. This harness extracts the current method
bodies and Trace definition so it tests production logic without copying it or
adding test-only interfaces to the plugin. It does not validate the game ABI.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def block(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    source = (ROOT / "Src/BulletTracer.cpp").read_text()
    header = (ROOT / "Src/BulletTracer.h").read_text()
    constants = source[source.index("constexpr size_t kMaxTraces"):
                       source.index("using SetUpFn")]
    definitions = "\n".join(block(header, name) + ";" for name in
                            ("enum class EKind", "struct Trace"))
    methods = "\n".join(block(source, name) for name in (
        "float Distance(", "BulletTracer::~BulletTracer()",
        "void BulletTracer::Classify(", "void BulletTracer::UpdateTraces(",
        "void BulletTracer::TrimTraces()", "void BulletTracer::RemoveCrippleBox()"))
    harness = (ROOT / "tests/regression.cpp.in").read_text()
    harness = harness.replace("// PRODUCTION_CONSTANTS", constants)
    harness = harness.replace("// PRODUCTION_TYPES", definitions)
    harness = harness.replace("// PRODUCTION_METHODS", methods)
    with tempfile.TemporaryDirectory(prefix="bullettracer-tests-") as directory:
        cpp = Path(directory) / "regression.cpp"
        binary = Path(directory) / "regression"
        cpp.write_text(harness)
        command = shlex.split(os.environ.get("CXX", "c++"))
        command += ["-std=c++17", "-Wall", "-Wextra", "-Werror"]
        command += shlex.split(os.environ.get("CXXFLAGS", ""))
        subprocess.run(command + [str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
