# SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
# SPDX-License-Identifier: MIT

# Compile every example under examples/ locally with "pio ci", mirroring the CI.
# Example folders are discovered recursively and classified by content: a folder with a *.ino file
# is an Arduino example, otherwise (a folder with C/C++ sources) a Native example. Each example is
# compiled once per environment in the matching list. You pass the Arduino and Native envs; an empty
# list skips that example type.
#
# At the end, a summary is printed with the result of each compilation.
#
# Usage:
# Arduino and Native envs:
#   python scripts/compile-examples.py --arduino-envs esp32-s3-test --native-envs native-test
# Only Native envs:
#   python scripts/compile-examples.py --native-envs native-test
# Multiple envs for each type:
#   python scripts/compile-examples.py --arduino-envs esp32-s3-test stm32f103-test
#   python scripts/compile-examples.py --arduino-envs "esp32-s3-test stm32f103-test" (quoted works too)

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

EXAMPLES_DIR = "examples"
LIB_PATH = "."
PROJECT_CONF = "platformio.ini"
SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".c")


def splitEnvs(values):
    # Accept both "--arduino-envs a b" and "--arduino-envs \"a b\"" by splitting on whitespace.
    envs = []
    for value in values:
        envs.extend(value.split())
    return envs


def discoverExamples():
    root = Path(EXAMPLES_DIR)
    if not root.is_dir():
        sys.stderr.write("Examples directory not found: " + EXAMPLES_DIR + "\n")
        return [], []

    # Classify each example folder by the source it directly contains. A folder holding a *.ino is
    # an Arduino example even if it also has C/C++ helper sources.
    arduino_set = set()
    native_set = set()
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix == ".ino":
            arduino_set.add(path.parent)
        elif path.suffix in SOURCE_SUFFIXES:
            native_set.add(path.parent)

    arduino_dirs = sorted(arduino_set)
    native_dirs = sorted(native_set - arduino_set)
    return arduino_dirs, native_dirs


def compileExample(pio_path, example, env, type_label, results):
    # "pio ci" copies the example into a temporary project, links the library from -l and builds it
    # with the -e environment taken from -c.
    target = example.as_posix()
    print(
        "=== Compiling "
        + type_label
        + " example: "
        + target
        + " (env: "
        + env
        + ") ==="
    )
    command = [pio_path, "ci", "-l", LIB_PATH, "-c", PROJECT_CONF, "-e", env, target]
    completed = subprocess.run(command)
    ok = completed.returncode == 0
    results.append((target, type_label, env, ok))


def printSummary(results):
    name_width = max(max((len(row[0]) for row in results), default=0), len("Example"))
    env_width = max(
        max((len(row[2]) for row in results), default=0), len("Environment")
    )

    header = (
        "  "
        + "Example".ljust(name_width)
        + "  "
        + "Type".ljust(8)
        + "  "
        + "Environment".ljust(env_width)
        + "  Result"
    )
    print("\n-> Summary:\n")
    print(header)
    print("  " + "-" * (len(header) - 2))
    for example, type_label, env, ok in results:
        result = "OK" if ok else "FAILED"
        print(
            "  "
            + example.ljust(name_width)
            + "  "
            + type_label.ljust(8)
            + "  "
            + env.ljust(env_width)
            + "  "
            + result
        )

    passed = sum(1 for row in results if row[3])
    print("")
    print(
        "Compiled " + str(passed) + "/" + str(len(results)) + " build(s) successfully."
    )


def parseArgs():
    parser = argparse.ArgumentParser(
        description="Compile all examples locally with pio ci, mirroring the CI."
    )
    parser.add_argument(
        "--arduino-envs",
        nargs="*",
        default=[],
        metavar="ENV",
        help="PlatformIO envs to compile Arduino examples (folders with a *.ino). Empty skips them.",
    )
    parser.add_argument(
        "--native-envs",
        nargs="*",
        default=[],
        metavar="ENV",
        help="PlatformIO envs to compile Native examples. Empty skips them.",
    )
    return parser.parse_args()


def main():
    args = parseArgs()
    arduino_envs = splitEnvs(args.arduino_envs)
    native_envs = splitEnvs(args.native_envs)

    if not arduino_envs and not native_envs:
        sys.stderr.write(
            "Nothing to do: provide --arduino-envs and/or --native-envs.\n"
        )
        return 2

    pio_path = shutil.which("pio")
    if pio_path is None:
        sys.stderr.write(
            "PlatformIO 'pio' was not found on PATH. Install it or activate the venv first.\n"
        )
        return 1

    arduino_dirs, native_dirs = discoverExamples()

    results = []
    for example in arduino_dirs:
        for env in arduino_envs:
            compileExample(pio_path, example, env, "Arduino", results)
    for example in native_dirs:
        for env in native_envs:
            compileExample(pio_path, example, env, "Native", results)

    if not results:
        print("No examples matched the provided environments. Nothing compiled.")
        return 0

    printSummary(results)
    return 1 if any(not row[3] for row in results) else 0


if __name__ == "__main__":
    sys.exit(main())
