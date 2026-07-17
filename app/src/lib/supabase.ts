import { createClient } from '@supabase/supabase-js'

const supabaseUrl  = import.meta.env.VITE_SUPABASE_URL as string
const supabaseAnon = import.meta.env.VITE_SUPABASE_ANON_KEY as string

if (!supabaseUrl || !supabaseAnon) {
  console.warn('[Supabase] Missing env vars – check .env.local')
}

export const supabase = createClient(supabaseUrl || '', supabaseAnon || '')

// ─── Types ─────────────────────────────────────────────────────
export interface Device {
  id: string
  user_id: string
  name: string
  description?: string
  device_key: string
  online: boolean
  mode: 'wifi' | 'bluetooth' | 'setup'
  flash_pct: number
  fw_version?: string
  last_seen?: string
  created_at: string
}

export interface IRCommand {
  id: string
  user_id: string
  device_id?: string
  name: string
  protocol: string
  address?: number
  command?: number
  raw_data?: number[]
  frequency: number
  source: 'learned' | 'irdb' | 'imported' | 'manual'
  irdb_id?: string
  irdb_brand?: string
  irdb_device?: string
  use_count: number
  last_used?: string
  created_at: string
}

export interface Remote {
  id: string
  user_id: string
  device_id?: string
  name: string
  description?: string
  icon: string
  columns: number
  theme_color: string
  created_at: string
  updated_at: string
}

export interface Button {
  id: string
  remote_id: string
  ir_command_id?: string
  label: string
  icon?: string
  color: string
  text_color: string
  position_x: number
  position_y: number
  width: number
  height: number
  style?: Record<string, string>
}

export interface CommandQueue {
  id: string
  device_id: string
  command_id?: string
  raw_data?: number[]
  protocol?: string
  status: 'pending' | 'sent' | 'error' | 'timeout'
  created_at: string
}
