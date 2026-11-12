import { useEffect, useMemo, useRef, useState } from "react";
import {
  deleteNetwork,
  getSavedNetworks,
  getStatus,
  resetCredentials,
  saveNetwork,
  scanNetworks,
} from "./api";

function signalLevel(rssi) {
  if (rssi >= -50) return "Excellent";
  if (rssi >= -60) return "Good";
  if (rssi >= -70) return "Fair";
  return "Weak";
}

function SignalBars({ rssi }) {
  const level = rssi >= -50 ? 4 : rssi >= -60 ? 3 : rssi >= -70 ? 2 : 1;

  return (
    <span className="signal" aria-label={`${signalLevel(rssi)} signal`}>
      {[1, 2, 3, 4].map((bar) => (
        <span
          key={bar}
          className={`signal-bar ${bar <= level ? "active" : ""}`}
          style={{ height: `${7 + bar * 3}px` }}
        />
      ))}
    </span>
  );
}

function SecurityBadge({ security }) {
  const open = security === "Open";
  return (
    <span className={`badge ${open ? "badge-open" : "badge-secure"}`}>
      {open ? "Open" : security}
    </span>
  );
}

function formatUptime(ms) {
  const seconds = Math.floor((ms || 0) / 1000);
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const secs = seconds % 60;
  if (days) return `${days}d ${hours}h`;
  if (hours) return `${hours}h ${minutes}m`;
  if (minutes) return `${minutes}m ${secs}s`;
  return `${secs}s`;
}

