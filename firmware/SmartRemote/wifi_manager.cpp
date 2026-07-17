/**
 * SmartRemote – wifi_manager.cpp
 * WiFi, HTTP REST API, WebSocket real-time
 *
 * Endpoints:
 *   GET  /status          → trạng thái device (JSON)
 *   GET  /commands        → danh sách lệnh IR trên flash
 *   POST /send            → phát lệnh IR
 *   POST /learn           → bật/tắt chế độ học lệnh
 *   WS   /ws              → WebSocket real-time
 */

#include "wifi_manager.h"

// ─── Constructor ─────────────────────────────────────────────
WiFiManager_::WiFiManager_(Storage& storage, IRHandler& ir)
  : _server(HTTP_PORT),
    _ws(WS_PATH),
    _storage(storage),
    _ir(ir) {}

// ─── begin() – WiFiManager setup ─────────────────────────────
bool WiFiManager_::begin(const String& deviceKey) {
  _deviceKey = deviceKey;

  ::WiFiManager wm;
  wm.setConfigPortalTimeout(180); // 3 phút timeout portal
  wm.setAPName(("SmartRemote-" + deviceKey.substring(0, 4)).c_str());
  wm.setAPPassword("smartremote123");

  // Custom parameter: Supabase device key (hiển thị trong portal)
  WiFiManagerParameter keyParam("device_key", "Device Key (read-only)",
                                deviceKey.c_str(), 20);
  wm.addParameter(&keyParam);

  Serial.println("[WiFi] Starting WiFiManager...");
  if (!wm.autoConnect()) {
    Serial.println("[WiFi] Config timeout – restarting");
    ESP.restart();
    return false;
  }

  Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

// ─── startServer() ───────────────────────────────────────────
void WiFiManager_::startServer() {
  _setupRoutes();

  // WebSocket event handler
  _ws.onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                     AwsEventType t, void* a, uint8_t* d, size_t l) {
    _onWsEvent(s, c, t, a, d, l);
  });
  _server.addHandler(&_ws);

  // CORS headers cho mọi response
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods",
                                       "GET, POST, PUT, DELETE, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
                                       "Content-Type, X-Device-Key");

  _server.begin();
  Serial.printf("[WiFi] Server started on http://%s:%d\n",
    WiFi.localIP().toString().c_str(), HTTP_PORT);
}

// ─── update() ────────────────────────────────────────────────
void WiFiManager_::update() {
  _ws.cleanupClients(); // Dọn WS clients đã đóng
}

// ─── broadcastStatus() ───────────────────────────────────────
void WiFiManager_::broadcastStatus() {
  String json = _buildStatusJson();
  _ws.textAll(json);
}

bool WiFiManager_::isConnected() const { return WiFi.isConnected(); }
String WiFiManager_::getIP() const     { return WiFi.localIP().toString(); }

void WiFiManager_::resetWifi() {
  ::WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

// ─── _setupRoutes() ──────────────────────────────────────────
void WiFiManager_::_setupRoutes() {

  // OPTIONS preflight
  _server.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
    req->send(204);
  });

  // GET /status
  _server.on("/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
    _handleStatus(req);
  });

  // GET /commands
  _server.on("/commands", HTTP_GET, [this](AsyncWebServerRequest* req) {
    _handleCommandsList(req);
  });

  // POST /send – Body: {id} hoặc {raw_data, protocol, address, command}
  AsyncCallbackJsonWebHandler* sendHandler =
    new AsyncCallbackJsonWebHandler("/send",
      [this](AsyncWebServerRequest* req, JsonVariant& body) {
        _handleSend(req, body);
      });
  _server.addHandler(sendHandler);

  // POST /learn – Body: {active: true|false}
  AsyncCallbackJsonWebHandler* learnHandler =
    new AsyncCallbackJsonWebHandler("/learn",
      [this](AsyncWebServerRequest* req, JsonVariant& body) {
        _handleLearn(req, body);
      });
  _server.addHandler(learnHandler);

  // 404
  _server.onNotFound([this](AsyncWebServerRequest* req) {
    _handleNotFound(req);
  });
}

// ─── _handleStatus() ─────────────────────────────────────────
void WiFiManager_::_handleStatus(AsyncWebServerRequest* req) {
  String json = _buildStatusJson();
  req->send(200, "application/json", json);
}

// ─── _handleSend() ───────────────────────────────────────────
void WiFiManager_::_handleSend(AsyncWebServerRequest* req, JsonVariant& body) {
  if (!_isAuthorized(req)) {
    req->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return;
  }

  IRCommand cmd;
  cmd.valid = false;

  // Trường hợp 1: Gửi theo ID (tra flash)
  if (body.containsKey("id")) {
    String id = body["id"].as<String>();
    cmd = _storage.loadCommand(id);
    if (!cmd.valid) {
      req->send(404, "application/json", "{\"error\":\"Command not found\"}");
      return;
    }
  }
  // Trường hợp 2: Gửi raw data trực tiếp
  else if (body.containsKey("raw_data")) {
    cmd.protocol  = IRHandler::stringToProtocol(body["protocol"] | "RAW");
    cmd.address   = body["address"] | 0;
    cmd.command   = body["command"] | 0;
    cmd.frequency = body["frequency"] | (uint32_t)(IR_CARRIER_FREQ * 1000);
    cmd.valid     = true;

    JsonArray raw = body["raw_data"].as<JsonArray>();
    cmd.rawLen = 0;
    for (JsonVariant v : raw) {
      if (cmd.rawLen >= IR_RAW_BUF_SIZE) break;
      cmd.rawData[cmd.rawLen++] = v.as<uint16_t>();
    }
  }
  else {
    req->send(400, "application/json", "{\"error\":\"Missing id or raw_data\"}");
    return;
  }

  // Phát IR
  bool ok = _ir.send(cmd);
  if (ok) {
    req->send(200, "application/json", "{\"success\":true}");
    broadcastStatus(); // Notify WS clients
  } else {
    req->send(500, "application/json", "{\"error\":\"IR send failed\"}");
  }
}

