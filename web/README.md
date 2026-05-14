# Esp-WiFi-BLE · Console

Browser console for the Esp-WiFi-BLE library — talk to an ESP32 over Web Bluetooth
and over Wi-Fi (HTTP) at the same time. Built with React + Vite.

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
