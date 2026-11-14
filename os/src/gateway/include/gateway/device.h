/* Open TV gateway — device description file.
 *
 * One file per device names the backend each service uses (the Android
 * analogue: board configs + HAL configuration files, unified here). The QEMU
 * targets ship a file that describes the SIMULATED device; a real TV port
 * writes its own — the OS above the gateway stays identical.
 *
 * Format (tiny INI, no dependencies):
 *   [section]          section name
 *   key = value        trimmed on both sides
 *   # or ; comments    full-line only
 */
#ifndef GATEWAY_DEVICE_H
#define GATEWAY_DEVICE_H

/* Parse the device description. 0 = ok; missing file = -1 (built-in
 * defaults stay active and accessors just return their defaults). */
int otv_device_init(const char *path);

/* Value of key in section, or def when the file/section/key is absent. */
const char *otv_device_str(const char *section, const char *key, const char *def);
int otv_device_int(const char *section, const char *key, int def);

/* "name=value" summary of the entries actually loaded (NUL-separated, ends
 * with an empty string) — used for the one-time startup log. */
const char *otv_device_summary(void);

#endif /* GATEWAY_DEVICE_H */
