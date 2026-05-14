import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

// Serve over http://localhost so Web Bluetooth treats it as a secure context.
// `base` matches the GitHub Pages project path for this repo.
export default defineConfig({
  base: '/Esp-Wifi-Ble-Now/',
  plugins: [react(), tailwindcss()],
  server: { host: 'localhost', port: 8000 },
})
