/**
 * SmartRemote – supabase_client.cpp
 * Supabase REST API + Realtime WebSocket
 */

#include "supabase_client.h"

// ─── Constructor ─────────────────────────────────────────────
SupabaseClient::SupabaseClient(const String& deviceId, const String& deviceKey)
  : _deviceId(deviceId), _deviceKey(deviceKey), _wsConnected(false) {}

// ─── begin() ─────────────────────────────────────────────────
void SupabaseClient::begin() {
  Serial.println("[Supabase] Client initialized");
  Serial.printf("[Supabase] Device ID: %s\n", _deviceId.c_str());
}

// ─── update() ────────────────────────────────────────────────
void SupabaseClient::update() {
  _ws.loop(); // Xử lý Realtime WebSocket events
}

// ─── uploadCommand() ─────────────────────────────────────────
bool SupabaseClient::uploadCommand(const IRCommand& cmd) {
  DynamicJsonDocument doc(4096);
  doc["device_id"] = _deviceId;
  doc["name"]      = cmd.name;
  doc["protocol"]  = IRHandler::protocolToString(cmd.protocol);
  doc["address"]   = cmd.address;
  doc["command"]   = cmd.command;
  doc["frequency"] = cmd.frequency;
  doc["source"]    = "learned";

  JsonArray raw = doc.createNestedArray("raw_data");
  for (uint16_t i = 0; i < cmd.rawLen; i++) {
    raw.add(cmd.rawData[i]);
  }

  String body;
  serializeJson(doc, body);

  String response;
  int code = _httpPost("/rest/v1/ir_commands", body, response);

  if (code == 201) {
    Serial.printf("[Supabase] Command uploaded: %s\n", cmd.name.c_str());
    return true;
  }

  Serial.printf("[Supabase] Upload failed: %d – %s\n", code, response.c_str());
  return false;
}

// ─── fetchCommand() ──────────────────────────────────────────
IRCommand SupabaseClient::fetchCommand(const String& id) {
  IRCommand empty; empty.valid = false;

  String path = "/rest/v1/ir_commands?id=eq." + id + "&select=*";
  String response;
  int code = _httpGet(path, response);

  if (code != 200) {
    Serial.printf("[Supabase] Fetch failed: %d\n", code);
    return empty;
  }

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, response) != DeserializationError::Ok) return empty;

  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() == 0) return empty;

  JsonObject obj = arr[0];
  IRCommand cmd;
  cmd.id       = obj["id"].as<String>();
  cmd.name     = obj["name"].as<String>();
  cmd.protocol = IRHandler::stringToProtocol(obj["protocol"] | "RAW");
  cmd.address  = obj["address"] | 0;
  cmd.command  = obj["command"] | 0;
  cmd.frequency = obj["frequency"] | (uint32_t)(IR_CARRIER_FREQ * 1000);
  cmd.valid    = true;

  JsonArray raw = obj["raw_data"].as<JsonArray>();
  cmd.rawLen = 0;
  for (JsonVariant v : raw) {
    if (cmd.rawLen >= IR_RAW_BUF_SIZE) break;
    cmd.rawData[cmd.rawLen++] = v.as<uint16_t>();
  }

  return cmd;
}

// ─── updateDeviceStatus() ────────────────────────────────────
bool SupabaseClient::updateDeviceStatus(bool online, const String& mode,
                                         uint8_t flashPct) {
  DynamicJsonDocument doc(256);
  doc["online"]     = online;
  doc["mode"]       = mode;
  doc["flash_pct"]  = flashPct;
  doc["fw_version"] = FW_VERSION;
  doc["last_seen"]  = "now()";

  String body;
  serializeJson(doc, body);

  String path = "/rest/v1/devices?device_key=eq." + _deviceKey;
  String response;
  int code = _httpPatch(path, body, response);

  return (code == 200 || code == 204);
}

// ─── subscribeRealtime() ─────────────────────────────────────
void SupabaseClient::subscribeRealtime() {
  if (_wsConnected) return;

  // Supabase Realtime WebSocket URL
  // Format: wss://{project}.supabase.co/realtime/v1/websocket?apikey={key}&vsn=1.0.0
  String host = String(SUPABASE_URL).substring(8); // Remove "https://"
  String path = "/realtime/v1/websocket?apikey=" +
                String(SUPABASE_ANON_KEY) + "&vsn=1.0.0";

  _ws.beginSSL(host.c_str(), 443, path.c_str());
  _ws.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    _onWsEvent(type, payload, length);
  });
  _ws.setReconnectInterval(5000);

  Serial.println("[Supabase] Connecting to Realtime...");
}

void SupabaseClient::unsubscribeRealtime() {
  _ws.disconnect();
  _wsConnected = false;
}

