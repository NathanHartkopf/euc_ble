#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>

#include <stdio.h>
#include <string.h>

#include "ble_mac.h"
#include "config.h"

static BleMacNotifyFn g_notify_cb = nullptr;
static void *g_notify_user = nullptr;
static char g_status[64] = "Starting";
static char g_device_name[32] = "";
static bool g_connected = false;

static NSString *const kSavedWheelIdKey = @"eucble.savedWheelId";

static bool serviceUuidIsFfe0(CBUUID *uuid) {
  if (!uuid) {
    return false;
  }

  NSString *value = [[uuid UUIDString] uppercaseString];
  return [value isEqualToString:@"FFE0"] || [value hasSuffix:@"-FFE0-0000-1000-8000-00805F9B34FB"];
}

static bool advertisementHasFfe0(NSDictionary<NSString *, id> *advertisementData) {
  id services = advertisementData[CBAdvertisementDataServiceUUIDsKey];
  if (![services isKindOfClass:[NSArray class]]) {
    return false;
  }

  for (CBUUID *uuid in (NSArray *)services) {
    if (serviceUuidIsFfe0(uuid)) {
      return true;
    }
  }

  return false;
}

static bool nameMatches(NSString *name) {
  if (!name || name.length == 0) {
    return false;
  }

  if ([[name uppercaseString] isEqualToString:@"EUC"]) {
    return false;
  }

  for (uint8_t i = 0; i < EUC_NAME_HINT_COUNT; i++) {
    NSString *hint = [NSString stringWithUTF8String:EUC_NAME_HINTS[i]];
    if ([[name uppercaseString] containsString:[hint uppercaseString]]) {
      return true;
    }
  }

  return false;
}

static void setStatus(const char *text) {
  strncpy(g_status, text, sizeof(g_status) - 1);
  g_status[sizeof(g_status) - 1] = '\0';
  fprintf(stderr, "BLE: %s\n", g_status);
}

@interface EucBleDelegate : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
@property(nonatomic, strong) dispatch_queue_t bleQueue;
@property(nonatomic, strong) CBCentralManager *central;
@property(nonatomic, strong) CBPeripheral *peripheral;
@property(nonatomic, strong) CBCharacteristic *telemetryChar;
@property(nonatomic, assign) BOOL scanning;
@property(nonatomic, assign) BOOL connecting;
@property(nonatomic, assign) NSTimeInterval connectStartedAt;
@end

@implementation EucBleDelegate

- (instancetype)init {
  self = [super init];
  if (self) {
    // Dedicated queue: SDL does not pump NSRunLoop, so queue:nil callbacks stall.
    self.bleQueue = dispatch_queue_create("com.eucble.ble", DISPATCH_QUEUE_SERIAL);
    self.central = [[CBCentralManager alloc] initWithDelegate:self queue:self.bleQueue];
  }
  return self;
}

- (void)scheduleOnBleQueue:(dispatch_block_t)block delay:(NSTimeInterval)seconds {
  if (!block) {
    return;
  }

  if (seconds <= 0.0) {
    dispatch_async(self.bleQueue, block);
    return;
  }

  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC)), self.bleQueue,
                 block);
}

- (void)saveWheelIdentifier:(NSUUID *)identifier {
  if (!identifier) {
    return;
  }

  [[NSUserDefaults standardUserDefaults] setObject:identifier.UUIDString forKey:kSavedWheelIdKey];
}

- (BOOL)tryConnectSavedWheel {
  if (self.central.state != CBManagerStatePoweredOn || self.connecting || g_connected) {
    return NO;
  }

  NSString *saved = [[NSUserDefaults standardUserDefaults] stringForKey:kSavedWheelIdKey];
  if (!saved.length) {
    return NO;
  }

  NSUUID *uuid = [[NSUUID alloc] initWithUUIDString:saved];
  if (!uuid) {
    return NO;
  }

  NSArray<CBPeripheral *> *known = [self.central retrievePeripheralsWithIdentifiers:@[ uuid ]];
  if (known.count == 0) {
    return NO;
  }

  CBPeripheral *peripheral = known.firstObject;
  fprintf(stderr, "BLE: reconnecting to saved wheel %s\n", saved.UTF8String);
  [self beginConnect:peripheral name:peripheral.name];
  return YES;
}

- (void)startScan {
  if (self.central.state != CBManagerStatePoweredOn || self.scanning || self.connecting) {
    return;
  }

  if ([self tryConnectSavedWheel]) {
    return;
  }

  self.peripheral = nil;
  self.telemetryChar = nil;
  g_connected = false;
  g_device_name[0] = '\0';
  setStatus("Scanning");

  self.scanning = YES;
  NSDictionary *options = @{CBCentralManagerScanOptionAllowDuplicatesKey : @NO};
  [self.central scanForPeripheralsWithServices:nil options:options];
}

