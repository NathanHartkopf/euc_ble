#pragma once

#include <WiFiNINA.h>

#include "ble_scan_store.h"
#include "config.h"
#include "veteran_protocol.h"

extern WiFiServer g_httpServer;
extern bool g_httpServerStarted;
extern bool g_bleScanActive;
extern uint32_t g_bleScanUntilMs;
extern bool g_wheelConnected;
extern char g_wheelName[32];
extern char g_wheelAddress[18];
extern veteran::Telemetry g_wheelTelemetry;
extern uint32_t g_telemetryFrameCount;
extern uint32_t g_telemetryLastGapMs;
extern uint32_t g_telemetryAvgGapMs;
extern uint32_t g_telemetryLastFrameMs;
extern bool g_wifiApMode;

bool wifiWebReady();

inline bool webBleScanActive() {
  return g_bleScanActive;
}

inline void webBleScanMarkInactive() {
  g_bleScanActive = false;
  g_bleScanUntilMs = 0;
}

inline void webBleScanStart() {
  bleScanStoreClear();
  g_bleScanActive = true;
  g_bleScanUntilMs = millis() + BLE_SCAN_DURATION_MS;
}

inline void webJsonPrintEscaped(WiFiClient &client, const char *value) {
  for (const char *p = value; *p; p++) {
    const char c = *p;
    if (c == '"' || c == '\\') {
      client.print('\\');
    }
    if (c == '\n' || c == '\r') {
      continue;
    }
    client.print(c);
  }
}

inline void webPrintJsonFloat(WiFiClient &client, float value, uint8_t decimals) {
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%.*f", decimals, static_cast<double>(value));
  client.print(buffer);
}

inline void webPrintTelemetryJson(WiFiClient &client) {
  client.print(F(",\"connected\":"));
  client.print(g_wheelConnected ? F("true") : F("false"));

  if (g_wheelConnected) {
    client.print(F(",\"device\":{\"name\":\""));
    webJsonPrintEscaped(client, g_wheelName);
    client.print(F("\",\"address\":\""));
    webJsonPrintEscaped(client, g_wheelAddress);
    client.print(F("\"}"));
  }

  client.print(F(",\"telemetry\":{"));
  client.print(F("\"valid\":"));
  client.print(g_wheelTelemetry.valid ? F("true") : F("false"));
  client.print(F(",\"frame_count\":"));
  client.print(g_telemetryFrameCount);
  client.print(F(",\"last_gap_ms\":"));
  client.print(g_telemetryLastGapMs);
  client.print(F(",\"avg_gap_ms\":"));
  client.print(g_telemetryAvgGapMs);
  client.print(F(",\"age_ms\":"));
  if (g_telemetryLastFrameMs != 0) {
    client.print(millis() - g_telemetryLastFrameMs);
  } else {
    client.print(0);
  }

  if (g_wheelTelemetry.valid) {
    client.print(F(",\"speed_kmh\":"));
    webPrintJsonFloat(client, g_wheelTelemetry.speed_kmh, 1);
    client.print(F(",\"voltage_v\":"));
    webPrintJsonFloat(client, g_wheelTelemetry.voltage_v, 1);
    client.print(F(",\"current_a\":"));
    webPrintJsonFloat(client, g_wheelTelemetry.current_a, 1);
    client.print(F(",\"battery_pct\":"));
    client.print(g_wheelTelemetry.battery_pct);
    client.print(F(",\"temp_c\":"));
    webPrintJsonFloat(client, g_wheelTelemetry.temp_c, 1);
    client.print(F(",\"trip_km\":"));
    webPrintJsonFloat(client, g_wheelTelemetry.trip_m / 1000.0f, 2);
    client.print(F(",\"odometer_km\":"));
    webPrintJsonFloat(client, g_wheelTelemetry.total_m / 1000.0f, 1);
    client.print(F(",\"firmware_ver\":"));
    client.print(g_wheelTelemetry.firmware_ver);
    client.print(F(",\"charging\":"));
    client.print(g_wheelTelemetry.charging ? F("true") : F("false"));
  }

  client.print(F("}"));
}

