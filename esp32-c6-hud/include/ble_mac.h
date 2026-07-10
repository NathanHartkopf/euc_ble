#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*BleMacNotifyFn)(const uint8_t *data, size_t length, void *user_data);

void ble_mac_set_notify_callback(BleMacNotifyFn callback, void *user_data);
bool ble_mac_begin(void);
void ble_mac_poll(void);
bool ble_mac_is_connected(void);
const char *ble_mac_status_text(void);
const char *ble_mac_device_name(void);

#ifdef __cplusplus
}
#endif
