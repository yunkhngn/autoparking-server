#include <WiFi.h>
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
const int OPEN_ANGLE = 10;
const int CLOSE_ANGLE = 110;

const int slotLED[4] = {26, 25, 33, 32};
const int sensorPin[4] = {18, 19, 21, 22};

unsigned long lastBlink[4] = {0};
bool ledState[4] = {false};

void handleCheckInTask(void *parameter) {
  int slot = *((int*)parameter);
  delete (int*)parameter;
  int i = slot - 1;

  digitalWrite(slotLED[i], LOW);
  ledState[i] = false;
  lastBlink[i] = millis();

  delay(3000);
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

  vTaskDelete(NULL);
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
    digitalWrite(slotLED[i], LOW);
  }

  SPIFFS.begin(true);
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *req){ req->redirect("/"); });
  server.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest *req){ req->redirect("/"); });
  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *req){ req->redirect("/"); });

  server.on("/esp-checkin", HTTP_POST, [](AsyncWebServerRequest *req){}, NULL,
  [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
    StaticJsonDocument<200> doc;
    if (deserializeJson(doc, data)) {
      req->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }

    int slot = doc["slot"];
    if (slot < 1 || slot > 4) {
      req->send(400, "application/json", "{\"error\":\"Invalid slot\"}");
      return;
    }

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
    if (slot < 1 || slot > 4) {
      req->send(400, "application/json", "{\"error\":\"Invalid slot\"}");
      return;
    }

    int i = slot - 1;
    int state = digitalRead(sensorPin[i]);
    if (state == LOW) {
      req->send(403, "application/json", "{\"error\":\"Xe chưa rời khỏi slot\"}");
      return;
    }

    gateServo.write(OPEN_ANGLE);
    delay(10000);
    gateServo.write(CLOSE_ANGLE);
    req->send(200, "application/json", "{\"status\":\"Check-out ok\"}");
  });

  server.onNotFound([](AsyncWebServerRequest *req) {
    req->redirect("/");
  });

  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
}