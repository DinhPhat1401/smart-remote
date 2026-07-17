-- =============================================================
-- SmartRemote – Initial Database Schema
-- Supabase / PostgreSQL
-- =============================================================

-- ─────────────────────────────────────────────────────────────
-- 1. DEVICES – Quản lý ESP32 devices
-- ─────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS devices (
  id          UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id     UUID        NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
  name        TEXT        NOT NULL,
  description TEXT,
  device_key  TEXT        UNIQUE NOT NULL, -- Key xác thực ESP32 ↔ Server
  online      BOOLEAN     DEFAULT false,
  mode        TEXT        DEFAULT 'wifi' CHECK (mode IN ('wifi', 'bluetooth', 'setup')),
  flash_pct   INTEGER     DEFAULT 0 CHECK (flash_pct BETWEEN 0 AND 100),
  fw_version  TEXT,       -- Firmware version string
  last_seen   TIMESTAMPTZ,
  created_at  TIMESTAMPTZ DEFAULT now(),
  updated_at  TIMESTAMPTZ DEFAULT now()
);

-- ─────────────────────────────────────────────────────────────
-- 2. IR_COMMANDS – Lưu tín hiệu hồng ngoại
-- ─────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS ir_commands (
  id          UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id     UUID        NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
  device_id   UUID        REFERENCES devices(id) ON DELETE SET NULL,
  name        TEXT        NOT NULL,
  protocol    TEXT        DEFAULT 'RAW',  -- NEC, SONY, RAW, SAMSUNG, ...
  address     INTEGER,
  command     INTEGER,
  raw_data    INTEGER[],                  -- Raw timing array (microseconds)
  frequency   INTEGER     DEFAULT 38000, -- Carrier frequency Hz
  source      TEXT        DEFAULT 'learned'
                          CHECK (source IN ('learned', 'irdb', 'imported', 'manual')),
  irdb_id     TEXT,       -- ID từ IRDB nếu import
  irdb_brand  TEXT,       -- Brand (LG, Samsung, ...)
  irdb_device TEXT,       -- Device type (TV, AC, ...)
  use_count   INTEGER     DEFAULT 0,     -- Số lần đã dùng (LRU tracking)
  last_used   TIMESTAMPTZ,
  created_at  TIMESTAMPTZ DEFAULT now()
);

-- ─────────────────────────────────────────────────────────────
-- 3. REMOTES – Layout remote ảo
-- ─────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS remotes (
  id          UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
  user_id     UUID        NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
  device_id   UUID        REFERENCES devices(id) ON DELETE SET NULL,
  name        TEXT        NOT NULL,
  description TEXT,
  icon        TEXT        DEFAULT '📺',
  columns     INTEGER     DEFAULT 4 CHECK (columns BETWEEN 1 AND 8),
  theme_color TEXT        DEFAULT '#4A90E2',
  created_at  TIMESTAMPTZ DEFAULT now(),
  updated_at  TIMESTAMPTZ DEFAULT now()
);

-- ─────────────────────────────────────────────────────────────
-- 4. BUTTONS – Nút bấm trên remote ảo (không giới hạn)
-- ─────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS buttons (
  id              UUID    PRIMARY KEY DEFAULT gen_random_uuid(),
  remote_id       UUID    NOT NULL REFERENCES remotes(id) ON DELETE CASCADE,
  ir_command_id   UUID    REFERENCES ir_commands(id) ON DELETE SET NULL,
  label           TEXT    NOT NULL,
  icon            TEXT,           -- Emoji hoặc icon name (material icons)
  color           TEXT    DEFAULT '#4A90E2',
  text_color      TEXT    DEFAULT '#FFFFFF',
  position_x      INTEGER NOT NULL CHECK (position_x >= 0),
  position_y      INTEGER NOT NULL CHECK (position_y >= 0),
  width           INTEGER DEFAULT 1 CHECK (width BETWEEN 1 AND 8),
  height          INTEGER DEFAULT 1 CHECK (height BETWEEN 1 AND 4),
  style           JSONB   DEFAULT '{}',  -- Custom CSS overrides
  created_at      TIMESTAMPTZ DEFAULT now()
);

-- ─────────────────────────────────────────────────────────────
-- 5. COMMAND_QUEUE – Hàng đợi lệnh real-time (Internet mode)
-- ─────────────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS command_queue (
  id          UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
  device_id   UUID        NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
  command_id  UUID        REFERENCES ir_commands(id) ON DELETE SET NULL,
  -- Payload đầy đủ để ESP32 phát không cần tra flash
  raw_data    INTEGER[],
  protocol    TEXT,
  status      TEXT        DEFAULT 'pending'
                          CHECK (status IN ('pending', 'sent', 'error', 'timeout')),
  error_msg   TEXT,
  created_at  TIMESTAMPTZ DEFAULT now(),
  processed_at TIMESTAMPTZ
);

