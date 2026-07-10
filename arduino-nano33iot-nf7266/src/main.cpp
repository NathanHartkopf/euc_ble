#include <SPI.h>
#include <WiFiNINA.h>
#include <ArduinoOTA.h>
#include <ArduinoBLE.h>
#include <string.h>

#include "ble_scan_store.h"
#include "config.h"
#include "veteran_protocol.h"
#include "web_server.h"
#include "wifi_secrets.h"

BleAdvertisement g_bleDevices[BLE_SCAN_MAX_DEVICES];
uint8_t g_bleDeviceCount = 0;
WiFiServer g_httpServer(HTTP_PORT);
bool g_httpServerStarted = false;
bool g_bleScanActive = false;
uint32_t g_bleScanUntilMs = 0;
bool g_wheelConnected = false;
char g_wheelName[32] = {};
char g_wheelAddress[18] = {};
veteran::Telemetry g_wheelTelemetry = {};
uint32_t g_telemetryFrameCount = 0;
uint32_t g_telemetryLastGapMs = 0;
uint32_t g_telemetryAvgGapMs = 0;
uint32_t g_telemetryLastFrameMs = 0;
bool g_wifiApMode = true;
bool g_wifiOtaReady = false;

namespace {

enum class State : uint8_t {
  Idle,
  Scanning,
  Connecting,
  Connected,
};

State state = State::Idle;
BLEDevice peripheral;
BLECharacteristic telemetryChar;
veteran::FrameReassembler frameParser;

uint32_t reconnectAtMs = 0;
uint32_t lastWifiAttemptMs = 0;
bool notificationsReady = false;
bool otaReady = false;
bool wifiBleConcurrent = false;
bool g_wifiRequestSta = false;
bool g_wifiRequestAp = false;
bool g_directConnectActive = false;
uint32_t g_directConnectUntilMs = 0;
bool g_pendingBootDirectConnect = false;

void onTelemetryCharacteristicUpdated(BLEDevice device, BLECharacteristic characteristic);
void resetTelemetryTiming();
void requestDirectConnect();

bool ninaFirmwareSupportsConcurrent(const String &version) {
  const int dot = version.indexOf('.');
  if (dot < 0) {
    return false;
  }

  const int major = version.substring(0, dot).toInt();
  if (major < 3) {
    return false;
  }

  if (major > 3) {
    return true;
  }

  const int dot2 = version.indexOf('.', dot + 1);
  const int minor = (dot2 < 0) ? version.substring(dot + 1).toInt()
                               : version.substring(dot + 1, dot2).toInt();
  if (minor > 0) {
    return true;
  }

  if (dot2 < 0) {
    return false;
  }

  const int patch = version.substring(dot2 + 1).toInt();
  return patch >= 1;
}

void disconnectWifiForBle() {
  if (WiFi.status() == WL_CONNECTED || WiFi.status() == WL_AP_LISTENING ||
      WiFi.status() == WL_AP_CONNECTED) {
    Serial.println(F("WiFi: disconnecting for BLE"));
    WiFi.disconnect();
    delay(250);
  }
  otaReady = false;
  g_wifiOtaReady = false;
  g_httpServerStarted = false;
}

void resetHttpServer() {
  g_httpServerStarted = false;
}

bool startAccessPoint() {
  otaReady = false;
  g_wifiOtaReady = false;
  resetHttpServer();
  WiFi.disconnect();
  delay(250);

  g_wifiApMode = true;
  Serial.print(F("Starting hotspot "));
  Serial.println(WIFI_AP_SSID);

  const uint8_t status = WiFi.beginAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  if (status != WL_AP_LISTENING) {
    Serial.println(F("Hotspot start failed"));
    return false;
  }

  Serial.print(F("Hotspot ready @ "));
  Serial.println(WiFi.localIP());
  return true;
}

bool startStationWifi() {
  otaReady = false;
  g_wifiOtaReady = false;
  resetHttpServer();
  WiFi.disconnect();
  delay(250);

  g_wifiApMode = false;
  Serial.print(F("Connecting to WiFi "));
  Serial.println(SECRET_SSID);
  WiFi.begin(SECRET_SSID, SECRET_PASS);
  lastWifiAttemptMs = 0;
  return true;
}

void processWifiModeRequests() {
  if (g_wifiRequestSta) {
    g_wifiRequestSta = false;
    startStationWifi();
    return;
  }

  if (g_wifiRequestAp) {
    g_wifiRequestAp = false;
    startAccessPoint();
  }
}

void onWebWifiOtaRequested() {
  g_wifiRequestSta = true;
}

void onWebWifiApRequested() {
  g_wifiRequestAp = true;
}

bool uuidIsFfe0(const String &uuid) {
  if (uuid.length() == 0) {
    return false;
  }

  String normalized = uuid;
  normalized.toLowerCase();

  if (normalized.equalsIgnoreCase(EUC_SERVICE_UUID)) {
    return true;
  }

  if (normalized.equalsIgnoreCase(EUC_SERVICE_UUID_SHORT)) {
    return true;
  }

  return normalized.endsWith(EUC_SERVICE_UUID_SHORT);
}

void printDiscoveredDevice(BLEDevice &device) {
#if BLE_DEBUG_SCAN
  Serial.print(F("  adv "));
  Serial.print(device.address());
  Serial.print(F(" rssi="));
  Serial.print(device.rssi());

  if (device.hasLocalName()) {
    Serial.print(F(" name='"));
    Serial.print(device.localName());
    Serial.print('\'');
  } else {
    Serial.print(F(" name=<none>"));
  }

  if (device.hasAdvertisedServiceUuid()) {
    Serial.print(F(" svcs="));
    for (int i = 0; i < device.advertisedServiceUuidCount(); i++) {
      if (i > 0) {
        Serial.print(',');
      }
      Serial.print(device.advertisedServiceUuid(i));
    }
  }

  Serial.println();
#endif
}

bool nameMatchesHint(const BLEDevice &device) {
  if (!device.hasLocalName()) {
    return false;
  }

  String upper = device.localName();
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

bool serviceMatches(const BLEDevice &device) {
  if (!device.hasAdvertisedServiceUuid()) {
    return false;
  }

  for (int i = 0; i < device.advertisedServiceUuidCount(); i++) {
    if (uuidIsFfe0(device.advertisedServiceUuid(i))) {
      return true;
    }
  }

  return false;
}

bool isTargetDevice(const BLEDevice &device) {
  return serviceMatches(device) || nameMatchesHint(device);
}

bool isAutoConnectTarget(const BLEDevice &device) {
  if (serviceMatches(device)) {
    return true;
  }

  if (!nameMatchesHint(device)) {
    return false;
  }

  if (!device.hasLocalName()) {
    return false;
  }

  String upper = device.localName();
  upper.toUpperCase();

  // Skip the persistent Apple "EUC" beacon (no FFE0) seen on some Macs.
  if (upper == "EUC") {
    return false;
  }

  return true;
}

bool isKnownTargetAddress(const BLEDevice &device) {
  char address[18];
  device.address().toCharArray(address, sizeof(address));
  const int index = bleScanStoreFind(address);
  if (index < 0 || !g_bleDevices[index].isTarget) {
    return false;
  }

  // Never auto-connect to the Apple "EUC" beacon via stored scan entries.
  if (!g_bleDevices[index].hasFfe0 &&
      strcasecmp(g_bleDevices[index].name, "EUC") == 0) {
    return false;
  }

  return true;
}

bool macAddressEquals(const BLEDevice &device, const char *mac) {
  char address[18];
  device.address().toCharArray(address, sizeof(address));
  return strcasecmp(address, mac) == 0;
}

void requestDirectConnect() {
  if (state == State::Connected || state == State::Connecting) {
    return;
  }

  g_directConnectActive = true;
  g_directConnectUntilMs = millis() + WHEEL_DIRECT_SCAN_MS;
  BLE.stopScan();
  state = State::Scanning;
  reconnectAtMs = 0;
  BLE.scan(true);

  Serial.print(F("Direct connect scan for "));
  Serial.println(WHEEL_MAC_ADDRESS);
}

bool beginDirectWheelConnect() {
  requestDirectConnect();
  return false;
}

void scheduleConnectRetry(bool direct) {
  reconnectAtMs = millis() + (direct ? WHEEL_DIRECT_RETRY_MS : EUC_RECONNECT_DELAY_MS);

  if (direct) {
    g_directConnectActive = false;
    webBleScanMarkInactive();
    state = State::Idle;
    Serial.println(F("Direct connect failed — will retry"));
    return;
  }

  if (webBleScanActive()) {
    state = State::Scanning;
    BLE.scan(true);
    Serial.println(F("Resuming scan"));
    return;
  }

  webBleScanMarkInactive();
  state = State::Idle;
}

void beginBleScan() {
  g_directConnectActive = false;
  if (state == State::Connected) {
    Serial.println(F("Disconnecting for new scan"));
    peripheral.disconnect();
    notificationsReady = false;
    frameParser.reset();
    g_wheelConnected = false;
    g_wheelTelemetry.valid = false;
    resetTelemetryTiming();
    telemetryChar = BLECharacteristic();
    peripheral = BLEDevice();
  }

  BLE.stopScan();
  webBleScanStart();
  state = State::Scanning;
  reconnectAtMs = 0;
  BLE.scan(true);
  Serial.println(F("BLE scan started (web)"));
}

void stopBleScan() {
  if (state == State::Scanning) {
    BLE.stopScan();
    state = State::Idle;
  }
  g_directConnectActive = false;
  webBleScanMarkInactive();
  Serial.println(F("BLE scan finished"));
}

void serviceDirectConnectTimeout() {
  if (!g_directConnectActive) {
    return;
  }

  if (millis() < g_directConnectUntilMs) {
    return;
  }

  g_directConnectActive = false;
  if (state == State::Scanning) {
    BLE.stopScan();
    state = State::Idle;
  }

  Serial.println(F("Direct connect scan timed out"));
  reconnectAtMs = millis() + WHEEL_DIRECT_RETRY_MS;
}

void onWebScanRequested() {
  beginBleScan();
}

void onWebConnectRequested() {
  beginDirectWheelConnect();
}

const char *connectionStatusText() {
  switch (state) {
    case State::Connected:
      if (peripheral.hasLocalName()) {
        static char buffer[48];
        peripheral.localName().toCharArray(buffer, sizeof(buffer));
        return buffer;
      }
      return "Connected";
    case State::Connecting:
      return "Connecting…";
    case State::Scanning:
      return g_directConnectActive ? "Connecting…" : "Scanning";
    default:
      return "Idle";
  }
}

bool setupTelemetryNotifications() {
  telemetryChar = peripheral.characteristic(EUC_TELEMETRY_UUID);
  if (!telemetryChar) {
    telemetryChar = peripheral.characteristic("ffe1");
  }

  if (!telemetryChar) {
    Serial.println(F("FFE1 telemetry characteristic not found"));
    return false;
  }

  if (!telemetryChar.canSubscribe()) {
    Serial.println(F("FFE1 does not support notifications"));
    return false;
  }

  telemetryChar.setEventHandler(BLEUpdated, onTelemetryCharacteristicUpdated);

  if (!telemetryChar.subscribe()) {
    Serial.println(F("Failed to subscribe to FFE1 notifications"));
    return false;
  }

  Serial.println(F("Subscribed to FFE1 — waiting for Veteran frames"));
  return true;
}

void resetTelemetryTiming() {
  g_telemetryFrameCount = 0;
  g_telemetryLastGapMs = 0;
  g_telemetryAvgGapMs = 0;
  g_telemetryLastFrameMs = 0;
}

void recordTelemetryFrame() {
  const uint32_t now = millis();
  if (g_telemetryLastFrameMs != 0) {
    g_telemetryLastGapMs = now - g_telemetryLastFrameMs;
    if (g_telemetryAvgGapMs == 0) {
      g_telemetryAvgGapMs = g_telemetryLastGapMs;
    } else {
      g_telemetryAvgGapMs = (g_telemetryAvgGapMs * 4 + g_telemetryLastGapMs) / 5;
    }
  }
  g_telemetryLastFrameMs = now;
  g_telemetryFrameCount++;
}

void processTelemetryChunk(const uint8_t *data, size_t length) {
  if (length == 0) {
    return;
  }

#if BLE_DEBUG_NOTIFY
  Serial.print(F("notify "));
  Serial.print(length);
  Serial.print(F("b:"));
  for (size_t i = 0; i < length; i++) {
    Serial.print(' ');
    if (data[i] < 0x10) {
      Serial.print('0');
    }
    Serial.print(data[i], HEX);
  }
  Serial.println();
#endif

  frameParser.feed(data, length);

  if (!frameParser.hasTelemetry()) {
    return;
  }

  g_wheelTelemetry = frameParser.consumeTelemetry();
  recordTelemetryFrame();
  veteran::printTelemetry(g_wheelTelemetry);
}

void onTelemetryCharacteristicUpdated(BLEDevice /*device*/, BLECharacteristic characteristic) {
  uint8_t buffer[64];
  const int length = characteristic.readValue(buffer, sizeof(buffer));
  processTelemetryChunk(buffer, static_cast<size_t>(length));
}

bool connectToTarget(BLEDevice device, bool direct = false) {
  BLE.stopScan();
  state = State::Connecting;

  Serial.print(F("Connecting to "));
  if (device.hasLocalName()) {
    Serial.print(device.localName());
  } else {
    Serial.print(F("<no name>"));
  }
  Serial.print(F(" @ "));
  Serial.println(device.address());

  if (!device.connected()) {
    if (!device.connect()) {
      Serial.println(F("Connection failed"));
      scheduleConnectRetry(direct);
      return false;
    }
  }

  webBleScanMarkInactive();

  Serial.println(F("Discovering services/characteristics..."));
  if (!device.discoverAttributes()) {
    Serial.println(F("Attribute discovery failed"));
    device.disconnect();
    scheduleConnectRetry(direct);
    return false;
  }

  peripheral = device;

  if (!setupTelemetryNotifications()) {
    peripheral.disconnect();
    scheduleConnectRetry(direct);
    return false;
  }

  frameParser.reset();
  notificationsReady = true;
  state = State::Connected;
  g_directConnectActive = false;

  g_wheelConnected = true;
  if (device.hasLocalName()) {
    device.localName().toCharArray(g_wheelName, sizeof(g_wheelName));
  } else {
    g_wheelName[0] = '\0';
  }
  device.address().toCharArray(g_wheelAddress, sizeof(g_wheelAddress));
  g_wheelTelemetry = {};
  resetTelemetryTiming();

  Serial.println(F("Connected — streaming Veteran telemetry"));
  return true;
}

void connectWifiBlocking() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.print(F("WiFi: connecting to "));
  Serial.println(SECRET_SSID);
  WiFi.begin(SECRET_SSID, SECRET_PASS);

  const uint32_t deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("WiFi connected, IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("WiFi connect timeout"));
  }
}

void runBootOtaWindow() {
  if (WiFi.status() == WL_NO_MODULE) {
    return;
  }

  connectWifiBlocking();
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  ArduinoOTA.begin(WiFi.localIP(), OTA_HOSTNAME, OTA_UPLOAD_PASSWORD, InternalStorage);
  otaReady = true;
  Serial.print(F("OTA window "));
  Serial.print(OTA_BOOT_WINDOW_MS / 1000);
  Serial.print(F("s @ "));
  Serial.println(WiFi.localIP());

  const uint32_t deadline = millis() + OTA_BOOT_WINDOW_MS;
  while (millis() < deadline) {
    ArduinoOTA.handle();
    delay(10);
  }

  Serial.println(F("OTA window closed"));
}

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.print(F("WiFi: connecting to "));
  Serial.println(SECRET_SSID);
  WiFi.begin(SECRET_SSID, SECRET_PASS);
}

