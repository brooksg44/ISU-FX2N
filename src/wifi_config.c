#include "wifi_config.h"

#include <string.h>

#define WIFI_KEY_MIN 8 /* WPA2 passphrase minimum */

static bool is_space(char c) { return c == ' ' || c == '\t'; }

static const char *skip_spaces(const char *p) {
    while (is_space(*p)) {
        p++;
    }
    return p;
}

static size_t token_length(const char *p) {
    size_t n = 0;
    while (p[n] != '\0' && !is_space(p[n])) {
        n++;
    }
    return n;
}

bool wifi_config_parse(const char *line, char ssid[WIFI_SSID_SIZE],
                       char key[WIFI_KEY_SIZE]) {
    const char *p = skip_spaces(line);
    if (strncmp(p, "wifi", 4) != 0 || !is_space(p[4])) {
        return false;
    }

    p = skip_spaces(p + 4);
    size_t ssid_len = token_length(p);
    if (ssid_len == 0 || ssid_len >= WIFI_SSID_SIZE) {
        return false;
    }
    const char *ssid_start = p;

    p = skip_spaces(p + ssid_len);
    size_t key_len = token_length(p);
    if (key_len < WIFI_KEY_MIN || key_len >= WIFI_KEY_SIZE) {
        return false;
    }
    const char *key_start = p;

    /* Anything after the key means the line was not what it looked like -
     * most likely an SSID or passphrase containing a space. Rejecting is the
     * honest answer; storing a truncated credential would fail later, on the
     * network, where it is far harder to diagnose. */
    if (*skip_spaces(p + key_len) != '\0') {
        return false;
    }

    memcpy(ssid, ssid_start, ssid_len);
    ssid[ssid_len] = '\0';
    memcpy(key, key_start, key_len);
    key[key_len] = '\0';
    return true;
}
