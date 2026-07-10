#pragma once

#include <Arduino.h>
#include <ArduinoBLE.h>

#include "config.h"

struct BleAdvertisement {
  char address[18];
  char name[32];
  char services[64];
  int8_t rssi;
  bool hasFfe0;
  bool isTarget;
  uint32_t lastSeenMs;
};

extern BleAdvertisement g_bleDevices[BLE_SCAN_MAX_DEVICES];
extern uint8_t g_bleDeviceCount;

inline void bleScanStoreClear() {
  g_bleDeviceCount = 0;
}

inline int bleScanStoreFind(const char *address) {
  for (uint8_t i = 0; i < g_bleDeviceCount; i++) {
    if (strcmp(g_bleDevices[i].address, address) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

inline void bleScanStoreAppendServices(BLEDevice &device, char *out, size_t outLen) {
  out[0] = '\0';
  if (!device.hasAdvertisedServiceUuid() || outLen < 2) {
    return;
  }

  size_t used = 0;
  for (int i = 0; i < device.advertisedServiceUuidCount(); i++) {
    const String uuid = device.advertisedServiceUuid(i);
    if (uuid.length() == 0) {
      continue;
    }

    if (used > 0) {
      if (used + 1 >= outLen) {
        break;
      }
      out[used++] = ',';
      out[used] = '\0';
    }

    uuid.toCharArray(out + used, outLen - used);
    used = strlen(out);
    if (used >= outLen - 1) {
      break;
    }
  }
}

inline bool bleScanStoreDeviceHasFfe0(BLEDevice &device) {
  if (!device.hasAdvertisedServiceUuid()) {
    return false;
  }

  for (int i = 0; i < device.advertisedServiceUuidCount(); i++) {
    String uuid = device.advertisedServiceUuid(i);
    uuid.toLowerCase();
    if (uuid.endsWith(EUC_SERVICE_UUID_SHORT)) {
      return true;
    }
  }

  return false;
}

inline bool bleScanStoreNameIsTarget(const char *name) {
  if (name[0] == '\0') {
    return false;
  }

  String upper = name;
  upper.toUpperCase();

  for (uint8_t i = 0; i < EUC_NAME_HINT_COUNT; i++) {
    String hint = EUC_NAME_HINTS[i];
    hint.toUpperCase();
    if (upper.indexOf(hint) >= 0) {
      return true;
    }
  }

  return false;
}

inline void bleScanStoreUpsert(BLEDevice &device, bool isTarget) {
  char address[18];
  device.address().toCharArray(address, sizeof(address));

  const bool hasFfe0 = bleScanStoreDeviceHasFfe0(device);
  char name[32] = "";
  if (device.hasLocalName()) {
    device.localName().toCharArray(name, sizeof(name));
  }

  const int existing = bleScanStoreFind(address);
  if (existing >= 0) {
    BleAdvertisement &entry = g_bleDevices[existing];
    entry.rssi = static_cast<int8_t>(device.rssi());
    entry.hasFfe0 = hasFfe0;
    entry.isTarget = isTarget || bleScanStoreNameIsTarget(name) || hasFfe0;
    entry.lastSeenMs = millis();
    if (name[0] != '\0') {
      strncpy(entry.name, name, sizeof(entry.name) - 1);
      entry.name[sizeof(entry.name) - 1] = '\0';
    }
    bleScanStoreAppendServices(device, entry.services, sizeof(entry.services));
    return;
  }

  if (g_bleDeviceCount >= BLE_SCAN_MAX_DEVICES) {
    return;
  }

  BleAdvertisement &entry = g_bleDevices[g_bleDeviceCount++];
  strncpy(entry.address, address, sizeof(entry.address) - 1);
  entry.address[sizeof(entry.address) - 1] = '\0';
  strncpy(entry.name, name, sizeof(entry.name) - 1);
  entry.name[sizeof(entry.name) - 1] = '\0';
  entry.services[0] = '\0';
  bleScanStoreAppendServices(device, entry.services, sizeof(entry.services));
  entry.rssi = static_cast<int8_t>(device.rssi());
  entry.hasFfe0 = hasFfe0;
  entry.isTarget = isTarget || bleScanStoreNameIsTarget(name) || hasFfe0;
  entry.lastSeenMs = millis();
}
