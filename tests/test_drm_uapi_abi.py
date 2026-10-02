#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""test_drm_uapi_abi - the DRM ioctl names in the docs must still exist in the
kernel UAPI headers (host-only).

Every `DRM_IOCTL_*` token used in docs/ is collected and fed to the compiler
together with <drm/drm.h> / <drm/drm_mode.h>. If a name was renamed or removed
in the UAPI, the probe no longer compiles and this test fails.

# ORACLE: SPEC
# SOURCE: /usr/include/drm/drm.h and /usr/include/drm/drm_mode.h (running
#         kernel UAPI)
# EXPECTED: every DRM_IOCTL_* referenced by docs/ is defined by the headers

Exit codes: 0 PASS, 1 FAIL, 2 INVALID_USAGE, 3 ENVIRONMENT_ERROR,
            4 TIMEOUT, 5 INCONCLUSIVE.
"""

import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "common"))
import testlib  # noqa: E402

VERSION = "1.0.0"
NAME = "test_drm_uapi_abi"
ORACLE = "SPEC"
SOURCE = "/usr/include/drm/drm.h + /usr/include/drm/drm_mode.h (kernel UAPI)"
EXPECTED = "every DRM_IOCTL_* / flag referenced in docs/ is defined"
USAGE = "test_drm_uapi_abi.py [options]"

IOCTL_RE = re.compile(r"\bDRM_IOCTL_[A-Z0-9_]+\b")
EXTRA_FLAGS = ["DRM_MODE_ATOMIC_TEST_ONLY", "DRM_CLIENT_CAP_UNIVERSAL_PLANES"]
HEADERS = ["/usr/include/drm/drm.h", "/usr/include/drm/drm_mode.h"]

PROBE_TEMPLATE = """\
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
static const unsigned long k_ioctls[] = {
%s
};
static const int k_flags[] = {
%s
};
int main(void) { return (int)(sizeof(k_ioctls) + sizeof(k_flags)); }
"""


def collect_tokens():
    tokens = set()
    problems = []
    docs = sorted((testlib.REPO_DIR / "docs").glob("*.md"))
    for doc in docs:
        if doc.name.endswith(".zh-CN.md"):
            continue
        tokens.update(IOCTL_RE.findall(doc.read_text()))
    tokens.update(EXTRA_FLAGS)
    if not tokens:
        problems.append("no DRM tokens found in docs/")
    return sorted(tokens), problems


def compiler():
    for candidate in (os.environ.get("CC"), "cc", "gcc"):
        if candidate and shutil.which(candidate):
            return candidate
    return None


def main():
    options, _ = testlib.parse_common(sys.argv[1:])
    if options["help"]:
        testlib.usage(NAME, USAGE, ORACLE, SOURCE, EXPECTED)
    if options["version"]:
        print("%s %s" % (NAME, VERSION))
        return testlib.PASS

    tokens, problems = collect_tokens()
    if problems:
        return testlib.report(options, NAME, testlib.FAIL, ORACLE, SOURCE,
                              EXPECTED, "doc scan failed", "; ".join(problems))

    missing = [h for h in HEADERS if not Path(h).exists()]
    if missing:
        return testlib.report(options, NAME, testlib.ENVIRONMENT_ERROR, ORACLE,
                              SOURCE, EXPECTED,
                              "kernel UAPI headers not present",
                              ", ".join(missing))

    cc = compiler()
    if cc is None:
        return testlib.report(options, NAME, testlib.ENVIRONMENT_ERROR, ORACLE,
                              SOURCE, EXPECTED,
                              "no C compiler (cc/gcc) available",
                              "set CC or install a compiler")

    body = "\n".join("  %s," % t for t in tokens)
    flags = "\n".join("  %s," % t for t in EXTRA_FLAGS)
    source = PROBE_TEMPLATE % (body, flags)

    with tempfile.TemporaryDirectory(prefix="drmabi-") as tmpdir:
        src = Path(tmpdir) / "probe.c"
        src.write_text(source)
        rc, out, err = testlib.run([cc, "-Wall", "-fsyntax-only", str(src)])

    observation = "%d tokens checked: %s" % (
        len(tokens), ", ".join(tokens))
    if rc == 0:
        return testlib.report(options, NAME, testlib.PASS, ORACLE, SOURCE,
                              EXPECTED, observation)

    blame = sorted({t for t in tokens + EXTRA_FLAGS
                    if re.search(r"\b%s\b" % re.escape(t), err)})
    detail = "undefined/renamed: %s" % ", ".join(blame) if blame else err.strip()
    return testlib.report(options, NAME, testlib.FAIL, ORACLE, SOURCE, EXPECTED,
                          observation, detail)


if __name__ == "__main__":
    sys.exit(main())