// ─── _handleLearn() ──────────────────────────────────────────
void WiFiManager_::_handleLearn(AsyncWebServerRequest* req, JsonVariant& body) {
  if (!_isAuthorized(req)) {
    req->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
    return;
  }

  bool active = body["active"] | true;
  if (_onLearn) _onLearn(active);

  req->send(200, "application/json",
    active ? "{\"success\":true,\"status\":\"learning\"}"
           : "{\"success\":true,\"status\":\"cancelled\"}");
}

// ─── _handleCommandsList() ───────────────────────────────────
void WiFiManager_::_handleCommandsList(AsyncWebServerRequest* req) {
  auto metas = _storage.listCommands();

  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("commands");
  for (auto& m : metas) {
    JsonObject obj = arr.createNestedObject();
    obj["id"]        = m.id;
    obj["name"]      = m.name;
    obj["use_count"] = m.useCount;
    obj["synced"]    = m.synced;
  }
  doc["count"]      = metas.size();
  doc["flash_pct"]  = _storage.getUsagePercent();

  String json;
  serializeJson(doc, json);
  req->send(200, "application/json", json);
}

// ─── _handleNotFound() ───────────────────────────────────────
void WiFiManager_::_handleNotFound(AsyncWebServerRequest* req) {
  req->send(404, "application/json", "{\"error\":\"Not found\"}");
}

// ─── _onWsEvent() ────────────────────────────────────────────
void WiFiManager_::_onWsEvent(AsyncWebSocket* server,
                               AsyncWebSocketClient* client,
                               AwsEventType type, void* arg,
                               uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("[WS] Client #%u connected from %s\n",
        client->id(), client->remoteIP().toString().c_str());
      // Gửi status ngay khi connect
      client->text(_buildStatusJson());
      break;

    case WS_EVT_DISCONNECT:
      Serial.printf("[WS] Client #%u disconnected\n", client->id());
      break;

    case WS_EVT_DATA: {
      AwsFrameInfo* info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && info->len == len
          && info->opcode == WS_TEXT) {
        String msg = String((char*)data, len);
        _handleWsMessage(client, msg);
      }
      break;
    }

    case WS_EVT_ERROR:
      Serial.printf("[WS] Error #%u\n", client->id());
      break;

    default:
      break;
  }
}

// ─── _handleWsMessage() ──────────────────────────────────────
void WiFiManager_::_handleWsMessage(AsyncWebSocketClient* client,
                                     const String& msg) {
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, msg) != DeserializationError::Ok) {
    client->text("{\"error\":\"Invalid JSON\"}");
    return;
  }

  String type = doc["type"] | "";

  if (type == "IR_SEND") {
    // Phát IR theo id hoặc raw_data
    IRCommand cmd;
    cmd.valid = false;

    if (doc.containsKey("id")) {
      cmd = _storage.loadCommand(doc["id"].as<String>());
    } else if (doc.containsKey("raw_data")) {
      cmd.valid = true;
      JsonArray raw = doc["raw_data"].as<JsonArray>();
      cmd.rawLen = 0;
      for (JsonVariant v : raw) {
        if (cmd.rawLen >= IR_RAW_BUF_SIZE) break;
        cmd.rawData[cmd.rawLen++] = v.as<uint16_t>();
      }
      cmd.frequency = doc["frequency"] | (uint32_t)(IR_CARRIER_FREQ * 1000);
    }

    if (cmd.valid && _ir.send(cmd)) {
      client->text("{\"type\":\"IR_SENT\",\"success\":true}");
    } else {
      client->text("{\"type\":\"IR_SENT\",\"success\":false}");
    }

  } else if (type == "LEARN_START") {
    if (_onLearn) _onLearn(true);
    client->text("{\"type\":\"LEARN_STARTED\"}");

  } else if (type == "LEARN_CANCEL") {
    if (_onLearn) _onLearn(false);
    client->text("{\"type\":\"LEARN_CANCELLED\"}");

  } else if (type == "GET_STATUS") {
    client->text(_buildStatusJson());

  } else {
    client->text("{\"error\":\"Unknown message type\"}");
  }
}

// ─── _isAuthorized() ─────────────────────────────────────────
bool WiFiManager_::_isAuthorized(AsyncWebServerRequest* req) {
  // Kiểm tra header X-Device-Key
  if (req->hasHeader(HTTP_AUTH_HEADER)) {
    String key = req->header(HTTP_AUTH_HEADER);
    return key == _deviceKey;
  }
  // Cho phép từ local network không cần key (LAN mode)
  // TODO: Thêm option strict auth nếu cần
  return true;
}

// ─── _buildStatusJson() ──────────────────────────────────────
String WiFiManager_::_buildStatusJson(const String& mode) {
  DynamicJsonDocument doc(512);
  doc["type"]        = "STATUS";
  doc["ip"]          = WiFi.localIP().toString();
  doc["rssi"]        = WiFi.RSSI();
  doc["flash_pct"]   = _storage.getUsagePercent();
  doc["cmd_count"]   = _storage.getCommandCount();
  doc["fw_version"]  = FW_VERSION;
  doc["uptime_ms"]   = millis();
  if (!mode.isEmpty()) doc["mode"] = mode;

  String json;
  serializeJson(doc, json);
  return json;
}
