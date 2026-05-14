import { useCallback, useEffect, useRef, useState } from 'react'

// ESP32 wire format — manufacturer data: company 0xFFFF, then 'E' 'W' seq payload.
// (see src/EspWB_BLE.cpp)
const MFR_COMPANY = 0xffff
const MAGIC_E = 0x45
const MAGIC_W = 0x57
const FLAG_URL = 'chrome://flags/#enable-experimental-web-platform-features'

const hasBluetooth = typeof navigator !== 'undefined' && !!navigator.bluetooth

// MARK: log
function useLog() {
  const [lines, setLines] = useState([])
  const idRef = useRef(0)
  const append = useCallback((kind, text, src) => {
    setLines((prev) => {
      const line = { id: idRef.current++, kind, text, src, time: new Date().toLocaleTimeString() }
      const next = [...prev, line]
      return next.length > 200 ? next.slice(-200) : next
    })
  }, [])
  return { lines, append, clear: useCallback(() => setLines([]), []) }
}

// MARK: BLE passive scan — requestLEScan(), no pairing, no connection. Reads the
// ESP32's manufacturer-data broadcasts and keeps the 'EW' ones. Needs the flag
// chrome://flags/#enable-experimental-web-platform-features; not supported on
// Chrome for Windows (its BLE backend can't initiate a scan).
function useScan(append) {
  const [scanning, setScanning] = useState(false)
  const scanRef = useRef(null)
  const seqRef = useRef(new Map()) // device.id -> last seq, drops repeated adverts
  const supported = hasBluetooth && !!navigator.bluetooth.requestLEScan

  const onAdv = useCallback(
    (e) => {
      const d = e.manufacturerData.get(MFR_COMPANY)
      if (!d || d.byteLength < 3) return
      if (d.getUint8(0) !== MAGIC_E || d.getUint8(1) !== MAGIC_W) return
      const seq = d.getUint8(2)
      if (seqRef.current.get(e.device.id) === seq) return // controller repeats adv ~10×/s
      seqRef.current.set(e.device.id, seq)
      const payload = new TextDecoder().decode(new DataView(d.buffer, d.byteOffset + 3))
      append('rx', payload, `${e.device.name || e.device.id} ${e.rssi}dBm`)
    },
    [append],
  )

  const stop = useCallback(() => {
    if (!scanRef.current) return
    scanRef.current.stop()
    scanRef.current = null
    navigator.bluetooth.removeEventListener('advertisementreceived', onAdv)
    setScanning(false)
    append('sys', 'scan stopped')
  }, [append, onAdv])

  const start = useCallback(async () => {
    if (scanRef.current) return
    if (!supported) {
      append('err', `BLE scan unavailable — enable ${FLAG_URL}, then restart the browser`)
      return
    }
    try {
      scanRef.current = await navigator.bluetooth.requestLEScan({
        acceptAllAdvertisements: true,
        keepRepeatedDevices: true, // ESP32 re-broadcasts a fresh seq every second
      })
      navigator.bluetooth.addEventListener('advertisementreceived', onAdv)
      setScanning(true)
      append('sys', 'scanning ESP32 broadcasts — no pairing, no connection')
    } catch (e) {
      append('err', 'scan failed: ' + e.message)
    }
  }, [append, onAdv, supported])

  useEffect(() => () => scanRef.current?.stop(), [])

  return { scanning, supported, start, stop }
}

// MARK: Wi-Fi — simple GET, no preflight.
function useWifi(append) {
  const [busy, setBusy] = useState(false)
  const send = useCallback(
    async (host, text) => {
      if (!host || !text) return
      setBusy(true)
      try {
        const res = await fetch(`http://${host}/message?msg=${encodeURIComponent(text)}`)
        if (res.ok) {
          localStorage.setItem('espwb-ip', host)
          append('tx', text, host)
        } else {
          append('err', `${host} replied HTTP ${res.status}`)
        }
      } catch {
        append('err', `failed to reach ${host} — same network? accepted PNA prompt?`)
      } finally {
        setBusy(false)
      }
    },
    [append],
  )
  return { busy, send }
}

const KIND_COLOR = { rx: 'text-ob-rx', tx: 'text-ob-tx', sys: 'text-ob-sys', err: 'text-ob-err' }
const KIND_TAG = { rx: '<<', tx: '>>', sys: '::', err: '!!' }
const BTN =
  'rounded border border-ob-accent/40 px-2.5 py-1 text-xs text-ob-accent transition-colors hover:bg-ob-accent/10 disabled:opacity-30'

