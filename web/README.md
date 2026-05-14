# Esp-WiFi-BLE · Console

Browser console for the Esp-WiFi-BLE library — talk to an ESP32 over Web Bluetooth
and over Wi-Fi (HTTP) at the same time. Built with React + Vite + Tailwind.

Live: https://hamzayslmn.github.io/Esp-Wifi-Ble-Now/

## Develop

```sh
pnpm install
pnpm dev
```

Open http://localhost:8000. Serving over `http://localhost` keeps it a secure
context, so Web Bluetooth works.

## Build

```sh
pnpm build      # outputs to dist/
pnpm preview
```

## Deploy

`.github/workflows/static.yml` builds this `web/` folder with pnpm and publishes
`dist/` to GitHub Pages. It runs on a manual dispatch from the Actions tab, or on
a push to `main` whose commit message contains `release`. Vite's `base` is set to
`/Esp-Wifi-Ble-Now/` to match the project Pages path.

## BLE modes

- **scan** — `requestLEScan()`, no pairing, receive-only. Needs
  `chrome://flags/#enable-experimental-web-platform-features`; not supported in
  Chrome on Windows.
- **connect** — GATT connect to one ESP32; it relays every broadcast it hears and
  accepts messages back. Works everywhere, including Windows.