inline void webPrintWifiJson(WiFiClient &client) {
  client.print(F(",\"wifi\":{\"mode\":\"ap\",\"ssid\":\""));
  webJsonPrintEscaped(client, WiFi.SSID());
  client.print(F("\",\"ip\":\""));
  const IPAddress ip = WiFi.localIP();
  char ipBuf[16];
  snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  webJsonPrintEscaped(client, ipBuf);
  client.print(F("\"}"));
}

inline void webSendJsonDevices(WiFiClient &client, const char *connectionStatus) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: application/json"));
  client.println(F("Connection: close"));
  client.println(F("Cache-Control: no-store"));
  client.println();

  client.print(F("{\"scanning\":"));
  client.print(g_bleScanActive ? F("true") : F("false"));
  client.print(F(",\"count\":"));
  client.print(g_bleDeviceCount);
  client.print(F(",\"uptime_ms\":"));
  client.print(millis());
  client.print(F(",\"connection\":\""));
  webJsonPrintEscaped(client, connectionStatus);
  client.print(F("\",\"devices\":["));

  for (uint8_t i = 0; i < g_bleDeviceCount; i++) {
    const BleAdvertisement &d = g_bleDevices[i];
    if (i > 0) {
      client.print(',');
    }
    client.print(F("{\"address\":\""));
    webJsonPrintEscaped(client, d.address);
    client.print(F("\",\"name\":\""));
    webJsonPrintEscaped(client, d.name);
    client.print(F("\",\"rssi\":"));
    client.print(d.rssi);
    client.print(F(",\"services\":\""));
    webJsonPrintEscaped(client, d.services);
    client.print(F("\",\"ffe0\":"));
    client.print(d.hasFfe0 ? F("true") : F("false"));
    client.print(F(",\"target\":"));
    client.print(d.isTarget ? F("true") : F("false"));
    client.print(F("}"));
  }

  client.print(F("]"));
  webPrintWifiJson(client);
  webPrintTelemetryJson(client);
  client.println(F("}"));
}

inline void webSendScanResponse(WiFiClient &client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: application/json"));
  client.println(F("Connection: close"));
  client.println();
  client.println(F("{\"ok\":true}"));
}

