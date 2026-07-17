/**
 * SmartRemote – ir_handler.cpp
 * Thu nhận và phát tín hiệu hồng ngoại
 */

#include "ir_handler.h"
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>

// ─── Constructor ─────────────────────────────────────────────
IRHandler::IRHandler(uint8_t recvPin, uint8_t sendPin)
  : _recv(recvPin, IR_RAW_BUF_SIZE, 50, true),
    _send(sendPin),
    _learning(false),
    _learnStartMs(0),
    _onDone(nullptr),
    _onError(nullptr) {}

// ─── begin() ─────────────────────────────────────────────────
void IRHandler::begin() {
  _send.begin();
  // Receiver sẽ được bật khi startLearn() gọi
  Serial.println("[IR] IRHandler initialized");
  Serial.printf("[IR] Recv pin: %d, Send pin: %d\n", PIN_IR_RECV, PIN_IR_SEND);
}

// ─── update() – Gọi trong loop() ────────────────────────────
void IRHandler::update() {
  if (!_learning) return;

  // Kiểm tra timeout
  if (millis() - _learnStartMs > IR_RECV_TIMEOUT_MS) {
    Serial.println("[IR] Learn timeout!");
    _recv.disableIRIn();
    _learning = false;
    if (_onError) _onError("timeout");
    return;
  }

  // Kiểm tra có tín hiệu không
  decode_results results;
  if (_recv.decode(&results)) {
    _processReceived(results);
    _recv.resume();
  }
}

// ─── startLearn() ────────────────────────────────────────────
void IRHandler::startLearn(IRLearnCallback onDone, IRErrorCallback onError) {
  if (_learning) {
    Serial.println("[IR] Already in learn mode");
    return;
  }

  _onDone       = onDone;
  _onError      = onError;
  _learning     = true;
  _learnStartMs = millis();

  _recv.enableIRIn();
  Serial.println("[IR] Learn mode started – waiting for IR signal...");
}

// ─── cancelLearn() ───────────────────────────────────────────
void IRHandler::cancelLearn() {
  if (_learning) {
    _recv.disableIRIn();
    _learning = false;
    Serial.println("[IR] Learn cancelled");
  }
}

// ─── _processReceived() ──────────────────────────────────────
void IRHandler::_processReceived(decode_results& results) {
  _recv.disableIRIn();
  _learning = false;

  IRCommand cmd;
  cmd.valid    = true;
  cmd.protocol = results.decode_type;
  cmd.address  = results.address;
  cmd.command  = results.command;
  cmd.frequency = IR_CARRIER_FREQ * 1000;

  // Luôn lưu cả raw data để đảm bảo phát lại chính xác
  if (results.rawlen > 0 && results.rawlen <= IR_RAW_BUF_SIZE) {
    cmd.rawLen = results.rawlen - 1; // rawbuf[0] là khoảng cách đầu
    for (uint16_t i = 0; i < cmd.rawLen; i++) {
      cmd.rawData[i] = results.rawbuf[i + 1] * RAWTICK;
    }
  } else {
    cmd.rawLen = 0;
  }

  Serial.printf("[IR] Received: protocol=%s address=0x%X command=0x%X rawLen=%d\n",
    typeToString(results.decode_type).c_str(),
    results.address,
    results.command,
    cmd.rawLen
  );

  if (_onDone) _onDone(cmd);
}

// ─── send() ──────────────────────────────────────────────────
bool IRHandler::send(const IRCommand& cmd) {
  if (!cmd.valid) {
    Serial.println("[IR] Invalid command – cannot send");
    return false;
  }

  // Ưu tiên phát RAW nếu có (chính xác nhất)
  if (cmd.rawLen > 0) {
    return sendRaw(cmd.rawData, cmd.rawLen, cmd.frequency);
  }

  return sendProtocol(cmd);
}

