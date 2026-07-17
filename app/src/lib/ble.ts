/**
 * ble.ts
 * BLE Connection Manager
 * Auto-detect: Capacitor native (Android/iOS app) vs Web Bluetooth API (browser)
 */

import { Capacitor } from '@capacitor/core'
import { BleClient, dataViewToText, textToDataView } from '@capacitor-community/bluetooth-le'
import type { BleDevice } from '@capacitor-community/bluetooth-le'

// ─── UUIDs phải khớp với firmware config.h ──────────────────────
export const SERVICE_UUID  = '12345678-0001-0000-0000-000000000001'
export const CHAR_IR_SEND  = '12345678-0001-0000-0000-000000000002'
export const CHAR_STATUS   = '12345678-0001-0000-0000-000000000003'
export const CHAR_LEARN    = '12345678-0001-0000-0000-000000000004'

export type StatusCallback  = (status: DeviceStatus) => void
export type LearnCallback   = (result: LearnResult) => void

export interface DeviceStatus {
  type:       string
  mode:       string
  flash_pct:  number
  cmd_count:  number
  connected:  boolean
  fw:         string
  wifi?:      boolean
}

export interface LearnResult {
  success:   boolean
  id?:       string
  protocol?: string
  address?:  number
  command?:  number
  raw_len?:  number
}

// ─── BLE Connection class ─────────────────────────────────────
class BLEConnection {
  private connected  = false
  private deviceId:  string | null = null
  private webDevice: any = null // Web Bluetooth device object

  private statusCb?: StatusCallback
  private learnCb?:  LearnCallback

  isNative = Capacitor.isNativePlatform()
  isSupported = this.isNative || ('bluetooth' in navigator)

  onStatus(cb: StatusCallback) { this.statusCb = cb }
  onLearn(cb: LearnCallback)   { this.learnCb = cb }

  get isConnected() { return this.connected }

  // ─── Connect ───────────────────────────────────────────────
  async connect(): Promise<boolean> {
    if (this.isNative) {
      return this._connectNative()
    } else {
      return this._connectWeb()
    }
  }

  // ─── Native BLE (Capacitor – Android/iOS app) ──────────────
  private async _connectNative(): Promise<boolean> {
    try {
      await BleClient.initialize()

      const device: BleDevice = await BleClient.requestDevice({
        services: [SERVICE_UUID],
        optionalServices: [],
      })

      this.deviceId = device.deviceId
      await BleClient.connect(device.deviceId, (id) => {
        console.log('[BLE] Disconnected:', id)
        this.connected = false
      })

      this.connected = true
      console.log('[BLE] Connected (native):', device.name)

      // Subscribe to STATUS notifications
      await BleClient.startNotifications(
        device.deviceId,
        SERVICE_UUID,
        CHAR_STATUS,
        (value) => {
          const text = dataViewToText(value)
          this._handleNotification(text)
        }
      )

      return true
    } catch (err) {
      console.error('[BLE] Native connect error:', err)
      return false
    }
  }

  // ─── Web Bluetooth API (Chrome browser) ────────────────────
  private async _connectWeb(): Promise<boolean> {
    if (!('bluetooth' in navigator)) {
      throw new Error('BLE_NOT_SUPPORTED')
    }

    try {
      const device = await (navigator as any).bluetooth.requestDevice({
        filters: [{ name: 'SmartRemote' }],
        optionalServices: [SERVICE_UUID],
      })

      this.webDevice = device
      const server  = await device.gatt.connect()
      const service = await server.getPrimaryService(SERVICE_UUID)

      this.connected = true
      console.log('[BLE] Connected (web):', device.name)

      // Subscribe to STATUS notifications
      const statusChar = await service.getCharacteristic(CHAR_STATUS)
      await statusChar.startNotifications()
      statusChar.addEventListener('characteristicvaluechanged', (event: any) => {
        const decoder = new TextDecoder()
        const text = decoder.decode(event.target.value)
        this._handleNotification(text)
      })

      device.addEventListener('gattserverdisconnected', () => {
        this.connected = false
        console.log('[BLE] Web disconnected')
      })

      return true
    } catch (err) {
      console.error('[BLE] Web connect error:', err)
      return false
    }
  }

  // ─── Disconnect ────────────────────────────────────────────
  async disconnect(): Promise<void> {
    if (this.isNative && this.deviceId) {
      await BleClient.disconnect(this.deviceId)
    } else if (this.webDevice?.gatt?.connected) {
      this.webDevice.gatt.disconnect()
    }
    this.connected = false
    this.deviceId  = null
    this.webDevice = null
  }

  // ─── Send IR Command ───────────────────────────────────────
  async sendIRPacket(packetJson: string): Promise<boolean> {
    if (!this.connected) return false

    try {
      if (this.isNative && this.deviceId) {
        await BleClient.write(
          this.deviceId,
          SERVICE_UUID,
          CHAR_IR_SEND,
          textToDataView(packetJson)
        )
      } else if (this.webDevice) {
        const server  = await this.webDevice.gatt.connect()
        const service = await server.getPrimaryService(SERVICE_UUID)
        const char    = await service.getCharacteristic(CHAR_IR_SEND)
        const encoder = new TextEncoder()
        await char.writeValueWithoutResponse(encoder.encode(packetJson))
      }
      return true
    } catch (err) {
      console.error('[BLE] Send error:', err)
      return false
    }
  }

  // ─── Trigger Learn Mode ────────────────────────────────────
  async triggerLearn(start: boolean): Promise<boolean> {
    if (!this.connected) return false

    const byte = new Uint8Array([start ? 0x01 : 0x00])

    try {
      if (this.isNative && this.deviceId) {
        await BleClient.write(
          this.deviceId,
          SERVICE_UUID,
          CHAR_LEARN,
          new DataView(byte.buffer)
        )
      } else if (this.webDevice) {
        const server  = await this.webDevice.gatt.connect()
        const service = await server.getPrimaryService(SERVICE_UUID)
        const char    = await service.getCharacteristic(CHAR_LEARN)
        await char.writeValueWithoutResponse(byte)
      }
      return true
    } catch (err) {
      console.error('[BLE] Learn trigger error:', err)
      return false
    }
  }

  // ─── Handle notifications from ESP32 ───────────────────────
  private _handleNotification(text: string) {
    try {
      const data = JSON.parse(text)
      if (data.type === 'STATUS' && this.statusCb) {
        this.statusCb(data as DeviceStatus)
      } else if (data.type === 'LEARN_RESULT' && this.learnCb) {
        this.learnCb(data as LearnResult)
      }
    } catch (e) {
      console.warn('[BLE] Invalid notification JSON:', text)
    }
  }
}

// Singleton instance
export const ble = new BLEConnection()
