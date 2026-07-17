/**
 * SmartRemote – config.h
 * Cấu hình toàn cục: GPIO, BLE UUIDs, Supabase, limits
 */

#pragma once

// ─────────────────────────────────────────────────────────────
// GPIO Pin Definitions
// ─────────────────────────────────────────────────────────────
#define PIN_IR_RECV       15    // TSOP1738 DATA pin
#define PIN_IR_SEND       4     // IR LED (qua transistor NPN)
#define PIN_LED_R         25    // RGB LED – Red   (220Ω)
#define PIN_LED_G         26    // RGB LED – Green (220Ω)
#define PIN_LED_B         27    // RGB LED – Blue  (220Ω)
#define PIN_BUTTON        0     // Boot button – có pull-up nội

// ─────────────────────────────────────────────────────────────
// Button Timing
// ─────────────────────────────────────────────────────────────
#define BUTTON_DEBOUNCE_MS    50
#define BUTTON_LONG_PRESS_MS  3000   // Giữ 3s → Reset WiFi
#define BUTTON_SHORT_MAX_MS   800    // < 800ms → Short press

// ─────────────────────────────────────────────────────────────
// Flash / LittleFS Storage
// ─────────────────────────────────────────────────────────────
#define STORAGE_COMMANDS_FILE   "/commands.json"
#define STORAGE_CONFIG_FILE     "/config.json"
#define STORAGE_WARN_PCT        80   // % → Cảnh báo
#define STORAGE_EVICT_PCT       90   // % → Tự động xóa LRU
#define STORAGE_EVICT_RATIO     0.20 // Xóa 20% lệnh ít dùng nhất
#define STORAGE_MAX_COMMANDS    200  // Tối đa 200 lệnh trên flash

// ─────────────────────────────────────────────────────────────
// BLE – GATT Service & Characteristics
// ─────────────────────────────────────────────────────────────
#define BLE_DEVICE_NAME         "SmartRemote"
#define BLE_SERVICE_UUID        "12345678-0001-0000-0000-000000000001"
// Write: Nhận lệnh IR để phát (RAW data + metadata)
#define BLE_CHAR_IR_SEND        "12345678-0001-0000-0000-000000000002"
// Notify: Gửi trạng thái về app (mode, flash%, learn status)
#define BLE_CHAR_STATUS         "12345678-0001-0000-0000-000000000003"
// Write: Trigger học lệnh IR (1 byte: 0x01=start, 0x00=cancel)
#define BLE_CHAR_LEARN          "12345678-0001-0000-0000-000000000004"

#define BLE_MAX_PACKET_SIZE     512  // MTU tối đa bytes

// ─────────────────────────────────────────────────────────────
// WebServer (WiFi Mode)
// ─────────────────────────────────────────────────────────────
#define HTTP_PORT               80
#define WS_PATH                 "/ws"
#define HTTP_AUTH_HEADER        "X-Device-Key"

// ─────────────────────────────────────────────────────────────
// Supabase Cloud
// ─────────────────────────────────────────────────────────────
// !!! Thay bằng thông tin Supabase project của bạn !!!
#define SUPABASE_URL            "https://your-project.supabase.co"
#define SUPABASE_ANON_KEY       "your-anon-key-here"
#define SUPABASE_REALTIME_URL   "wss://your-project.supabase.co/realtime/v1/websocket"

// Thời gian timeout HTTP request
#define SUPABASE_TIMEOUT_MS     10000

// ─────────────────────────────────────────────────────────────
// IR Settings
// ─────────────────────────────────────────────────────────────
#define IR_RECV_TIMEOUT_MS      5000  // Timeout chờ tín hiệu khi học
#define IR_RAW_BUF_SIZE         1024  // Buffer raw IR timing
#define IR_CARRIER_FREQ         38    // kHz

// ─────────────────────────────────────────────────────────────
// LED Timing
// ─────────────────────────────────────────────────────────────
#define LED_BLINK_INTERVAL_MS   500   // Chớp học lệnh
#define LED_BLINK_FAST_MS       150   // Chớp nhanh (kết nối)
#define LED_BLINK_SLOW_MS       1000  // Chớp chậm (AP Mode)
#define LED_LEARN_DONE_MS       5000  // Sáng đỏ 5s sau khi học xong

// ─────────────────────────────────────────────────────────────
// Firmware
// ─────────────────────────────────────────────────────────────
#define FW_VERSION              "1.0.0"
#define DEVICE_MODEL            "SmartRemote-ESP32"

// ─────────────────────────────────────────────────────────────
// State Machine
// ─────────────────────────────────────────────────────────────
enum DeviceMode {
  MODE_IDLE        = 0,
  MODE_BLUETOOTH   = 1,
  MODE_WIFI        = 2,
  MODE_INTERNET    = 3,
  MODE_LEARN_WAIT  = 4,  // Đang chờ tín hiệu IR
  MODE_LEARN_DONE  = 5,  // Đã học xong, hiển thị đèn đỏ
  MODE_SETUP       = 6,  // WiFiManager AP Mode
  MODE_OTA         = 7,  // OTA Firmware Update
};
