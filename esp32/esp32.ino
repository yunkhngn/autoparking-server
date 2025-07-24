#include <WiFi.h>
#include <HTTPClient.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// Cấu hình mạng
const char* SSID = "Parking Hotspot";
const char* PASSWORD = "12042005";
const IPAddress AP_IP(192, 168, 4, 1);
const byte DNS_PORT = 53;

// Cấu hình phần cứng
const int SERVO_PIN = 13;
const int GATE_SENSOR_PIN = 27;
const int OPEN_ANGLE = 10;
const int CLOSE_ANGLE = 110;
const int SLOT_LED[4] = {26, 25, 33, 32};
const int SLOT_SENSOR[4] = {18, 19, 21, 22};

// Trạng thái hệ thống
bool ledState[4] = {false};
bool slotOccupied[4] = {false};
unsigned long lastBlink[4] = {0};

// Khởi tạo các đối tượng
DNSServer dnsServer;
AsyncWebServer server(80);
Servo gateServo;

// -------------------- HÀM TIỆN ÍCH --------------------

void startBlinking(int i) {
  ledState[i] = false;
  lastBlink[i] = millis();
}

void updateLEDs() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(SLOT_LED[i], slotOccupied[i] ? LOW : HIGH);
  }
}

bool isVehicleAtGate() {
  return digitalRead(GATE_SENSOR_PIN) == LOW;
}

void syncSlotStatusFromServer() {
  HTTPClient http;
  http.begin("http://192.168.4.2:1204/esp-slots");
  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<512> doc;
    if (!deserializeJson(doc, payload)) {
      for (JsonObject slot : doc.as<JsonArray>()) {
        int i = slot["slot_number"].as<int>() - 1;
        slotOccupied[i] = strcmp(slot["status"], "available") != 0;
      }
    }
  }
  http.end();
  updateLEDs();
}

// -------------------- CHECK-IN --------------------

void handleCheckInTask(void* parameter) {
  int slot = *((int*)parameter);
  delete (int*)parameter;
  int i = slot - 1;

  startBlinking(i);
  gateServo.write(OPEN_ANGLE);

  while (digitalRead(SLOT_SENSOR[i]) != LOW) {
    unsigned long now = millis();
    if (now - lastBlink[i] >= 400) {
      ledState[i] = !ledState[i];
      digitalWrite(SLOT_LED[i], ledState[i] ? HIGH : LOW);
      lastBlink[i] = now;
    }
    delay(10);
  }

  digitalWrite(SLOT_LED[i], LOW);
  delay(500);
  gateServo.write(CLOSE_ANGLE);
  slotOccupied[i] = true;
  vTaskDelete(NULL);
}

// -------------------- CHECK-OUT --------------------

void handleCheckOut(int slot, AsyncWebServerRequest* req) {
  int i = slot - 1;

  if (digitalRead(SLOT_SENSOR[i]) == LOW) {
    req->send(403, "application/json", "{\"error\":\"Xe vẫn ở trong slot\"}");
    return;
  }

  gateServo.write(OPEN_ANGLE);

  unsigned long timeout = millis() + 10000;
  while (digitalRead(GATE_SENSOR_PIN) == HIGH && millis() < timeout) {
    delay(50);
  }

  timeout = millis() + 10000;
  while (digitalRead(GATE_SENSOR_PIN) == LOW && millis() < timeout) {
    delay(50);
  }

  gateServo.write(CLOSE_ANGLE);
  slotOccupied[i] = false;
  updateLEDs();
  req->send(200, "application/json", "{\"status\":\"Check-out ok\"}");
}

// -------------------- SETUP --------------------

void setup() {
  WiFi.softAP(SSID, PASSWORD);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  dnsServer.start(DNS_PORT, "*", AP_IP);

  gateServo.attach(SERVO_PIN);
  gateServo.write(CLOSE_ANGLE);

  for (int i = 0; i < 4; i++) {
    pinMode(SLOT_LED[i], OUTPUT);
    pinMode(SLOT_SENSOR[i], INPUT);
  }
  pinMode(GATE_SENSOR_PIN, INPUT);

  SPIFFS.begin(true);
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("/"); });
  server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("/"); });
  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* req){ req->redirect("/"); });

  server.on("/check-gate", HTTP_GET, [](AsyncWebServerRequest* req) {
    bool hasVehicle = isVehicleAtGate();
    String res = String("{\"has_vehicle\":") + (hasVehicle ? "true" : "false") + "}";
    req->send(200, "application/json", res);
  });

  server.on("/esp-checkin", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      StaticJsonDocument<200> doc;
      if (deserializeJson(doc, data)) {
        req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
      }

      int slot = doc["slot"];
      if (!isVehicleAtGate()) {
        req->send(403, "application/json", "{\"error\":\"No vehicle at gate\"}");
        return;
      }

      slotOccupied[slot - 1] = true;
      updateLEDs();
      req->send(200, "application/json", "{\"status\":\"Check-in ok\"}");
      int* arg = new int(slot);
      xTaskCreate(handleCheckInTask, "CheckInTask", 2048, arg, 1, NULL);
    });

  server.on("/esp-checkout", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      StaticJsonDocument<200> doc;
      if (deserializeJson(doc, data)) {
        req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
      }
      int slot = doc["slot"];
      handleCheckOut(slot, req);
    });

  server.on("/esp-led", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      StaticJsonDocument<200> doc;
      deserializeJson(doc, data);
      int slot = doc["slot"];
      const char* status = doc["status"];
      if (strcmp(status, "registered") == 0) {
        digitalWrite(SLOT_LED[slot - 1], LOW);
      }
      req->send(200, "application/json", "{\"ok\":true}");
    });

  server.on("/slot-status", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("slot")) {
      req->send(400, "application/json", "{\"error\":\"Missing slot param\"}");
      return;
    }
    int slot = req->getParam("slot")->value().toInt();
    bool occupied = digitalRead(SLOT_SENSOR[slot - 1]) == LOW;
    String response = String("{\"occupied\":") + (occupied ? "true" : "false") + "}";
    req->send(200, "application/json", response);
  });

  server.onNotFound([](AsyncWebServerRequest* req) {
    req->redirect("/");
  });

  syncSlotStatusFromServer();
  server.begin();
}

// -------------------- LOOP --------------------

void loop() {
  dnsServer.processNextRequest();
}