- (void)stopScan {
  if (self.scanning) {
    [self.central stopScan];
    self.scanning = NO;
  }
}

- (void)cancelConnect {
  if (self.peripheral && self.connecting) {
    [self.central cancelPeripheralConnection:self.peripheral];
  }
  self.connecting = NO;
  self.peripheral = nil;
}

- (void)beginConnect:(CBPeripheral *)peripheral name:(NSString *)name {
  [self stopScan];
  self.connecting = YES;
  self.connectStartedAt = [NSDate timeIntervalSinceReferenceDate];
  setStatus("Connecting");

  self.peripheral = peripheral;
  peripheral.delegate = self;

  if (name.length > 0) {
    strncpy(g_device_name, [name UTF8String], sizeof(g_device_name) - 1);
  }

  NSDictionary *options = @{CBConnectPeripheralOptionNotifyOnConnectionKey : @YES};
  [self.central connectPeripheral:peripheral options:options];
}

- (void)serviceConnectTimeoutIfNeeded {
  if (!self.connecting) {
    return;
  }

  const NSTimeInterval elapsed = [NSDate timeIntervalSinceReferenceDate] - self.connectStartedAt;
  if (elapsed < 12.0) {
    return;
  }

  fprintf(stderr, "BLE: connect timeout (quit phone apps using the wheel)\n");
  setStatus("Timeout — phone?");
  [self cancelConnect];
  [self scheduleOnBleQueue:^{
    [self startScan];
  }
                 delay:1.5];
}

- (void)centralManagerDidUpdateState:(CBCentralManager *)central {
  switch (central.state) {
    case CBManagerStatePoweredOn:
      [self startScan];
      break;
    case CBManagerStateUnauthorized:
      setStatus("BLE denied");
      break;
    case CBManagerStatePoweredOff:
      setStatus("BLE off");
      break;
    default:
      setStatus("BLE init");
      break;
  }
}

- (BOOL)shouldConnectToPeripheral:(CBPeripheral *)peripheral
                advertisementData:(NSDictionary<NSString *, id> *)advertisementData {
  NSString *name = peripheral.name;
  if (!name) {
    id local_name = advertisementData[CBAdvertisementDataLocalNameKey];
    if ([local_name isKindOfClass:[NSString class]]) {
      name = local_name;
    }
  }

  const BOOL has_ff_e0 = advertisementHasFfe0(advertisementData);
  const BOOL name_ok = nameMatches(name);

  if (name_ok && has_ff_e0) {
    return YES;
  }

  if (name_ok) {
    return YES;
  }

  if (has_ff_e0 && (!name || name.length == 0)) {
    return YES;
  }

  return NO;
}

- (void)centralManager:(CBCentralManager *)central
    didDiscoverPeripheral:(CBPeripheral *)peripheral
        advertisementData:(NSDictionary<NSString *, id> *)advertisementData
                     RSSI:(NSNumber *)RSSI {
  (void)central;

  if (self.connecting || g_connected) {
    return;
  }

  NSString *name = peripheral.name;
  if (!name) {
    id local_name = advertisementData[CBAdvertisementDataLocalNameKey];
    if ([local_name isKindOfClass:[NSString class]]) {
      name = local_name;
    }
  }

  if (![self shouldConnectToPeripheral:peripheral advertisementData:advertisementData]) {
    return;
  }

  fprintf(stderr, "BLE: found target '%s' (%s) rssi=%d\n",
          name ? [name UTF8String] : "<no name>", peripheral.identifier.UUIDString.UTF8String,
          RSSI.intValue);

  [self beginConnect:peripheral name:name];
}

- (void)centralManager:(CBCentralManager *)central didConnect:(CBPeripheral *)peripheral {
  (void)central;
  self.connecting = NO;
  [self saveWheelIdentifier:peripheral.identifier];
  setStatus("Discovering");

  CBUUID *service_uuid =
      [CBUUID UUIDWithString:@"0000FFE0-0000-1000-8000-00805F9B34FB"];
  [peripheral discoverServices:@[ service_uuid ]];
}

- (void)centralManager:(CBCentralManager *)central
    didFailToConnectPeripheral:(CBPeripheral *)peripheral
                         error:(NSError *)error {
  (void)central;
  (void)peripheral;
  self.connecting = NO;
  self.peripheral = nil;

  if (error) {
    char line[64];
    snprintf(line, sizeof(line), "Connect failed (%ld)", (long)error.code);
    setStatus(line);
    fprintf(stderr, "BLE: %s\n", error.localizedDescription.UTF8String);
  } else {
    setStatus("Connect failed");
  }

  [self scheduleOnBleQueue:^{
    [self startScan];
  }
                 delay:1.5];
}

