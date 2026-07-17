import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { supabase } from '../lib/supabase'
import type { IRCommand } from '../lib/supabase'
import { syncFromSupabase } from '../lib/ir-store'

interface IRDBResult {
  brand: string
  deviceType: string
  functions: string[]
}

export default function Library() {
  const navigate = useNavigate()
  const [commands, setCommands] = useState<IRCommand[]>([])
  const [loading, setLoading]   = useState(true)
  const [search, setSearch]     = useState('')
  const [tab, setTab]           = useState<'all' | 'irdb'>('all')

  // IRDB import state
  const [irdbBrand,   setIrdbBrand]   = useState('')
  const [irdbType,    setIrdbType]    = useState('TV')
  const [irdbResult,  setIrdbResult]  = useState<IRDBResult | null>(null)
  const [importing,   setImporting]   = useState(false)
  const [selected,    setSelected]    = useState<Set<string>>(new Set())

  useEffect(() => { loadCommands() }, [])

  async function loadCommands() {
    setLoading(true)
    const { data } = await supabase
      .from('ir_commands')
      .select('*')
      .order('use_count', { ascending: false })
    setCommands(data || [])
    setLoading(false)
  }

  async function deleteCommand(id: string) {
    if (!confirm('Xóa lệnh này?')) return
    await supabase.from('ir_commands').delete().eq('id', id)
    setCommands(prev => prev.filter(c => c.id !== id))
  }

  async function searchIRDB() {
    if (!irdbBrand.trim()) return
    setImporting(true)
    try {
      // Gọi IRDB Edge Function
      const { data, error } = await supabase.functions.invoke('irdb-import', {
        body: {
          brand:      irdbBrand.trim(),
          deviceType: irdbType,
          deviceId:   await getFirstDeviceId(),
          // Không gửi selectedFunctions → lấy danh sách trước
          preview:    true,
        },
      })

      if (error) throw error
      setIrdbResult({
        brand:      data.brand,
        deviceType: data.device_type,
        functions:  data.available_functions || [],
      })
      // Chọn tất cả mặc định
      setSelected(new Set(data.available_functions || []))
    } catch (e: any) {
      alert('Không tìm thấy: ' + (e.message || 'Unknown error'))
    }
    setImporting(false)
  }

  async function importSelected() {
    if (!irdbResult || selected.size === 0) return
    setImporting(true)
    try {
      const { data, error } = await supabase.functions.invoke('irdb-import', {
        body: {
          brand:             irdbResult.brand,
          deviceType:        irdbResult.deviceType,
          deviceId:          await getFirstDeviceId(),
          selectedFunctions: Array.from(selected),
        },
      })

      if (error) throw error
      alert(`✅ Đã import ${data.imported_count} lệnh IR!`)

      // Sync lại
      await loadCommands()
      const { data: allCmds } = await supabase.from('ir_commands').select('*')
      if (allCmds) await syncFromSupabase(allCmds as IRCommand[])

      setIrdbResult(null)
      setTab('all')
    } catch (e: any) {
      alert('Import thất bại: ' + e.message)
    }
    setImporting(false)
  }

  async function getFirstDeviceId(): Promise<string> {
    const { data } = await supabase.from('devices').select('id').limit(1).single()
    return data?.id || ''
  }

  const filtered = commands.filter(c => {
    const q = search.toLowerCase()
    return !q ||
      c.name.toLowerCase().includes(q) ||
      c.irdb_brand?.toLowerCase().includes(q) ||
      c.irdb_device?.toLowerCase().includes(q) ||
      c.protocol?.toLowerCase().includes(q)
  })

  const sourceIcon = (s: string) =>
    s === 'learned' ? '📡' : s === 'irdb' ? '🌐' : '📥'

  return (
    <div className="animate-fade-in">
      <div className="page-header">
        <button onClick={() => navigate(-1)} className="back-btn">←</button>
        <h1>📚 Thư viện IR</h1>
        <span className="text-sm text-muted">{commands.length} lệnh</span>
      </div>

      {/* Tabs */}
      <div className="flex gap-2" style={{ marginBottom: 16 }}>
        {['all', 'irdb'].map(t => (
          <button
            key={t}
            className={`btn ${tab === t ? 'btn-primary' : 'btn-ghost'}`}
            style={{ flex: 1 }}
            onClick={() => setTab(t as any)}
          >
            {t === 'all' ? '📋 Tất cả' : '🌐 Import IRDB'}
          </button>
        ))}
      </div>

      {/* All Commands Tab */}
      {tab === 'all' && (
        <div>
          <input
            className="input"
            placeholder="🔍 Tìm kiếm lệnh IR..."
            value={search}
            onChange={e => setSearch(e.target.value)}
            style={{ marginBottom: 16 }}
          />

          {loading ? (
            <div className="flex justify-center" style={{ padding: 40 }}>
              <div className="loading-spinner" />
            </div>
          ) : filtered.length === 0 ? (
            <div className="empty-state">
              <div className="empty-icon">🔍</div>
              <h3>Không tìm thấy lệnh IR</h3>
              <p>Học lệnh mới hoặc import từ IRDB</p>
            </div>
          ) : (
            <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
              {filtered.map(cmd => (
                <div key={cmd.id} className="card flex items-center gap-3" style={{ padding: '12px 16px' }}>
                  <span style={{ fontSize: '1.2rem' }}>{sourceIcon(cmd.source)}</span>
                  <div style={{ flex: 1, minWidth: 0 }}>
                    <div style={{ fontWeight: 600, fontSize: '0.9rem' }} className="truncate">{cmd.name}</div>
                    <div className="text-xs text-muted">
                      {cmd.protocol} · {cmd.irdb_brand || 'Custom'} · ×{cmd.use_count}
                    </div>
                  </div>
                  <button
                    className="btn btn-danger btn-icon"
                    onClick={() => deleteCommand(cmd.id)}
                    title="Xóa"
                  >
                    🗑️
                  </button>
                </div>
              ))}
            </div>
          )}
        </div>
      )}

      {/* IRDB Import Tab */}
      {tab === 'irdb' && (
        <div className="animate-fade-in">
          <div className="card" style={{ marginBottom: 16 }}>
            <h2 style={{ marginBottom: 4 }}>Import từ IRDB</h2>
            <p className="text-sm text-muted" style={{ marginBottom: 16 }}>
              Nhập brand và loại thiết bị để tìm lệnh IR có sẵn
            </p>

            <div className="flex gap-2" style={{ marginBottom: 12 }}>
              <div className="form-group" style={{ flex: 2 }}>
                <label className="form-label">Brand</label>
                <input className="input" placeholder="VD: LG, Samsung, Sony..." value={irdbBrand} onChange={e => setIrdbBrand(e.target.value)} />
              </div>
              <div className="form-group" style={{ flex: 1 }}>
                <label className="form-label">Loại</label>
                <select
                  className="input"
                  value={irdbType}
                  onChange={e => setIrdbType(e.target.value)}
                  style={{ cursor: 'pointer' }}
                >
                  {['TV', 'AC', 'Audio', 'DVD', 'Cable', 'Projector', 'Fan'].map(t => (
                    <option key={t}>{t}</option>
                  ))}
                </select>
              </div>
            </div>

            <button
              className="btn btn-primary btn-full"
              onClick={searchIRDB}
              disabled={importing || !irdbBrand.trim()}
            >
              {importing ? '⏳ Đang tìm...' : '🔍 Tìm kiếm'}
            </button>
          </div>

          {/* IRDB Results */}
          {irdbResult && (
            <div className="card animate-slide-up">
              <div className="flex items-center justify-between" style={{ marginBottom: 12 }}>
                <div>
                  <h3>{irdbResult.brand} {irdbResult.deviceType}</h3>
                  <p className="text-xs text-muted">{irdbResult.functions.length} lệnh tìm thấy</p>
                </div>
                <div className="flex gap-2">
                  <button
                    className="btn btn-ghost"
                    style={{ fontSize: '0.75rem', padding: '4px 10px' }}
                    onClick={() => setSelected(new Set(irdbResult.functions))}
                  >
                    Chọn tất cả
                  </button>
                  <button
                    className="btn btn-ghost"
                    style={{ fontSize: '0.75rem', padding: '4px 10px' }}
                    onClick={() => setSelected(new Set())}
                  >
                    Bỏ chọn
                  </button>
                </div>
              </div>

              <div style={{
                display: 'grid',
                gridTemplateColumns: '1fr 1fr',
                gap: 8,
                maxHeight: 300,
                overflowY: 'auto',
                marginBottom: 16,
              }}>
                {irdbResult.functions.map(fn => (
                  <label
                    key={fn}
                    style={{
                      display: 'flex',
                      alignItems: 'center',
                      gap: 8,
                      padding: '8px 10px',
                      background: selected.has(fn) ? 'var(--accent-glow)' : 'var(--bg-elevated)',
                      border: `1px solid ${selected.has(fn) ? 'var(--accent)' : 'var(--border)'}`,
                      borderRadius: 'var(--radius-sm)',
                      cursor: 'pointer',
                      fontSize: '0.82rem',
                      transition: 'all 0.15s ease',
                    }}
                  >
                    <input
                      type="checkbox"
                      checked={selected.has(fn)}
                      onChange={e => {
                        const ns = new Set(selected)
                        e.target.checked ? ns.add(fn) : ns.delete(fn)
                        setSelected(ns)
                      }}
                      style={{ accentColor: 'var(--accent)' }}
                    />
                    {fn}
                  </label>
                ))}
              </div>

              <button
                className="btn btn-primary btn-full"
                onClick={importSelected}
                disabled={importing || selected.size === 0}
              >
                {importing ? '⏳ Đang import...' : `📥 Import ${selected.size} lệnh`}
              </button>
            </div>
          )}
        </div>
      )}
    </div>
  )
}
