/**
 * SmartRemote – ble_handler.h
 * BLE GATT Server dùng NimBLE-Arduino
 * Giao tiếp với app qua 3 Characteristics: IR_SEND, STATUS, LEARN
 */

#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "config.h"
#include "ir_handler.h"
#include "storage.h"

typedef std::function<void(IRCommand)> BLECommandCb;
typedef std::function<void(bool)>      BLELearnCb;

class BLEHandler : public NimBLEServerCallbacks {
public:
  BLEHandler(Storage& storage, IRHandler& ir);

  void begin();
  void update();  // Gọi trong loop()

  // Gửi trạng thái JSON tới app (notify)
  void notifyStatus(const String& statusJson);

  // Gửi kết quả học lệnh (notify)
  void notifyLearnResult(const IRCommand& cmd, bool success);

  // Kiểm tra có client kết nối không
  bool isConnected() const { return _connected; }

  // Register callbacks
  void onCommand(BLECommandCb cb)  { _onCommand = cb; }
  void onLearn(BLELearnCb cb)      { _onLearn = cb; }

  // NimBLEServerCallbacks
  void onConnect(NimBLEServer* srv) override;
  void onDisconnect(NimBLEServer* srv) override;

private:
  Storage&    _storage;
  IRHandler&  _ir;
  bool        _connected;

  NimBLEServer*         _server;
  NimBLECharacteristic* _charStatus;    // Notify
  NimBLECharacteristic* _charIRSend;    // Write
  NimBLECharacteristic* _charLearn;     // Write

  BLECommandCb _onCommand;
  BLELearnCb   _onLearn;

  // Parse packet BLE → IRCommand
  IRCommand _parsePacket(const std::string& data);

  // Build status JSON
  String _buildStatusJson();
};

// ─── Characteristic callback classes ─────────────────────────
class IRSendCallback : public NimBLECharacteristicCallbacks {
public:
  IRSendCallback(BLEHandler* handler) : _handler(handler) {}
  void onWrite(NimBLECharacteristic* chr) override;
private:
  BLEHandler* _handler;
};

class LearnCallback : public NimBLECharacteristicCallbacks {
public:
  LearnCallback(BLEHandler* handler) : _handler(handler) {}
  void onWrite(NimBLECharacteristic* chr) override;
private:
  BLEHandler* _handler;
};
