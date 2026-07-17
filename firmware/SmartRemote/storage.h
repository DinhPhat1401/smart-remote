/**
 * SmartRemote – storage.h
 * Lưu trữ lệnh IR trên LittleFS với LRU Cache management
 */

#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "ir_handler.h"

// Metadata nhẹ để quản lý LRU (không load full raw_data)
struct IRCommandMeta {
  String   id;
  String   name;
  uint32_t useCount;
  uint32_t lastUsed;  // Unix timestamp (seconds)
  bool     synced;    // Đã sync lên Supabase chưa
};

class Storage {
public:
  Storage();

  bool begin();

  // ─── CRUD lệnh IR ──────────────────────────────────────────
  bool       saveCommand(const IRCommand& cmd, bool synced = false);
  IRCommand  loadCommand(const String& id);
  bool       deleteCommand(const String& id);
  bool       commandExists(const String& id);

  // Lấy tất cả lệnh (chỉ metadata, không load raw_data)
  std::vector<IRCommandMeta> listCommands();

  // Lấy các lệnh chưa sync lên cloud
  std::vector<IRCommand> getUnsynced();

  // Đánh dấu đã sync
  bool markSynced(const String& id);

  // Cập nhật use_count và last_used
  void updateUsage(const String& id);

  // ─── Storage health ─────────────────────────────────────────
  uint8_t  getUsagePercent();   // % flash đã dùng
  uint32_t getTotalBytes();
  uint32_t getUsedBytes();
  uint16_t getCommandCount();

  // Tự động dọn nếu cần (gọi trong loop)
  void checkAndEvict();

  // Xóa 20% lệnh LRU nhất (đã sync)
  uint16_t evictLRU();

  // ─── Device config ──────────────────────────────────────────
  String  getDeviceKey();   // Tự generate nếu chưa có
  bool    saveConfig(const String& key, const String& value);
  String  loadConfig(const String& key, const String& defaultVal = "");

private:
  uint32_t _lastHealthCheck;

  // Đường dẫn file lệnh theo ID
  String _commandPath(const String& id);

  // Load/save metadata index (không chứa raw_data)
  bool _saveMeta(const IRCommandMeta& meta);
  bool _loadMetaIndex(std::vector<IRCommandMeta>& out);
  bool _saveMetaIndex(const std::vector<IRCommandMeta>& metas);

  // Sinh UUID ngắn (8 hex chars)
  String _generateId();
};
