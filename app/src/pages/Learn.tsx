import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { supabase } from '../lib/supabase'
import type { IRCommand } from '../lib/supabase'
import { connection } from '../lib/connection'
import { ble } from '../lib/ble'
import { syncFromSupabase } from '../lib/ir-store'

type LearnStep = 'name' | 'waiting' | 'done' | 'error'

export default function Learn() {
  const navigate = useNavigate()
  const [step, setStep]       = useState<LearnStep>('name')
  const [cmdName, setCmdName] = useState('')
  const [deviceId, setDeviceId] = useState<string>('')
  const [learnedCmd, setLearnedCmd] = useState<any>(null)
  const [errorMsg, setErrorMsg]     = useState('')
  const [saving, setSaving]         = useState(false)
  const [connMode, setConnMode]     = useState('')

  useEffect(() => {
    // Lấy device đầu tiên
    supabase.from('devices').select('id').limit(1).single()
      .then(({ data }) => { if (data) setDeviceId(data.id) })

    // Lắng nghe kết quả learn từ BLE
    ble.onLearn((result: any) => {
      if (result.success) {
        setLearnedCmd(result)
        setStep('done')
      } else {
        setErrorMsg('Không nhận được tín hiệu IR. Hãy thử lại.')
        setStep('error')
      }
    })

    setConnMode(connection.getState().mode)
  }, [])

  async function startLearn() {
    if (!cmdName.trim()) return
    setStep('waiting')

    const ok = await connection.triggerLearn(true)
    if (!ok) {
      // Nếu không có kết nối → thông báo user nhấn button vật lý
      setErrorMsg('Không thể kết nối đến thiết bị. Vui lòng nhấn giữ button trên ESP32 để bắt đầu học lệnh.')
      setStep('error')
    }

    // Timeout 8s nếu không nhận được kết quả
    setTimeout(() => {
      if (step === 'waiting') {
        setErrorMsg('Hết thời gian chờ. Hãy chắc chắn hướng remote vào TSOP1738 và nhấn nút.')
        setStep('error')
        connection.triggerLearn(false)
      }
    }, 8000)
  }

  async function saveCommand() {
    if (!learnedCmd || !cmdName.trim()) return
    setSaving(true)

    try {
      const { data, error } = await supabase.from('ir_commands').insert({
        user_id:   (await supabase.auth.getUser()).data.user?.id,
        device_id: deviceId || null,
        name:      cmdName.trim(),
        protocol:  learnedCmd.protocol || 'UNKNOWN',
        address:   learnedCmd.address,
        command:   learnedCmd.command,
        source:    'learned',
        frequency: 38000,
      }).select().single()

      if (error) throw error

      // Sync to IndexedDB
      await syncFromSupabase([data as IRCommand])

      navigate('/library')
    } catch (e: any) {
      setErrorMsg(e.message)
    }
    setSaving(false)
  }

  function reset() {
    setStep('name')
    setLearnedCmd(null)
    setErrorMsg('')
    connection.triggerLearn(false)
  }

  return (
    <div className="animate-fade-in">
      <div className="page-header">
        <button onClick={() => navigate(-1)} className="back-btn">←</button>
        <h1>📡 Học lệnh IR</h1>
      </div>

      {/* Step Indicator */}
      <StepIndicator current={step} />

      <div className="card" style={{ marginTop: 20 }}>

        {/* Step 1: Đặt tên */}
        {step === 'name' && (
          <div className="animate-fade-in">
            <h2 style={{ marginBottom: 8 }}>Bước 1: Đặt tên lệnh</h2>
            <p className="text-sm text-muted" style={{ marginBottom: 20 }}>
              Nhập tên cho lệnh IR bạn muốn học
            </p>

            <div className="form-group" style={{ marginBottom: 16 }}>
              <label className="form-label">Tên lệnh</label>
              <input
                className="input"
                placeholder="VD: Bật nguồn TV, Tăng âm lượng..."
                value={cmdName}
                onChange={e => setCmdName(e.target.value)}
                onKeyDown={e => e.key === 'Enter' && startLearn()}
                autoFocus
              />
            </div>

            {/* Quick name suggestions */}
            <div className="flex gap-2" style={{ flexWrap: 'wrap', marginBottom: 20 }}>
              {['Power', 'Vol +', 'Vol -', 'Mute', 'CH +', 'CH -', 'OK', 'Back', 'Home'].map(s => (
                <button
                  key={s}
                  className="btn btn-ghost"
                  style={{ padding: '4px 10px', fontSize: '0.78rem' }}
                  onClick={() => setCmdName(s)}
                >
                  {s}
                </button>
              ))}
            </div>

            {connMode !== 'ble' && connMode !== 'wifi' && (
              <div style={{
                padding: '10px 14px',
                background: 'rgba(245,158,11,0.1)',
                border: '1px solid rgba(245,158,11,0.3)',
                borderRadius: 'var(--radius-sm)',
                fontSize: '0.82rem',
                color: '#f59e0b',
                marginBottom: 16,
              }}>
                ⚠️ Không có kết nối trực tiếp. Hãy kết nối BLE hoặc WiFi để học lệnh.
              </div>
            )}

            <button
              className="btn btn-primary btn-full"
              disabled={!cmdName.trim()}
              onClick={startLearn}
            >
              Tiếp theo →
            </button>
          </div>
        )}

        {/* Step 2: Chờ tín hiệu */}
        {step === 'waiting' && (
          <div className="animate-fade-in" style={{ textAlign: 'center', padding: '20px 0' }}>
            <div className="learn-indicator" style={{
              flexDirection: 'column',
              alignItems: 'center',
              marginBottom: 24,
            }}>
              <div style={{ fontSize: '3rem', marginBottom: 12 }}>📻</div>
              <h2 style={{ color: '#ef4444' }}>Đang chờ tín hiệu...</h2>
              <p className="text-sm text-muted" style={{ marginTop: 8 }}>
                Hướng remote vào cảm biến TSOP1738 và nhấn nút muốn học
              </p>
            </div>

            <div style={{
              padding: '12px',
              background: 'var(--bg-elevated)',
              borderRadius: 'var(--radius-md)',
              fontSize: '0.85rem',
              marginBottom: 20,
              color: 'var(--text-secondary)',
            }}>
              💡 Giữ remote gần thiết bị (~30cm), nhấn và giữ nút ~1 giây
            </div>

            <button className="btn btn-ghost btn-full" onClick={reset}>
              Huỷ
            </button>
          </div>
        )}

        {/* Step 3: Học xong */}
        {step === 'done' && (
          <div className="animate-fade-in">
            <div style={{ textAlign: 'center', marginBottom: 24 }}>
              <div style={{ fontSize: '3rem', marginBottom: 8 }}>✅</div>
              <h2>Đã học xong!</h2>
              <p className="text-sm text-muted">
                Lệnh "{cmdName}" đã được thu nhận thành công
              </p>
            </div>

            {learnedCmd && (
              <div style={{
                padding: '12px',
                background: 'var(--bg-elevated)',
                borderRadius: 'var(--radius-md)',
                marginBottom: 20,
                fontSize: '0.82rem',
                fontFamily: 'JetBrains Mono, monospace',
              }}>
                <div>Protocol: <strong>{learnedCmd.protocol || 'RAW'}</strong></div>
                {learnedCmd.address !== undefined && <div>Address: <strong>0x{learnedCmd.address?.toString(16).toUpperCase()}</strong></div>}
                {learnedCmd.raw_len && <div>Raw length: <strong>{learnedCmd.raw_len} pulses</strong></div>}
              </div>
            )}

            <div className="flex gap-2">
              <button className="btn btn-ghost" style={{ flex: 1 }} onClick={reset}>
                Học lại
              </button>
              <button
                className="btn btn-primary"
                style={{ flex: 2 }}
                onClick={saveCommand}
                disabled={saving}
              >
                {saving ? '⏳ Đang lưu...' : '💾 Lưu lệnh'}
              </button>
            </div>
          </div>
        )}

        {/* Error */}
        {step === 'error' && (
          <div className="animate-fade-in" style={{ textAlign: 'center' }}>
            <div style={{ fontSize: '3rem', marginBottom: 8 }}>❌</div>
            <h2>Có lỗi xảy ra</h2>
            <p className="text-sm" style={{ color: 'var(--error)', margin: '12px 0 20px' }}>
              {errorMsg}
            </p>
            <button className="btn btn-primary btn-full" onClick={reset}>
              Thử lại
            </button>
          </div>
        )}
      </div>
    </div>
  )
}

