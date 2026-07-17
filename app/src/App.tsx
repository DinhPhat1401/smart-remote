import { useState, useEffect } from 'react'
import { BrowserRouter, Routes, Route, useLocation, Navigate } from 'react-router-dom'
import { supabase } from './lib/supabase'
import { connection } from './lib/connection'

import Home     from './pages/Home'
import Remote   from './pages/Remote'
import Editor   from './pages/Editor'
import Learn    from './pages/Learn'
import Library  from './pages/Library'
import Settings from './pages/Settings'
import Login    from './pages/Login'

import './index.css'

// ─── Nav Icons ────────────────────────────────────────────────
const NavItems = [
  { to: '/',        icon: '🏠', label: 'Home' },
  { to: '/library', icon: '📚', label: 'Library' },
  { to: '/learn',   icon: '📡', label: 'Học lệnh' },
  { to: '/settings',icon: '⚙️', label: 'Cài đặt' },
]

function BottomNav() {
  const location = useLocation()
  const hide = ['/login', '/remote/', '/editor/'].some(p => location.pathname.startsWith(p))
  if (hide) return null

  return (
    <nav className="bottom-nav">
      {NavItems.map(item => (
        <a
          key={item.to}
          href={item.to}
          className={`nav-item ${location.pathname === item.to ? 'active' : ''}`}
          onClick={e => { e.preventDefault(); window.history.pushState({}, '', item.to); window.dispatchEvent(new PopStateEvent('popstate')) }}
        >
          <span style={{ fontSize: '1.3rem' }}>{item.icon}</span>
          <span>{item.label}</span>
        </a>
      ))}
    </nav>
  )
}

function AuthGuard({ children }: { children: React.ReactNode }) {
  const [user, setUser]     = useState<any>(undefined) // undefined = loading

  useEffect(() => {
    supabase.auth.getUser().then(({ data }) => setUser(data.user))
    supabase.auth.onAuthStateChange((_evt, session) => setUser(session?.user || null))

    // Restore device from localStorage
    const deviceId  = localStorage.getItem('smartremote_device_id')
    const deviceKey = localStorage.getItem('smartremote_device_key')
    if (deviceId && deviceKey) {
      connection.setDevice(deviceId, deviceKey)
      connection.useCloud() // Auto-switch to cloud mode
    }

  }, [])

  if (user === undefined) {
    // Loading
    return (
      <div style={{
        height: '100vh',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        flexDirection: 'column',
        gap: 16,
      }}>
        <div style={{ fontSize: '3rem' }}>🎮</div>
        <div className="loading-spinner" />
      </div>
    )
  }

  if (!user) return <Navigate to="/login" replace />
  return <>{children}</>
}

export default function App() {
  return (
    <BrowserRouter>
      <div className="app-layout">
        {/* Connection status bar */}
        <ConnectionBar />

        <main className="page-content">
          <Routes>
            <Route path="/login" element={<Login />} />
            <Route path="/" element={<AuthGuard><Home /></AuthGuard>} />
            <Route path="/remote/:id" element={<AuthGuard><Remote /></AuthGuard>} />
            <Route path="/editor/:id" element={<AuthGuard><Editor /></AuthGuard>} />
            <Route path="/learn" element={<AuthGuard><Learn /></AuthGuard>} />
            <Route path="/library" element={<AuthGuard><Library /></AuthGuard>} />
            <Route path="/settings" element={<AuthGuard><Settings /></AuthGuard>} />
          </Routes>
        </main>

        <BottomNav />
      </div>
    </BrowserRouter>
  )
}

function ConnectionBar() {
  const [mode, setMode] = useState(connection.getState().mode)

  useEffect(() => {
    return connection.onStateChange(s => setMode(s.mode))
  }, [])

  const config: Record<string, { label: string; color: string }> = {
    ble:    { label: '🔵 BLE',    color: '#1d4ed8' },
    wifi:   { label: '🟢 WiFi',   color: '#15803d' },
    cloud:  { label: '🟣 Cloud',  color: '#4f46e5' },
    offline:{ label: '⚫ Offline', color: '#374151' },
  }

  const cfg = config[mode] || config.offline
  return (
    <div style={{
      height: 28,
      background: cfg.color + '33',
      borderBottom: `1px solid ${cfg.color}55`,
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      fontSize: '0.72rem',
      fontWeight: 600,
      color: cfg.color,
      letterSpacing: '0.04em',
    }}>
      {cfg.label}
    </div>
  )
}