export default function App() {
  const { lines, append, clear } = useLog()
  const scan = useScan(append)
  const wifi = useWifi(append)

  const [ip, setIp] = useState(() => localStorage.getItem('espwb-ip') || '')
  const [wifiMsg, setWifiMsg] = useState('')

  const bodyRef = useRef(null)
  useEffect(() => {
    if (bodyRef.current) bodyRef.current.scrollTop = bodyRef.current.scrollHeight
  }, [lines])
  useEffect(() => {
    if (!hasBluetooth) append('err', 'no Web Bluetooth — use Chrome/Edge on desktop or Android')
  }, [append])

  const submitWifi = (e) => {
    e.preventDefault()
    const t = wifiMsg.trim()
    if (!ip.trim() || !t) return
    wifi.send(ip.trim(), t)
    setWifiMsg('')
  }

  return (
    <div className="flex h-dvh w-screen flex-col bg-ob-panel">
      {/* title bar */}
      <div className="flex items-center gap-2 border-b border-ob-edge bg-ob-bg/60 px-3 py-2.5 sm:px-4">
        <span className="hidden h-3 w-3 rounded-full bg-[#e06c75] sm:block" />
        <span className="hidden h-3 w-3 rounded-full bg-[#e5c07b] sm:block" />
        <span className="hidden h-3 w-3 rounded-full bg-[#98c379] sm:block" />
        <span className="text-xs text-ob-muted sm:ml-2">esp-wifi-ble — console</span>
        <button
          onClick={clear}
          disabled={!lines.length}
          className="ml-auto text-xs text-ob-muted transition-colors hover:text-ob-text disabled:opacity-30"
        >
          clear
        </button>
      </div>

      {/* status bar */}
      <div className="flex flex-wrap items-center gap-x-4 gap-y-1 border-b border-ob-edge px-3 py-1.5 text-xs sm:px-4">
        <Stat label="ble" on={scan.scanning} detail={scan.scanning ? 'scanning' : 'idle'} />
        <Stat label="wifi" on={!!ip.trim()} detail={ip.trim() || 'no host'} />
      </div>

      {/* flag notice — shown when the browser can't scan broadcasts */}
      {!scan.supported && <FlagBanner />}

      {/* output */}
      <div
        ref={bodyRef}
        className="flex-1 overflow-y-auto px-3 py-3 text-[13px] leading-relaxed sm:px-4"
      >
        <p className="text-ob-muted">
          esp-wifi-ble console · scans ESP32 BLE broadcasts, sends over Wi-Fi.
        </p>
        {lines.map((l) => (
          <div key={l.id} className="flex gap-2 whitespace-pre-wrap break-words">
            <span className="shrink-0 text-ob-muted/70">{l.time}</span>
            <span className={`shrink-0 ${KIND_COLOR[l.kind]}`}>{KIND_TAG[l.kind]}</span>
            {l.src && <span className="shrink-0 text-ob-accent">{l.src}</span>}
            <span className={KIND_COLOR[l.kind]}>{l.text}</span>
          </div>
        ))}
      </div>

      {/* controls */}
      <div className="border-t border-ob-edge">
        <div className="flex flex-wrap items-center gap-2 px-3 py-2 sm:px-4">
          <Prompt sym="ble" />
          <span className="min-w-[6rem] flex-1 text-[13px] text-ob-muted/60">
            passive scan — no pairing, no connection
          </span>
          <button
            onClick={scan.scanning ? scan.stop : scan.start}
            disabled={!hasBluetooth}
            className={BTN}
          >
            {scan.scanning ? 'stop' : 'scan ble'}
          </button>
        </div>
        <form
          onSubmit={submitWifi}
          className="flex flex-wrap items-center gap-2 border-t border-ob-edge/60 px-3 py-2 sm:px-4"
        >
          <Prompt sym="wifi" />
          <input
            value={ip}
            onChange={(e) => setIp(e.target.value)}
            autoComplete="off"
            placeholder="esp32 ip"
            className="w-full bg-transparent text-[13px] text-ob-accent placeholder:text-ob-muted/60 focus:outline-none sm:w-44"
          />
          <input
            value={wifiMsg}
            onChange={(e) => setWifiMsg(e.target.value)}
            disabled={wifi.busy}
            maxLength={240}
            autoComplete="off"
            placeholder="message over Wi-Fi…"
            className="min-w-[7rem] flex-1 bg-transparent text-[13px] text-ob-text placeholder:text-ob-muted/60 focus:outline-none"
          />
          <button type="submit" disabled={wifi.busy} className={BTN}>
            {wifi.busy ? 'sending…' : 'send'}
          </button>
        </form>
      </div>
    </div>
  )
}

// chrome:// links aren't navigable from a page, so the URL is copy-to-clipboard.
function FlagBanner() {
  const [copied, setCopied] = useState(false)
  const copy = () => {
    navigator.clipboard?.writeText(FLAG_URL).then(() => {
      setCopied(true)
      setTimeout(() => setCopied(false), 1500)
    })
  }
  return (
    <div className="flex flex-wrap items-center gap-x-2 gap-y-1 border-b border-ob-edge bg-ob-accent/10 px-3 py-2 text-xs sm:px-4">
      <span className="text-ob-accent">BLE scan needs a flag:</span>
      <code className="rounded bg-ob-bg/70 px-1.5 py-0.5 text-ob-text">{FLAG_URL}</code>
      <button onClick={copy} className={`${BTN} py-0.5`}>
        {copied ? 'copied' : 'copy'}
      </button>
      <span className="text-ob-muted">enable it, then restart the browser.</span>
    </div>
  )
}

function Stat({ label, on, detail }) {
  return (
    <span className="flex items-center gap-1.5">
      <span className={`h-2 w-2 rounded-full ${on ? 'bg-ob-ok' : 'bg-ob-muted/50'}`} />
      <span className="text-ob-muted">{label}</span>
      <span className={on ? 'text-ob-text' : 'text-ob-muted/60'}>{detail}</span>
    </span>
  )
}

function Prompt({ sym }) {
  return (
    <span className="shrink-0 select-none text-ob-accent">
      {sym}
      <span className="text-ob-muted"> ▸</span>
    </span>
  )
}
