#include <SPI.h>
#include <WiFiNINA.h>
#include <ArduinoOTA.h>

#include "wifi_secrets.h"

namespace {

constexpr unsigned long kWifiRetryMs = 5000;

void connectWifi() {
  Serial.print(F("Connecting to "));
  Serial.println(SECRET_SSID);

  WiFi.begin(SECRET_SSID, SECRET_PASS);

  unsigned long lastAttempt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastAttempt >= kWifiRetryMs) {
      Serial.println(F("WiFi status: retrying..."));
      WiFi.begin(SECRET_SSID, SECRET_PASS);
      lastAttempt = millis();
    }
    delay(250);
    Serial.print('.');
  }

  Serial.println();
  Serial.print(F("WiFi connected, IP: "));
  Serial.println(WiFi.localIP());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println(F("WiFiNINA module not found"));
    while (true) {
      delay(1000);
    }
  }

  const String fv = WiFi.firmwareVersion();
  Serial.print(F("NINA firmware: "));
  Serial.println(fv);

  connectWifi();

  ArduinoOTA.begin(WiFi.localIP(), "nano33iot-nf7266", OTA_UPLOAD_PASSWORD, InternalStorage);

  Serial.println(F("ArduinoOTA ready"));
  Serial.println(F("Use Arduino IDE network port or Upload Using Programmer"));
}

void loop() {
  ArduinoOTA.handle();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi lost, reconnecting..."));
    connectWifi();
    ArduinoOTA.begin(WiFi.localIP(), "nano33iot-nf7266", OTA_UPLOAD_PASSWORD, InternalStorage);
  }
}
