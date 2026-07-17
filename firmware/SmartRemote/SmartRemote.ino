/**
 * SmartRemote – SmartRemote.ino
 * ─────────────────────────────────────────────────────────────
 * Main firmware – ESP32 IR Remote Controller
 *
 * Thư viện cần cài (Arduino Library Manager):
 *   - IRremoteESP8266 (by crankyoldgit)
 *   - ArduinoJson (by Benoit Blanchon) v6
 *   - NimBLE-Arduino (by h2zero)
 *   - ESPAsyncWebServer (by lacamera / me-no-dev)
 *   - AsyncTCP (by me-no-dev)
 *   - WiFiManager (by tzapu) – ESP32 version
 *   - WebSockets (by Markus Sattler)
 *   - LittleFS (built-in ESP32 Arduino Core)
 *
 * Board: ESP32 Dev Module | Partition: Default 4MB with SPIFFS
 * ─────────────────────────────────────────────────────────────
 */

#include <Arduino.h>
#include "config.h"
#include "led_controller.h"
#include "ir_handler.h"
#include "storage.h"
#include "wifi_manager.h"
#include "ble_handler.h"
#include "supabase_client.h"

// ─── Global instances ─────────────────────────────────────────
LEDController  led(PIN_LED_R, PIN_LED_G, PIN_LED_B);
IRHandler      ir(PIN_IR_RECV, PIN_IR_SEND);
Storage        storage;
WiFiManager_   wifiMgr(storage, ir);
BLEHandler     ble(storage, ir);
SupabaseClient* supabase = nullptr;

// ─── State ────────────────────────────────────────────────────
DeviceMode currentMode = MODE_IDLE;
DeviceMode pendingMode = MODE_WIFI; // Chế độ sau khi boot

// Button state
volatile bool  buttonPressed    = false;
volatile uint32_t buttonPressMs = 0;
bool           lastButtonState  = HIGH;

// ─── Timers ───────────────────────────────────────────────────
uint32_t lastStatusBroadcast = 0;
uint32_t lastCloudSync       = 0;
#define  STATUS_INTERVAL_MS   5000   // Broadcast status mỗi 5s
#define  CLOUD_SYNC_MS        30000  // Sync cloud mỗi 30s

// ─────────────────────────────────────────────────────────────
// BUTTON ISR
// ─────────────────────────────────────────────────────────────
void IRAM_ATTR buttonISR() {
  uint32_t now = millis();
  bool state = digitalRead(PIN_BUTTON);

  if (state == LOW && !buttonPressed) {
    // Button pressed down
    buttonPressed  = true;
    buttonPressMs  = now;
  }
  // Release handled in loop() để phân biệt short/long press
}

// ─────────────────────────────────────────────────────────────
// setMode() – Chuyển chế độ hoạt động
// ─────────────────────────────────────────────────────────────
void setMode(DeviceMode mode) {
  if (currentMode == mode) return;

  Serial.printf("[Main] Mode: %d → %d\n", currentMode, mode);
  currentMode = mode;
  led.setMode(mode);

  // Cập nhật Supabase status
  if (supabase && WiFi.isConnected()) {
    String modeStr = "wifi";
    if (mode == MODE_BLUETOOTH) modeStr = "bluetooth";
    else if (mode == MODE_SETUP) modeStr = "setup";
    supabase->updateDeviceStatus(true, modeStr, storage.getUsagePercent());
  }
}

