# SPDX-License-Identifier: GPL-2.0
"""testlib - shared conventions for the host-only tests.

Mirrors tests/common/test_common.{h,c} for the Python tests: the workspace exit
codes, a tiny option parser and a reporter that prints observation, oracle and
decision either as a human block or as one JSON line.

Exit codes (workspace convention, see ../AGENTS.md):

    0 = PASS   1 = FAIL   2 = INVALID_USAGE
    3 = ENVIRONMENT_ERROR  4 = TIMEOUT   5 = INCONCLUSIVE

Oracle types: SPEC / REQUIREMENT / INVARIANT / RELATIONSHIP / GOLDEN /
BASELINE / USER_DEFINED / NONE.
"""

import json
import subprocess
import sys
from pathlib import Path

PASS = 0
FAIL = 1
INVALID_USAGE = 2
ENVIRONMENT_ERROR = 3
TIMEOUT = 4
INCONCLUSIVE = 5

STATUS_NAMES = {
    PASS: "PASS",
    FAIL: "FAIL",
    INVALID_USAGE: "INVALID_USAGE",
    ENVIRONMENT_ERROR: "ENVIRONMENT_ERROR",
    TIMEOUT: "TIMEOUT",
    INCONCLUSIVE: "INCONCLUSIVE",
}

TESTS_DIR = Path(__file__).resolve().parent.parent
REPO_DIR = TESTS_DIR.parent

OPTIONS = {"--help": "help", "--json": "json", "--quiet": "quiet",
           "--verbose": "verbose", "--version": "version"}


def parse_common(argv):
    """Split argv into common flags and positional arguments.

    Returns (options, positionals) where options is a dict with the boolean
    keys help/json/quiet/verbose/version.
    """
    options = {name: False for name in OPTIONS.values()}
    positionals = []
    for arg in argv:
        if arg in OPTIONS:
            options[OPTIONS[arg]] = True
        else:
            positionals.append(arg)
    return options, positionals


def usage(name, usage_text, oracle, source, expected):
    """Print the usage/oracle block and exit PASS (for --help)."""
    print("%s\n" % name)
    print("Usage: %s\n" % usage_text)
    print("Oracle:   %s" % oracle)
    print("Source:   %s" % source)
    print("Expected: %s\n" % expected)
    print("Options:")
    print("  --help      show this help and exit 0")
    print("  --version   show the test version and exit 0")
    print("  --json      emit one JSON result line")
    print("  --quiet     suppress the human-readable result block")
    print("  --verbose   print extra diagnostics\n")
    print("Exit codes: 0 PASS, 1 FAIL, 2 INVALID_USAGE, 3 ENVIRONMENT_ERROR,")
    print("            4 TIMEOUT, 5 INCONCLUSIVE")
    sys.exit(PASS)


def report(options, name, status, oracle, source, expected, observation,
           detail="", device=""):
    """Print the result and return the exit code (0..5)."""
    if options.get("json"):
        print(json.dumps({
            "test": name,
            "device": device,
            "status": STATUS_NAMES.get(status, "UNKNOWN"),
            "oracle": {"type": oracle, "source": source, "expected": expected},
            "observation": observation,
            "detail": detail,
        }))
        return status
    if options.get("quiet"):
        return status
    print("[TEST] %s" % name)
    if device:
        print("[DEVICE] %s" % device)
    print("[OBSERVE] %s" % observation)
    print("[ORACLE] %s" % oracle)
    print("[SOURCE] %s" % source)
    print("[EXPECTED] %s" % expected)
    if detail:
        print("[DETAIL] %s" % detail)
    print("[RESULT] %s" % STATUS_NAMES.get(status, "UNKNOWN"))
    return status


def run(cmd, timeout=60, cwd=None, env=None):
    """Run cmd, returning (returncode, stdout, stderr).

    A timeout yields returncode None and the reason in stderr.
    """
    try:
        proc = subprocess.run(
            cmd, cwd=cwd, env=env, timeout=timeout, check=False,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    except FileNotFoundError as exc:
        return None, "", "not found: %s" % exc
    except subprocess.TimeoutExpired:
        return None, "", "timed out after %ss" % timeout
    return proc.returncode, proc.stdout, proc.stderr