void ensureOta() {
  if (g_wifiApMode || WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!otaReady) {
    ArduinoOTA.begin(WiFi.localIP(), OTA_HOSTNAME, OTA_UPLOAD_PASSWORD, InternalStorage);
    otaReady = true;
    g_wifiOtaReady = true;
    Serial.print(F("ArduinoOTA ready @ "));
    Serial.println(WiFi.localIP());
    return;
  }

  g_wifiOtaReady = true;
  ArduinoOTA.handle();
}

void serviceWifiAndWeb() {
  if (!wifiBleConcurrent) {
    return;
  }

  processWifiModeRequests();

  const uint32_t now = millis();

  if (!wifiWebReady()) {
    otaReady = false;
    g_wifiOtaReady = false;
    g_httpServerStarted = false;

    if (g_wifiApMode) {
      if (now - lastWifiAttemptMs >= WIFI_RETRY_MS) {
        lastWifiAttemptMs = now;
        startAccessPoint();
      }
      return;
    }

    if (now - lastWifiAttemptMs >= WIFI_RETRY_MS) {
      lastWifiAttemptMs = now;
      connectWifi();
    }
    return;
  }

  if (!g_wifiApMode) {
    ensureOta();
  } else {
    otaReady = false;
    g_wifiOtaReady = false;
  }

  if (state != State::Connecting) {
    webServerHandleClient(connectionStatusText(), onWebScanRequested, onWebConnectRequested,
                          onWebWifiOtaRequested, onWebWifiApRequested);
  }
}