// ─────────────────────────────────────────────────────────────
// onIRReceived() – Callback khi học xong lệnh IR
// ─────────────────────────────────────────────────────────────
void onIRReceived(IRCommand cmd) {
  Serial.printf("[Main] IR learned: protocol=%s rawLen=%d\n",
    IRHandler::protocolToString(cmd.protocol).c_str(), cmd.rawLen);

  // Lưu vào flash (chưa sync)
  if (storage.saveCommand(cmd, false)) {
    // LED: đỏ solid 5s
    setMode(MODE_LEARN_DONE);

    // Notify qua BLE nếu đang kết nối
    if (ble.isConnected()) {
      ble.notifyLearnResult(cmd, true);
    }

    // Broadcast qua WiFi WebSocket
    if (WiFi.isConnected()) {
      wifiMgr.broadcastStatus();
    }

    // Sync lên Supabase nếu có WiFi
    if (supabase && WiFi.isConnected()) {
      supabase->uploadCommand(cmd);
      storage.markSynced(cmd.id);
    }
  }

  // Sau 5s, quay về mode trước
  // (led_controller tự tắt sau LED_LEARN_DONE_MS)
  delay(LED_LEARN_DONE_MS);
  setMode(pendingMode);
}

void onIRLearnError(String error) {
  Serial.printf("[Main] IR learn error: %s\n", error.c_str());
  led.blinkFast(LEDColor::RED); // 3 chớp nhanh báo lỗi
  delay(1500);
  setMode(pendingMode);

  if (ble.isConnected()) {
    IRCommand empty; empty.valid = false;
    ble.notifyLearnResult(empty, false);
  }
}

// ─────────────────────────────────────────────────────────────
// onCommandReceived() – Nhận lệnh phát IR (BLE / WiFi)
// ─────────────────────────────────────────────────────────────
void onCommandReceived(IRCommand cmd) {
  Serial.printf("[Main] IR command received, rawLen=%d\n", cmd.rawLen);

  // Chớp LED ngắn để báo đang phát
  led.blink(currentMode == MODE_BLUETOOTH ? LEDColor::BLUE : LEDColor::GREEN, 100);

  bool ok = ir.send(cmd);

  // Restore LED
  led.setMode(currentMode);

  if (!ok) {
    Serial.println("[Main] IR send failed");
  }
}

// ─────────────────────────────────────────────────────────────
// onLearnRequest() – Trigger học lệnh từ app
// ─────────────────────────────────────────────────────────────
void onLearnRequest(bool startLearn) {
  if (startLearn) {
    Serial.println("[Main] Learn mode requested by app");
    setMode(MODE_LEARN_WAIT);
    ir.startLearn(onIRReceived, onIRLearnError);
  } else {
    ir.cancelLearn();
    setMode(pendingMode);
  }
}