inline void webSendHtmlPage(WiFiClient &client) {
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Content-Type: text/html; charset=utf-8"));
  client.println(F("Connection: close"));
  client.println(F("Cache-Control: no-store"));
  client.println();
  client.println(F("<!DOCTYPE html><html><head>"));
  client.println(F("<meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"));
  client.println(F("<title>Nano 33 IoT BLE Scanner</title>"));
  client.println(F("<style>"));
  client.println(F("body{font-family:system-ui,sans-serif;margin:1rem;background:#111;color:#eee}"));
  client.println(F("h1{font-size:1.2rem}button{padding:.6rem 1rem;font-size:1rem;cursor:pointer}"));
  client.println(F("table{width:100%;border-collapse:collapse;margin-top:1rem;font-size:.9rem}"));
  client.println(F("th,td{border-bottom:1px solid #333;padding:.45rem;text-align:left}"));
  client.println(F(".target{color:#6f6}.muted{color:#888}.badge{display:inline-block;padding:.1rem .35rem;border-radius:.25rem;background:#333;font-size:.75rem}"));
  client.println(F("#telemetry{margin-top:1.5rem;padding:1rem;border:1px solid #333;border-radius:.5rem;background:#1a1a1a}"));
  client.println(F("#speedometer{text-align:center;padding:.5rem 0 1.25rem;margin-bottom:.75rem;border-bottom:1px solid #333}"));
  client.println(F("#speed_mph{display:block;font-size:4.5rem;font-weight:700;line-height:1;letter-spacing:-.04em}"));
  client.println(F(".speed-unit{display:block;font-size:1rem;color:#888;font-weight:500;margin-top:.25rem}"));
  client.println(F("#telemetry h2{font-size:1rem;margin:0 0 .75rem}"));
  client.println(F(".metrics{display:grid;grid-template-columns:repeat(auto-fill,minmax(8rem,1fr));gap:.75rem}"));
  client.println(F(".metric label{display:block;font-size:.75rem;color:#888;margin-bottom:.15rem}"));
  client.println(F(".metric span{font-size:1.25rem;font-weight:600}"));
  client.println(F("#timing{margin-top:.75rem;font-size:.8rem;color:#888;line-height:1.4}"));
  client.println(F("#wifiBar{margin:.75rem 0;padding:.6rem .75rem;border:1px solid #333;border-radius:.5rem;background:#1a1a1a;font-size:.85rem}"));
  client.println(F(".hidden{display:none}"));
  client.println(F("</style></head><body>"));
  client.println(F("<h1>Nano 33 IoT — BLE Scanner</h1>"));
  client.println(F("<div id=wifiBar><span id=wifiInfo class=muted>WiFi…</span></div>"));
  client.println(F("<p><button id=scan>Scan</button> <button id=connect>Connect</button> <span id=status class=muted>Idle</span></p>"));
  client.println(F("<div id=telemetry class=hidden>"));
  client.println(F("<div id=speedometer><span id=speed_mph>—</span><span class=speed-unit>mph</span></div>"));
  client.println(F("<h2>Live telemetry</h2><p id=wheel class=muted></p>"));
  client.println(F("<div class=metrics>"));
  client.println(F("<div class=metric><label>Voltage</label><span id=t_volt>—</span></div>"));
  client.println(F("<div class=metric><label>Current</label><span id=t_amp>—</span></div>"));
  client.println(F("<div class=metric><label>Battery</label><span id=t_batt>—</span></div>"));
  client.println(F("<div class=metric><label>Temp</label><span id=t_temp>—</span></div>"));
  client.println(F("<div class=metric><label>Trip</label><span id=t_trip>—</span></div>"));
  client.println(F("<div class=metric><label>Odometer</label><span id=t_odo>—</span></div>"));
  client.println(F("<div class=metric><label>Charging</label><span id=t_chg>—</span></div>"));
  client.println(F("</div><p id=timing class=muted></p></div>"));
  client.println(F("<table><thead><tr><th>Name</th><th>Address</th><th>RSSI</th><th>Services</th><th></th></tr></thead>"));
  client.println(F("<tbody id=rows><tr><td colspan=5 class=muted>No devices yet — press Scan</td></tr></tbody>"));
  client.println(F("</table><script>"));
  client.println(F("const statusEl=document.getElementById('status');const rowsEl=document.getElementById('rows');"));
  client.println(F("const wifiInfoEl=document.getElementById('wifiInfo');"));
  client.println(F("const telEl=document.getElementById('telemetry');const wheelEl=document.getElementById('wheel');"));
  client.println(F("const speedEl=document.getElementById('speed_mph');const timingEl=document.getElementById('timing');const WEB_POLL_MS="));
  client.print(WEB_REFRESH_MS);
  client.println(F(";const KMH_TO_MPH=0.621371;let lastFetchAt=0,lastFrameCount=0;"));
  client.println(F("const kmToMi=v=>v*KMH_TO_MPH;const cToF=v=>v*9/5+32;"));
  client.println(F("function setTel(id,v){document.getElementById(id).textContent=v;}"));
  client.println(F("function updateWifiBar(w){if(!w)return;wifiInfoEl.textContent=`Hotspot ${w.ssid} @ ${w.ip}`;}"));
  client.println(F("function updateTiming(t){if(!timingEl)return;const now=Date.now();"));
  client.println(F("const webPollMs=lastFetchAt?(now-lastFetchAt):WEB_POLL_MS;const framesDelta=lastFrameCount?(t.frame_count-lastFrameCount):0;"));
  client.println(F("const wheelHz=t.avg_gap_ms>0?(1000/t.avg_gap_ms):0;const ageMs=(t.age_ms||0)+(now-(window._fetchedAt||now));"));
  client.println(F("timingEl.textContent=`Wheel ~${wheelHz?wheelHz.toFixed(1):'—'} Hz (${t.avg_gap_ms||'—'} ms avg) · Web poll ${(webPollMs/1000).toFixed(1)} s · Data age ${Math.round(ageMs)} ms · ~${framesDelta} wheel frame${framesDelta===1?'':'s'}/poll`;"));
  client.println(F("lastFetchAt=now;lastFrameCount=t.frame_count||0;}"));
  client.println(F("function showTelemetry(d){if(!d.connected){telEl.className='hidden';return;}"));
  client.println(F("telEl.className='';const t=d.telemetry||{};const dev=d.device?`${d.device.name||'Wheel'} (${d.device.address})`:'Connected';"));
  client.println(F("wheelEl.textContent=dev;updateTiming(t);if(!t.valid){speedEl.textContent='—';['t_volt','t_amp','t_batt','t_temp','t_trip','t_odo','t_chg'].forEach(id=>setTel(id,'—'));return;}"));
  client.println(F("speedEl.textContent=Math.abs(kmToMi(t.speed_kmh)).toFixed(1);setTel('t_volt',t.voltage_v.toFixed(1)+' V');"));
  client.println(F("setTel('t_amp',t.current_a.toFixed(1)+' A');setTel('t_batt',t.battery_pct+'%');setTel('t_temp',cToF(t.temp_c).toFixed(1)+' °F');"));
  client.println(F("setTel('t_trip',kmToMi(t.trip_km).toFixed(2)+' mi');setTel('t_odo',kmToMi(t.odometer_km).toFixed(1)+' mi');setTel('t_chg',t.charging?'Yes':'No');}"));
  client.println(F("async function refresh(){try{const r=await fetch('/api/devices');const d=await r.json();window._fetchedAt=Date.now();"));
  client.println(F("updateWifiBar(d.wifi);const conn=d.connected?('Connected'+(d.device&&d.device.name?': '+d.device.name:'')):(d.scanning?'Scanning…':d.connection||'Idle');"));
  client.println(F("statusEl.textContent=conn+' · '+d.count+' device(s)';showTelemetry(d);"));
  client.println(F("if(!d.devices.length){rowsEl.innerHTML='<tr><td colspan=5 class=muted>No devices</td></tr>';return;}"));
  client.println(F("rowsEl.innerHTML=d.devices.map(x=>`<tr><td>${x.name||'<span class=muted>—</span>'}</td><td>${x.address}</td><td>${x.rssi}</td><td>${x.services||'—'}</td><td>${x.target?'<span class=badge>target</span>':''}${x.ffe0?' <span class=badge>FFE0</span>':''}</td></tr>`).join('');"));
  client.println(F("}catch(e){statusEl.textContent='Refresh failed';}}"));
  client.println(F("document.getElementById('scan').onclick=async()=>{statusEl.textContent='Starting scan…';"));
  client.println(F("try{await fetch('/api/scan',{method:'POST'});}catch(e){} refresh();};"));
  client.println(F("document.getElementById('connect').onclick=async()=>{statusEl.textContent='Connecting…';"));
  client.println(F("try{await fetch('/api/connect',{method:'POST'});}catch(e){} refresh();};"));
  client.println(F("refresh(); setInterval(refresh,"));
  client.print(WEB_REFRESH_MS);
  client.println(F(");"));
  client.println(F("</script></body></html>"));
}