export default function App() {
  const [ssid, setSsid] = useState("");
  const [password, setPassword] = useState("");
  const [networks, setNetworks] = useState([]);
  const [saved, setSaved] = useState([]);
  const [status, setStatus] = useState(null);
  const [scanning, setScanning] = useState(false);
  const [message, setMessage] = useState("");
  const [error, setError] = useState("");
  const [liveMode, setLiveMode] = useState(true);
  const [lastUpdated, setLastUpdated] = useState(null);
  const passwordRef = useRef(null);
  const wasConnected = useRef(null);
  const [notice, setNotice] = useState(null);

  async function refreshStatus() {
    if (!liveMode) return;
    try {
      const data = await getStatus();
      setStatus(data);
      setLastUpdated(new Date());
      setError((prev) => (prev.startsWith("Device status unavailable") ? "" : prev));
    } catch (err) {
      setError(`Device status unavailable: ${err.message}`);
    }
  }

  async function refreshSaved() {
    if (!liveMode) return;
    try {
      const data = await getSavedNetworks();
      setSaved(data.networks ?? []);
    } catch (err) {
      setError(err.message);
    }
  }

  useEffect(() => {
    refreshStatus();
    refreshSaved();

    const timer = setInterval(() => {
      refreshStatus();
      refreshSaved();
    }, 2500);

    return () => clearInterval(timer);
  }, [liveMode]);

  useEffect(() => {
    if (!status) return;
    const isConnected = Boolean(status.wifi?.connected);

    if (wasConnected.current === false && isConnected) {
      setNotice({ ssid: status.wifi.ssid, ip: status.wifi.ip, viaHotspot: Boolean(status.provisioning) });
      setMessage("");
      if (navigator.vibrate) navigator.vibrate([120, 60, 120]);
    }
    if (!isConnected) setNotice(null);

    wasConnected.current = isConnected;
  }, [status]);

  const sortedNetworks = useMemo(
    () => [...networks].sort((a, b) => b.rssi - a.rssi),
    [networks]
  );

  async function handleScan() {
    setScanning(true);
    setError("");
    setMessage("");

    try {
      let data = await scanNetworks();

      for (let i = 0; i < 30 && data.scanning; i += 1) {
        await new Promise((resolve) => setTimeout(resolve, 500));
        data = await scanNetworks();
      }

      if (data.scanning) {
        setMessage("The ESP32 is busy connecting. Try scanning again in a few seconds.");
        return;
      }

      const found = data.networks ?? [];
      setNetworks(found);
      setMessage(found.length
        ? `Found ${found.length} nearby networks. Tap Use next to the one you want.`
        : "No networks found. Move the ESP32 closer to the router and scan again.");
      await refreshStatus();
    } catch (err) {
      setError(err.message);
    } finally {
      setScanning(false);
    }
  }

  async function handleSave(event) {
    event.preventDefault();
    if (!ssid.trim()) return;

    setError("");
    setMessage("");

    try {
      await saveNetwork(ssid.trim(), password);
      setPassword("");
      setMessage(`${ssid.trim()} saved. Connecting now, you will see a notice here when it is online.`);
      await refreshSaved();
      await refreshStatus();
    } catch (err) {
      setError(err.message);
    }
  }

  async function handleDelete(index) {
    setError("");
    try {
      await deleteNetwork(index);
      setMessage("Saved network removed.");
      await refreshSaved();
      await refreshStatus();
    } catch (err) {
      setError(err.message);
    }
  }

  async function handleReset() {
    if (!window.confirm("Erase all saved Wi-Fi credentials?")) return;

    setError("");
    try {
      await resetCredentials();
      setMessage("Credentials erased. The ESP32 is now in provisioning mode.");
      setNetworks([]);
      await refreshSaved();
      await refreshStatus();
    } catch (err) {
      setError(err.message);
    }
  }

  function selectNetwork(network) {
    setSsid(network.ssid);
    setPassword("");
    setError("");
    setMessage(network.security === "Open"
      ? `${network.ssid || "This network"} is open. Leave the password empty and tap Save network.`
      : `Enter the password for ${network.ssid || "this network"}.`);
    const field = passwordRef.current;
    if (field) {
      field.scrollIntoView({ behavior: "smooth", block: "center" });
      field.focus({ preventScroll: true });
    }
  }

  const connected = Boolean(status?.wifi?.connected);
  const provisioning = Boolean(status?.provisioning);
  const connecting = !connected && (status?.mode === "connecting" || status?.mode === "scanning") &&
    (status?.savedNetworks ?? 0) > 0;

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand">
          <div className="brand-mark">W</div>
          <div>
            <strong>ESP32</strong>
            <span>Wi-Fi Setup</span>
          </div>
        </div>

        <nav>
          <a className="nav-item active" href="#overview">Overview</a>
          <a className="nav-item" href="#networks">Nearby Networks</a>
          <a className="nav-item" href="#saved">Saved Networks</a>
          <a className="nav-item" href="#reset">Reset</a>
        </nav>

        <div className="sidebar-footer">
          <span className={`dot ${provisioning || connected ? "online" : ""}`} />
          {provisioning ? "Provisioning AP online" : connected ? "Wi-Fi connected" : "Wi-Fi offline"}
          <small>{provisioning ? (status?.ap?.ip || "4.3.2.1") : (status?.wifi?.ip || "—")}</small>
        </div>
      </aside>

      <main className="main">
        <header className="topbar">
          <div>
            <div className="eyebrow">DEVICE PROVISIONING</div>
            <h1>Wi-Fi Setup</h1>
            <p>Live status and real Wi-Fi scans from the ESP32.</p>
          </div>

          <div className={`connection-pill ${connected ? "connected" : ""}`}>
            <span className="dot online" />
            {connected ? "Connected" : provisioning ? "Provisioning mode" : (status?.mode || "Connecting")}
          </div>
        </header>

        {notice && (
          <section className="notice notice-success" role="status">
            <strong>Connected to {notice.ssid}</strong>
            <p>
              The ESP32 is online at{" "}
              <a href={`http://${notice.ip}`}>http://{notice.ip}</a>.
            </p>
            {notice.viaHotspot && (
              <p>
                The ESP32-Setup hotspot turns off in about 30 seconds. Switch your phone to {notice.ssid},
                then open the address above to keep using this dashboard.
              </p>
            )}
            <button className="link-button" onClick={() => setNotice(null)}>Dismiss</button>
          </section>
        )}

        {connecting && (
          <section className="notice notice-progress" role="status">
            <span className="spinner" />
            <div>
              <strong>Connecting to your Wi-Fi…</strong>
              <p>This can take up to 30 seconds. Your phone may drop off ESP32-Setup for a moment and reconnect.</p>
            </div>
          </section>
        )}

        <section id="overview" className="stats-grid">
          <div className="stat-card">
            <span className="stat-label">Device</span>
            <strong>{status?.device || "ESP32"}</strong>
            <small>{liveMode ? "Live device status" : "Live API disabled"}</small>
          </div>

          <div className="stat-card">
            <span className="stat-label">Wi-Fi</span>
            <strong>{connected ? status.wifi.ssid : provisioning ? "Setup AP" : "Not connected"}</strong>
            <small>
              {connected
                ? `${status.wifi.ip} · ${status.wifi.rssi} dBm`
                : provisioning
                  ? status?.ap?.ip || "4.3.2.1"
                  : "Waiting for connection"}
            </small>
          </div>

          <div className="stat-card">
            <span className="stat-label">Saved networks</span>
            <strong>{status?.savedNetworks ?? saved.length}</strong>
            <small>Stored in NVS</small>
          </div>

          <div className="stat-card">
            <span className="stat-label">Uptime</span>
            <strong>{formatUptime(status?.uptime)}</strong>
            <small>{lastUpdated ? `Updated ${lastUpdated.toLocaleTimeString()}` : "Waiting for device"}</small>
          </div>
        </section>

        <section className="panel device-panel">
          <div>
            <div className="eyebrow">LIVE DEVICE</div>
            <h2>{connected ? `Connected to ${status.wifi.ssid}` : provisioning ? "Provisioning access point active" : "Waiting for Wi-Fi"}</h2>
            <p>
              {connected
                ? `IP ${status.wifi.ip}, channel ${status.wifi.channel}, RSSI ${status.wifi.rssi} dBm.`
                : provisioning
                  ? `Join ${status?.ap?.ssid || "ESP32-Setup"} and use ${status?.ap?.ip || "4.3.2.1"}.`
                  : `Current state: ${status?.mode || "unknown"}.`}
            </p>
            {!connected && status?.lastError && (
              <div className="message error">Last attempt failed: {status.lastError}</div>
            )}
          </div>
          <div className="status-orb">
            <span className={`dot ${connected || provisioning ? "online" : ""}`} />
            {connected ? "ONLINE" : provisioning ? "SETUP" : "OFFLINE"}
          </div>
        </section>

        <section className="panel provision-panel">
          <div className="panel-heading">
            <div>
              <div className="eyebrow">PROVISION</div>
              <h2>Add a Wi-Fi network</h2>
            </div>
            <span className="secure-note">Password is written to ESP32 NVS</span>
          </div>

          <form className="provision-form" onSubmit={handleSave}>
            <label>
              Network name
              <input
                value={ssid}
                onChange={(e) => setSsid(e.target.value)}
                placeholder="Tap Use on a scanned network, or type its name"
                maxLength={32}
                required
              />
            </label>

            <label>
              Password
              <input
                ref={passwordRef}
                type="password"
                value={password}
                onChange={(e) => setPassword(e.target.value)}
                placeholder="Enter Wi-Fi password"
                maxLength={63}
              />
            </label>

            <button className="primary-button" type="submit">
              Save network
            </button>
          </form>

          {message && <div className="message">{message}</div>}
          {error && <div className="message error">{error}</div>}
        </section>

        <section id="networks" className="panel">
          <div className="panel-heading">
            <div>
              <div className="eyebrow">SCAN</div>
              <h2>Nearby networks</h2>
            </div>
            <button className="secondary-button" onClick={handleScan} disabled={scanning}>
              {scanning ? "Scanning…" : "Scan Wi-Fi"}
            </button>
          </div>

          <div className="table-wrap">
            <table>
              <thead>
                <tr>
                  <th>Network</th>
                  <th>Signal</th>
                  <th>Channel</th>
                  <th>Security</th>
                  <th />
                </tr>
              </thead>
              <tbody>
                {sortedNetworks.length === 0 ? (
                  <tr><td colSpan="5" className="empty">No scan results yet. Click “Scan Wi-Fi”.</td></tr>
                ) : (
                  sortedNetworks.map((network, index) => (
                    <tr key={`${network.ssid}-${network.channel}-${index}`}>
                      <td><strong>{network.ssid || "(Hidden network)"}</strong></td>
                      <td>
                        <div className="signal-cell">
                          <SignalBars rssi={network.rssi} />
                          <span>{network.rssi} dBm</span>
                        </div>
                      </td>
                      <td>Ch {network.channel}</td>
                      <td><SecurityBadge security={network.security} /></td>
                      <td>
                        <button className="link-button" onClick={() => selectNetwork(network)}>
                          Use
                        </button>
                      </td>
                    </tr>
                  ))
                )}
              </tbody>
            </table>
          </div>
        </section>

        <section id="saved" className="panel">
          <div className="panel-heading">
            <div>
              <div className="eyebrow">NVS</div>
              <h2>Saved networks</h2>
            </div>
            <span className="secure-note">{saved.length} stored</span>
          </div>

          <div className="saved-list">
            {saved.length === 0 ? (
              <div className="empty-card">No saved networks.</div>
            ) : (
              saved.map((network, index) => (
                <div className="saved-row" key={`${network.ssid}-${index}`}>
                  <div>
                    <strong>{network.ssid}</strong>
                    <small>Credential {index + 1}</small>
                  </div>
                  <button className="danger-button" onClick={() => handleDelete(index)}>
                    Delete
                  </button>
                </div>
              ))
            )}
          </div>
        </section>

        <section id="reset" className="panel reset-panel">
          <div>
            <div className="eyebrow">FACTORY-LIKE RESET</div>
            <h2>Erase Wi-Fi credentials</h2>
            <p>Deletes all saved SSIDs and passwords from the ESP32 NVS and starts the provisioning AP.</p>
          </div>
          <button className="danger-button" onClick={handleReset}>Reset credentials</button>
        </section>

        <div className="footer-controls">
          <label className="toggle">
            <input
              type="checkbox"
              checked={liveMode}
              onChange={(e) => setLiveMode(e.target.checked)}
            />
            <span />
            Live ESP32 API
          </label>
          <span>Expected device address: {provisioning ? (status?.ap?.ip || "4.3.2.1") : "current LAN IP"}</span>
        </div>
      </main>
    </div>
  );
}
