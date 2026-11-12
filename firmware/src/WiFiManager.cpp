#include "WiFiManager.h"

#include <esp_wifi.h>

static volatile uint8_t lastDisconnectReason = 0;
static volatile uint8_t disconnectEvents = 0;
static volatile unsigned long lastDisconnectAt = 0;
static volatile bool staAssociated = false;

static void onStaConnected(arduino_event_id_t, arduino_event_info_t) {
  staAssociated = true;
}

static void onStaDisconnected(arduino_event_id_t, arduino_event_info_t info) {
  const uint8_t reason = info.wifi_sta_disconnected.reason;
  staAssociated = false;
  if (reason != WIFI_REASON_ASSOC_LEAVE) {
    lastDisconnectReason = reason;
    disconnectEvents++;
    lastDisconnectAt = millis();
  }
}

bool WiFiManager::connected() const {
  if (WiFi.status() == WL_CONNECTED) return true;
  wifi_ap_record_t info;
  return esp_wifi_sta_get_ap_info(&info) == ESP_OK && static_cast<uint32_t>(WiFi.localIP()) != 0;
}

void WiFiManager::begin(const char* apSsid) {
  _apSsid = apSsid;

  WiFi.onEvent(onStaDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(onStaConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);

  _prefs.begin("wifi", false);
  loadCredentials();

  Serial.printf("Loaded %u saved network(s).\n", _count);

  if (_count == 0) {
    enterProvisioning();
  } else {
    startConnectionCycle();
  }
}

void WiFiManager::loop() {
  const unsigned long now = millis();

  if (_scanning) {
    const int result = WiFi.scanComplete();
    if (result >= 0) {
      collectScanResults(result);
    } else if (result == WIFI_SCAN_FAILED && now - _scanStartedAt > SCAN_TIMEOUT_MS) {
      _scanning = false;
      Serial.println("Wi-Fi scan failed.");
      if (_cycleWaitingForScan) {
        _cycleWaitingForScan = false;
        sortSavedBySignal();
        beginNextConnectionAttempt();
      }
    }
  }

  if (_disconnectAt && now >= _disconnectAt) {
    _disconnectAt = 0;
    WiFi.disconnect(false, false);
  }

  if (_attempting) {
    const unsigned long elapsed = now - _connectStarted;
    const bool gaveUp = disconnectEvents >= 2 ||
                        (disconnectEvents >= 1 && now - lastDisconnectAt > 8000);

    if (staAssociated && !_reportedAssociated) {
      _reportedAssociated = true;
      Serial.println("Associated with the router, waiting for an IP address...");
    }

    if (connected()) {
      finishConnectionCycle(true);
    } else if (elapsed > CONNECT_TIMEOUT_MS || gaveUp) {
      const String ssid = _saved[_order[_candidateIndex]].ssid;
      _lastError = describeFailure(ssid);
      Serial.printf("Could not join \"%s\": %s (reason %u)\n",
                    ssid.c_str(), _lastError.c_str(), lastDisconnectReason);
      WiFi.disconnect(false, false);

      const bool wrongPassword = _lastError.indexOf("wrong password") >= 0 ||
                                 _lastError.indexOf("Enterprise") >= 0;
      if (!_retried && !wrongPassword) {
        startAttempt(true);
      } else {
        beginNextConnectionAttempt();
      }
    }
    return;
  }

  const bool isConnected = connected();

  if (!isConnected && _wasConnected && now >= _settleUntil) {
    _wasConnected = false;
    Serial.println("Wi-Fi connection lost.");
    if (!_cycleActive) {
      startConnectionCycle();
      return;
    }
  }

  if (_provisioning && isConnected && _count > 0 && now - _connectedAt > AP_GRACE_MS) {
    stopProvisioning();
  }

  if (_count == 0 && !_provisioning && !_cycleActive) {
    enterProvisioning();
  }

  if (_connectRequested && !_cycleActive && now - _connectRequestedAt > SAVE_CONNECT_DELAY_MS) {
    _connectRequested = false;
    if (!isConnected) {
      startConnectionCycle();
      return;
    }
  }

  if (!isConnected && !_cycleActive && _provisioning && _count > 0 &&
      now - _lastReconnect > RETRY_INTERVAL_MS && WiFi.softAPgetStationNum() == 0) {
    startConnectionCycle();
  }
}

bool WiFiManager::addOrUpdateNetwork(const String& ssid, const String& password) {
  if (ssid.length() == 0 || ssid.length() > 32) return false;

  const size_t len = password.length();
  bool validPassword = len == 0 || (len >= 8 && len <= 63);
  if (len == 64) {
    validPassword = true;
    for (size_t i = 0; i < len; ++i) {
      if (!isxdigit(static_cast<unsigned char>(password[i]))) {
        validPassword = false;
        break;
      }
    }
  }
  if (!validPassword) return false;

  const int existing = findSaved(ssid);
  if (existing >= 0) {
    _saved[existing].password = password;
  } else {
    if (_count >= MAX_NETWORKS) return false;
    _saved[_count].ssid = ssid;
    _saved[_count].password = password;
    _count++;
  }

  persistCredentials();
  Serial.printf("Saved network \"%s\".\n", ssid.c_str());

  _connectRequested = true;
  _connectRequestedAt = millis();
  return true;
}

bool WiFiManager::deleteNetwork(uint8_t index) {
  if (index >= _count) return false;

  const String removed = _saved[index].ssid;

  for (uint8_t i = index; i + 1 < _count; ++i) {
    _saved[i] = _saved[i + 1];
  }
  _count--;
  _saved[_count] = SavedNetwork();

  persistCredentials();
  Serial.printf("Deleted network \"%s\".\n", removed.c_str());

  if (connected() && WiFi.SSID() == removed) {
    _disconnectAt = millis() + SAVE_CONNECT_DELAY_MS;
  }
  return true;
}

void WiFiManager::resetCredentials() {
  _prefs.clear();
  for (uint8_t i = 0; i < MAX_NETWORKS; ++i) {
    _saved[i] = SavedNetwork();
  }
  _count = 0;
  _orderCount = 0;
  _connectRequested = false;
  _lastError = "";

  if (_attempting || _cycleActive) {
    _attempting = false;
    _cycleActive = false;
    _cycleWaitingForScan = false;
    _candidateIndex = -1;
    WiFi.disconnect(false, false);
  }

  if (connected()) {
    _disconnectAt = millis() + SAVE_CONNECT_DELAY_MS;
  }

  enterProvisioning();
  Serial.println("All saved networks erased.");
}

bool WiFiManager::startScan() {
  if (_scanning) return true;
  if (_attempting) return false;

  WiFi.scanDelete();
  _scanStartedAt = millis();
  const int result = WiFi.scanNetworks(true, true, false, 300);

  if (result == WIFI_SCAN_RUNNING) {
    _scanning = true;
    return true;
  }
  if (result >= 0) {
    _scanning = true;
    collectScanResults(result);
    return true;
  }
  return false;
}

bool WiFiManager::scanInProgress() const {
  return _scanning;
}

bool WiFiManager::scanReady() const {
  return _hasScan && !_scanning && millis() - _scanFinished < SCAN_FRESH_MS;
}

int WiFiManager::scanCount() const {
  return _scanCount;
}

String WiFiManager::scanSsid(int index) const {
  if (index < 0 || index >= _scanCount) return "";
  return _scanResults[index].ssid;
}

int WiFiManager::scanRssi(int index) const {
  if (index < 0 || index >= _scanCount) return -127;
  return _scanResults[index].rssi;
}

int WiFiManager::scanChannel(int index) const {
  if (index < 0 || index >= _scanCount) return 0;
  return _scanResults[index].channel;
}

String WiFiManager::scanSecurity(int index) const {
  if (index < 0 || index >= _scanCount) return "";
  return authName(_scanResults[index].auth);
}

void WiFiManager::clearScan() {
  if (!_scanning) WiFi.scanDelete();
  _scanCount = 0;
  _hasScan = false;
}

WiFiManager::State WiFiManager::state() const {
  if (_cycleWaitingForScan) return State::Scanning;
  if (_attempting) return State::Connecting;
  if (connected()) return State::Connected;
  if (_provisioning) return State::Provisioning;
  if (_scanning) return State::Scanning;
  return State::Idle;
}

uint8_t WiFiManager::savedCount() const {
  return _count;
}

WiFiManager::SavedNetwork WiFiManager::getSavedNetwork(uint8_t index) const {
  if (index >= _count) return SavedNetwork();
  return _saved[index];
}

String WiFiManager::connectedSsid() const {
  return connected() ? WiFi.SSID() : String();
}

int WiFiManager::connectedRssi() const {
  return connected() ? WiFi.RSSI() : 0;
}

int WiFiManager::connectedChannel() const {
  return connected() ? WiFi.channel() : 0;
}

void WiFiManager::loadCredentials() {
  _count = 0;
  const uint8_t stored = _prefs.getUChar("count", 0);

  for (uint8_t i = 0; i < stored && i < MAX_NETWORKS; ++i) {
    const String ssid = _prefs.getString(("s" + String(i)).c_str(), "");
    if (ssid.length() == 0) continue;
    _saved[_count].ssid = ssid;
    _saved[_count].password = _prefs.getString(("p" + String(i)).c_str(), "");
    _count++;
  }
}

void WiFiManager::persistCredentials() {
  _prefs.clear();
  _prefs.putUChar("count", _count);
  for (uint8_t i = 0; i < _count; ++i) {
    _prefs.putString(("s" + String(i)).c_str(), _saved[i].ssid);
    _prefs.putString(("p" + String(i)).c_str(), _saved[i].password);
  }
}

int WiFiManager::findSaved(const String& ssid) const {
  for (uint8_t i = 0; i < _count; ++i) {
    if (_saved[i].ssid == ssid) return i;
  }
  return -1;
}

int WiFiManager::findScan(const String& ssid) const {
  if (ssid.length() == 0) return -1;
  for (uint8_t i = 0; i < _scanCount; ++i) {
    if (_scanResults[i].ssid == ssid) return i;
  }
  return -1;
}

String WiFiManager::authName(wifi_auth_mode_t auth) const {
  switch (auth) {
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "Secured";
  }
}

void WiFiManager::sortSavedBySignal() {
  _orderCount = 0;
  bool seen[MAX_NETWORKS] = {false};

  for (uint8_t i = 0; i < _count; ++i) {
    if (findScan(_saved[i].ssid) >= 0) {
      _order[_orderCount++] = i;
      seen[i] = true;
    }
  }

  for (uint8_t i = 1; i < _orderCount; ++i) {
    const uint8_t current = _order[i];
    const int currentRssi = _scanResults[findScan(_saved[current].ssid)].rssi;
    int j = i - 1;
    while (j >= 0 && _scanResults[findScan(_saved[_order[j]].ssid)].rssi < currentRssi) {
      _order[j + 1] = _order[j];
      j--;
    }
    _order[j + 1] = current;
  }

  for (uint8_t i = 0; i < _count; ++i) {
    if (!seen[i]) _order[_orderCount++] = i;
  }

  _candidateIndex = -1;
}

void WiFiManager::startConnectionCycle() {
  _lastReconnect = millis();
  _attemptNumber = 0;

  if (_count == 0) {
    finishConnectionCycle(false);
    return;
  }

  _cycleActive = true;
  Serial.println("Scanning for the strongest saved network...");

  if (!connected()) WiFi.disconnect(false, false);

  if (startScan()) {
    if (_scanning) {
      _cycleWaitingForScan = true;
    }
    return;
  }

  sortSavedBySignal();
  beginNextConnectionAttempt();
}

void WiFiManager::beginNextConnectionAttempt() {
  _candidateIndex++;
  if (_candidateIndex >= _orderCount) {
    finishConnectionCycle(false);
    return;
  }
  startAttempt(false);
}

void WiFiManager::startAttempt(bool retry) {
  const SavedNetwork& network = _saved[_order[_candidateIndex]];
  const int scanIndex = findScan(network.ssid);
  _attemptNumber++;
  _retried = retry;
  _reportedAssociated = false;

  lastDisconnectReason = 0;
  disconnectEvents = 0;
  lastDisconnectAt = 0;
  staAssociated = false;

  const char* label = retry ? "Retry" : "Attempt";

  if (scanIndex >= 0 && _scanResults[scanIndex].channel > 0) {
    const ScanNetwork& target = _scanResults[scanIndex];

    if (_provisioning && WiFi.channel() != target.channel) {
      Serial.printf("Moving the setup hotspot to channel %d to match the router.\n", target.channel);
      WiFi.softAP(_apSsid, nullptr, target.channel);
      delay(100);
    }

    Serial.printf("%s %u: joining \"%s\" (%d dBm, ch %d)\n",
                  label, _attemptNumber, network.ssid.c_str(), target.rssi, target.channel);

    if (retry) {
      WiFi.begin(network.ssid.c_str(), network.password.c_str());
    } else {
      WiFi.begin(network.ssid.c_str(), network.password.c_str(), target.channel, target.bssid);
    }
  } else {
    Serial.printf("%s %u: joining \"%s\" (not seen in scan)\n",
                  label, _attemptNumber, network.ssid.c_str());
    WiFi.begin(network.ssid.c_str(), network.password.c_str());
  }

  _attempting = true;
  _connectStarted = millis();
}

void WiFiManager::finishConnectionCycle(bool success) {
  _cycleActive = false;
  _cycleWaitingForScan = false;
  _attempting = false;
  _candidateIndex = -1;
  _lastReconnect = millis();

  if (success) {
    _lastError = "";
    _wasConnected = true;
    _connectedAt = millis();
    Serial.printf("Connected to \"%s\", IP %s, %d dBm\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return;
  }

  Serial.println("No saved network could be joined. Retrying in 30 seconds.");
  WiFi.disconnect(false, false);
  enterProvisioning();
}

void WiFiManager::collectScanResults(int found) {
  _scanCount = 0;

  for (int i = 0; i < found; ++i) {
    const String ssid = WiFi.SSID(i);
    const int rssi = WiFi.RSSI(i);

    const int existing = findScan(ssid);
    ScanNetwork* slot = nullptr;

    if (existing >= 0) {
      if (rssi <= _scanResults[existing].rssi) continue;
      slot = &_scanResults[existing];
    } else {
      if (_scanCount >= MAX_SCAN_RESULTS) continue;
      slot = &_scanResults[_scanCount++];
    }

    slot->ssid = ssid;
    slot->rssi = rssi;
    slot->channel = WiFi.channel(i);
    slot->auth = WiFi.encryptionType(i);
    const uint8_t* bssid = WiFi.BSSID(i);
    if (bssid) {
      memcpy(slot->bssid, bssid, sizeof(slot->bssid));
    } else {
      memset(slot->bssid, 0, sizeof(slot->bssid));
    }
  }

  WiFi.scanDelete();
  _scanning = false;
  _hasScan = true;
  _scanFinished = millis();
  Serial.printf("Scan found %u network(s).\n", _scanCount);

  if (_cycleWaitingForScan) {
    _cycleWaitingForScan = false;
    sortSavedBySignal();
    beginNextConnectionAttempt();
  }
}

void WiFiManager::enterProvisioning() {
  _connectedAt = millis();
  if (_provisioning) return;

  const IPAddress apIp(4, 3, 2, 1);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(_apSsid);
  _provisioning = true;

  Serial.printf("Provisioning AP \"%s\" started at %s\n",
                _apSsid, WiFi.softAPIP().toString().c_str());
}

void WiFiManager::stopProvisioning() {
  if (!_provisioning) return;

  _settleUntil = millis() + 8000;
  WiFi.softAPdisconnect(true);
  _provisioning = false;

  Serial.println("Provisioning AP stopped.");
}

String WiFiManager::describeFailure(const String& ssid) const {
  const int scanIndex = findScan(ssid);
  String text;

  if (scanIndex >= 0 && _scanResults[scanIndex].auth == WIFI_AUTH_WPA2_ENTERPRISE) {
    text = "uses WPA2-Enterprise (username and password), which this device cannot join";
  } else {
    switch (lastDisconnectReason) {
      case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
      case WIFI_REASON_HANDSHAKE_TIMEOUT:
      case WIFI_REASON_AUTH_FAIL:
      case WIFI_REASON_MIC_FAILURE:
        text = "wrong password";
        break;
      case WIFI_REASON_NO_AP_FOUND:
        text = "network not found, move the ESP32 closer to the router";
        break;
      case WIFI_REASON_BEACON_TIMEOUT:
      case WIFI_REASON_AUTH_EXPIRE:
      case WIFI_REASON_ASSOC_EXPIRE:
        text = "signal too weak, move the ESP32 closer to the router";
        break;
      case WIFI_REASON_ASSOC_TOOMANY:
        text = "the router has too many devices connected";
        break;
      case 0:
        text = staAssociated
          ? "joined the router but it never gave an IP address (DHCP problem or a login page network)"
          : "no reply from the router in time";
        break;
      default:
        text = "the router refused the connection (reason " + String(lastDisconnectReason) + ")";
        break;
    }
  }

  if (scanIndex >= 0 && _scanResults[scanIndex].rssi < -80 &&
      text.indexOf("Enterprise") < 0 && text.indexOf("weak") < 0) {
    text += ", and the signal is very weak (" + String(_scanResults[scanIndex].rssi) + " dBm)";
  }

  return "\"" + ssid + "\": " + text;
}
