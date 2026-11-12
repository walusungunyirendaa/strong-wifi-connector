#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>

class WiFiManager {
public:
  static constexpr uint8_t MAX_NETWORKS = 8;
  static constexpr uint8_t MAX_SCAN_RESULTS = 50;

  struct SavedNetwork {
    String ssid;
    String password;
  };

  enum class State : uint8_t {
    Idle,
    Scanning,
    Connecting,
    Connected,
    Provisioning
  };

  void begin(const char* apSsid);
  void loop();

  bool addOrUpdateNetwork(const String& ssid, const String& password);
  bool deleteNetwork(uint8_t index);
  void resetCredentials();

  bool startScan();
  bool scanInProgress() const;
  bool scanReady() const;
  int scanCount() const;
  String scanSsid(int index) const;
  int scanRssi(int index) const;
  int scanChannel(int index) const;
  String scanSecurity(int index) const;
  void clearScan();

  bool provisioning() const { return _provisioning; }
  bool connected() const;
  State state() const;
  uint8_t savedCount() const;
  SavedNetwork getSavedNetwork(uint8_t index) const;
  String connectedSsid() const;
  int connectedRssi() const;
  IPAddress localIP() const { return WiFi.localIP(); }
  int connectedChannel() const;
  String lastError() const { return _lastError; }

private:
  static constexpr unsigned long CONNECT_TIMEOUT_MS = 15000;
  static constexpr unsigned long RETRY_INTERVAL_MS = 30000;
  static constexpr unsigned long AP_GRACE_MS = 30000;
  static constexpr unsigned long SCAN_FRESH_MS = 15000;
  static constexpr unsigned long SCAN_TIMEOUT_MS = 20000;
  static constexpr unsigned long SAVE_CONNECT_DELAY_MS = 1500;

  struct ScanNetwork {
    String ssid;
    int rssi = -127;
    int channel = 0;
    wifi_auth_mode_t auth = WIFI_AUTH_OPEN;
    uint8_t bssid[6] = {0};
  };

  void loadCredentials();
  void persistCredentials();
  int findSaved(const String& ssid) const;
  int findScan(const String& ssid) const;
  String authName(wifi_auth_mode_t auth) const;
  void sortSavedBySignal();
  void startConnectionCycle();
  void beginNextConnectionAttempt();
  void finishConnectionCycle(bool success);
  void collectScanResults(int found);
  void enterProvisioning();
  void stopProvisioning();
  void startAttempt(bool retry);
  String describeFailure(const String& ssid) const;

  const char* _apSsid = "ESP32-Setup";
  String _lastError;
  Preferences _prefs;
  SavedNetwork _saved[MAX_NETWORKS];
  ScanNetwork _scanResults[MAX_SCAN_RESULTS];
  uint8_t _order[MAX_NETWORKS] = {0};
  uint8_t _orderCount = 0;
  uint8_t _count = 0;
  uint8_t _scanCount = 0;
  bool _provisioning = false;
  bool _scanning = false;
  bool _hasScan = false;
  bool _cycleActive = false;
  bool _cycleWaitingForScan = false;
  bool _attempting = false;
  bool _wasConnected = false;
  bool _connectRequested = false;
  uint8_t _attemptNumber = 0;
  int8_t _candidateIndex = -1;
  unsigned long _connectStarted = 0;
  unsigned long _lastReconnect = 0;
  unsigned long _scanFinished = 0;
  unsigned long _scanStartedAt = 0;
  unsigned long _connectedAt = 0;
  unsigned long _connectRequestedAt = 0;
  unsigned long _disconnectAt = 0;
  unsigned long _settleUntil = 0;
  bool _retried = false;
  bool _reportedAssociated = false;
};
