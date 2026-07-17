/**
 * SmartRemote – wifi_manager.h
 * WiFi setup, HTTP REST API, WebSocket real-time
 * Thư viện: WiFiManager, ESPAsyncWebServer, AsyncTCP
 */

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include "config.h"
#include "ir_handler.h"
#include "storage.h"

// Callback khi nhận lệnh qua WebSocket/HTTP
typedef std::function<void(IRCommand)>  CommandReceivedCb;
typedef std::function<void(bool)>       LearnRequestCb;
typedef std::function<void()>           ResetRequestCb;

class WiFiManager_ {
public:
  WiFiManager_(Storage& storage, IRHandler& ir);

  // Khởi động WiFiManager (AP portal nếu chưa cấu hình)
  bool begin(const String& deviceKey);

  // Khởi động WebServer + WebSocket (sau khi đã connect WiFi)
  void startServer();

  // Gọi trong loop() – xử lý sự kiện WebSocket
  void update();

  // Broadcast trạng thái tới tất cả WebSocket clients
  void broadcastStatus();

  // Kiểm tra kết nối
  bool isConnected() const;
  String getIP() const;

  // Reset WiFi credentials → vào AP Mode
  void resetWifi();

  // Register callbacks
  void onCommandReceived(CommandReceivedCb cb)  { _onCommand = cb; }
  void onLearnRequest(LearnRequestCb cb)         { _onLearn = cb; }

private:
  AsyncWebServer _server;
  AsyncWebSocket _ws;
  Storage&       _storage;
  IRHandler&     _ir;
  String         _deviceKey;

  CommandReceivedCb _onCommand;
  LearnRequestCb    _onLearn;

  // HTTP route handlers
  void _setupRoutes();
  void _handleStatus(AsyncWebServerRequest* req);
  void _handleSend(AsyncWebServerRequest* req, JsonVariant& body);
  void _handleLearn(AsyncWebServerRequest* req, JsonVariant& body);
  void _handleCommandsList(AsyncWebServerRequest* req);
  void _handleNotFound(AsyncWebServerRequest* req);

  // WebSocket handlers
  void _onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                  AwsEventType type, void* arg, uint8_t* data, size_t len);
  void _handleWsMessage(AsyncWebSocketClient* client, const String& msg);

  // Auth
  bool _isAuthorized(AsyncWebServerRequest* req);

  // Build status JSON
  String _buildStatusJson(const String& mode = "");
};
