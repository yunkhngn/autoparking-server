#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_ADS1X15.h>
#include <DHT.h>

// ===== PIN & SENSORS =====
#define SDA_PIN   4   // ESP8266 D2
#define SCL_PIN   5   // ESP8266 D1
#define DHTPIN    14  // ESP8266 D5
#define DHTTYPE   DHT11

// ===== OLED =====
#define OLED_W 128
#define OLED_H 64
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);

// ===== ADS1115 =====
Adafruit_ADS1115 ads;
int DRY_RAW = 19000;   // mốc khô (bro đã đo)
int WET_RAW = 8000;    // mốc ướt (bro đã đo)

// ===== DHT =====
DHT dht(DHTPIN, DHTTYPE);
float tempC = NAN, humi = NAN;
unsigned long lastDht = 0;

// ===== OFFSETS (nếu cần chỉnh) =====
float TEMP_OFFSET = 0.0f;   // ví dụ -2.0 nếu muốn bù trừ
float HUMI_OFFSET = 0.0f;   // ví dụ -12.0 nếu muốn bù trừ

// ===== SOIL smoothing =====
const int AVG_N = 8;
int16_t buf[AVG_N]; int idxBuf=0; long sumBuf=0;

// ===== MODES & TIMERS =====
enum UIMode { MODE_FACE, MODE_PANEL };
UIMode uiMode = MODE_FACE;
unsigned long lastModeToggle = 0;
const unsigned long FACE_MS  = 10000;  // 10s mặt
const unsigned long PANEL_MS = 10000;  // 10s panel

// Trong PANEL: T/H đổi mỗi 5s
unsigned long lastTHtoggle = 0;
const unsigned long TH_MS = 5000;      // 5s đổi
bool showTemp = true;

// ===== EYES / EXPRESSIONS =====
struct FaceParams {
  // mở mắt 0..1 (0 = nhắm), offset đồng tử, style lông mày
  float open;   // 0..1
  int pupX;     // -8..+8
  int pupY;     // -6..+6
  int browTiltL; // -2..+2 (âm: chau mày, dương: ngạc nhiên)
  int browTiltR; // -2..+2
  bool cuteHighlight; // chấm sáng trong đồng tử
  bool suspiciousRaise; // một bên mày nhướng
};

String currentMood = "normal";
bool blinking = false;
uint8_t blinkPhase = 0;
unsigned long nextBlinkAt = 0;
unsigned long lastBlinkStep = 0;

void scheduleNextBlink() {
  nextBlinkAt = millis() + random(3000, 7000); // 3-7s
}

int16_t readSoilRawOnce() { return ads.readADC_SingleEnded(0); }
int16_t readSoilAvg() {
  int16_t v = readSoilRawOnce();
  sumBuf -= buf[idxBuf];
  buf[idxBuf] = v;
  sumBuf += v;
  idxBuf = (idxBuf + 1) % AVG_N;
  return (int16_t)(sumBuf / AVG_N);
}
int pctFromRaw(int16_t raw) {
  if (raw > DRY_RAW) raw = DRY_RAW;
  if (raw < WET_RAW) raw = WET_RAW;
  float pct = 100.0f * (float)(DRY_RAW - raw) / (float)(DRY_RAW - WET_RAW);
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  return (int)(pct + 0.5f);
}

// ======= Mood logic =======
String decideMood(int soilPct, float t, float h) {
  // Env override trước
  if (!isnan(t) && !isnan(h)) {
    if (h >= 90.0 && t >= 30.0) return "angry"; // nóng ẩm quá cao
    if (t <= 15.0)              return "sad";   // quá lạnh
  }
  // Soil-based
  if (soilPct < 20)  return "upset";
  if (soilPct < 40)  return "sad";
  if (soilPct < 60)  return "normal";
  if (soilPct < 80)  return "happy";
  return "cute"; // 80-100
}

// map mood -> FaceParams
FaceParams paramsForMood(const String& mood) {
  FaceParams p{1.0f, 0,0, 0,0, false, false};
  if (mood == "normal") {
    p.open=1.0f; p.pupX=0; p.pupY=0; p.browTiltL=0; p.browTiltR=0;
  } else if (mood == "sad") {
    p.open=0.6f; p.pupY=3; p.browTiltL=+1; p.browTiltR=+1;
  } else if (mood == "upset") {
    p.open=0.5f; p.pupY=2; p.browTiltL=+2; p.browTiltR=+2;
  } else if (mood == "wonder") {
    p.open=1.0f; p.pupY=-2; p.browTiltL=-1; p.browTiltR=-1;
  } else if (mood == "happy") {
    p.open=0.9f; p.pupY=-1; p.browTiltL=0; p.browTiltR=0;
  } else if (mood == "cute") {
    p.open=1.0f; p.pupY=-1; p.cuteHighlight=true;
  } else if (mood == "angry") {
    p.open=0.7f; p.pupX=0; p.pupY=0; p.browTiltL=-2; p.browTiltR=-2;
  } else if (mood == "suspicious") {
    p.open=0.6f; p.pupX=6; p.browTiltL=-1; p.browTiltR=+1; p.suspiciousRaise=true;
  } else if (mood == "close" || mood == "blink") {
    p.open=0.0f;
  } else {
    p.open=1.0f;
  }
  return p;
}