void serviceBleScanTimeout() {
  if (!webBleScanActive()) {
    return;
  }

  if (millis() < g_bleScanUntilMs) {
    return;
  }

  stopBleScan();
}

}  // namespace

bool wifiWebReady() {
  if (g_wifiApMode) {
    const uint8_t status = WiFi.status();
    return status == WL_AP_LISTENING || status == WL_AP_CONNECTED;
  }

  return WiFi.status() == WL_CONNECTED;
}

void setup() {
  Serial.begin(115200);
  const uint32_t serialDeadline = millis() + 3000;
  while (!Serial && millis() < serialDeadline) {
    delay(10);
  }

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println(F("WiFiNINA module not found — OTA/web disabled"));
  } else {
    const String ninaFw = WiFi.firmwareVersion();
    Serial.print(F("NINA firmware: "));
    Serial.println(ninaFw);
    wifiBleConcurrent = ninaFirmwareSupportsConcurrent(ninaFw);
    if (wifiBleConcurrent) {
      Serial.println(F("NINA supports concurrent WiFi+BLE"));
      startAccessPoint();
    } else {
      Serial.println(F("NINA < 3.0.1: boot OTA window, then BLE-only (no web UI)"));
      runBootOtaWindow();
      disconnectWifiForBle();
    }
  }

  if (!BLE.begin()) {
    Serial.println(F("BLE init failed"));
    while (true) {
      delay(1000);
    }
  }

  Serial.println(F("Nano 33 IoT — BLE scanner web UI + OTA"));
  Serial.print(F("Hotspot: "));
  Serial.print(WIFI_AP_SSID);
  Serial.print(F(" / "));
  Serial.println(WIFI_AP_PASSWORD);
  Serial.println(F("Open http://192.168.4.1/ when connected to the hotspot"));

