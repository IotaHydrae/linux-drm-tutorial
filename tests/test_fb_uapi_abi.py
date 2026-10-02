#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""test_fb_uapi_abi - framebuffer ABI consistency check (host-only).

tools/fbview.py hard-codes the framebuffer ioctl and the layout of
`struct fb_var_screeninfo`. This test compiles a tiny probe against the running
kernel's UAPI header and checks that those hard-coded values still match, so
the viewer cannot silently drift from the kernel ABI.

# ORACLE: SPEC
# SOURCE: /usr/include/linux/fb.h (running kernel UAPI), cross-checked with
#         the constants in tools/fbview.py
# EXPECTED: FBIOGET_VSCREENINFO == 0x4600, sizeof(struct fb_var_screeninfo) ==
#           160 bytes, and the fb_bitfield/colourspace/reserved offsets equal
#           fbview's _VAR_NAMES index * 4

Exit codes: 0 PASS, 1 FAIL, 2 INVALID_USAGE, 3 ENVIRONMENT_ERROR,
            4 TIMEOUT, 5 INCONCLUSIVE.
"""

import importlib.util
import os
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "common"))
import testlib  # noqa: E402

VERSION = "1.0.0"
NAME = "test_fb_uapi_abi"
ORACLE = "SPEC"
SOURCE = ("/usr/include/linux/fb.h (running kernel UAPI) cross-checked with "
          "the constants hard-coded in tools/fbview.py")
EXPECTED = ("FBIOGET_VSCREENINFO == 0x4600, sizeof(struct fb_var_screeninfo) "
            "== 160, and fb_bitfield/reserved offsets == "
            "fbview._VAR_NAMES index * 4")
USAGE = "test_fb_uapi_abi.py [options]"

PROBE = r"""
#include <stddef.h>
#include <stdio.h>
#include <linux/fb.h>
int main(void)
{
  printf("FBIOGET_VSCREENINFO=%#x\n", FBIOGET_VSCREENINFO);
  printf("sizeof=%zu\n", sizeof(struct fb_var_screeninfo));
  printf("red_offset=%zu\n", offsetof(struct fb_var_screeninfo, red));
  printf("green_offset=%zu\n", offsetof(struct fb_var_screeninfo, green));
  printf("blue_offset=%zu\n", offsetof(struct fb_var_screeninfo, blue));
  printf("transp_offset=%zu\n", offsetof(struct fb_var_screeninfo, transp));
  printf("nonstd=%zu\n", offsetof(struct fb_var_screeninfo, nonstd));
  printf("colorspace=%zu\n", offsetof(struct fb_var_screeninfo, colorspace));
  printf("reserved0=%zu\n", offsetof(struct fb_var_screeninfo, reserved));
  return 0;
}
"""

OFFSETS = {
    "red_offset": "red_offset",
    "green_offset": "green_offset",
    "blue_offset": "blue_offset",
    "transp_offset": "transp_offset",
    "nonstd": "nonstd",
    "colorspace": "colorspace",
    "reserved0": "reserved0",
}


def load_fbview():
    path = testlib.REPO_DIR / "tools" / "fbview.py"
    spec = importlib.util.spec_from_file_location("fbview", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def compiler():
    for candidate in (os.environ.get("CC"), "cc", "gcc"):
        if candidate and shutil.which(candidate):
            return candidate
    return None


def build_and_run(cc, tmpdir):
    src = Path(tmpdir) / "probe.c"
    exe = Path(tmpdir) / "probe"
    src.write_text(PROBE)
    rc, out, err = testlib.run([cc, "-Wall", "-o", str(exe), str(src)])
    if rc != 0:
        return None, err or out
    rc, out, err = testlib.run([str(exe)])
    if rc != 0:
        return None, err or out
    values = {}
    for line in out.splitlines():
        key, _, value = line.partition("=")
        values[key] = value
    return values, ""


def main():
    options, _ = testlib.parse_common(sys.argv[1:])
    if options["help"]:
        testlib.usage(NAME, USAGE, ORACLE, SOURCE, EXPECTED)
    if options["version"]:
        print("%s %s" % (NAME, VERSION))
        return testlib.PASS

    cc = compiler()
    if cc is None:
        return testlib.report(
            options, NAME, testlib.ENVIRONMENT_ERROR, ORACLE, SOURCE, EXPECTED,
            "no C compiler (cc/gcc) available",
            "set CC or install a compiler")

    if not Path("/usr/include/linux/fb.h").exists():
        return testlib.report(
            options, NAME, testlib.ENVIRONMENT_ERROR, ORACLE, SOURCE, EXPECTED,
            "/usr/include/linux/fb.h not present",
            "install the kernel UAPI headers (linux-libc-dev)")

    with tempfile.TemporaryDirectory(prefix="fbabi-") as tmpdir:
        values, err = build_and_run(cc, tmpdir)
    if values is None:
        return testlib.report(
            options, NAME, testlib.FAIL, ORACLE, SOURCE, EXPECTED,
            "C probe did not build or run", err.strip())

    fbview = load_fbview()
    problems = []

    c_ioctl = int(values["FBIOGET_VSCREENINFO"], 16)
    if c_ioctl != fbview.FBIOGET_VSCREENINFO:
        problems.append("FBIOGET_VSCREENINFO header=0x%x fbview=0x%x" %
                        (c_ioctl, fbview.FBIOGET_VSCREENINFO))

    c_size = int(values["sizeof"])
    if c_size != fbview.VAR_SIZE:
        problems.append("sizeof header=%d fbview=%d" % (c_size,
                                                        fbview.VAR_SIZE))

    for key, name in OFFSETS.items():
        want = fbview._VAR_NAMES.index(name) * 4
        got = int(values[key])
        if got != want:
            problems.append("offset %s header=%d fbview=%d" % (key, got, want))

    observation = ("header: FBIOGET_VSCREENINFO=0x%x sizeof=%d "
                   "red=%s nonstd=%s colorspace=%s reserved0=%s" %
                   (c_ioctl, c_size, values["red_offset"], values["nonstd"],
                    values["colorspace"], values["reserved0"]))
    detail = "fbview: FBIOGET_VSCREENINFO=0x%x VAR_SIZE=%d" % (
        fbview.FBIOGET_VSCREENINFO, fbview.VAR_SIZE)

    if problems:
        return testlib.report(options, NAME, testlib.FAIL, ORACLE, SOURCE,
                              EXPECTED, observation, "; ".join(problems))
    return testlib.report(options, NAME, testlib.PASS, ORACLE, SOURCE,
                          EXPECTED, observation, detail)


if __name__ == "__main__":
    sys.exit(main())