// ─── sendRaw() ───────────────────────────────────────────────
bool IRHandler::sendRaw(const uint16_t* rawData, uint16_t len, uint32_t freq) {
  if (!rawData || len == 0) {
    Serial.println("[IR] Empty raw data");
    return false;
  }

  Serial.printf("[IR] Sending RAW: %d pulses @ %u Hz\n", len, freq);
  _send.sendRaw(rawData, len, freq / 1000); // IRsend dùng kHz
  return true;
}

// ─── sendProtocol() ──────────────────────────────────────────
bool IRHandler::sendProtocol(const IRCommand& cmd) {
  Serial.printf("[IR] Sending protocol: %s addr=0x%X cmd=0x%X\n",
    typeToString(cmd.protocol).c_str(), cmd.address, cmd.command);

  switch (cmd.protocol) {
    case NEC:
      _send.sendNEC(cmd.address << 8 | cmd.command, 32);
      break;
    case SONY:
      _send.sendSony(cmd.command, 12, 3);
      break;
    case SAMSUNG:
      _send.sendSAMSUNG((uint64_t)cmd.address << 16 | cmd.command);
      break;
    case LG:
      _send.sendLG((uint64_t)cmd.address << 8 | cmd.command, 28);
      break;
    case PANASONIC:
      _send.sendPanasonic(cmd.address, cmd.command);
      break;
    case RC5:
      _send.sendRC5(cmd.command, 12);
      break;
    case RC6:
      _send.sendRC6(cmd.command, 20);
      break;
    default:
      Serial.printf("[IR] Unsupported protocol: %d\n", cmd.protocol);
      return false;
  }

  return true;
}

// ─── protocolToString() ──────────────────────────────────────
String IRHandler::protocolToString(decode_type_t protocol) {
  return typeToString(protocol);
}

decode_type_t IRHandler::stringToProtocol(const String& str) {
  String upper = str;
  upper.toUpperCase();
  if (upper == "NEC")       return NEC;
  if (upper == "SONY")      return SONY;
  if (upper == "SAMSUNG")   return SAMSUNG;
  if (upper == "LG")        return LG;
  if (upper == "PANASONIC") return PANASONIC;
  if (upper == "RC5")       return RC5;
  if (upper == "RC6")       return RC6;
  return UNKNOWN;
}

// ─── toJson() – IRCommand → JSON ─────────────────────────────
String IRHandler::toJson(const IRCommand& cmd) {
  DynamicJsonDocument doc(4096);

  doc["id"]        = cmd.id;
  doc["name"]      = cmd.name;
  doc["protocol"]  = typeToString(cmd.protocol);
  doc["address"]   = cmd.address;
  doc["command"]   = cmd.command;
  doc["frequency"] = cmd.frequency;
  doc["valid"]     = cmd.valid;

  JsonArray raw = doc.createNestedArray("raw_data");
  for (uint16_t i = 0; i < cmd.rawLen; i++) {
    raw.add(cmd.rawData[i]);
  }

  String output;
  serializeJson(doc, output);
  return output;
}

// ─── fromJson() – JSON → IRCommand ───────────────────────────
IRCommand IRHandler::fromJson(const String& json) {
  IRCommand cmd;
  cmd.valid = false;

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("[IR] JSON parse error: %s\n", err.c_str());
    return cmd;
  }

  cmd.id        = doc["id"].as<String>();
  cmd.name      = doc["name"].as<String>();
  cmd.protocol  = stringToProtocol(doc["protocol"].as<String>());
  cmd.address   = doc["address"] | 0;
  cmd.command   = doc["command"] | 0;
  cmd.frequency = doc["frequency"] | (uint32_t)(IR_CARRIER_FREQ * 1000);
  cmd.valid     = doc["valid"] | true;

  JsonArray raw = doc["raw_data"].as<JsonArray>();
  cmd.rawLen = 0;
  for (JsonVariant v : raw) {
    if (cmd.rawLen >= IR_RAW_BUF_SIZE) break;
    cmd.rawData[cmd.rawLen++] = v.as<uint16_t>();
  }

  return cmd;
}