// ======= Eyes drawing =======
struct EyeGeom { int cxL=38, cxR=90, cy=34, rEye=18, rPupil=6; } eye;
void drawEyebrow(int cx, int y, int width, int tilt) {
  // tilt -2..+2: âm => chau mày (nghiêng xuống giữa), dương => ngạc nhiên (nghiêng lên giữa)
  int x0 = cx - width/2, x1 = cx + width/2;
  int y0 = y + tilt*2;
  int y1 = y - tilt*2;
  display.drawLine(x0, y0, x1, y1, SSD1306_WHITE);
}
void drawOneEye(int cx, float openAmt, int pupX, int pupY, bool cuteHL) {
  // tròng trắng
  display.fillCircle(cx, eye.cy, eye.rEye, SSD1306_WHITE);

  // mí mắt (cắt trên/dưới)
  int closePx = (int)((1.0f - openAmt) * eye.rEye); // 0..rEye
  if (closePx > 0) {
    // mi trên
    display.fillRect(cx - eye.rEye - 1, eye.cy - eye.rEye - 4, eye.rEye*2 + 2, closePx, SSD1306_BLACK);
    // mi dưới
    display.fillRect(cx - eye.rEye - 1, eye.cy + eye.rEye - closePx + 1, eye.rEye*2 + 2, closePx, SSD1306_BLACK);
  }

  // đồng tử (nếu chưa khép hết)
  if (openAmt > 0.05f) {
    int px = cx + pupX;
    int py = eye.cy + pupY;
    display.fillCircle(px, py, eye.rPupil, SSD1306_BLACK);
    if (cuteHL) {
      // highlight nhỏ
      display.drawPixel(px-2, py-2, SSD1306_WHITE);
      display.drawPixel(px-1, py-3, SSD1306_WHITE);
    }
  }
  // viền
  display.drawCircle(cx, eye.cy, eye.rEye, SSD1306_WHITE);
}

void drawFace(const String& mood) {
  // Blink overlay (ưu tiên)
  float openOverlay = 1.0f;
  if (blinking) {
    // 0..5 pha -> open fraction
    static const uint8_t stepH[6] = {0, 8, 14, 14, 8, 0}; // pixel đóng
    int px = stepH[blinkPhase];
    openOverlay = 1.0f - (float)px / (float)eye.rEye; // 0..1
    if (openOverlay < 0) openOverlay = 0;
  }

  FaceParams p = paramsForMood(mood);
  float openFinal = p.open * openOverlay;

  display.clearDisplay();
  // lông mày
  drawEyebrow(eye.cxL, eye.cy - eye.rEye - 6, 24, p.browTiltL + (p.suspiciousRaise ? +1 : 0));
  drawEyebrow(eye.cxR, eye.cy - eye.rEye - 6, 24, p.browTiltR - (p.suspiciousRaise ? +1 : 0));

  // hai mắt
  drawOneEye(eye.cxL, openFinal, p.pupX, p.pupY, p.cuteHighlight);
  drawOneEye(eye.cxR, openFinal, p.pupX, p.pupY, p.cuteHighlight);

  // caption mood nhỏ phía dưới
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print("Mood: ");
  display.print(mood);

  display.display();
}

void updateBlink() {
  unsigned long now = millis();
  if (!blinking && now >= nextBlinkAt) {
    blinking = true; blinkPhase = 0; lastBlinkStep = now;
  }
  if (blinking && now - lastBlinkStep > 40) {
    lastBlinkStep = now;
    blinkPhase++;
    if (blinkPhase > 5) { blinking = false; scheduleNextBlink(); }
  }
}

