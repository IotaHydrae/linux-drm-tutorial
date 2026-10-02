/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_common - shared conventions for the framebuffer tests.
 *
 * Provides:
 *   - the workspace exit codes (see ../../AGENTS.md)
 *   - a small option parser for --help/--json/--quiet/--verbose
 *   - a reporter that prints the observation, the oracle and the decision
 *     in a human-readable block or as one JSON line.
 */
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Exit codes - workspace convention (AGENTS.md section "CLI and exit codes"). */
enum test_status {
  TEST_PASS = 0,
  TEST_FAIL = 1,
  TEST_INVALID_USAGE = 2,
  TEST_ENVIRONMENT_ERROR = 3,
  TEST_TIMEOUT = 4,
  TEST_INCONCLUSIVE = 5,
};

/*
 * Oracle types - workspace convention (AGENTS.md section "Oracle
 * declaration"). Declared as strings because every test only prints them.
 */
#define ORACLE_SPEC "SPEC"
#define ORACLE_REQUIREMENT "REQUIREMENT"
#define ORACLE_INVARIANT "INVARIANT"
#define ORACLE_RELATIONSHIP "RELATIONSHIP"
#define ORACLE_GOLDEN "GOLDEN"
#define ORACLE_BASELINE "BASELINE"
#define ORACLE_USER_DEFINED "USER_DEFINED"
#define ORACLE_NONE "NONE"

struct test_options {
  bool help;
  bool json;
  bool quiet;
  bool verbose;
};

/*
 * Scan one argument and set the matching flag. Returns true when the argument
 * was a common flag, false when the caller should handle it (positional
 * argument or a test-specific option).
 */
bool test_opt_parse(struct test_options *opt, const char *arg);

/*
 * Print the usage block (including the oracle declaration) and exit PASS.
 * Call this when opt.help is set.
 */
void test_usage(const char *name, const char *usage, const char *oracle,
                const char *source, const char *expected);

/*
 * Print observation, oracle and decision; return the status so callers can do
 * `return test_report(...)`.
 *
 * Human output uses [TEST]/[DEVICE]/[OBSERVE]/[ORACLE]/[SOURCE]/[EXPECTED]/
 * [RESULT] lines. With --json a single machine-readable line is emitted
 * instead. With --quiet only JSON is suppressed; with --quiet and no --json
 * nothing is printed.
 */
int test_report(const struct test_options *opt, const char *name,
                const char *device, const char *oracle, const char *source,
                const char *expected, const char *observation,
                enum test_status status, const char *detail);

/* Human-readable name of a status, e.g. "ENVIRONMENT_ERROR". */
const char *test_status_name(enum test_status status);

#endif /* TEST_COMMON_H */
