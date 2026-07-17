/**
 * SmartRemote – storage.cpp
 * LittleFS-based storage với LRU cache và metadata index
 *
 * Cấu trúc file system:
 *   /commands/          ← Thư mục lệnh IR
 *     {id}.json         ← File lệnh đầy đủ (raw_data + metadata)
 *   /meta_index.json    ← Index nhẹ: id, name, useCount, lastUsed, synced
 *   /config.json        ← Cấu hình device (device_key, ...)
 */

#include "storage.h"
#include <esp_random.h>

// ─── Constructor ─────────────────────────────────────────────
Storage::Storage() : _lastHealthCheck(0) {}

// ─── begin() ─────────────────────────────────────────────────
bool Storage::begin() {
  if (!LittleFS.begin(true)) { // true = format nếu mount thất bại
    Serial.println("[Storage] LittleFS mount FAILED");
    return false;
  }

  // Tạo thư mục /commands nếu chưa có
  if (!LittleFS.exists("/commands")) {
    LittleFS.mkdir("/commands");
  }

  Serial.printf("[Storage] LittleFS OK – Used: %u / %u bytes (%.1f%%)\n",
    getUsedBytes(), getTotalBytes(), (float)getUsagePercent());
  return true;
}

// ─── saveCommand() ───────────────────────────────────────────
bool Storage::saveCommand(const IRCommand& cmd, bool synced) {
  // Kiểm tra dung lượng trước khi lưu
  if (getUsagePercent() >= STORAGE_EVICT_PCT) {
    Serial.println("[Storage] Flash >90%, evicting LRU before save...");
    evictLRU();
  }

  if (getCommandCount() >= STORAGE_MAX_COMMANDS) {
    Serial.println("[Storage] Max commands reached, evicting...");
    evictLRU();
  }

  // Xác định ID
  IRCommand toSave = cmd;
  if (toSave.id.isEmpty()) {
    toSave.id = _generateId();
  }

  // Lưu file JSON đầy đủ
  String path = _commandPath(toSave.id);
  File f = LittleFS.open(path, "w");
  if (!f) {
    Serial.printf("[Storage] Cannot open for write: %s\n", path.c_str());
    return false;
  }

  // Serialize sử dụng IRHandler::toJson
  String json = IRHandler::toJson(toSave);

  // Thêm trường synced vào JSON
  DynamicJsonDocument doc(4096);
  deserializeJson(doc, json);
  doc["synced"] = synced;
  doc["use_count"] = 0;
  doc["last_used"] = (uint32_t)0;

  serializeJson(doc, f);
  f.close();

  // Cập nhật meta index
  IRCommandMeta meta;
  meta.id       = toSave.id;
  meta.name     = toSave.name;
  meta.useCount = 0;
  meta.lastUsed = 0;
  meta.synced   = synced;
  _saveMeta(meta);

  Serial.printf("[Storage] Saved: %s (%s)\n", toSave.name.c_str(), toSave.id.c_str());
  return true;
}

// ─── loadCommand() ───────────────────────────────────────────
IRCommand Storage::loadCommand(const String& id) {
  String path = _commandPath(id);
  File f = LittleFS.open(path, "r");
  if (!f) {
    Serial.printf("[Storage] Command not found: %s\n", id.c_str());
    IRCommand empty; empty.valid = false;
    return empty;
  }

  String json = f.readString();
  f.close();

  IRCommand cmd = IRHandler::fromJson(json);
  
  // Cập nhật usage
  updateUsage(id);

  return cmd;
}

// ─── deleteCommand() ─────────────────────────────────────────
bool Storage::deleteCommand(const String& id) {
  String path = _commandPath(id);
  bool ok = LittleFS.remove(path);
  
  if (ok) {
    // Xóa khỏi meta index
    std::vector<IRCommandMeta> metas;
    _loadMetaIndex(metas);
    metas.erase(
      std::remove_if(metas.begin(), metas.end(),
        [&id](const IRCommandMeta& m) { return m.id == id; }),
      metas.end()
    );
    _saveMetaIndex(metas);
    Serial.printf("[Storage] Deleted: %s\n", id.c_str());
  }

  return ok;
}

// ─── commandExists() ─────────────────────────────────────────
bool Storage::commandExists(const String& id) {
  return LittleFS.exists(_commandPath(id));
}

// ─── listCommands() – Chỉ trả metadata ──────────────────────
std::vector<IRCommandMeta> Storage::listCommands() {
  std::vector<IRCommandMeta> metas;
  _loadMetaIndex(metas);
  return metas;
}

// ─── getUnsynced() ───────────────────────────────────────────
std::vector<IRCommand> Storage::getUnsynced() {
  std::vector<IRCommandMeta> metas;
  _loadMetaIndex(metas);

  std::vector<IRCommand> result;
  for (auto& meta : metas) {
    if (!meta.synced) {
      IRCommand cmd = loadCommand(meta.id);
      if (cmd.valid) result.push_back(cmd);
    }
  }
  return result;
}

