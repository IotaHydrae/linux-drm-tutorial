/* SPDX-License-Identifier: GPL-2.0 */
/* test_common - see test_common.h. */

#include "test_common.h"

#include <stdlib.h>
#include <string.h>

bool test_opt_parse(struct test_options *opt, const char *arg)
{
  if (strcmp(arg, "--help") == 0) {
    opt->help = true;
    return true;
  }
  if (strcmp(arg, "--json") == 0) {
    opt->json = true;
    return true;
  }
  if (strcmp(arg, "--quiet") == 0) {
    opt->quiet = true;
    return true;
  }
  if (strcmp(arg, "--verbose") == 0) {
    opt->verbose = true;
    return true;
  }
  return false;
}

void test_usage(const char *name, const char *usage, const char *oracle,
                const char *source, const char *expected)
{
  printf("%s\n\n", name);
  printf("Usage: %s\n\n", usage);
  printf("Oracle:   %s\n", oracle);
  printf("Source:   %s\n", source);
  printf("Expected: %s\n\n", expected);
  printf("Options:\n");
  printf("  --help      show this help and exit 0\n");
  printf("  --json      emit one JSON result line\n");
  printf("  --quiet     suppress the human-readable result block\n");
  printf("  --verbose   print extra diagnostics\n\n");
  printf("Exit codes: 0 PASS, 1 FAIL, 2 INVALID_USAGE, 3 ENVIRONMENT_ERROR,\n");
  printf("            4 TIMEOUT, 5 INCONCLUSIVE\n");
  exit(TEST_PASS);
}

const char *test_status_name(enum test_status status)
{
  switch (status) {
  case TEST_PASS:
    return "PASS";
  case TEST_FAIL:
    return "FAIL";
  case TEST_INVALID_USAGE:
    return "INVALID_USAGE";
  case TEST_ENVIRONMENT_ERROR:
    return "ENVIRONMENT_ERROR";
  case TEST_TIMEOUT:
    return "TIMEOUT";
  case TEST_INCONCLUSIVE:
    return "INCONCLUSIVE";
  }
  return "UNKNOWN";
}

/*
 * Minimal JSON string escaper: quotes, backslashes and control characters.
 * The strings printed by the tests are short and ASCII, so this is enough.
 */
static void json_escape(const char *in, char *out, size_t outlen)
{
  size_t o = 0;

  for (; in && *in && o + 7 < outlen; in++) {
    unsigned char c = (unsigned char)*in;

    if (c == '"' || c == '\\') {
      out[o++] = '\\';
      out[o++] = (char)c;
    } else if (c == '\n') {
      out[o++] = '\\';
      out[o++] = 'n';
    } else if (c < 0x20) {
      o += (size_t)snprintf(out + o, outlen - o, "\\u%04x", c);
    } else {
      out[o++] = (char)c;
    }
  }
  out[o] = '\0';
}

static void print_json(const char *name, const char *device, const char *oracle,
                       const char *source, const char *expected,
                       const char *observation, enum test_status status,
                       const char *detail)
{
  char e_name[128], e_dev[256], e_oracle[64], e_source[256];
  char e_expected[512], e_obs[512], e_detail[512];

  json_escape(name, e_name, sizeof(e_name));
  json_escape(device ? device : "", e_dev, sizeof(e_dev));
  json_escape(oracle, e_oracle, sizeof(e_oracle));
  json_escape(source, e_source, sizeof(e_source));
  json_escape(expected, e_expected, sizeof(e_expected));
  json_escape(observation, e_obs, sizeof(e_obs));
  json_escape(detail ? detail : "", e_detail, sizeof(e_detail));

  printf("{\"test\":\"%s\",\"device\":\"%s\",\"status\":\"%s\","
         "\"oracle\":{\"type\":\"%s\",\"source\":\"%s\",\"expected\":\"%s\"},"
         "\"observation\":\"%s\",\"detail\":\"%s\"}\n",
         e_name, e_dev, test_status_name(status), e_oracle, e_source,
         e_expected, e_obs, e_detail);
}

int test_report(const struct test_options *opt, const char *name,
                const char *device, const char *oracle, const char *source,
                const char *expected, const char *observation,
                enum test_status status, const char *detail)
{
  if (opt->json) {
    print_json(name, device, oracle, source, expected, observation, status,
               detail);
    return status;
  }
  if (opt->quiet)
    return status;

  printf("[TEST] %s\n", name);
  if (device && device[0])
    printf("[DEVICE] %s\n", device);
  printf("[OBSERVE] %s\n", observation);
  printf("[ORACLE] %s\n", oracle);
  printf("[SOURCE] %s\n", source);
  printf("[EXPECTED] %s\n", expected);
  if (detail && detail[0])
    printf("[DETAIL] %s\n", detail);
  printf("[RESULT] %s\n", test_status_name(status));
  return status;
}