// ─────────────────────────────────────────────────────────────
// handleButton() – Gọi trong loop()
// ─────────────────────────────────────────────────────────────
void handleButton() {
  bool state = digitalRead(PIN_BUTTON);

  // Phát hiện nhả button
  if (buttonPressed && state == HIGH) {
    uint32_t duration = millis() - buttonPressMs;
    buttonPressed = false;

    if (duration >= BUTTON_LONG_PRESS_MS) {
      // Long press → Reset WiFi
      Serial.println("[Button] Long press → Reset WiFi credentials");
      led.blinkFast(LEDColor::WHITE);
      delay(1000);
      wifiMgr.resetWifi(); // → Restart + AP Mode
    }
    else if (duration >= BUTTON_DEBOUNCE_MS) {
      // Short press → Toggle BLE ↔ WiFi
      if (currentMode == MODE_BLUETOOTH) {
        pendingMode = MODE_WIFI;
        setMode(MODE_WIFI);
        Serial.println("[Button] Short press → WiFi mode");
      } else {
        pendingMode = MODE_BLUETOOTH;
        setMode(MODE_BLUETOOTH);
        Serial.println("[Button] Short press → Bluetooth mode");
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────
// syncUnsynced() – Đẩy các lệnh chưa sync lên Supabase
// ─────────────────────────────────────────────────────────────
void syncUnsynced() {
  if (!supabase || !WiFi.isConnected()) return;

  auto unsynced = storage.getUnsynced();
  if (unsynced.empty()) return;

  Serial.printf("[Sync] Uploading %d unsynced commands...\n", unsynced.size());
  for (auto& cmd : unsynced) {
    if (supabase->uploadCommand(cmd)) {
      storage.markSynced(cmd.id);
    }
  }
}

// ─────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n============================");
  Serial.println(" SmartRemote v" FW_VERSION);
  Serial.println("============================");

  // 1. LED – Khởi động trước để show feedback
  led.begin();
  led.blinkSlow(LEDColor::WHITE); // Báo đang boot

  // 2. Storage (LittleFS)
  if (!storage.begin()) {
    Serial.println("[Main] Storage FAILED – halting");
    led.solid(LEDColor::RED);
    while(true) delay(1000);
  }

  // 3. IR Handler
  ir.begin();

  // 4. Button
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), buttonISR, CHANGE);

  // 5. Lấy device key
  String deviceKey = storage.getDeviceKey();
  Serial.printf("[Main] Device Key: %s\n", deviceKey.c_str());

  // 6. WiFi Manager
  led.blinkSlow(LEDColor::WHITE); // Đang kết nối
  setMode(MODE_SETUP);
  wifiMgr.begin(deviceKey);

  // 7. Sau khi có WiFi → start WebServer
  wifiMgr.startServer();

  // Register WiFi callbacks
  wifiMgr.onCommandReceived(onCommandReceived);
  wifiMgr.onLearnRequest(onLearnRequest);

  // 8. BLE – Always running cùng WiFi
  ble.begin();
  ble.onCommand(onCommandReceived);
  ble.onLearn(onLearnRequest);

  // 9. Supabase – Chỉ khi có WiFi
  String deviceId = storage.loadConfig("supabase_device_id");
  if (!deviceId.isEmpty()) {
    supabase = new SupabaseClient(deviceId, deviceKey);
    supabase->begin();
    supabase->subscribeRealtime();
    supabase->onCloudCommand(onCommandReceived);
  } else {
    Serial.println("[Main] No Supabase device_id – cloud disabled");
    Serial.println("[Main] Pair device via app to enable cloud control");
  }

  // 10. Default mode
  pendingMode = MODE_WIFI;
  setMode(MODE_WIFI);

  Serial.println("[Main] Setup complete!");
}

// ─────────────────────────────────────────────────────────────
// loop()
// ─────────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  // LED pattern update
  led.update();

  // IR learning update (timeout check)
  ir.update();

  // Button handling
  handleButton();

  // WiFi WebSocket cleanup
  if (WiFi.isConnected()) {
    wifiMgr.update();
  }

  // Supabase Realtime (WebSocket loop)
  if (supabase && WiFi.isConnected()) {
    supabase->update();
  }

  // Broadcast status mỗi 5s
  if (now - lastStatusBroadcast > STATUS_INTERVAL_MS) {
    lastStatusBroadcast = now;

    // Broadcast qua WiFi WebSocket
    if (WiFi.isConnected()) {
      wifiMgr.broadcastStatus();
    }

    // Notify qua BLE nếu có client
    if (ble.isConnected()) {
      DynamicJsonDocument doc(256);
      doc["type"]      = "STATUS";
      doc["mode"]      = (currentMode == MODE_BLUETOOTH) ? "bluetooth" : "wifi";
      doc["flash_pct"] = storage.getUsagePercent();
      doc["cmd_count"] = storage.getCommandCount();
      doc["wifi"]      = WiFi.isConnected();
      String json;
      serializeJson(doc, json);
      ble.notifyStatus(json);
    }
  }

  // Storage health check (dọn LRU nếu cần)
  storage.checkAndEvict();

  // Sync unsynced commands lên cloud mỗi 30s
  if (now - lastCloudSync > CLOUD_SYNC_MS) {
    lastCloudSync = now;
    syncUnsynced();
  }

  // Cập nhật device status lên Supabase mỗi 30s
  if (supabase && WiFi.isConnected() && (now - lastCloudSync < 1000)) {
    String mode = (currentMode == MODE_BLUETOOTH) ? "bluetooth" : "wifi";
    supabase->updateDeviceStatus(true, mode, storage.getUsagePercent());
  }
}
