#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""test_tool_cli - scripts/tools must honour the workspace CLI conventions.

Checks, without any kernel module or hardware:

  scripts/ga   --help/--version -> 0, no arguments -> 2, missing vmlinux -> 3
  scripts/pa   --help/--version -> 0, no arguments -> 2, missing file -> 3
  tools/fbview.py  --help/--version -> 0, missing device -> 3

# ORACLE: REQUIREMENT
# SOURCE: ../AGENTS.md, section "CLI and exit codes"
# EXPECTED: known flags exit 0; invalid usage exits 2; missing resources exit 3

Exit codes: 0 PASS, 1 FAIL, 2 INVALID_USAGE, 3 ENVIRONMENT_ERROR,
            4 TIMEOUT, 5 INCONCLUSIVE.
"""

import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "common"))
import testlib  # noqa: E402

VERSION = "1.0.0"
NAME = "test_tool_cli"
ORACLE = "REQUIREMENT"
SOURCE = "../AGENTS.md, section 'CLI and exit codes'"
EXPECTED = ("--help/--version exit 0; invalid usage exits 2; missing "
            "file/device exits 3")
USAGE = "test_tool_cli.py [options]"


def check(cases, failures, label, rc):
    if rc != cases["want"]:
        failures.append("%s: exit %s (want %d)" % (label, rc, cases["want"]))


def main():
    options, _ = testlib.parse_common(sys.argv[1:])
    if options["help"]:
        testlib.usage(NAME, USAGE, ORACLE, SOURCE, EXPECTED)
    if options["version"]:
        print("%s %s" % (NAME, VERSION))
        return testlib.PASS

    repo = testlib.REPO_DIR
    ga = repo / "scripts" / "ga"
    pa = repo / "scripts" / "pa"
    fbview = repo / "tools" / "fbview.py"

    for tool in (ga, pa, fbview):
        if not tool.exists():
            return testlib.report(options, NAME, testlib.FAIL, ORACLE, SOURCE,
                                  EXPECTED, "missing tool %s" % tool)

    if shutil.which("bash") is None or shutil.which("python3") is None:
        return testlib.report(options, NAME, testlib.ENVIRONMENT_ERROR, ORACLE,
                              SOURCE, EXPECTED,
                              "bash or python3 not available")

    missing_vmlinux = "/nonexistent/vmlinux"
    missing_file = "/nonexistent/context.c:10"

    cases = [
        # (label, argv, wanted exit code)
        ("ga --help", ["bash", str(ga), "--help"], 0),
        ("ga --version", ["bash", str(ga), "--version"], 0),
        ("ga (no args)", ["bash", str(ga)], 2),
        ("ga bad offset", ["bash", str(ga), missing_vmlinux, "sym"], 2),
        ("ga missing vmlinux",
         ["bash", str(ga), missing_vmlinux, "sym+0x1"], 3),
        ("pa --help", ["bash", str(pa), "--help"], 0),
        ("pa --version", ["bash", str(pa), "--version"], 0),
        ("pa (no args)", ["bash", str(pa)], 2),
        ("pa bad format", ["bash", str(pa), "noseparator"], 2),
        ("pa missing file", ["bash", str(pa), missing_file], 3),
        ("fbview --help", ["python3", str(fbview), "--help"], 0),
        ("fbview --version", ["python3", str(fbview), "--version"], 0),
        ("fbview missing device",
         ["python3", str(fbview), "/dev/does-not-exist"], 3),
    ]

    failures = []
    observed = []
    for label, argv, want in cases:
        rc, out, err = testlib.run(argv, timeout=30)
        if rc is None:
            failures.append("%s: did not run (%s)" % (label, err.strip()))
            observed.append("%s=ERR" % label)
            continue
        observed.append("%s=%d" % (label, rc))
        if rc != want:
            detail = (err or out).strip().splitlines()
            failures.append("%s: exit %d (want %d)%s" % (
                label, rc, want, ": " + detail[-1] if detail else ""))

    observation = "; ".join(observed)
    if failures:
        return testlib.report(options, NAME, testlib.FAIL, ORACLE, SOURCE,
                              EXPECTED, observation, "; ".join(failures))
    return testlib.report(options, NAME, testlib.PASS, ORACLE, SOURCE,
                          EXPECTED, observation,
                          "%d CLI cases checked" % len(cases))


if __name__ == "__main__":
    sys.exit(main())
