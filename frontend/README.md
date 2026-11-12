# React UI

The UI talks directly to the ESP32 using relative HTTP paths.

## Development

    npm install
    npm run dev

For a laptop development server, configure the browser/network so it can reach the ESP32. The final production deployment does not need Node.js.

## Production

    npm run build

Copy the *contents* of `dist/` into:

    ../firmware/data/

Then from the firmware directory:

    pio run -t uploadfs

The ESP32 serves the React app from LittleFS.
