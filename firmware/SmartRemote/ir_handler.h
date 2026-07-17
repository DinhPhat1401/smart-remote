/**
 * SmartRemote – ir_handler.h
 * Thu nhận và phát tín hiệu hồng ngoại
 * Thư viện: IRremoteESP8266
 */

#pragma once
#include <Arduino.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include "config.h"

// ─── Struct lưu một lệnh IR ──────────────────────────────────
struct IRCommand {
  String   id;            // UUID (từ Supabase hoặc local)
  String   name;          // Tên người dùng đặt
  decode_type_t protocol; // NEC, SONY, RAW, SAMSUNG, ...
  uint16_t address;
  uint16_t command;
  uint16_t rawData[IR_RAW_BUF_SIZE]; // Raw timing (microseconds)
  uint16_t rawLen;        // Số phần tử trong rawData
  uint32_t frequency;     // Carrier frequency (mặc định 38000 Hz)
  bool     valid;         // false nếu không có dữ liệu
};

// ─── Callback types ──────────────────────────────────────────
typedef std::function<void(IRCommand)> IRLearnCallback;
typedef std::function<void(String)>    IRErrorCallback;

class IRHandler {
public:
  IRHandler(uint8_t recvPin, uint8_t sendPin);

  void begin();
  void update(); // Gọi trong loop()

  // ─── Học lệnh ──────────────────────────────────────────────
  // Kích hoạt chế độ chờ tín hiệu từ TSOP1738
  void startLearn(IRLearnCallback onDone, IRErrorCallback onError = nullptr);
  void cancelLearn();
  bool isLearning() const { return _learning; }

  // ─── Phát lệnh ─────────────────────────────────────────────
  // Phát theo protocol (NEC, SONY, ...)
  bool sendProtocol(const IRCommand& cmd);

  // Phát RAW timing array (dùng cho BLE – không cần protocol)
  bool sendRaw(const uint16_t* rawData, uint16_t len,
               uint32_t freq = IR_CARRIER_FREQ * 1000);

  // Phát IRCommand (tự chọn sendProtocol vs sendRaw)
  bool send(const IRCommand& cmd);

  // ─── Helpers ────────────────────────────────────────────────
  static String protocolToString(decode_type_t protocol);
  static decode_type_t stringToProtocol(const String& str);

  // Chuyển IRCommand → JSON string (để lưu vào LittleFS/Supabase)
  static String toJson(const IRCommand& cmd);

  // Parse JSON → IRCommand
  static IRCommand fromJson(const String& json);

private:
  IRrecv   _recv;
  IRsend   _send;
  bool     _learning;
  uint32_t _learnStartMs;

  IRLearnCallback _onDone;
  IRErrorCallback _onError;

  void _processReceived(decode_results& results);
};
