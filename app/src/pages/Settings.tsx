import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { supabase } from '../lib/supabase'
import { connection } from '../lib/connection'
import { ble } from '../lib/ble'

export default function Settings() {
  const navigate = useNavigate()
  const [user, setUser]       = useState<any>(null)
  const [devices, setDevices] = useState<any[]>([])
  const [connState, setConnState] = useState(connection.getState())

  // BLE form
  const [bleConnecting, setBleConnecting] = useState(false)
  const [bleStatus, setBleStatus]         = useState('')

  // WiFi form
  const [wifiIP, setWifiIP]       = useState('192.168.1.')
  const [wifiConnecting, setWifiConnecting] = useState(false)
  const [wifiStatus, setWifiStatus]         = useState('')

  // New device pair
  const [newDeviceName, setNewDeviceName] = useState('')
  const [newDeviceKey,  setNewDeviceKey]  = useState('')
  const [pairing, setPairing]             = useState(false)

  useEffect(() => {
    supabase.auth.getUser().then(({ data }) => setUser(data.user))
    supabase.from('devices').select('*').then(({ data }) => setDevices(data || []))

    const unsub = connection.onStateChange(setConnState)
    return unsub
  }, [])

  async function connectBLE() {
    if (!ble.isSupported) {
      setBleStatus('❌ Thiết bị/trình duyệt không hỗ trợ BLE')
      return
    }
    setBleConnecting(true)
    setBleStatus('Đang tìm SmartRemote...')
    try {
      const ok = await connection.connectBLE()
      setBleStatus(ok ? '✅ Kết nối BLE thành công!' : '❌ Kết nối thất bại')
    } catch (e: any) {
      setBleStatus(e.message === 'BLE_NOT_SUPPORTED'
        ? '❌ iOS Safari không hỗ trợ BLE. Dùng WiFi hoặc Cloud.'
        : '❌ ' + e.message)
    }
    setBleConnecting(false)
  }

  async function connectWifi() {
    if (!wifiIP.trim()) return
    setWifiConnecting(true)
    setWifiStatus('Đang kết nối...')
    const ok = await connection.connectWiFi(wifiIP.trim())
    setWifiStatus(ok ? '✅ Kết nối WiFi LAN thành công!' : '❌ Không thể kết nối đến ' + wifiIP)
    setWifiConnecting(false)
  }

  function useCloud() {
    connection.useCloud()
    setWifiStatus('')
    setBleStatus('')
  }

  async function pairDevice() {
    if (!newDeviceName.trim() || !newDeviceKey.trim()) return
    setPairing(true)
    try {
      const { data, error } = await supabase.from('devices').insert({
        user_id:    user?.id,
        name:       newDeviceName.trim(),
        device_key: newDeviceKey.trim().toUpperCase(),
      }).select().single()

      if (error) throw error
      setDevices(prev => [...prev, data])

      // Lưu device_id vào localStorage để dùng cho cloud mode
      localStorage.setItem('smartremote_device_id',  data.id)
      localStorage.setItem('smartremote_device_key', data.device_key)
      connection.setDevice(data.id, data.device_key)

      setNewDeviceName('')
      setNewDeviceKey('')
      alert('✅ Đã thêm thiết bị!')
    } catch (e: any) {
      alert('Lỗi: ' + e.message)
    }
    setPairing(false)
  }

  async function signOut() {
    await supabase.auth.signOut()
    navigate('/login')
  }

  const modeLabels: Record<string, string> = {
    ble:    '🔵 Bluetooth BLE',
    wifi:   '🟢 WiFi LAN',
    cloud:  '🟣 Cloud (Internet)',
    offline:'⚫ Offline',
  }

  return (
    <div className="animate-fade-in">
      <div className="page-header">
        <button onClick={() => navigate(-1)} className="back-btn">←</button>
        <h1>⚙️ Cài đặt</h1>
      </div>

      {/* Current Connection */}
      <div className="card-glass" style={{ marginBottom: 16 }}>
        <div className="flex items-center justify-between">
          <div>
            <div className="text-xs text-muted">Kết nối hiện tại</div>
            <div style={{ fontWeight: 700, marginTop: 2 }}>
              {modeLabels[connState.mode] || 'Chưa kết nối'}
            </div>
          </div>
          {connState.connected && (
            <button className="btn btn-ghost" style={{ fontSize: '0.8rem' }}
              onClick={() => connection.disconnect()}>
              Ngắt kết nối
            </button>
          )}
        </div>
      </div>

      {/* BLE Connection */}
      <div className="card" style={{ marginBottom: 12 }}>
        <h3 style={{ marginBottom: 4 }}>🔵 Bluetooth BLE</h3>
        <p className="text-sm text-muted" style={{ marginBottom: 12 }}>
          Kết nối gần, không cần WiFi. Android/Chrome Desktop.
        </p>
        <button className="btn btn-primary btn-full" onClick={connectBLE} disabled={bleConnecting}>
          {bleConnecting ? '⏳ Đang tìm...' : '🔍 Tìm SmartRemote'}
        </button>
        {bleStatus && <p className="text-sm" style={{ marginTop: 8 }}>{bleStatus}</p>}
      </div>

      {/* WiFi LAN Connection */}
      <div className="card" style={{ marginBottom: 12 }}>
        <h3 style={{ marginBottom: 4 }}>🟢 WiFi LAN</h3>
        <p className="text-sm text-muted" style={{ marginBottom: 12 }}>
          Kết nối qua mạng nội bộ. Xem IP trên Serial Monitor.
        </p>
        <div className="flex gap-2" style={{ marginBottom: 8 }}>
          <input
            className="input"
            placeholder="192.168.1.xxx"
            value={wifiIP}
            onChange={e => setWifiIP(e.target.value)}
            style={{ flex: 1 }}
          />
          <button className="btn btn-primary" onClick={connectWifi} disabled={wifiConnecting}>
            {wifiConnecting ? '⏳' : 'Kết nối'}
          </button>
        </div>
        {wifiStatus && <p className="text-sm" style={{ marginTop: 4 }}>{wifiStatus}</p>}
      </div>

      {/* Cloud Mode */}
      <div className="card" style={{ marginBottom: 12 }}>
        <h3 style={{ marginBottom: 4 }}>🟣 Internet / Cloud</h3>
        <p className="text-sm text-muted" style={{ marginBottom: 12 }}>
          Điều khiển từ bất kỳ đâu qua Supabase Realtime. ESP32 phải có WiFi.
        </p>
        <button className="btn btn-secondary btn-full" onClick={useCloud}>
          Dùng Cloud Mode
        </button>
      </div>

      {/* Paired Devices */}
      <div className="card" style={{ marginBottom: 12 }}>
        <h3 style={{ marginBottom: 12 }}>📱 Thiết bị đã ghép đôi</h3>

        {devices.length === 0 ? (
          <p className="text-sm text-muted">Chưa có thiết bị nào</p>
        ) : (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 8, marginBottom: 16 }}>
            {devices.map(d => (
              <div key={d.id} className="flex items-center gap-3" style={{
                padding: '10px 12px',
                background: 'var(--bg-elevated)',
                borderRadius: 'var(--radius-sm)',
              }}>
                <span>📡</span>
                <div style={{ flex: 1 }}>
                  <div style={{ fontWeight: 600, fontSize: '0.875rem' }}>{d.name}</div>
                  <div className="font-mono text-xs text-muted">{d.device_key}</div>
                </div>
                <div style={{
                  width: 8, height: 8,
                  borderRadius: '50%',
                  background: d.online ? 'var(--success)' : 'var(--text-muted)',
                }} />
              </div>
            ))}
          </div>
        )}

        {/* Add device */}
        <h3 style={{ marginBottom: 8, fontSize: '0.875rem' }}>Thêm thiết bị mới</h3>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          <input className="input" placeholder="Tên thiết bị (VD: Phòng khách)"
            value={newDeviceName} onChange={e => setNewDeviceName(e.target.value)} />
          <input className="input font-mono" placeholder="Device Key (từ Serial Monitor)"
            value={newDeviceKey} onChange={e => setNewDeviceKey(e.target.value.toUpperCase())} />
          <button className="btn btn-primary" onClick={pairDevice}
            disabled={pairing || !newDeviceName.trim() || !newDeviceKey.trim()}>
            {pairing ? '⏳ Đang ghép đôi...' : '➕ Thêm thiết bị'}
          </button>
        </div>
      </div>

      {/* Account */}
      {user && (
        <div className="card">
          <h3 style={{ marginBottom: 8 }}>👤 Tài khoản</h3>
          <p className="text-sm text-muted" style={{ marginBottom: 12 }}>{user.email}</p>
          <button className="btn btn-danger btn-full" onClick={signOut}>Đăng xuất</button>
        </div>
      )}
    </div>
  )
}
