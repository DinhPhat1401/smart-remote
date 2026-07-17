import { useState, useEffect } from 'react'
import { useParams, Link, useNavigate } from 'react-router-dom'
import { supabase } from '../lib/supabase'
import type { Remote, Button, IRCommand } from '../lib/supabase'
import { connection } from '../lib/connection'

interface ButtonWithCommand extends Button {
  ir_command?: IRCommand
}

export default function RemotePage() {
  const { id } = useParams<{ id: string }>()
  const navigate = useNavigate()

  const [remote, setRemote]   = useState<Remote | null>(null)
  const [buttons, setButtons] = useState<ButtonWithCommand[]>([])
  const [loading, setLoading] = useState(true)
  const [pressing, setPressing] = useState<string | null>(null)
  const [toast, setToast]     = useState<{ msg: string; type: 'success' | 'error' } | null>(null)

  useEffect(() => { loadRemote() }, [id])

  async function loadRemote() {
    if (!id) return
    setLoading(true)
    try {
      const { data: r } = await supabase.from('remotes').select('*').eq('id', id).single()
      setRemote(r)

      const { data: btns } = await supabase
        .from('buttons')
        .select('*, ir_command:ir_commands(*)')
        .eq('remote_id', id)
        .order('position_y').order('position_x')
      setButtons((btns || []) as ButtonWithCommand[])
    } catch (e) {
      console.error('[Remote] Load error:', e)
    }
    setLoading(false)
  }

  async function handleButtonPress(btn: ButtonWithCommand) {
    if (!btn.ir_command_id) {
      showToast('Nút chưa được gán lệnh IR', 'error')
      return
    }

    setPressing(btn.id)
    try {
      const ok = await connection.sendCommand(btn.ir_command_id)
      if (ok) showToast(`✓ ${btn.label}`, 'success')
      else    showToast('Gửi lệnh thất bại', 'error')
    } catch (e) {
      showToast('Lỗi kết nối', 'error')
    } finally {
      setTimeout(() => setPressing(null), 300)
    }
  }

  function showToast(msg: string, type: 'success' | 'error') {
    setToast({ msg, type })
    setTimeout(() => setToast(null), 2000)
  }

  if (loading) return (
    <div className="flex justify-center items-center" style={{ height: '60vh' }}>
      <div className="loading-spinner" />
    </div>
  )

  if (!remote) return (
    <div className="empty-state">
      <div className="empty-icon">❌</div>
      <h3>Không tìm thấy remote</h3>
      <Link to="/" className="btn btn-secondary mt-4">← Quay lại</Link>
    </div>
  )

  const gridStyle = {
    display: 'grid',
    gridTemplateColumns: `repeat(${remote.columns}, 1fr)`,
    gap: '8px',
    padding: '16px',
  }

  // Tạo grid với placeholder cho ô trống
  return (
    <div className="animate-fade-in">
      {/* Header */}
      <div className="page-header">
        <button onClick={() => navigate(-1)} className="back-btn">←</button>
        <div style={{ flex: 1 }}>
          <h1>{remote.icon} {remote.name}</h1>
          {remote.description && <p className="text-sm text-muted">{remote.description}</p>}
        </div>
        <Link to={`/editor/${remote.id}`} className="btn btn-ghost btn-icon" title="Chỉnh sửa">
          ✏️
        </Link>
      </div>

      {/* Remote Grid */}
      {buttons.length === 0 ? (
        <div className="empty-state">
          <div className="empty-icon">🎛️</div>
          <h3>Remote chưa có nút bấm</h3>
          <p>Vào Editor để thêm nút và gán lệnh IR</p>
          <Link to={`/editor/${remote.id}`} className="btn btn-primary mt-4">
            ✏️ Mở Editor
          </Link>
        </div>
      ) : (
        <div
          className="card"
          style={{
            padding: 0,
            background: 'var(--bg-surface)',
            borderColor: remote.theme_color + '33',
          }}
        >
          <div style={gridStyle}>
            {buttons.map(btn => (
              <button
                key={btn.id}
                className="ir-button"
                style={{
                  gridColumn: `${btn.position_x + 1} / span ${btn.width}`,
                  gridRow:    `${btn.position_y + 1} / span ${btn.height}`,
                  background: btn.color,
                  color:      btn.text_color,
                  borderColor: pressing === btn.id
                    ? 'white'
                    : 'transparent',
                  minHeight: btn.height * 60 + (btn.height - 1) * 8,
                  opacity: !btn.ir_command_id ? 0.5 : 1,
                }}
                onClick={() => handleButtonPress(btn)}
                disabled={pressing === btn.id}
              >
                {btn.icon && <span className="btn-icon-label">{btn.icon}</span>}
                <span className="btn-label">{btn.label}</span>
              </button>
            ))}
          </div>
        </div>
      )}

      {/* Toast */}
      {toast && (
        <div className={`toast toast-${toast.type}`}>
          {toast.msg}
        </div>
      )}
    </div>
  )
}