function StepIndicator({ current }: { current: LearnStep }) {
  const steps = [
    { key: 'name',    label: 'Đặt tên' },
    { key: 'waiting', label: 'Chờ tín hiệu' },
    { key: 'done',    label: 'Hoàn thành' },
  ]
  const stepIdx = { name: 0, waiting: 1, done: 2, error: 2 }

  return (
    <div className="flex items-center gap-0" style={{ marginTop: 8 }}>
      {steps.map((s, i) => (
        <div key={s.key} className="flex items-center" style={{ flex: 1 }}>
          <div style={{
            width: 28, height: 28,
            borderRadius: '50%',
            background: i <= stepIdx[current]
              ? 'var(--accent)'
              : 'var(--bg-elevated)',
            border: `2px solid ${i <= stepIdx[current] ? 'var(--accent)' : 'var(--border)'}`,
            display: 'flex', alignItems: 'center', justifyContent: 'center',
            fontSize: '0.72rem', fontWeight: 700,
            color: i <= stepIdx[current] ? 'white' : 'var(--text-muted)',
            flexShrink: 0,
            transition: 'all 0.3s ease',
          }}>
            {i < stepIdx[current] ? '✓' : i + 1}
          </div>
          {i < steps.length - 1 && (
            <div style={{
              flex: 1,
              height: 2,
              background: i < stepIdx[current] ? 'var(--accent)' : 'var(--border)',
              transition: 'background 0.3s ease',
            }} />
          )}
        </div>
      ))}
    </div>
  )
}
