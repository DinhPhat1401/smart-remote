/**
 * SmartRemote – supabase_client.h
 * HTTP REST + Realtime WebSocket cho Supabase
 */

#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "ir_handler.h"

typedef std::function<void(IRCommand)> CloudCommandCb;
typedef std::function<void(bool)>      OnlineStatusCb;

class SupabaseClient {
public:
  SupabaseClient(const String& deviceId, const String& deviceKey);

  void begin();
  void update(); // Gọi trong loop() – xử lý Realtime WS

  // ─── REST API ────────────────────────────────────────────
  // Upload lệnh IR lên Supabase
  bool uploadCommand(const IRCommand& cmd);

  // Fetch lệnh theo ID từ cloud
  IRCommand fetchCommand(const String& id);

  // Cập nhật trạng thái online/mode của device
  bool updateDeviceStatus(bool online, const String& mode, uint8_t flashPct);

  // ─── Realtime ─────────────────────────────────────────────
  void subscribeRealtime(); // Subscribe command_queue cho device này
  void unsubscribeRealtime();

  bool isRealtimeConnected() const { return _wsConnected; }

  // Callback khi có lệnh mới từ cloud
  void onCloudCommand(CloudCommandCb cb) { _onCommand = cb; }
  void onOnlineStatus(OnlineStatusCb cb) { _onOnline = cb; }

private:
  String _deviceId;
  String _deviceKey;
  bool   _wsConnected;

  WebSocketsClient _ws;
  CloudCommandCb   _onCommand;
  OnlineStatusCb   _onOnline;

  // HTTP helpers
  int    _httpGet(const String& path, String& response);
  int    _httpPost(const String& path, const String& body, String& response);
  int    _httpPatch(const String& path, const String& body, String& response);

  // Supabase auth headers
  void _addHeaders(HTTPClient& http);

  // Realtime WebSocket handlers
  void _onWsEvent(WStype_t type, uint8_t* payload, size_t length);
  void _handleRealtimeMessage(const String& msg);

  // Build Supabase Realtime subscribe message
  String _buildSubscribeMsg();

  // Mark command as processed
  bool _markCommandProcessed(const String& queueId, bool success);
};