// ─── markSynced() ────────────────────────────────────────────
bool Storage::markSynced(const String& id) {
  String path = _commandPath(id);
  File f = LittleFS.open(path, "r");
  if (!f) return false;

  DynamicJsonDocument doc(4096);
  deserializeJson(doc, f);
  f.close();

  doc["synced"] = true;

  f = LittleFS.open(path, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();

  // Cập nhật meta index
  std::vector<IRCommandMeta> metas;
  _loadMetaIndex(metas);
  for (auto& m : metas) {
    if (m.id == id) { m.synced = true; break; }
  }
  _saveMetaIndex(metas);

  return true;
}

// ─── updateUsage() ───────────────────────────────────────────
void Storage::updateUsage(const String& id) {
  // Cập nhật meta index
  std::vector<IRCommandMeta> metas;
  _loadMetaIndex(metas);

  uint32_t now = millis() / 1000;
  for (auto& m : metas) {
    if (m.id == id) {
      m.useCount++;
      m.lastUsed = now;
      break;
    }
  }
  _saveMetaIndex(metas);
}

// ─── Health ──────────────────────────────────────────────────
uint8_t  Storage::getUsagePercent() {
  uint32_t total = LittleFS.totalBytes();
  if (total == 0) return 0;
  return (uint8_t)((uint64_t)LittleFS.usedBytes() * 100 / total);
}
uint32_t Storage::getTotalBytes()   { return LittleFS.totalBytes(); }
uint32_t Storage::getUsedBytes()    { return LittleFS.usedBytes(); }
uint16_t Storage::getCommandCount() { return listCommands().size(); }

// ─── checkAndEvict() – Gọi mỗi 60s trong loop ───────────────
void Storage::checkAndEvict() {
  if (millis() - _lastHealthCheck < 60000) return;
  _lastHealthCheck = millis();

  uint8_t pct = getUsagePercent();
  Serial.printf("[Storage] Health check: %u%% used, %u commands\n",
    pct, getCommandCount());

  if (pct >= STORAGE_EVICT_PCT) {
    uint16_t evicted = evictLRU();
    Serial.printf("[Storage] Evicted %u LRU commands\n", evicted);
  } else if (pct >= STORAGE_WARN_PCT) {
    Serial.printf("[Storage] WARNING: Flash at %u%%\n", pct);
    // App sẽ được notify qua BLE/WebSocket status
  }
}

// ─── evictLRU() ──────────────────────────────────────────────
uint16_t Storage::evictLRU() {
  std::vector<IRCommandMeta> metas;
  _loadMetaIndex(metas);

  // Chỉ xóa lệnh đã sync lên cloud
  std::vector<IRCommandMeta> syncedMetas;
  for (auto& m : metas) {
    if (m.synced) syncedMetas.push_back(m);
  }

  if (syncedMetas.empty()) {
    Serial.println("[Storage] No synced commands to evict!");
    return 0;
  }

  // Sắp xếp theo lastUsed tăng dần (cũ nhất trước)
  std::sort(syncedMetas.begin(), syncedMetas.end(),
    [](const IRCommandMeta& a, const IRCommandMeta& b) {
      return a.lastUsed < b.lastUsed;
    }
  );

  // Xóa STORAGE_EVICT_RATIO (20%) lệnh cũ nhất
  uint16_t toEvict = max((uint16_t)1,
    (uint16_t)(syncedMetas.size() * STORAGE_EVICT_RATIO));

  uint16_t evicted = 0;
  for (uint16_t i = 0; i < toEvict && i < syncedMetas.size(); i++) {
    if (deleteCommand(syncedMetas[i].id)) evicted++;
  }

  return evicted;
}

// ─── Device config ───────────────────────────────────────────
String Storage::getDeviceKey() {
  String key = loadConfig("device_key");
  if (!key.isEmpty()) return key;

  // Sinh key từ MAC address
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char keyBuf[13];
  snprintf(keyBuf, sizeof(keyBuf), "%02X%02X%02X%02X%02X%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  key = String(keyBuf);
  saveConfig("device_key", key);
  return key;
}

bool Storage::saveConfig(const String& key, const String& value) {
  File f = LittleFS.open(STORAGE_CONFIG_FILE, "r");
  DynamicJsonDocument doc(1024);

  if (f) {
    deserializeJson(doc, f);
    f.close();
  }

  doc[key] = value;

  f = LittleFS.open(STORAGE_CONFIG_FILE, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

String Storage::loadConfig(const String& key, const String& defaultVal) {
  File f = LittleFS.open(STORAGE_CONFIG_FILE, "r");
  if (!f) return defaultVal;

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, f);
  f.close();

  if (!doc.containsKey(key)) return defaultVal;
  return doc[key].as<String>();
}

// ─── Private helpers ─────────────────────────────────────────
String Storage::_commandPath(const String& id) {
  return "/commands/" + id + ".json";
}

bool Storage::_saveMeta(const IRCommandMeta& meta) {
  std::vector<IRCommandMeta> metas;
  _loadMetaIndex(metas);

  // Update nếu đã có, thêm mới nếu chưa
  bool found = false;
  for (auto& m : metas) {
    if (m.id == meta.id) { m = meta; found = true; break; }
  }
  if (!found) metas.push_back(meta);

  return _saveMetaIndex(metas);
}

bool Storage::_loadMetaIndex(std::vector<IRCommandMeta>& out) {
  File f = LittleFS.open("/meta_index.json", "r");
  if (!f) return false;

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  JsonArray arr = doc["commands"].as<JsonArray>();
  for (JsonObject obj : arr) {
    IRCommandMeta m;
    m.id       = obj["id"].as<String>();
    m.name     = obj["name"].as<String>();
    m.useCount = obj["use_count"] | 0;
    m.lastUsed = obj["last_used"] | 0;
    m.synced   = obj["synced"] | false;
    out.push_back(m);
  }
  return true;
}

bool Storage::_saveMetaIndex(const std::vector<IRCommandMeta>& metas) {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("commands");

  for (auto& m : metas) {
    JsonObject obj = arr.createNestedObject();
    obj["id"]        = m.id;
    obj["name"]      = m.name;
    obj["use_count"] = m.useCount;
    obj["last_used"] = m.lastUsed;
    obj["synced"]    = m.synced;
  }

  File f = LittleFS.open("/meta_index.json", "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

String Storage::_generateId() {
  char buf[9];
  uint32_t rnd = esp_random();
  snprintf(buf, sizeof(buf), "%08X", rnd);
  return String(buf);
}
