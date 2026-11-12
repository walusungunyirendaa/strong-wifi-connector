# Strong WiFi Connector

Wi-Fi provisioning for the ESP32. The board has no screen or keyboard, so when it has no network it opens its own hotspot, and you use your phone to pick a Wi-Fi network and type the password. It then connects by itself, always choosing the strongest saved network, and reconnects automatically after restarts or drop-outs.

The setup page is a React app that is stored on the ESP32 and served by the board itself, so no internet, app or computer is needed during setup.

## Features

- Captive portal: joining the ESP32-Setup hotspot opens the setup page automatically (or go to http://4.3.2.1)
- Live scan of nearby networks with signal strength, channel and security type
- Saves up to 8 networks in the ESP32's flash memory (NVS)
- Picks the strongest saved network that is in range and tries the others in order
- Explains failed connections in plain words (wrong password, signal too weak, network not found, WPA2-Enterprise not supported)
- Live dashboard: connection status, IP address, signal, channel and uptime
- "Connecting" and "Connected" notices, including the device's new address on the network
- Delete single networks or reset all saved credentials
- Falls back to the setup hotspot when no saved network works and keeps retrying every 30 seconds

## How it works

1. On boot the ESP32 loads saved networks from NVS.
2. If there are none, it starts the ESP32-Setup hotspot and a DNS server that sends every web request to the setup page.
3. From the page you scan, tap Use on a network, enter its password and tap Save network.
4. The ESP32 scans, sorts the saved networks by signal strength and joins the best one. The hotspot moves to the router's channel first so the connection is not interrupted.
5. After connecting, the hotspot stays on for 30 seconds so the page can show the new IP address, then turns off. The dashboard is then available at that IP on the same network.
6. If the connection drops, it scans again and rejoins the strongest saved network.

## Tech stack

| Part | Technology |
| --- | --- |
| Board | ESP32 DevKit (esp32dev) |
| Firmware | C++, Arduino framework, PlatformIO |
| Storage | Preferences (NVS) for credentials, LittleFS for the web page |
| Networking | WiFi (AP + STA), WebServer, DNSServer |
| Setup page | React 18, Vite 5 |

## Project structure

```
firmware/
  platformio.ini          PlatformIO configuration
  include/WiFiManager.h   Wi-Fi manager interface
  src/WiFiManager.cpp     Scanning, saving, connecting, hotspot logic
  src/main.cpp            Web server, API routes and captive portal
  data/                   Built setup page that gets uploaded to LittleFS
frontend/
  src/App.jsx             Setup page and dashboard
  src/api.js              Calls to the ESP32 API
  src/styles.css          Styles
build.sh                  Builds the page and the firmware in one go
```

## Getting started

### Requirements

- An ESP32 DevKit board and a USB data cable
- VS Code with the PlatformIO extension
- Node.js 18 or newer (only needed if you change the setup page)

### Flash the board

Open the firmware folder in VS Code, plug in the ESP32, then run from the firmware folder:

```bash
pio run -t upload
pio run -t uploadfs
pio device monitor
```

The first command uploads the firmware, the second uploads the setup page and the third opens the serial monitor. Hold the BOOT button if the upload is stuck at "Connecting...".

If pio is not recognised in a normal terminal, use the PlatformIO terminal in VS Code (PlatformIO icon → Miscellaneous → New Terminal), or call it by its full path:

Windows (PowerShell):

```powershell
& "$HOME\.platformio\penv\Scripts\pio.exe" run -t upload
```

Mac and Linux:

```bash
~/.platformio/penv/bin/pio run -t upload
```

To use plain pio everywhere on Mac or Linux, add it to your PATH once:

```bash
echo 'export PATH="$PATH:$HOME/.platformio/penv/bin"' >> ~/.bashrc
source ~/.bashrc
```

On Mac the default shell is zsh, so use ~/.zshrc instead of ~/.bashrc in both lines.

On Linux, if the upload fails with a permission error on the USB port (for example /dev/ttyUSB0), add your user to the dialout group, then log out and back in:

```bash
sudo usermod -a -G dialout $USER
```

### Set up Wi-Fi

1. Join the ESP32-Setup network on your phone. The setup page opens by itself, or go to http://4.3.2.1.
2. Tap Scan Wi-Fi, tap Use next to your network, enter the password and tap Save network.
3. When the green "Connected" notice appears, switch your phone to the same network and open the address it shows.

### Change the setup page

```bash
cd frontend
npm install
npm run build
```

Copy everything inside frontend/dist into firmware/data, then run pio run -t uploadfs again. On Mac or Linux, ./build.sh does all of this, and ./build.sh upload also flashes the board.

## API

| Method | Route | Purpose |
| --- | --- | --- |
| GET | /api/status | Mode, hotspot, connection details, uptime and last error |
| GET | /api/scan | Starts a scan (202 while running) and returns nearby networks |
| GET | /api/networks | Saved network names (passwords are never returned) |
| POST | /save | Save a network (form fields ssid and password) |
| POST | /delete | Delete a saved network (form field index) |
| POST | /reset | Erase all saved networks and start the setup hotspot |

## Troubleshooting

- Network missing from the scan: the ESP32 only supports 2.4 GHz Wi-Fi.
- "Signal too weak": move the board closer to the router. Below about -80 dBm connections usually fail.
- "WPA2-Enterprise": networks that need a username and password are not supported.
- Dashboard will not load on the network: some school and office networks block devices from talking to each other. Use a home router or a phone hotspot.
- "UI not installed": run pio run -t uploadfs.
- Start over: tap Reset credentials on the dashboard, or run pio run -t erase and flash again.
- Linux "Permission denied" on /dev/ttyUSB0 or /dev/ttyACM0: add your user to the dialout group (see Flash the board).

## Author

Walusungu Nyirenda - [GitHub](https://github.com/walusungunyirendaa)
