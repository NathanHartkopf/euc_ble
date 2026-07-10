#include "euc_ble.h"

#include <NimBLEDevice.h>

#include "config.h"

namespace {

constexpr size_t kProxyCacheMax = 256;

NimBLEClient *g_client = nullptr;
NimBLERemoteCharacteristic *g_telemetry_char = nullptr;
NimBLEServer *g_server = nullptr;
NimBLECharacteristic *g_proxy_char = nullptr;
EucBleClient *g_owner = nullptr;

uint8_t g_last_notify[kProxyCacheMax];
size_t g_last_notify_len = 0;

bool uuidIsFfe0(const std::string &uuid) {
  if (uuid.empty()) {
    return false;
  }

  if (uuid == EUC_SERVICE_UUID || uuid == EUC_SERVICE_UUID_SHORT) {
    return true;
  }

  return uuid.size() >= 4 && uuid.compare(uuid.size() - 4, 4, EUC_SERVICE_UUID_SHORT) == 0;
}

bool nameMatchesHint(const NimBLEAdvertisedDevice *device) {
  if (!device->haveName()) {
    return false;
  }

  String upper = device->getName().c_str();
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

bool serviceMatches(const NimBLEAdvertisedDevice *device) {
  if (!device->haveServiceUUID()) {
    return false;
  }

  for (int i = 0; i < device->getServiceUUIDCount(); i++) {
    if (uuidIsFfe0(device->getServiceUUID(i).toString())) {
      return true;
    }
  }

  return false;
}

bool isTargetDevice(const NimBLEAdvertisedDevice *device) {
  return serviceMatches(device) || nameMatchesHint(device);
}

bool macAddressEquals(const NimBLEAdvertisedDevice *device, const char *mac) {
  return strcasecmp(device->getAddress().toString().c_str(), mac) == 0;
}

void restartAdvertising() {
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  if (!advertising) {
    return;
  }

  advertising->stop();
  advertising->addServiceUUID(EUC_SERVICE_UUID);
  advertising->setName(g_owner ? g_owner->advertisedName() : REPEATER_DEVICE_NAME);
  advertising->start();
}

class ProxyCallbacks : public NimBLECharacteristicCallbacks {
 public:
  void onRead(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
    (void)connInfo;
    if (g_last_notify_len > 0) {
      characteristic->setValue(g_last_notify, g_last_notify_len);
    }
  }

  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &connInfo) override {
    (void)connInfo;
    if (!g_owner) {
      return;
    }

    const NimBLEAttValue &value = characteristic->getValue();
    if (value.size() == 0) {
      return;
    }

#if BLE_DEBUG_PROXY
    Serial.printf("proxy write %u bytes\n", value.size());
#endif

    g_owner->forwardProxyWrite(value.data(), value.size());
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(EucBleClient *owner) : owner_(owner) {}

  void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override {
    (void)server;
    (void)connInfo;
    if (owner_) {
      owner_->notifyProxyClientConnected();
    }
    Serial.println("App connected to BLE repeater");
  }

  void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override {
    (void)server;
    (void)connInfo;
    if (owner_) {
      owner_->notifyProxyClientDisconnected();
    }
    Serial.printf("App disconnected from BLE repeater (%d)\n", reason);
    restartAdvertising();
  }

 private:
  EucBleClient *owner_;
};

class ScanCallbacks : public NimBLEScanCallbacks {
 public:
  explicit ScanCallbacks(EucBleClient *owner) : owner_(owner) {}

  void onResult(const NimBLEAdvertisedDevice *advertisedDevice) override {
    if (!owner_ || owner_->state() != EucBleState::Scanning) {
      return;
    }

#if BLE_DEBUG_SCAN
    Serial.printf("adv %s rssi=%d", advertisedDevice->getAddress().toString().c_str(),
                  advertisedDevice->getRSSI());
    if (advertisedDevice->haveName()) {
      Serial.printf(" name='%s'", advertisedDevice->getName().c_str());
    }
    Serial.println();
#endif

#if WHEEL_DIRECT_CONNECT
    if (macAddressEquals(advertisedDevice, WHEEL_MAC_ADDRESS)) {
      Serial.println("Direct connect: MAC found");
      NimBLEDevice::getScan()->stop();
      owner_->connectToDevice(advertisedDevice);
      return;
    }
#endif

    if (isTargetDevice(advertisedDevice)) {
      Serial.println("Target matched — connecting");
      NimBLEDevice::getScan()->stop();
      owner_->connectToDevice(advertisedDevice);
    }
  }

 private:
  EucBleClient *owner_;
};

class ClientCallbacks : public NimBLEClientCallbacks {
 public:
  explicit ClientCallbacks(EucBleClient *owner) : owner_(owner) {}

  void onConnect(NimBLEClient *client) override {
    Serial.printf("Wheel connected: %s\n", client->getPeerAddress().toString().c_str());
  }

  void onDisconnect(NimBLEClient *client, int reason) override {
    Serial.printf("Wheel disconnected (%d)\n", reason);
    if (owner_) {
      owner_->handleDisconnect();
    }
  }

 private:
  EucBleClient *owner_;
};

void notifyCallback(NimBLERemoteCharacteristic *characteristic, uint8_t *data, size_t length,
                    bool is_notify) {
  (void)characteristic;
  (void)is_notify;
  if (g_owner) {
    g_owner->processTelemetryChunk(data, length);
  }
}

}  // namespace

void EucBleClient::notifyProxyClientConnected() { proxy_client_count_++; }

void EucBleClient::notifyProxyClientDisconnected() {
  if (proxy_client_count_ > 0) {
    proxy_client_count_--;
  }
}

bool EucBleClient::setupRepeater() {
  static ServerCallbacks server_callbacks(this);
  static ProxyCallbacks proxy_callbacks;

  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(&server_callbacks);

  NimBLEService *service = g_server->createService(EUC_SERVICE_UUID);
  g_proxy_char = service->createCharacteristic(
      EUC_TELEMETRY_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
          NIMBLE_PROPERTY::NOTIFY);
  g_proxy_char->setCallbacks(&proxy_callbacks);
  service->start();

  restartAdvertising();
  Serial.printf("BLE repeater advertising as '%s' (FFE0/FFE1)\n", advertised_name_);
  return true;
}

void EucBleClient::updateAdvertisedName(const char *name) {
  if (!name || name[0] == '\0') {
    return;
  }

  if (strncmp(advertised_name_, name, sizeof(advertised_name_)) == 0) {
    return;
  }

  strncpy(advertised_name_, name, sizeof(advertised_name_) - 1);
  advertised_name_[sizeof(advertised_name_) - 1] = '\0';
  NimBLEDevice::setDeviceName(advertised_name_);
  restartAdvertising();
  Serial.printf("Repeater now advertising as '%s'\n", advertised_name_);
}

bool EucBleClient::begin() {
  g_owner = this;
  strncpy(advertised_name_, REPEATER_DEVICE_NAME, sizeof(advertised_name_) - 1);

  NimBLEDevice::init(advertised_name_);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  if (!setupRepeater()) {
    return false;
  }

  static ClientCallbacks client_callbacks(this);
  g_client = NimBLEDevice::createClient();
  g_client->setClientCallbacks(&client_callbacks);

  static ScanCallbacks scan_callbacks(this);
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scan_callbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(45);
  scan->setWindow(30);
  scan->setDuplicateFilter(false);

#if WHEEL_DIRECT_CONNECT
  requestDirectConnect();
#endif

  return true;
}

void EucBleClient::requestDirectConnect() {
  if (state_ == EucBleState::Connected || state_ == EucBleState::Connecting) {
    return;
  }

  NimBLEDevice::getScan()->stop();
  scan_mode_ = ScanMode::DirectMac;
  direct_scan_until_ms_ = millis() + WHEEL_DIRECT_SCAN_MS;
  state_ = EucBleState::Scanning;
  reconnect_at_ms_ = 0;

  Serial.printf("Direct connect scan for %s\n", WHEEL_MAC_ADDRESS);
  NimBLEDevice::getScan()->start(WHEEL_DIRECT_SCAN_MS, false, true);
}

void EucBleClient::scheduleReconnect(uint32_t delay_ms) {
  reconnect_at_ms_ = millis() + delay_ms;
  state_ = EucBleState::Idle;
  scan_mode_ = ScanMode::None;
}

const char *EucBleClient::statusText() const {
  switch (state_) {
    case EucBleState::Connected:
      if (WHEEL_DISPLAY_NAME[0] != '\0') {
        return WHEEL_DISPLAY_NAME;
      }
      if (proxy_client_count_ > 0) {
        return advertised_name_[0] ? advertised_name_ : "Connected + DarknessBot";
      }
      return wheel_name_[0] ? wheel_name_ : "Connected";
    case EucBleState::Connecting:
      return "Connecting";
    case EucBleState::Scanning:
      return "Scanning";
    default:
      return advertised_name_;
  }
}

bool EucBleClient::consumeTelemetryUpdate() {
  if (!telemetry_updated_) {
    return false;
  }
  telemetry_updated_ = false;
  return true;
}

void EucBleClient::processTelemetryChunk(const uint8_t *data, size_t length) {
  if (length == 0) {
    return;
  }

#if BLE_DEBUG_NOTIFY
  Serial.printf("notify %ub:", length);
  for (size_t i = 0; i < length; i++) {
    Serial.printf(" %02X", data[i]);
  }
  Serial.println();
#endif

  if (length <= kProxyCacheMax) {
    memcpy(g_last_notify, data, length);
    g_last_notify_len = length;
  }

  if (g_proxy_char) {
    g_proxy_char->setValue(data, length);
    g_proxy_char->notify();
  }

  frame_parser_.feed(data, length);
  if (!frame_parser_.hasTelemetry()) {
    return;
  }

  telemetry_ = frame_parser_.consumeTelemetry();
  telemetry_updated_ = true;
}

void EucBleClient::forwardProxyWrite(const uint8_t *data, size_t length) {
  if (!data || length == 0 || !g_telemetry_char || !g_client || !g_client->isConnected()) {
    return;
  }

  if (g_telemetry_char->canWriteNoResponse()) {
    g_telemetry_char->writeValue(data, length, false);
    return;
  }

  if (g_telemetry_char->canWrite()) {
    g_telemetry_char->writeValue(data, length, true);
  }
}

bool EucBleClient::subscribeTelemetry() {
  if (!g_client || !g_client->isConnected()) {
    return false;
  }

  NimBLERemoteService *service = g_client->getService(EUC_SERVICE_UUID);
  if (!service) {
    service = g_client->getService(EUC_SERVICE_UUID_SHORT);
  }
  if (!service) {
    Serial.println("FFE0 service not found");
    return false;
  }

  g_telemetry_char = service->getCharacteristic(EUC_TELEMETRY_UUID);
  if (!g_telemetry_char) {
    g_telemetry_char = service->getCharacteristic("ffe1");
  }
  if (!g_telemetry_char) {
    Serial.println("FFE1 characteristic not found");
    return false;
  }

  if (!g_telemetry_char->canNotify()) {
    Serial.println("FFE1 does not support notifications");
    return false;
  }

  if (!g_telemetry_char->subscribe(true, notifyCallback)) {
    Serial.println("Failed to subscribe to FFE1");
    return false;
  }

  Serial.println("Subscribed to wheel FFE1");
  return true;
}

bool EucBleClient::connectToDevice(const NimBLEAdvertisedDevice *device) {
  if (!device || !g_client) {
    return false;
  }

  state_ = EucBleState::Connecting;
  scan_mode_ = ScanMode::None;
  NimBLEDevice::getScan()->stop();

  Serial.printf("Connecting to %s @ %s\n", device->haveName() ? device->getName().c_str() : "<no name>",
                device->getAddress().toString().c_str());

  if (!g_client->connect(device)) {
    Serial.println("Connection failed");
    scheduleReconnect(WHEEL_DIRECT_RETRY_MS);
    return false;
  }

  if (!subscribeTelemetry()) {
    g_client->disconnect();
    scheduleReconnect(WHEEL_DIRECT_RETRY_MS);
    return false;
  }

  frame_parser_.reset();
  telemetry_ = {};
  telemetry_updated_ = false;
  g_last_notify_len = 0;

  if (device->haveName()) {
    strncpy(wheel_name_, device->getName().c_str(), sizeof(wheel_name_) - 1);
    updateAdvertisedName(wheel_name_);
  } else {
    wheel_name_[0] = '\0';
  }
  strncpy(wheel_address_, device->getAddress().toString().c_str(), sizeof(wheel_address_) - 1);

  state_ = EucBleState::Connected;
  Serial.println("Wheel link up — repeater passthrough active");
  return true;
}

void EucBleClient::handleDisconnect() {
  g_telemetry_char = nullptr;
  frame_parser_.reset();
  telemetry_.valid = false;
  telemetry_updated_ = true;
  wheel_name_[0] = '\0';
  wheel_address_[0] = '\0';
  g_last_notify_len = 0;
  scheduleReconnect(EUC_RECONNECT_DELAY_MS);
}

void EucBleClient::loop() {
  if (state_ == EucBleState::Scanning && scan_mode_ == ScanMode::DirectMac &&
      millis() >= direct_scan_until_ms_) {
    Serial.println("Direct connect scan timed out");
    NimBLEDevice::getScan()->stop();
    scheduleReconnect(WHEEL_DIRECT_RETRY_MS);
  }

  if (state_ == EucBleState::Idle && reconnect_at_ms_ != 0 && millis() >= reconnect_at_ms_) {
    reconnect_at_ms_ = 0;
#if WHEEL_DIRECT_CONNECT
    requestDirectConnect();
#endif
  }

  if (state_ == EucBleState::Connected && g_client && !g_client->isConnected()) {
    handleDisconnect();
  }
}
