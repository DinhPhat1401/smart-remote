/**
 * ir-store.ts
 * IndexedDB local cache cho IR commands
 * Dùng idb (lightweight wrapper quanh IndexedDB)
 * Mục đích: BLE offline operation – gửi RAW data mà không cần network
 */

import { openDB } from 'idb'
import type { IDBPDatabase } from 'idb'
import type { IRCommand } from './supabase'

const DB_NAME    = 'smartremote'
const DB_VERSION = 1
const STORE_CMDS = 'ir_commands'

let db: IDBPDatabase | null = null

async function getDB() {
  if (db) return db
  db = await openDB(DB_NAME, DB_VERSION, {
    upgrade(database) {
      if (!database.objectStoreNames.contains(STORE_CMDS)) {
        const store = database.createObjectStore(STORE_CMDS, { keyPath: 'id' })
        store.createIndex('by_source',     'source')
        store.createIndex('by_use_count',  'use_count')
        store.createIndex('by_last_used',  'last_used')
        store.createIndex('by_irdb_brand', 'irdb_brand')
      }
    },
  })
  return db
}

// ─── CRUD ──────────────────────────────────────────────────────
export async function saveCommand(cmd: IRCommand): Promise<void> {
  const database = await getDB()
  await database.put(STORE_CMDS, cmd)
}

export async function bulkSaveCommands(cmds: IRCommand[]): Promise<void> {
  const database = await getDB()
  const tx = database.transaction(STORE_CMDS, 'readwrite')
  await Promise.all([
    ...cmds.map(c => tx.store.put(c)),
    tx.done,
  ])
}

export async function getCommand(id: string): Promise<IRCommand | undefined> {
  const database = await getDB()
  const cmd = await database.get(STORE_CMDS, id)
  if (cmd) {
    // Cập nhật use_count và last_used
    await database.put(STORE_CMDS, {
      ...cmd,
      use_count: (cmd.use_count || 0) + 1,
      last_used: new Date().toISOString(),
    })
  }
  return cmd
}

export async function getAllCommands(): Promise<IRCommand[]> {
  const database = await getDB()
  return database.getAll(STORE_CMDS)
}

export async function searchCommands(query: string): Promise<IRCommand[]> {
  const all = await getAllCommands()
  const q = query.toLowerCase()
  return all.filter(c =>
    c.name.toLowerCase().includes(q) ||
    c.irdb_brand?.toLowerCase().includes(q) ||
    c.irdb_device?.toLowerCase().includes(q)
  )
}

export async function deleteCommand(id: string): Promise<void> {
  const database = await getDB()
  await database.delete(STORE_CMDS, id)
}

export async function clearAll(): Promise<void> {
  const database = await getDB()
  await database.clear(STORE_CMDS)
}

export async function getCommandCount(): Promise<number> {
  const database = await getDB()
  return database.count(STORE_CMDS)
}

// ─── Sync từ Supabase ──────────────────────────────────────────
export async function syncFromSupabase(commands: IRCommand[]): Promise<void> {
  await bulkSaveCommands(commands)
  console.log(`[IRStore] Synced ${commands.length} commands to IndexedDB`)
}

// ─── Build BLE packet từ IRCommand ─────────────────────────────
export function buildBLEPacket(cmd: IRCommand): string {
  return JSON.stringify({
    type:      'IR_SEND',
    protocol:  cmd.protocol || 'RAW',
    address:   cmd.address  || 0,
    command:   cmd.command  || 0,
    frequency: cmd.frequency || 38000,
    raw_data:  cmd.raw_data || [],
  })
}