-- =============================================================
-- INDEXES
-- =============================================================

CREATE INDEX IF NOT EXISTS idx_ir_commands_user    ON ir_commands(user_id);
CREATE INDEX IF NOT EXISTS idx_ir_commands_device  ON ir_commands(device_id);
CREATE INDEX IF NOT EXISTS idx_ir_commands_source  ON ir_commands(source);
CREATE INDEX IF NOT EXISTS idx_ir_commands_irdb    ON ir_commands(irdb_brand, irdb_device);
CREATE INDEX IF NOT EXISTS idx_ir_commands_lru     ON ir_commands(last_used ASC NULLS FIRST);

CREATE INDEX IF NOT EXISTS idx_remotes_user        ON remotes(user_id);
CREATE INDEX IF NOT EXISTS idx_remotes_device      ON remotes(device_id);

CREATE INDEX IF NOT EXISTS idx_buttons_remote      ON buttons(remote_id);
CREATE INDEX IF NOT EXISTS idx_buttons_command     ON buttons(ir_command_id);
CREATE INDEX IF NOT EXISTS idx_buttons_position    ON buttons(remote_id, position_y, position_x);

CREATE INDEX IF NOT EXISTS idx_command_queue_device  ON command_queue(device_id);
CREATE INDEX IF NOT EXISTS idx_command_queue_status  ON command_queue(status, created_at);

-- =============================================================
-- ROW LEVEL SECURITY
-- =============================================================

ALTER TABLE devices       ENABLE ROW LEVEL SECURITY;
ALTER TABLE ir_commands   ENABLE ROW LEVEL SECURITY;
ALTER TABLE remotes       ENABLE ROW LEVEL SECURITY;
ALTER TABLE buttons       ENABLE ROW LEVEL SECURITY;
ALTER TABLE command_queue ENABLE ROW LEVEL SECURITY;

-- DEVICES
CREATE POLICY "devices_owner" ON devices
  FOR ALL USING (auth.uid() = user_id)
  WITH CHECK (auth.uid() = user_id);

-- IR_COMMANDS
CREATE POLICY "ir_commands_owner" ON ir_commands
  FOR ALL USING (auth.uid() = user_id)
  WITH CHECK (auth.uid() = user_id);

-- REMOTES
CREATE POLICY "remotes_owner" ON remotes
  FOR ALL USING (auth.uid() = user_id)
  WITH CHECK (auth.uid() = user_id);

-- BUTTONS (truy cập qua remote ownership)
CREATE POLICY "buttons_owner" ON buttons
  FOR ALL USING (
    remote_id IN (
      SELECT id FROM remotes WHERE user_id = auth.uid()
    )
  )
  WITH CHECK (
    remote_id IN (
      SELECT id FROM remotes WHERE user_id = auth.uid()
    )
  );

-- COMMAND_QUEUE (truy cập qua device ownership)
CREATE POLICY "command_queue_owner" ON command_queue
  FOR ALL USING (
    device_id IN (
      SELECT id FROM devices WHERE user_id = auth.uid()
    )
  )
  WITH CHECK (
    device_id IN (
      SELECT id FROM devices WHERE user_id = auth.uid()
    )
  );

-- =============================================================
-- REALTIME – Bật cho các bảng cần real-time
-- =============================================================

ALTER PUBLICATION supabase_realtime ADD TABLE command_queue;
ALTER PUBLICATION supabase_realtime ADD TABLE devices;

-- =============================================================
-- FUNCTIONS & TRIGGERS
-- =============================================================

-- Tự động cập nhật updated_at
CREATE OR REPLACE FUNCTION update_updated_at()
RETURNS TRIGGER AS $$
BEGIN
  NEW.updated_at = now();
  RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER devices_updated_at
  BEFORE UPDATE ON devices
  FOR EACH ROW EXECUTE FUNCTION update_updated_at();

CREATE TRIGGER remotes_updated_at
  BEFORE UPDATE ON remotes
  FOR EACH ROW EXECUTE FUNCTION update_updated_at();

-- Tăng use_count khi lệnh được dùng
CREATE OR REPLACE FUNCTION increment_command_use(command_uuid UUID)
RETURNS VOID AS $$
BEGIN
  UPDATE ir_commands
  SET use_count = use_count + 1,
      last_used = now()
  WHERE id = command_uuid AND user_id = auth.uid();
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;

-- Dọn command_queue cũ (> 24h, đã xử lý)
CREATE OR REPLACE FUNCTION cleanup_command_queue()
RETURNS VOID AS $$
BEGIN
  DELETE FROM command_queue
  WHERE status IN ('sent', 'error', 'timeout')
    AND created_at < now() - INTERVAL '24 hours';
END;
$$ LANGUAGE plpgsql SECURITY DEFINER;