#if WHEEL_DIRECT_CONNECT
  g_pendingBootDirectConnect = true;
#endif
}

void loop() {
  BLE.poll();
  serviceBleScanTimeout();
  serviceDirectConnectTimeout();

  switch (state) {
    case State::Idle:
#if WHEEL_DIRECT_CONNECT
      if (g_pendingBootDirectConnect) {
        g_pendingBootDirectConnect = false;
        requestDirectConnect();
      } else if (!g_directConnectActive && reconnectAtMs != 0 && millis() >= reconnectAtMs) {
        reconnectAtMs = 0;
        requestDirectConnect();
      }
#endif
      break;

    case State::Scanning: {
      if (reconnectAtMs != 0 && millis() < reconnectAtMs) {
        serviceWifiAndWeb();
        return;
      }
      reconnectAtMs = 0;

      if (!webBleScanActive() && !g_directConnectActive) {
        serviceWifiAndWeb();
        return;
      }

      BLEDevice discovered = BLE.available();
      if (!discovered) {
        serviceWifiAndWeb();
        return;
      }

      printDiscoveredDevice(discovered);
      const bool target = isTargetDevice(discovered);
      bleScanStoreUpsert(discovered, target);

#if AUTO_CONNECT_WHEEL
      if (g_directConnectActive) {
        if (macAddressEquals(discovered, WHEEL_MAC_ADDRESS)) {
          g_directConnectActive = false;
          Serial.println(F("Direct connect: MAC found"));
          connectToTarget(discovered, true);
        }
      } else if (isAutoConnectTarget(discovered) || isKnownTargetAddress(discovered)) {
        Serial.println(F("Target matched — connecting"));
        connectToTarget(discovered);
      }
#endif
      break;
    }

    case State::Connecting:
      break;

    case State::Connected:
      if (!peripheral.connected()) {
        Serial.println(F("Disconnected"));
        notificationsReady = false;
        frameParser.reset();
        g_wheelConnected = false;
        g_wheelTelemetry.valid = false;
        resetTelemetryTiming();
        reconnectAtMs = millis() + EUC_RECONNECT_DELAY_MS;
        state = State::Idle;
        serviceWifiAndWeb();
        return;
      }
      break;
  }

  serviceWifiAndWeb();
}
