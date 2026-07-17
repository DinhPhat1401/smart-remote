import { useState, useEffect } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { supabase } from '../lib/supabase'
import type { Remote, Button, IRCommand } from '../lib/supabase'

const COLORS = [
  '#4A90E2','#7C3AED','#059669','#DC2626','#D97706',
  '#DB2777','#0891B2','#64748B','#1e293b','#374151',
]

const ICONS = ['⚡','🔊','🔇','⏮','⏭','⏸','▶️','⏹','📺','🌡️','💡','🎵','🔔','📶','🏠','⭐']

interface EditButton extends Partial<Button> {
  tempId?: string
  ir_command?: IRCommand
}

export default function Editor() {
  const { id } = useParams<{ id: string }>()
  const isNew = id === 'new'
  const navigate = useNavigate()

  const [remote, setRemote]   = useState<Partial<Remote>>({
    name: '', icon: '📺', columns: 4, theme_color: '#4A90E2',
  })
  const [buttons, setButtons] = useState<EditButton[]>([])
  const [commands, setCommands] = useState<IRCommand[]>([])
  const [saving, setSaving]   = useState(false)
  const [showAddBtn, setShowAddBtn] = useState(false)

  // New button form
  const [newBtn, setNewBtn] = useState<EditButton>({
    label: '', icon: '', color: '#4A90E2', text_color: '#FFFFFF',
    position_x: 0, position_y: 0, width: 1, height: 1,
  })

  useEffect(() => {
    supabase.from('ir_commands').select('*').then(({ data }) => setCommands(data || []))

    if (!isNew) {
      supabase.from('remotes').select('*').eq('id', id).single()
        .then(({ data }) => { if (data) setRemote(data) })

      supabase.from('buttons').select('*, ir_command:ir_commands(*)')
        .eq('remote_id', id).order('position_y').order('position_x')
        .then(({ data }) => setButtons((data || []) as EditButton[]))
    }
  }, [id])

  async function saveRemote() {
    setSaving(true)
    try {
      const user = (await supabase.auth.getUser()).data.user
      let remoteId = id === 'new' ? undefined : id

      if (isNew) {
        const { data, error } = await supabase.from('remotes').insert({
          ...remote,
          user_id: user?.id,
        }).select().single()
        if (error) throw error
        remoteId = data.id
      } else {
        await supabase.from('remotes').update(remote).eq('id', id)
      }

      // Lưu buttons
      for (const btn of buttons) {
        if (btn.id && !btn.tempId) {
          // Update existing
          await supabase.from('buttons').update({
            label: btn.label, icon: btn.icon, color: btn.color,
            text_color: btn.text_color, ir_command_id: btn.ir_command_id,
            position_x: btn.position_x, position_y: btn.position_y,
            width: btn.width, height: btn.height,
          }).eq('id', btn.id)
        } else {
          // Insert new
          await supabase.from('buttons').insert({
            remote_id: remoteId,
            label: btn.label, icon: btn.icon, color: btn.color,
            text_color: btn.text_color, ir_command_id: btn.ir_command_id,
            position_x: btn.position_x || 0, position_y: btn.position_y || 0,
            width: btn.width || 1, height: btn.height || 1,
          })
        }
      }

      navigate(`/remote/${remoteId}`)
    } catch (e: any) {
      alert('Lỗi: ' + e.message)
    }
    setSaving(false)
  }

  function addButton() {
    const tempId = 'tmp_' + Date.now()
    setButtons(prev => [...prev, { ...newBtn, tempId }])
    setNewBtn({ label: '', icon: '', color: '#4A90E2', text_color: '#FFFFFF',
      position_x: 0, position_y: 0, width: 1, height: 1 })
    setShowAddBtn(false)
  }

  function removeButton(key: string) {
    setButtons(prev => prev.filter(b => (b.id || b.tempId) !== key))
  }

  const gridCols = remote.columns || 4

  return (
    <div className="animate-fade-in">
      <div className="page-header">
        <button onClick={() => navigate(-1)} className="back-btn">←</button>
        <h1>{isNew ? '➕ Tạo Remote' : '✏️ Chỉnh sửa'}</h1>
        <button className="btn btn-primary" onClick={saveRemote} disabled={saving}>
          {saving ? '⏳' : '💾 Lưu'}
        </button>
      </div>

      {/* Remote Info */}
      <div className="card" style={{ marginBottom: 16 }}>
        <h3 style={{ marginBottom: 12 }}>Thông tin Remote</h3>

        <div className="flex gap-2" style={{ marginBottom: 12 }}>
          {/* Icon picker */}
          <div className="form-group" style={{ width: 80 }}>
            <label className="form-label">Icon</label>
            <select className="input" value={remote.icon}
              onChange={e => setRemote(r => ({ ...r, icon: e.target.value }))}
              style={{ fontSize: '1.3rem', textAlign: 'center' }}>
              {['📺','🌡️','💡','🎵','📻','🖥️','📷','🎮','🏠','⚡'].map(ic => (
                <option key={ic} value={ic}>{ic}</option>
              ))}
            </select>
          </div>

          <div className="form-group" style={{ flex: 1 }}>
            <label className="form-label">Tên Remote</label>
            <input className="input" placeholder="VD: TV Phòng Khách"
              value={remote.name} onChange={e => setRemote(r => ({ ...r, name: e.target.value }))} />
          </div>
        </div>

        <div className="flex gap-2">
          <div className="form-group" style={{ flex: 1 }}>
            <label className="form-label">Số cột</label>
            <select className="input" value={remote.columns}
              onChange={e => setRemote(r => ({ ...r, columns: +e.target.value }))}>
              {[2, 3, 4, 5, 6].map(n => <option key={n}>{n}</option>)}
            </select>
          </div>
          <div className="form-group" style={{ flex: 1 }}>
            <label className="form-label">Màu chủ đạo</label>
            <div className="color-grid">
              {COLORS.slice(0, 6).map(c => (
                <div key={c} className={`color-swatch ${remote.theme_color === c ? 'selected' : ''}`}
                  style={{ background: c }}
                  onClick={() => setRemote(r => ({ ...r, theme_color: c }))} />
              ))}
            </div>
          </div>
        </div>
      </div>

      {/* Buttons Grid Preview */}
      <div className="card" style={{ marginBottom: 16 }}>
        <div className="flex items-center justify-between" style={{ marginBottom: 12 }}>
          <h3>Nút bấm ({buttons.length})</h3>
          <button className="btn btn-primary" style={{ padding: '6px 14px', fontSize: '0.8rem' }}
            onClick={() => setShowAddBtn(true)}>
            ➕ Thêm nút
          </button>
        </div>

        {buttons.length === 0 ? (
          <div style={{ textAlign: 'center', padding: '24px', color: 'var(--text-muted)', fontSize: '0.85rem' }}>
            Chưa có nút nào. Nhấn "+ Thêm nút" để bắt đầu.
          </div>
        ) : (
          <div style={{
            display: 'grid',
            gridTemplateColumns: `repeat(${gridCols}, 1fr)`,
            gap: 8,
          }}>
            {buttons.map(btn => {
              const key = btn.id || btn.tempId!
              return (
                <div key={key} style={{
                  gridColumn: `${(btn.position_x || 0) + 1} / span ${btn.width || 1}`,
                  position: 'relative',
                  minHeight: 60,
                }}>
                  <button
                    className="ir-button w-full"
                    style={{
                      background: btn.color || '#4A90E2',
                      color: btn.text_color || '#fff',
                      height: '100%',
                      minHeight: 60,
                    }}
                  >
                    {btn.icon && <span>{btn.icon}</span>}
                    <span style={{ fontSize: '0.75rem' }}>{btn.label || '(trống)'}</span>
                  </button>
                  <button
                    onClick={(e) => { e.stopPropagation(); removeButton(key) }}
                    style={{
                      position: 'absolute', top: -6, right: -6,
                      width: 18, height: 18, borderRadius: '50%',
                      background: 'var(--error)', border: 'none',
                      color: 'white', fontSize: '0.7rem', cursor: 'pointer',
                      display: 'flex', alignItems: 'center', justifyContent: 'center',
                    }}
                  >×</button>
                </div>
              )
            })}
          </div>
        )}
      </div>

      {/* Add Button Modal */}
      {showAddBtn && (
        <div style={{
          position: 'fixed', inset: 0,
          background: 'rgba(0,0,0,0.7)',
          display: 'flex', alignItems: 'flex-end',
          zIndex: 200,
        }} onClick={() => setShowAddBtn(false)}>
          <div style={{
            background: 'var(--bg-surface)',
            borderRadius: '20px 20px 0 0',
            padding: 24,
            width: '100%',
            maxWidth: 480,
            margin: '0 auto',
          }} onClick={e => e.stopPropagation()}>
            <h3 style={{ marginBottom: 16 }}>➕ Thêm nút mới</h3>

            <div className="flex gap-2" style={{ marginBottom: 12 }}>
              <div className="form-group" style={{ flex: 1 }}>
                <label className="form-label">Nhãn</label>
                <input className="input" placeholder="Power, Vol+..."
                  value={newBtn.label} onChange={e => setNewBtn(b => ({ ...b, label: e.target.value }))} />
              </div>
              <div className="form-group" style={{ width: 80 }}>
                <label className="form-label">Icon</label>
                <select className="input" value={newBtn.icon}
                  onChange={e => setNewBtn(b => ({ ...b, icon: e.target.value }))}
                  style={{ fontSize: '1.1rem' }}>
                  <option value="">-</option>
                  {ICONS.map(ic => <option key={ic} value={ic}>{ic}</option>)}
                </select>
              </div>
            </div>

            <div className="form-group" style={{ marginBottom: 12 }}>
              <label className="form-label">Gán lệnh IR</label>
              <select className="input" value={newBtn.ir_command_id || ''}
                onChange={e => setNewBtn(b => ({ ...b, ir_command_id: e.target.value || undefined }))}>
                <option value="">-- Chưa gán --</option>
                {commands.map(c => <option key={c.id} value={c.id}>{c.name}</option>)}
              </select>
            </div>

            <div className="flex gap-2" style={{ marginBottom: 16 }}>
              <div className="form-group" style={{ flex: 1 }}>
                <label className="form-label">Cột (0-based)</label>
                <input type="number" className="input" min={0} max={gridCols - 1}
                  value={newBtn.position_x} onChange={e => setNewBtn(b => ({ ...b, position_x: +e.target.value }))} />
              </div>
              <div className="form-group" style={{ flex: 1 }}>
                <label className="form-label">Hàng (0-based)</label>
                <input type="number" className="input" min={0}
                  value={newBtn.position_y} onChange={e => setNewBtn(b => ({ ...b, position_y: +e.target.value }))} />
              </div>
            </div>

            <div className="form-group" style={{ marginBottom: 16 }}>
              <label className="form-label">Màu nút</label>
              <div className="color-grid">
                {COLORS.map(c => (
                  <div key={c} className={`color-swatch ${newBtn.color === c ? 'selected' : ''}`}
                    style={{ background: c }} onClick={() => setNewBtn(b => ({ ...b, color: c }))} />
                ))}
              </div>
            </div>

            <div className="flex gap-2">
              <button className="btn btn-ghost" style={{ flex: 1 }} onClick={() => setShowAddBtn(false)}>Huỷ</button>
              <button className="btn btn-primary" style={{ flex: 2 }} onClick={addButton}
                disabled={!newBtn.label?.trim()}>➕ Thêm</button>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}