// ─── _onWsEvent() ────────────────────────────────────────────
void SupabaseClient::_onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      _wsConnected = true;
      Serial.println("[Supabase] Realtime connected");
      // Gửi subscribe message
      _ws.sendTXT(_buildSubscribeMsg());
      if (_onOnline) _onOnline(true);
      break;

    case WStype_DISCONNECTED:
      _wsConnected = false;
      Serial.println("[Supabase] Realtime disconnected");
      if (_onOnline) _onOnline(false);
      break;

    case WStype_TEXT: {
      String msg = String((char*)payload, length);
      _handleRealtimeMessage(msg);
      break;
    }

    case WStype_PING:
      _ws.sendTXT("{\"event\":\"heartbeat\",\"topic\":\"phoenix\",\"payload\":{},\"ref\":null}");
      break;

    default:
      break;
  }
}

// ─── _handleRealtimeMessage() ────────────────────────────────
void SupabaseClient::_handleRealtimeMessage(const String& msg) {
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, msg) != DeserializationError::Ok) return;

  String event = doc["event"] | "";

  // Supabase Realtime INSERT event
  if (event == "INSERT") {
    JsonObject record = doc["payload"]["record"];
    if (record.isNull()) return;

    String deviceId = record["device_id"] | "";
    if (deviceId != _deviceId) return; // Không phải cho device này

    String queueId   = record["id"] | "";
    String commandId  = record["command_id"] | "";

    Serial.printf("[Supabase] Cloud command received: queue=%s cmd=%s\n",
      queueId.c_str(), commandId.c_str());

    // Lấy raw_data từ record (đã được embed khi insert)
    IRCommand cmd;
    cmd.valid    = true;
    cmd.protocol = IRHandler::stringToProtocol(record["protocol"] | "RAW");
    cmd.id       = commandId;

    JsonArray raw = record["raw_data"].as<JsonArray>();
    cmd.rawLen = 0;
    for (JsonVariant v : raw) {
      if (cmd.rawLen >= IR_RAW_BUF_SIZE) break;
      cmd.rawData[cmd.rawLen++] = v.as<uint16_t>();
    }

    if (_onCommand) _onCommand(cmd);

    // Đánh dấu đã xử lý
    _markCommandProcessed(queueId, true);
  }
}

// ─── _buildSubscribeMsg() ────────────────────────────────────
String SupabaseClient::_buildSubscribeMsg() {
  // Supabase Realtime v2 subscribe format
  DynamicJsonDocument doc(512);
  doc["event"] = "phx_join";
  doc["topic"] = "realtime:public:command_queue:device_id=eq." + _deviceId;
  doc["ref"]   = "1";

  JsonObject payload = doc.createNestedObject("payload");
  JsonObject config  = payload.createNestedObject("config");
  config["broadcast"]["self"] = false;
  JsonObject presence = config.createNestedObject("postgres_changes");
  // (sẽ match với filter topic)

  String out;
  serializeJson(doc, out);
  return out;
}

// ─── _markCommandProcessed() ─────────────────────────────────
bool SupabaseClient::_markCommandProcessed(const String& queueId, bool success) {
  DynamicJsonDocument doc(128);
  doc["status"]       = success ? "sent" : "error";
  doc["processed_at"] = "now()";

  String body;
  serializeJson(doc, body);

  String path = "/rest/v1/command_queue?id=eq." + queueId;
  String response;
  int code = _httpPatch(path, body, response);
  return (code == 200 || code == 204);
}

// ─── HTTP Helpers ────────────────────────────────────────────
void SupabaseClient::_addHeaders(HTTPClient& http) {
  http.addHeader("apikey",        SUPABASE_ANON_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_ANON_KEY);
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("Prefer",        "return=representation");
}

int SupabaseClient::_httpGet(const String& path, String& response) {
  WiFiClientSecure client;
  client.setInsecure(); // TODO: Add proper cert for production
  HTTPClient http;

  String url = String(SUPABASE_URL) + path;
  http.begin(client, url);
  _addHeaders(http);
  http.setTimeout(SUPABASE_TIMEOUT_MS);

  int code = http.GET();
  if (code > 0) response = http.getString();
  http.end();
  return code;
}

int SupabaseClient::_httpPost(const String& path, const String& body,
                               String& response) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = String(SUPABASE_URL) + path;
  http.begin(client, url);
  _addHeaders(http);
  http.setTimeout(SUPABASE_TIMEOUT_MS);

  int code = http.POST(body);
  if (code > 0) response = http.getString();
  http.end();
  return code;
}

int SupabaseClient::_httpPatch(const String& path, const String& body,
                                String& response) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = String(SUPABASE_URL) + path;
  http.begin(client, url);
  _addHeaders(http);
  http.addHeader("X-HTTP-Method-Override", "PATCH");
  http.setTimeout(SUPABASE_TIMEOUT_MS);

  int code = http.PATCH(body);
  if (code > 0) response = http.getString();
  http.end();
  return code;
}
