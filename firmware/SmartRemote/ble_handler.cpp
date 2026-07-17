/**
 * SmartRemote – ble_handler.cpp
 * NimBLE GATT Server – BLE Low Energy
 *
 * Protocol (JSON qua BLE):
 *   App → ESP32 (IR_SEND char):
 *     {"type":"IR_SEND","protocol":"NEC","address":0x20,"command":0x08,
 *      "raw_data":[9000,4500,...],"frequency":38000}
 *
 *   App → ESP32 (LEARN char):
 *     0x01 = Start learn | 0x00 = Cancel
 *
 *   ESP32 → App (STATUS char, notify):
 *     {"type":"STATUS","mode":"bluetooth","flash_pct":12,"cmd_count":5,
 *      "connected":true,"fw":"1.0.0"}
 *
 *   ESP32 → App (STATUS char, learn result):
 *     {"type":"LEARN_RESULT","success":true,"name":"","id":"ABC123",
 *      "protocol":"NEC","address":32,"command":8}
 */

#include "ble_handler.h"
#include <ArduinoJson.h>

// ─── Constructor ─────────────────────────────────────────────
BLEHandler::BLEHandler(Storage& storage, IRHandler& ir)
  : _storage(storage), _ir(ir), _connected(false),
    _server(nullptr), _charStatus(nullptr),
    _charIRSend(nullptr), _charLearn(nullptr) {}

// ─── begin() ─────────────────────────────────────────────────
void BLEHandler::begin() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max TX power

  _server = NimBLEDevice::createServer();
  _server->setCallbacks(this);

  // Tạo GATT Service
  NimBLEService* svc = _server->createService(BLE_SERVICE_UUID);

  // Characteristic 1: STATUS (Notify → App)
  _charStatus = svc->createCharacteristic(
    BLE_CHAR_STATUS,
    NIMBLE_PROPERTY::NOTIFY
  );

  // Characteristic 2: IR_SEND (App → ESP32, Write)
  _charIRSend = svc->createCharacteristic(
    BLE_CHAR_IR_SEND,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  _charIRSend->setCallbacks(new IRSendCallback(this));

  // Characteristic 3: LEARN (App → ESP32, Write)
  _charLearn = svc->createCharacteristic(
    BLE_CHAR_LEARN,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  _charLearn->setCallbacks(new LearnCallback(this));

  svc->start();

  // Advertising
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->setMinInterval(0x20); // 20ms
  adv->setMaxInterval(0x40); // 40ms
  NimBLEDevice::startAdvertising();

  Serial.println("[BLE] GATT server started, advertising...");
  Serial.printf("[BLE] Device name: %s\n", BLE_DEVICE_NAME);
}

// ─── update() ────────────────────────────────────────────────
void BLEHandler::update() {
  // NimBLE xử lý async, không cần polling
  // Nhưng ta có thể gửi heartbeat status mỗi 5s nếu cần
}

// ─── notifyStatus() ──────────────────────────────────────────
void BLEHandler::notifyStatus(const String& statusJson) {
  if (!_connected || !_charStatus) return;

  // BLE MTU thường 512 bytes, chunk nếu dài hơn
  if (statusJson.length() <= BLE_MAX_PACKET_SIZE) {
    _charStatus->setValue(statusJson.c_str());
    _charStatus->notify();
  } else {
    Serial.println("[BLE] Status JSON too long to notify");
  }
}

// ─── notifyLearnResult() ─────────────────────────────────────
void BLEHandler::notifyLearnResult(const IRCommand& cmd, bool success) {
  DynamicJsonDocument doc(512);
  doc["type"]     = "LEARN_RESULT";
  doc["success"]  = success;

  if (success) {
    doc["id"]       = cmd.id;
    doc["name"]     = cmd.name;
    doc["protocol"] = IRHandler::protocolToString(cmd.protocol);
    doc["address"]  = cmd.address;
    doc["command"]  = cmd.command;
    doc["raw_len"]  = cmd.rawLen;
  }

  String json;
  serializeJson(doc, json);
  notifyStatus(json);
}

// ─── NimBLEServerCallbacks ───────────────────────────────────
void BLEHandler::onConnect(NimBLEServer* srv) {
  _connected = true;
  Serial.println("[BLE] Client connected");

  // Gửi status ngay khi kết nối
  notifyStatus(_buildStatusJson());
}

void BLEHandler::onDisconnect(NimBLEServer* srv) {
  _connected = false;
  Serial.println("[BLE] Client disconnected – restarting advertising");
  NimBLEDevice::startAdvertising();
}

// ─── _parsePacket() ──────────────────────────────────────────
IRCommand BLEHandler::_parsePacket(const std::string& data) {
  IRCommand cmd;
  cmd.valid = false;

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, data.c_str());
  if (err) {
    Serial.printf("[BLE] JSON parse error: %s\n", err.c_str());
    return cmd;
  }

  // Kiểm tra type
  String type = doc["type"] | "";
  if (type != "IR_SEND") return cmd;

  cmd.protocol  = IRHandler::stringToProtocol(doc["protocol"] | "RAW");
  cmd.address   = doc["address"] | 0;
  cmd.command   = doc["command"] | 0;
  cmd.frequency = doc["frequency"] | (uint32_t)(IR_CARRIER_FREQ * 1000);
  cmd.valid     = true;

  // Parse raw_data array
  cmd.rawLen = 0;
  if (doc.containsKey("raw_data")) {
    JsonArray raw = doc["raw_data"].as<JsonArray>();
    for (JsonVariant v : raw) {
      if (cmd.rawLen >= IR_RAW_BUF_SIZE) break;
      cmd.rawData[cmd.rawLen++] = v.as<uint16_t>();
    }
  }

  return cmd;
}

// ─── _buildStatusJson() ──────────────────────────────────────
String BLEHandler::_buildStatusJson() {
  DynamicJsonDocument doc(256);
  doc["type"]       = "STATUS";
  doc["mode"]       = "bluetooth";
  doc["flash_pct"]  = _storage.getUsagePercent();
  doc["cmd_count"]  = _storage.getCommandCount();
  doc["connected"]  = _connected;
  doc["fw"]         = FW_VERSION;

  String json;
  serializeJson(doc, json);
  return json;
}

// ─── IRSendCallback::onWrite() ───────────────────────────────
void IRSendCallback::onWrite(NimBLECharacteristic* chr) {
  std::string val = chr->getValue();
  if (val.empty()) return;

  Serial.printf("[BLE] IR_SEND received (%d bytes)\n", val.size());

  IRCommand cmd = _handler->_parsePacket(val);
  if (!cmd.valid) {
    Serial.println("[BLE] Invalid IR packet");
    return;
  }

  // Gọi callback → main sẽ phát IR
  if (_handler->_onCommand) {
    _handler->_onCommand(cmd);
  }
}

// ─── LearnCallback::onWrite() ────────────────────────────────
void LearnCallback::onWrite(NimBLECharacteristic* chr) {
  std::string val = chr->getValue();
  if (val.empty()) return;

  uint8_t byte0 = (uint8_t)val[0];
  bool startLearn = (byte0 == 0x01);

  Serial.printf("[BLE] LEARN trigger: %s\n", startLearn ? "START" : "CANCEL");

  if (_handler->_onLearn) {
    _handler->_onLearn(startLearn);
  }
}
