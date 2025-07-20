#include <WiFi.h>
#include <HTTPClient.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

const char* ssid = "Parking Hotspot";
const char* password = "12042005";
const IPAddress apIP(192, 168, 4, 1);
const byte DNS_PORT = 53;

DNSServer dnsServer;
AsyncWebServer server(80);
Servo gateServo;

const int servoPin = 13;
const int gateSensorPin = 27;
const int OPEN_ANGLE = 10;
const int CLOSE_ANGLE = 110;

const int slotLED[4] = {26, 25, 33, 32};
const int sensorPin[4] = {18, 19, 21, 22};

unsigned long lastBlink[4] = {0};
bool ledState[4] = {false};
bool slotOccupied[4] = {false};

void startBlinking(int i) {
  ledState[i] = false;
  lastBlink[i] = millis();
}

void updateLEDs() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(slotLED[i], slotOccupied[i] ? LOW : HIGH);
  }
}

bool isVehicleAtGate() {
  return digitalRead(gateSensorPin) == LOW;
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

void handleCheckInTask(void *parameter) {
  int slot = *((int*)parameter);
  delete (int*)parameter;
  int i = slot - 1;

  startBlinking(i);
  gateServo.write(OPEN_ANGLE);

  while (digitalRead(sensorPin[i]) != LOW) {
    unsigned long now = millis();
    if (now - lastBlink[i] >= 400) {
      ledState[i] = !ledState[i];
      digitalWrite(slotLED[i], ledState[i] ? HIGH : LOW);
      lastBlink[i] = now;
    }
    delay(10);
  }

  digitalWrite(slotLED[i], LOW);
  delay(500);
  gateServo.write(CLOSE_ANGLE);
  slotOccupied[i] = true;
  vTaskDelete(NULL);
}

void handleCheckOut(int slot, AsyncWebServerRequest *req) {
  int i = slot - 1;

  // Không cho checkout nếu còn xe trong slot
  if (digitalRead(sensorPin[i]) == LOW) {
    req->send(403, "application/json", "{\"error\":\"Xe vẫn ở trong slot\"}");
    return;
  }

  gateServo.write(OPEN_ANGLE);

  // Chờ xe vào cảm biến
  while (digitalRead(gateSensorPin) == HIGH) {
    delay(10);
  }

  // Chờ xe rời khỏi cảm biến
  while (digitalRead(gateSensorPin) == LOW) {
    delay(10);
  }

  gateServo.write(CLOSE_ANGLE);
  slotOccupied[i] = false;
  updateLEDs();
  req->send(200, "application/json", "{\"status\":\"Check-out ok\"}");
}

void setup() {
  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  dnsServer.start(DNS_PORT, "*", apIP);

  gateServo.attach(servoPin);
  gateServo.write(CLOSE_ANGLE);

  for (int i = 0; i < 4; i++) {
    pinMode(slotLED[i], OUTPUT);
    pinMode(sensorPin[i], INPUT);
  }
  pinMode(gateSensorPin, INPUT);
  SPIFFS.begin(true);

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req){ req->redirect("/"); });
  server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *req){ req->redirect("/"); });
  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *req){ req->redirect("/"); });

  server.on("/check-gate", HTTP_GET, [](AsyncWebServerRequest *req){
    bool hasVehicle = isVehicleAtGate();
    String res = String("{\"has_vehicle\":") + (hasVehicle ? "true" : "false") + "}";
    req->send(200, "application/json", res);
  });

  server.on("/esp-checkin", HTTP_POST, [](AsyncWebServerRequest *req){}, NULL,
  [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
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

  server.on("/esp-checkout", HTTP_POST, [](AsyncWebServerRequest *req){}, NULL,
  [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
    StaticJsonDocument<200> doc;
    if (deserializeJson(doc, data)) {
      req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    int slot = doc["slot"];
    handleCheckOut(slot, req);
  });

  server.on("/esp-led", HTTP_POST, [](AsyncWebServerRequest *req){}, NULL,
  [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, data);
    int slot = doc["slot"];
    const char* status = doc["status"];
    if (strcmp(status, "registered") == 0) {
      digitalWrite(slotLED[slot - 1], LOW);
    }
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/slot-status", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("slot")) {
      req->send(400, "application/json", "{\"error\":\"Missing slot param\"}");
      return;
    }
    int slot = req->getParam("slot")->value().toInt();
    bool occupied = digitalRead(sensorPin[slot - 1]) == LOW;
    String response = String("{\"occupied\":") + (occupied ? "true" : "false") + "}";
    req->send(200, "application/json", response);
  });

  server.onNotFound([](AsyncWebServerRequest *req) {
    req->redirect("/");
  });

  syncSlotStatusFromServer();
  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
}