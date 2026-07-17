import { useState, useEffect } from 'react'
import { Link } from 'react-router-dom'
import { supabase } from '../lib/supabase'
import type { Remote, Device } from '../lib/supabase'
import { syncFromSupabase, getAllCommands } from '../lib/ir-store'

export default function Home() {
  const [remotes, setRemotes]     = useState<Remote[]>([])
  const [device, setDevice]       = useState<Device | null>(null)
  const [loading, setLoading]     = useState(true)
  const [syncStatus, setSyncStatus] = useState('')

  useEffect(() => {
    loadData()
  }, [])

  async function loadData() {
    setLoading(true)
    try {
      // Load remotes
      const { data: remotesData } = await supabase
        .from('remotes').select('*').order('updated_at', { ascending: false })
      setRemotes(remotesData || [])

      // Load first device
      const { data: devicesData } = await supabase
        .from('devices').select('*').limit(1).single()
      setDevice(devicesData || null)

      // Sync IR commands to IndexedDB for offline BLE
      const { data: cmds } = await supabase.from('ir_commands').select('*')
      if (cmds) {
        await syncFromSupabase(cmds as any)
        const count = await getAllCommands()
        setSyncStatus(`${count.length} lệnh IR đã sync`)
      }
    } catch (e) {
      console.error('[Home] Load error:', e)
    }
    setLoading(false)
  }

  return (
    <div className="animate-fade-in">
      {/* Header */}
      <div className="page-header">
        <div>
          <h1>🎮 SmartRemote</h1>
          <p className="text-sm text-muted" style={{ marginTop: 4 }}>
            {syncStatus || 'Đang tải...'}
          </p>
        </div>
        <Link to="/settings" className="btn btn-icon btn-ghost">
          ⚙️
        </Link>
      </div>

      {/* Device Status Card */}
      {device && (
        <div className="card-glass mb-4" style={{ marginBottom: 16 }}>
          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              <span style={{ fontSize: '1.2rem' }}>📡</span>
              <div>
                <div style={{ fontWeight: 600, fontSize: '0.9rem' }}>{device.name}</div>
                <div className="text-xs text-muted">Flash: {device.flash_pct}%</div>
              </div>
            </div>
            <ConnectionBadge mode={device.mode} online={device.online} />
          </div>
          <div style={{
            marginTop: 10,
            background: 'var(--bg-base)',
            borderRadius: 8,
            height: 4,
            overflow: 'hidden',
          }}>
            <div style={{
              height: '100%',
              width: `${device.flash_pct}%`,
              background: device.flash_pct > 80
                ? 'var(--error)'
                : device.flash_pct > 60
                  ? 'var(--warning)'
                  : 'var(--accent)',
              transition: 'width 0.4s ease',
              borderRadius: 4,
            }} />
          </div>
        </div>
      )}

      {/* Quick Actions */}
      <div className="flex gap-2 mb-4" style={{ marginBottom: 16 }}>
        <Link to="/learn" className="btn btn-primary" style={{ flex: 1 }}>
          📡 Học lệnh mới
        </Link>
        <Link to="/library" className="btn btn-secondary" style={{ flex: 1 }}>
          📚 Thư viện IR
        </Link>
      </div>

      {/* Remotes List */}
      <div className="flex items-center justify-between mb-4" style={{ marginBottom: 12 }}>
        <h2>Remote của tôi</h2>
        <Link to="/editor/new" className="btn btn-ghost btn-icon" title="Tạo remote mới">
          ➕
        </Link>
      </div>

      {loading ? (
        <div className="flex justify-center" style={{ padding: 40 }}>
          <div className="loading-spinner" />
        </div>
      ) : remotes.length === 0 ? (
        <div className="empty-state">
          <div className="empty-icon">📺</div>
          <h3>Chưa có remote nào</h3>
          <p>Tạo remote đầu tiên hoặc import từ IRDB</p>
          <Link to="/editor/new" className="btn btn-primary mt-4" style={{ marginTop: 12 }}>
            ➕ Tạo remote
          </Link>
        </div>
      ) : (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
          {remotes.map(remote => (
            <Link key={remote.id} to={`/remote/${remote.id}`} className="remote-card">
              <div className="remote-icon">{remote.icon}</div>
              <div className="remote-info">
                <h3>{remote.name}</h3>
                <p>{remote.description || `${remote.columns} cột`}</p>
              </div>
              <span style={{ color: 'var(--text-muted)', fontSize: '1.1rem' }}>›</span>
            </Link>
          ))}
        </div>
      )}
    </div>
  )
}

function ConnectionBadge({ mode, online }: { mode: string, online: boolean }) {
  if (!online) return <span className="connection-badge badge-offline"><span className="dot" />Offline</span>
  if (mode === 'bluetooth') return <span className="connection-badge badge-ble"><span className="dot pulse" />BLE</span>
  if (mode === 'wifi')      return <span className="connection-badge badge-wifi"><span className="dot pulse" />WiFi</span>
  return <span className="connection-badge badge-cloud"><span className="dot pulse" />Cloud</span>
}