inline void webSendNotFound(WiFiClient &client) {
  client.println(F("HTTP/1.1 404 Not Found"));
  client.println(F("Content-Type: text/plain"));
  client.println(F("Connection: close"));
  client.println();
  client.println(F("Not found"));
}

inline void webServerEnsureStarted() {
  if (g_httpServerStarted || !wifiWebReady()) {
    return;
  }

  g_httpServer.begin();
  g_httpServerStarted = true;
  Serial.print(F("HTTP server on port "));
  Serial.println(HTTP_PORT);
}

inline void webServerHandleClient(const char *connectionStatus, void (*onScanRequested)(),
                                  void (*onConnectRequested)()) {
  webServerEnsureStarted();

  WiFiClient client = g_httpServer.available();
  if (!client) {
    return;
  }

  client.setTimeout(100);
  String requestLine = client.readStringUntil('\n');
  requestLine.trim();

  while (client.connected()) {
    const String line = client.readStringUntil('\n');
    if (line == "\r" || line.length() == 0) {
      break;
    }
  }

  if (requestLine.startsWith(F("GET /api/devices"))) {
    webSendJsonDevices(client, connectionStatus);
  } else if (requestLine.startsWith(F("POST /api/connect"))) {
    if (onConnectRequested) {
      onConnectRequested();
    }
    webSendScanResponse(client);
  } else if (requestLine.startsWith(F("POST /api/scan"))) {
    if (onScanRequested) {
      onScanRequested();
    }
    webSendScanResponse(client);
  } else if (requestLine.startsWith(F("GET / ")) || requestLine.startsWith(F("GET /index"))) {
    webSendHtmlPage(client);
  } else {
    webSendNotFound(client);
  }

  delay(1);
  client.stop();
}