- (void)centralManager:(CBCentralManager *)central
    didDisconnectPeripheral:(CBPeripheral *)peripheral
                      error:(NSError *)error {
  (void)central;
  (void)peripheral;
  (void)error;

  g_connected = false;
  g_device_name[0] = '\0';
  self.peripheral = nil;
  self.telemetryChar = nil;
  self.connecting = NO;
  setStatus("Reconnecting");

  [self scheduleOnBleQueue:^{
    [self startScan];
  }
                 delay:1.0];
}

- (void)peripheral:(CBPeripheral *)peripheral didDiscoverServices:(NSError *)error {
  if (error) {
    fprintf(stderr, "BLE: service discovery failed: %s\n", error.localizedDescription.UTF8String);
    setStatus("Service error");
    [self.central cancelPeripheralConnection:peripheral];
    return;
  }

  if (peripheral.services.count == 0) {
    setStatus("No FFE0 service");
    [self.central cancelPeripheralConnection:peripheral];
    return;
  }

  CBUUID *char_uuid =
      [CBUUID UUIDWithString:@"0000FFE1-0000-1000-8000-00805F9B34FB"];
  for (CBService *service in peripheral.services) {
    [peripheral discoverCharacteristics:@[ char_uuid ] forService:service];
  }
}

- (void)peripheral:(CBPeripheral *)peripheral
    didDiscoverCharacteristicsForService:(CBService *)service
                                   error:(NSError *)error {
  if (error) {
    fprintf(stderr, "BLE: char discovery failed: %s\n", error.localizedDescription.UTF8String);
    setStatus("FFE1 error");
    [self.central cancelPeripheralConnection:peripheral];
    return;
  }

  for (CBCharacteristic *characteristic in service.characteristics) {
    if (![[characteristic.UUID.UUIDString uppercaseString] hasSuffix:@"FFE1"]) {
      continue;
    }

    self.telemetryChar = characteristic;
    [peripheral setNotifyValue:YES forCharacteristic:characteristic];

    g_connected = true;
    self.connecting = NO;

    NSString *name = peripheral.name;
    if (!name || name.length == 0) {
      name = g_device_name[0] ? [NSString stringWithUTF8String:g_device_name] : @"Connected";
    }
    strncpy(g_device_name, [name UTF8String], sizeof(g_device_name) - 1);
    setStatus(g_device_name);
    return;
  }

  setStatus("No FFE1 char");
  [self.central cancelPeripheralConnection:peripheral];
}

- (void)peripheral:(CBPeripheral *)peripheral
    didUpdateValueForCharacteristic:(CBCharacteristic *)characteristic
                              error:(NSError *)error {
  (void)peripheral;

  if (error || !g_notify_cb) {
    return;
  }

  NSData *data = characteristic.value;
  if (data.length == 0) {
    return;
  }

  g_notify_cb(static_cast<const uint8_t *>(data.bytes), data.length, g_notify_user);
}

- (void)peripheral:(CBPeripheral *)peripheral
    didUpdateNotificationStateForCharacteristic:(CBCharacteristic *)characteristic
                                          error:(NSError *)error {
  (void)peripheral;
  if (error) {
    fprintf(stderr, "BLE: notify setup failed: %s\n", error.localizedDescription.UTF8String);
    setStatus("Notify failed");
    return;
  }

  if (characteristic.isNotifying) {
    fprintf(stderr, "BLE: notify enabled on FFE1\n");
  }
}

@end

static EucBleDelegate *g_delegate = nil;

void ble_mac_set_notify_callback(BleMacNotifyFn callback, void *user_data) {
  g_notify_cb = callback;
  g_notify_user = user_data;
}

bool ble_mac_begin(void) {
  @autoreleasepool {
    g_delegate = [[EucBleDelegate alloc] init];
    return g_delegate != nil;
  }
}

void ble_mac_poll(void) {
  if (!g_delegate.bleQueue) {
    return;
  }

  @autoreleasepool {
    // Drain pending CoreBluetooth callbacks on the dedicated queue.
    dispatch_sync(g_delegate.bleQueue, ^{
      [g_delegate serviceConnectTimeoutIfNeeded];
    });
  }
}

bool ble_mac_is_connected(void) { return g_connected; }

const char *ble_mac_status_text(void) { return g_status; }

const char *ble_mac_device_name(void) { return g_device_name; }
