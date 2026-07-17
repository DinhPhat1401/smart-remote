/**
 * connection.ts
 * Connection Manager – Tự động chọn kết nối tốt nhất
 * Priority: BLE → WiFi WebSocket → Cloud (Supabase Realtime)
 */

import { ble } from './ble'
import type { DeviceStatus } from './ble'
import { supabase } from './supabase'
import type { IRCommand } from './supabase'
import { getCommand, buildBLEPacket } from './ir-store'

export type ConnectionMode = 'ble' | 'wifi' | 'cloud' | 'offline'

export interface ConnectionState {
  mode:        ConnectionMode
  connected:   boolean
  deviceStatus?: DeviceStatus
  deviceIp?:   string
}

class ConnectionManager {
  private mode:      ConnectionMode = 'offline'
  private ws:        WebSocket | null = null
  private deviceIp:  string = ''
  private deviceId:  string = ''
  private deviceKey: string = ''

  private listeners: Array<(state: ConnectionState) => void> = []

  // ─── Subscribe to state changes ──────────────────────────────
  onStateChange(cb: (state: ConnectionState) => void) {
    this.listeners.push(cb)
    return () => { this.listeners = this.listeners.filter(l => l !== cb) }
  }

  private _notify() {
    const state = this.getState()
    this.listeners.forEach(cb => cb(state))
  }

  getState(): ConnectionState {
    return {
      mode:      this.mode,
      connected: this.isConnected(),
      deviceIp:  this.deviceIp,
    }
  }

  isConnected(): boolean {
    switch (this.mode) {
      case 'ble':    return ble.isConnected
      case 'wifi':   return this.ws?.readyState === WebSocket.OPEN
      case 'cloud':  return true // Supabase always available with internet
      default:       return false
    }
  }

  // ─── Setup ───────────────────────────────────────────────────
  setDevice(id: string, key: string, ip?: string) {
    this.deviceId  = id
    this.deviceKey = key
    if (ip) this.deviceIp = ip
  }

  // ─── Connect BLE ─────────────────────────────────────────────
  async connectBLE(): Promise<boolean> {
    if (!ble.isSupported) {
      throw new Error('BLE not supported on this device/browser')
    }

    const ok = await ble.connect()
    if (ok) {
      this.mode = 'ble'
      this._notify()
    }
    return ok
  }

  // ─── Connect WiFi WebSocket ───────────────────────────────────
  async connectWiFi(ip: string): Promise<boolean> {
    this.deviceIp = ip
    return new Promise((resolve) => {
      try {
        this.ws = new WebSocket(`ws://${ip}/ws`)

        this.ws.onopen = () => {
          console.log('[Conn] WiFi WS connected:', ip)
          this.mode = 'wifi'
          this._notify()
          resolve(true)
        }

        this.ws.onmessage = (event) => {
          this._handleWsMessage(event.data)
        }

        this.ws.onclose = () => {
          console.log('[Conn] WiFi WS closed')
          if (this.mode === 'wifi') {
            this.mode = 'offline'
            this._notify()
          }
        }

        this.ws.onerror = () => resolve(false)

        setTimeout(() => resolve(false), 5000) // 5s timeout
      } catch {
        resolve(false)
      }
    })
  }

  // ─── Use Cloud (Supabase) ─────────────────────────────────────
  useCloud() {
    this.mode = 'cloud'
    this._notify()
  }

  // ─── Disconnect ───────────────────────────────────────────────
  async disconnect() {
    if (this.mode === 'ble') await ble.disconnect()
    if (this.ws) { this.ws.close(); this.ws = null }
    this.mode = 'offline'
    this._notify()
  }

  // ─── Send IR Command ──────────────────────────────────────────
  async sendCommand(commandId: string): Promise<boolean> {
    // Lấy lệnh từ IndexedDB local cache
    const cmd = await getCommand(commandId)
    if (!cmd) {
      console.error('[Conn] Command not found in local cache:', commandId)
      return false
    }

    switch (this.mode) {
      case 'ble':
        return this._sendViaBLE(cmd)
      case 'wifi':
        return this._sendViaWiFi(cmd)
      case 'cloud':
        return this._sendViaCloud(cmd)
      default:
        console.error('[Conn] No connection available')
        return false
    }
  }

  // ─── BLE: Gửi full RAW data ───────────────────────────────────
  private async _sendViaBLE(cmd: IRCommand): Promise<boolean> {
    const packet = buildBLEPacket(cmd)
    return ble.sendIRPacket(packet)
  }

  // ─── WiFi LAN: Gửi qua WebSocket ─────────────────────────────
  private async _sendViaWiFi(cmd: IRCommand): Promise<boolean> {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return false

    const msg = JSON.stringify({
      type:      'IR_SEND',
      id:        cmd.id,
      raw_data:  cmd.raw_data,
      protocol:  cmd.protocol,
      frequency: cmd.frequency,
    })

    this.ws.send(msg)
    return true
  }

  // ─── Cloud: Insert vào command_queue ─────────────────────────
  private async _sendViaCloud(cmd: IRCommand): Promise<boolean> {
    if (!this.deviceId) {
      console.error('[Conn] No device ID for cloud mode')
      return false
    }

    const { error } = await supabase.from('command_queue').insert({
      device_id:  this.deviceId,
      command_id: cmd.id,
      raw_data:   cmd.raw_data,
      protocol:   cmd.protocol,
      status:     'pending',
    })

    if (error) {
      console.error('[Conn] Cloud send error:', error.message)
      return false
    }

    return true
  }

  // ─── Trigger Learn Mode ───────────────────────────────────────
  async triggerLearn(start: boolean): Promise<boolean> {
    switch (this.mode) {
      case 'ble':
        return ble.triggerLearn(start)

      case 'wifi':
        if (this.ws?.readyState === WebSocket.OPEN) {
          this.ws.send(JSON.stringify({
            type: start ? 'LEARN_START' : 'LEARN_CANCEL'
          }))
          return true
        }
        return false

      case 'cloud':
        // Cloud mode không hỗ trợ learn trực tiếp
        // Dùng WiFi LAN hoặc BLE để học lệnh
        console.warn('[Conn] Learn not supported in Cloud-only mode')
        return false

      default:
        return false
    }
  }

  // ─── WiFi REST API helpers ────────────────────────────────────
  async getDeviceStatus(): Promise<any> {
    if (!this.deviceIp) return null
    try {
      const res = await fetch(`http://${this.deviceIp}/status`, {
        headers: { 'X-Device-Key': this.deviceKey },
      })
      return res.json()
    } catch { return null }
  }

  // ─── Handle WebSocket messages ────────────────────────────────
  private _handleWsMessage(data: string) {
    try {
      const msg = JSON.parse(data)
      if (msg.type === 'STATUS') {
        this.listeners.forEach(cb => cb({ ...this.getState(), deviceStatus: msg }))
      }
    } catch { /* ignore */ }
  }
}

// Singleton
export const connection = new ConnectionManager()
