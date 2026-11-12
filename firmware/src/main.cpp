#include <Arduino.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "WiFiManager.h"

WebServer server(80);
DNSServer dnsServer;
WiFiManager wifiManager;

constexpr byte DNS_PORT = 53;
constexpr char AP_SSID[] = "ESP32-Setup";

String jsonEscape(const String &input) {
  String out;
  out.reserve(input.length() + 8);

  for (size_t i = 0; i < input.length(); ++i) {
    const char c = input[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
        break;
    }
  }
  return out;
}

String stateName() {
  switch (wifiManager.state()) {
    case WiFiManager::State::Scanning: return "scanning";
    case WiFiManager::State::Connecting: return "connecting";
    case WiFiManager::State::Connected: return "connected";
    case WiFiManager::State::Provisioning: return "provisioning";
    default: return "idle";
  }
}

void sendJson(int code, const String &body) {
  server.send(code, "application/json", body);
}

void handleStatus() {
  String json = "{";
  json += "\"device\":\"ESP32\",";
  json += "\"mode\":\"" + stateName() + "\",";
  json += "\"provisioning\":" + String(wifiManager.provisioning() ? "true" : "false") + ",";
  json += "\"ap\":{";
  json += "\"ssid\":\"" + jsonEscape(AP_SSID) + "\",";
  json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\"},";
  json += "\"wifi\":{";
  json += "\"connected\":" + String(wifiManager.connected() ? "true" : "false") + ",";
  json += "\"ssid\":\"" + jsonEscape(wifiManager.connectedSsid()) + "\",";
  json += "\"rssi\":" + String(wifiManager.connectedRssi()) + ",";
  json += "\"ip\":\"" + wifiManager.localIP().toString() + "\",";
  json += "\"channel\":" + String(wifiManager.connectedChannel());
  json += "},";
  json += "\"savedNetworks\":" + String(wifiManager.savedCount()) + ",";
  json += "\"scanCount\":" + String(wifiManager.scanCount()) + ",";
  json += "\"lastError\":\"" + jsonEscape(wifiManager.lastError()) + "\",";
  json += "\"uptime\":" + String(millis());
  json += "}";

  sendJson(200, json);
}

void handleScan() {
  if (wifiManager.scanInProgress()) {
    sendJson(202, "{\"scanning\":true,\"networks\":[]}");
    return;
  }

  if (!wifiManager.scanReady()) {
    wifiManager.startScan();
    sendJson(202, "{\"scanning\":true,\"networks\":[]}");
    return;
  }

  String json = "{\"scanning\":false,\"networks\":[";
  const int total = wifiManager.scanCount();

  for (int i = 0; i < total; ++i) {
    if (i) json += ",";
    json += "{";
    json += "\"ssid\":\"" + jsonEscape(wifiManager.scanSsid(i)) + "\",";
    json += "\"rssi\":" + String(wifiManager.scanRssi(i)) + ",";
    json += "\"channel\":" + String(wifiManager.scanChannel(i)) + ",";
    json += "\"security\":\"" + jsonEscape(wifiManager.scanSecurity(i)) + "\"";
    json += "}";
  }

  json += "]}";
  sendJson(200, json);
}

void handleNetworks() {
  String json = "{\"networks\":[";
  for (uint8_t i = 0; i < wifiManager.savedCount(); ++i) {
    if (i) json += ",";
    auto network = wifiManager.getSavedNetwork(i);
    json += "{\"index\":" + String(i) +
            ",\"ssid\":\"" + jsonEscape(network.ssid) + "\"}";
  }
  json += "]}";
  sendJson(200, json);
}

void handleSave() {
  if (!server.hasArg("ssid") || !server.hasArg("password")) {
    sendJson(400, "{\"error\":\"ssid and password are required\"}");
    return;
  }

  const String ssid = server.arg("ssid");
  const String password = server.arg("password");

  if (!wifiManager.addOrUpdateNetwork(ssid, password)) {
    sendJson(400, "{\"error\":\"invalid credentials or storage limit reached\"}");
    return;
  }

  sendJson(200, "{\"ok\":true}");
}

void handleDelete() {
  if (!server.hasArg("index")) {
    sendJson(400, "{\"error\":\"index is required\"}");
    return;
  }

  int index = server.arg("index").toInt();
  if (index < 0 || index >= wifiManager.savedCount() ||
      !wifiManager.deleteNetwork(static_cast<uint8_t>(index))) {
    sendJson(400, "{\"error\":\"invalid network index\"}");
    return;
  }

  sendJson(200, "{\"ok\":true}");
}

void handleReset() {
  wifiManager.resetCredentials();
  sendJson(200, "{\"ok\":true}");
}

String contentTypeFor(const String &path) {
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".woff2")) return "font/woff2";
  return "text/plain";
}

bool serveFile(String path) {
  if (path.endsWith("/")) path += "index.html";
  if (!LittleFS.exists(path)) return false;

  File file = LittleFS.open(path, "r");
  if (!file) return false;

  if (path.startsWith("/assets/")) {
    server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  } else {
    server.sendHeader("Cache-Control", "no-cache");
  }
  server.streamFile(file, contentTypeFor(path));
  file.close();
  return true;
}

void redirectToPortal() {
  const IPAddress ip = wifiManager.provisioning() ? WiFi.softAPIP() : WiFi.localIP();
  server.sendHeader("Location", "http://" + ip.toString() + "/", true);
  server.sendHeader("Cache-Control", "no-store");
  server.send(302, "text/plain", "");
}

bool isForeignHost() {
  if (!wifiManager.provisioning()) return false;
  const String host = server.hostHeader();
  if (host.length() == 0) return false;
  if (host == WiFi.softAPIP().toString()) return false;
  if (wifiManager.connected() && host == WiFi.localIP().toString()) return false;
  return true;
}

void handleRoot() {
  if (isForeignHost()) {
    redirectToPortal();
    return;
  }
  if (!serveFile("/index.html")) {
    server.send(500, "text/plain", "UI not installed. Run: pio run -t uploadfs");
  }
}

void handleNotFound() {
  if (isForeignHost()) {
    redirectToPortal();
    return;
  }

  const String uri = server.uri();

  if (uri.startsWith("/api/")) {
    sendJson(404, "{\"error\":\"not found\"}");
    return;
  }

  if (serveFile(uri)) return;

  if (uri.startsWith("/assets/")) {
    server.send(404, "text/plain", "Not found");
    return;
  }

  handleRoot();
}

void setupWeb() {
  const char *probes[] = {
    "/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
    "/connecttest.txt", "/ncsi.txt", "/redirect", "/fwlink", "/canonical.html", "/success.txt"
  };
  for (const char *probe : probes) {
    server.on(probe, HTTP_GET, redirectToPortal);
  }

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/networks", HTTP_GET, handleNetworks);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/delete", HTTP_POST, handleDelete);
  server.on("/reset", HTTP_POST, handleReset);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204, "text/plain", ""); });
  server.onNotFound(handleNotFound);

  server.begin();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed.");
  }

  wifiManager.begin(AP_SSID);

  setupWeb();

  Serial.println("ESP32 Wi-Fi provisioning service started.");
}

void loop() {
  static bool dnsRunning = false;

  wifiManager.loop();

  const bool provisioning = wifiManager.provisioning();
  if (provisioning && !dnsRunning) {
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    dnsRunning = true;
  } else if (!provisioning && dnsRunning) {
    dnsServer.stop();
    dnsRunning = false;
  }

  if (dnsRunning) {
    dnsServer.processNextRequest();
  }

  server.handleClient();
}
