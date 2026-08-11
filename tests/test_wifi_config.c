/* Host-side tests for the console provisioning command. */
#include <stdio.h>
#include <string.h>

#include "wifi_config.h"

static int failures;
static int checks;

static void check(const char *what, long expected, long actual) {
    checks++;
    if (expected != actual) {
        failures++;
        printf("  FAIL %-52s expected %ld, got %ld\n", what, expected, actual);
    }
}

static void check_str(const char *what, const char *expected, const char *actual) {
    checks++;
    if (strcmp(expected, actual) != 0) {
        failures++;
        printf("  FAIL %-52s expected \"%s\", got \"%s\"\n", what, expected, actual);
    }
}

/* The command as the lab will actually type it. */
static void test_accepts_the_documented_form(void) {
    char ssid[WIFI_SSID_SIZE] = "", key[WIFI_KEY_SIZE] = "";
    check("documented form parses", 1,
          wifi_config_parse("wifi PLCLAB idahostate", ssid, key));
    check_str("ssid is taken from the first token", "PLCLAB", ssid);
    check_str("key is taken from the second token", "idahostate", key);
}

static void test_tolerates_surrounding_whitespace(void) {
    char ssid[WIFI_SSID_SIZE] = "", key[WIFI_KEY_SIZE] = "";
    check("extra spacing parses", 1,
          wifi_config_parse("  wifi\tPLCLAB   idahostate  ", ssid, key));
    check_str("padded ssid is trimmed", "PLCLAB", ssid);
    check_str("padded key is trimmed", "idahostate", key);
}

/*
 * A third token means a space inside the SSID or passphrase. Storing the
 * fragment would fail on the network hours later, so it is refused outright.
 */
static void test_rejects_a_third_token(void) {
    char ssid[WIFI_SSID_SIZE] = "untouched", key[WIFI_KEY_SIZE] = "untouched";
    check("trailing token is rejected", 0,
          wifi_config_parse("wifi PLC LAB idahostate", ssid, key));
    check_str("rejection leaves the ssid alone", "untouched", ssid);
    check_str("rejection leaves the key alone", "untouched", key);
}

static void test_rejects_unusable_credentials(void) {
    char ssid[WIFI_SSID_SIZE], key[WIFI_KEY_SIZE];
    check("missing key is rejected", 0, wifi_config_parse("wifi PLCLAB", ssid, key));
    check("seven-character key is rejected", 0,
          wifi_config_parse("wifi PLCLAB short7c", ssid, key));
    check("empty ssid is rejected", 0, wifi_config_parse("wifi  ", ssid, key));
    check("bare verb is rejected", 0, wifi_config_parse("wifi", ssid, key));
}

/* 32 characters is the 802.11 maximum and 63 the WPA2 maximum; one more of
 * either would be silently truncated into a credential that cannot connect. */
static void test_enforces_the_length_limits(void) {
    char ssid[WIFI_SSID_SIZE], key[WIFI_KEY_SIZE];
    char line[200];

    char longest_ssid[33], longest_key[64];
    memset(longest_ssid, 'S', 32);
    longest_ssid[32] = '\0';
    memset(longest_key, 'K', 63);
    longest_key[63] = '\0';

    snprintf(line, sizeof line, "wifi %s %s", longest_ssid, longest_key);
    check("maximum length credentials parse", 1, wifi_config_parse(line, ssid, key));
    check_str("longest ssid survives intact", longest_ssid, ssid);
    check_str("longest key survives intact", longest_key, key);

    char oversize_ssid[34];
    memset(oversize_ssid, 'S', 33);
    oversize_ssid[33] = '\0';
    snprintf(line, sizeof line, "wifi %s idahostate", oversize_ssid);
    check("33-character ssid is rejected", 0, wifi_config_parse(line, ssid, key));

    char oversize_key[65];
    memset(oversize_key, 'K', 64);
    oversize_key[64] = '\0';
    snprintf(line, sizeof line, "wifi PLCLAB %s", oversize_key);
    check("64-character key is rejected", 0, wifi_config_parse(line, ssid, key));
}

/*
 * The console shares its port with the binary protocol, so anything that is
 * not this command must fall through untouched and produce no reply.
 */
static void test_ignores_lines_that_are_not_the_command(void) {
    char ssid[WIFI_SSID_SIZE], key[WIFI_KEY_SIZE];
    check("unrelated line is not a command", 0,
          wifi_config_parse("hello there", ssid, key));
    check("verb prefix alone is not a command", 0,
          wifi_config_parse("wifiPLCLAB idahostate", ssid, key));
    check("empty line is not a command", 0, wifi_config_parse("", ssid, key));
}

int main(void) {
    test_accepts_the_documented_form();
    test_tolerates_surrounding_whitespace();
    test_rejects_a_third_token();
    test_rejects_unusable_credentials();
    test_enforces_the_length_limits();
    test_ignores_lines_that_are_not_the_command();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
