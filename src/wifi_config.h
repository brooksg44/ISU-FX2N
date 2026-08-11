/*
 * wifi_config.h - parsing the console provisioning command.
 *
 * Wi-Fi credentials are deliberately not compiled into the firmware. The
 * release UF2 is published, and string literals survive into it intact, so a
 * baked-in passphrase would be one `strings` away from anyone who downloads
 * it. Each trainer is provisioned once instead, over the same USB console the
 * diagnostics dump uses:
 *
 *     wifi PLCLAB idahostate
 *
 * The credentials are then kept in flash by plc_storage, which is why this
 * file holds only the parsing - it stays free of SDK dependencies so the
 * validation can be tested on the host.
 */
#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <stdbool.h>

/* 32 bytes is the 802.11 SSID maximum and 63 the longest WPA2 passphrase,
 * both sized here with room for a terminator. */
#define WIFI_SSID_SIZE 33
#define WIFI_KEY_SIZE 64

/*
 * Parses a "wifi <ssid> <key>" line. Returns false and leaves both outputs
 * untouched if the line is not that command or the credentials are unusable.
 *
 * Neither field may contain spaces: the line is split on whitespace, and a
 * third token is rejected rather than silently folded into the key. Open
 * networks are not supported - WPA2 requires at least 8 characters, and a
 * shorter key is a typo rather than a request for an open network.
 */
bool wifi_config_parse(const char *line, char ssid[WIFI_SSID_SIZE],
                       char key[WIFI_KEY_SIZE]);

#endif /* WIFI_CONFIG_H */
