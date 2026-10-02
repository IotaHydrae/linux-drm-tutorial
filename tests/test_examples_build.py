#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""test_examples_build - the raw DRM ioctl examples must compile (host-only).

examples/drm/{probe,setcrtc,atomic}.c deliberately avoid libdrm and use only
the kernel UAPI headers. Compiling them proves that every struct, ioctl and
format constant they use is still present in /usr/include/drm/.

Build products (`*.out`) are removed again, so the repository stays clean.

# ORACLE: REQUIREMENT
# SOURCE: docs/drm-ioctls.md - "the programs in examples/drm/ use only the raw
#         ioctl() syscall - no libdrm"
# EXPECTED: `make -C examples/drm` exits 0 against /usr/include/drm/drm.h

Exit codes: 0 PASS, 1 FAIL, 2 INVALID_USAGE, 3 ENVIRONMENT_ERROR,
            4 TIMEOUT, 5 INCONCLUSIVE.
"""

import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "common"))
import testlib  # noqa: E402

VERSION = "1.0.0"
NAME = "test_examples_build"
ORACLE = "REQUIREMENT"
SOURCE = "docs/drm-ioctls.md: examples/drm uses only kernel UAPI headers"
EXPECTED = "make -C examples/drm exits 0 without libdrm"
USAGE = "test_examples_build.py [options]"


def main():
    options, _ = testlib.parse_common(sys.argv[1:])
    if options["help"]:
        testlib.usage(NAME, USAGE, ORACLE, SOURCE, EXPECTED)
    if options["version"]:
        print("%s %s" % (NAME, VERSION))
        return testlib.PASS

    if shutil.which("make") is None:
        return testlib.report(options, NAME, testlib.ENVIRONMENT_ERROR, ORACLE,
                              SOURCE, EXPECTED, "make not found",
                              "install build-essential")
    if not Path("/usr/include/drm/drm.h").exists():
        return testlib.report(options, NAME, testlib.ENVIRONMENT_ERROR, ORACLE,
                              SOURCE, EXPECTED,
                              "/usr/include/drm/drm.h not present",
                              "install libdrm-dev")

    examples = testlib.REPO_DIR / "examples" / "drm"
    if not examples.is_dir():
        return testlib.report(options, NAME, testlib.ENVIRONMENT_ERROR, ORACLE,
                              SOURCE, EXPECTED, "examples/drm not found",
                              str(examples))

    testlib.run(["make", "-C", str(examples), "clean"])
    try:
        rc, out, err = testlib.run(["make", "-C", str(examples)])
    finally:
        testlib.run(["make", "-C", str(examples), "clean"])

    if rc is None:
        return testlib.report(options, NAME, testlib.TIMEOUT, ORACLE, SOURCE,
                              EXPECTED, "build timed out", err.strip())
    if rc != 0:
        return testlib.report(options, NAME, testlib.FAIL, ORACLE, SOURCE,
                              EXPECTED, "build failed (rc=%d)" % rc,
                              (err or out).strip().splitlines()[-1]
                              if (err or out).strip() else "")
    return testlib.report(options, NAME, testlib.PASS, ORACLE, SOURCE,
                          EXPECTED, "examples/drm built against the UAPI headers",
                          str(examples))


if __name__ == "__main__":
    sys.exit(main())