// ======= PANEL drawing =======
void drawHeart(int x, int y, int s, bool filled) {
  // trái tim đơn giản: 2 hình tròn + tam giác
  if (filled) {
    display.fillCircle(x + s/3, y + s/3, s/3, SSD1306_WHITE);
    display.fillCircle(x + 2*s/3, y + s/3, s/3, SSD1306_WHITE);
    display.fillTriangle(x + s/6, y + s/3,
                         x + 5*s/6, y + s/3,
                         x + s/2, y + s, SSD1306_WHITE);
  } else {
    display.drawCircle(x + s/3, y + s/3, s/3, SSD1306_WHITE);
    display.drawCircle(x + 2*s/3, y + s/3, s/3, SSD1306_WHITE);
    display.drawLine(x + s/6, y + s/3, x + 5*s/6, y + s/3, SSD1306_WHITE);
    display.drawLine(x + s/6, y + s/3, x + s/2, y + s, SSD1306_WHITE);
    display.drawLine(x + 5*s/6, y + s/3, x + s/2, y + s, SSD1306_WHITE);
  }
}

void drawPanel(int soilPct, float t, float h, bool showT) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  // Thanh máu = 5 trái tim theo Soil% (mỗi 20%)
  int hearts = (soilPct + 19) / 20; if (hearts > 5) hearts = 5;
  int s = 12; // kích thước tim
  int x0 = 2;
  for (int i=0; i<5; i++) {
    bool filled = (i < hearts);
    drawHeart(x0 + i*(s+3), 0, s, filled);
  }

  // Soil %
  display.setTextSize(2);
  display.setCursor(0, 22);
  display.print("Soil ");
  display.print(soilPct); display.print("%");

  // Dòng dưới: T hoặc H (5s đổi)
  display.setTextSize(2);
  display.setCursor(0, 46);
  if (showT) {
    display.print("T:");
    if (isnan(t)) display.print("--.-");
    else          display.print(t, 1);
    display.write(247); display.print("C");
  } else {
    display.print("H:");
    if (isnan(h)) display.print("--");
    else          display.print((int)h);
    display.print("%");
  }

  display.display();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  // Wire.setClock(100000); // nếu bus yếu, mở dòng này để 100kHz

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED fail"); while (1) {}
  }
  display.clearDisplay(); display.display();

  if (!ads.begin(0x48)) {
    Serial.println("ADS1115 fail"); while (1) {}
  }
  ads.setGain(GAIN_ONE);

  dht.begin();

  // prime soil buffer
  for (int i=0;i<AVG_N;i++){ buf[i]=readSoilRawOnce(); sumBuf+=buf[i]; delay(20); }

  scheduleNextBlink();
  lastModeToggle = millis();
  lastTHtoggle  = millis();
}

void loop() {
  unsigned long now = millis();

  // ---- Read sensors ----
  // soil
  static int16_t soilRawHold = readSoilAvg();
  if (now % 400 > 300) { // đọc đều tay ~2.5Hz
    soilRawHold = readSoilAvg();
  }
  int soilPct = pctFromRaw(soilRawHold);

  // dht (>=2s)
  if (now - lastDht >= 2000) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h)) humi = h + HUMI_OFFSET;
    if (!isnan(t)) tempC = t + TEMP_OFFSET;
    lastDht = now;
  }

  // ---- Decide mood ----
  String mood = decideMood(soilPct, tempC, humi);
  // thêm chút “suspicious” khi soil cao nhưng temp/hum bất thường
  if (mood == "cute" && (!isnan(humi) && humi > 85)) mood = "suspicious";
  currentMood = mood;

  // ---- Toggle face/panel 10s ----
  unsigned long span = (uiMode == MODE_FACE) ? FACE_MS : PANEL_MS;
  if (now - lastModeToggle >= span) {
    uiMode = (uiMode == MODE_FACE) ? MODE_PANEL : MODE_FACE;
    lastModeToggle = now;
  }

  // ---- Panel inside: toggle T/H 5s ----
  if (uiMode == MODE_PANEL && now - lastTHtoggle >= TH_MS) {
    showTemp = !showTemp;
    lastTHtoggle = now;
  }

  // ---- Draw ----
  if (uiMode == MODE_FACE) {
    updateBlink();           // blink overlay
    drawFace(currentMood);   // draw mood eyes
  } else {
    drawPanel(soilPct, tempC, humi, showTemp);
  }

  // ---- Log ----
  if ((now % 1000) < 50) {
    Serial.printf("Soil=%d%% | T=%.1fC H=%.0f%% | mood=%s | mode=%s\n",
      soilPct,
      isnan(tempC)?-99.9:tempC,
      isnan(humi)?-1.0:humi,
      currentMood.c_str(),
      (uiMode==MODE_FACE?"FACE":"PANEL"));
  }

  delay(30); // nhịp vẽ mượt
}