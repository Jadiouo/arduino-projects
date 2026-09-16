/* ===========================================================
    ESP32-S3 Ambient Console (All-in-One) - Final Corrected Version
    Hardware: ESP32-S3, ST7789(SPI), VL53L0X(ToF), MPR121(CapTouch),
              BME680(Air), MAX98357A(I2S)
    =========================================================== */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_MPR121.h>
#include <Adafruit_BME680.h>
#include "driver/i2s.h"

// --------- Display (ST7789) Pins ----------
#define TFT_SCLK 18
#define TFT_MOSI 23

// --------- I2C Pins ----------
#ifndef SDA
#define SDA 21        // I2C SDA(MPR121)
#endif
#ifndef SCL
#define SCL 22        // I2C SCL(ME680)
#endif

// --------- I2S (MAX98357A) Pins ----------
#define I2S_BCLK  26
#define I2S_LRCK  25
#define I2S_DOUT  27


// --------- Configuration ----------
#include "secrets.h"   // CWA_API_KEY / WIFI_SSID / WIFI_PASSWORD (see secrets.h.example)
String      CWA_LOCATION = "臺中市";
String CFG_CITY = "Taichung";

// --------- Constants ----------
const uint32_t WEATHER_TTL_MS_MIN = 30UL * 60UL * 1000UL;
const uint32_t WEATHER_TTL_MS_MAX = 60UL * 60UL * 1000UL;
const uint16_t DIST_THRESHOLD_MM = 800;
const uint32_t AWAY_TIMEOUT_MS   = 30UL * 1000UL;

// ------------ Global Objects & State ------------
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_VL53L0X tof = Adafruit_VL53L0X();
Adafruit_MPR121 cap = Adafruit_MPR121();
Adafruit_BME680 bme;

bool vl53_ok = false, mpr_ok = false, bme_ok = false, wifiConnected = false, inSleep = false;
enum UICard { CARD_IDLE, CARD_WEATHER, CARD_AIR, CARD_NOTIFY };
UICard gCard = CARD_IDLE;
String gNotifyMsg = "System Initializing...";
uint32_t lastPresentMs = 0;
uint16_t lastTouched = 0;

// ------------ Forward Declarations ------------
void refreshUI();
void setNotify(const String& msg);

// ------------ Backlight & Power ------------
void setBacklight(uint8_t val) {
  analogWrite(TFT_BL, val);   // 0~255
}
void enterPowerSave() { if (inSleep) return; inSleep = true; setBacklight(0); setCpuFrequencyMhz(80); }
void wakeFromPowerSave() { if (!inSleep) return; inSleep = false; setCpuFrequencyMhz(240); setBacklight(255); refreshUI(); }

// ------------ Wi-Fi & Time ------------
void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) { wifiConnected = true; return; }
  WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) delay(200);
  wifiConnected = (WiFi.status() == WL_CONNECTED);
}
void syncTimeNTP() {
  if (!wifiConnected) return;
  configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
  int retries = 0;
  while (time(nullptr) < 1700000000 && retries++ < 25) delay(200);
}

// ------------ Weather Data ------------
struct WeatherData { float tempC=NAN, minT=NAN, maxT=NAN; uint8_t pop=255; String wx, ci, period; uint32_t fetchedAt=0; };
WeatherData gWeather;
uint32_t weatherTTL = WEATHER_TTL_MS_MIN;
bool weatherCacheExpired() { return (gWeather.fetchedAt == 0) || (millis() - gWeather.fetchedAt > weatherTTL); }

String urlEncodeUTF8(const String& s) {
  String out; out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); ++i) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') { out += char(c); }
    else { out += '%'; char hex[3]; sprintf(hex, "%02X", c); out += hex; }
  }
  return out;
}

bool fetchWeather(); // Declaration for use in setup

// ------------ IAQ Data ------------
struct AirStats { float tempC=NAN, humidity=NAN, pressure=NAN, gasKOhms=NAN; uint8_t iaqLevel=3; };
AirStats gAir;

// ------------ UI Drawing Functions ------------
void drawIdle() {
  tft.fillScreen(ST77XX_BLACK); tft.setTextWrap(false); tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(8, 8); tft.setTextSize(3);
  time_t now = time(nullptr); struct tm tminfo; localtime_r(&now, &tminfo); char buf[16];
  if (tminfo.tm_year > 100) strftime(buf, sizeof(buf), "%H:%M", &tminfo); else strcpy(buf, "--:--");
  tft.print(buf);
  tft.setTextSize(2); tft.setCursor(8, 48);
  String wline = CFG_CITY + " " + String(gWeather.tempC, 1) + " C"; tft.print(wline);
  tft.setTextSize(1); tft.setCursor(8, 72); tft.print(gWeather.wx);
}
void drawWeather() { /* Your weather screen drawing code here */ }
void drawAir() { /* Your air quality screen drawing code here */ }
void drawNotify(const String& msg) {
  tft.fillScreen(ST77XX_BLACK); tft.setTextColor(0xF81F); tft.setTextSize(2); tft.setCursor(8, 8); tft.print("Notification");
  tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.setCursor(8, 44); tft.setTextWrap(true); tft.print(msg); tft.setTextWrap(false);
}
void refreshUI() {
  switch (gCard) { case CARD_IDLE: drawIdle(); break; case CARD_WEATHER: drawWeather(); break; case CARD_AIR: drawAir(); break; case CARD_NOTIFY: drawNotify(gNotifyMsg); break; }
}

// Brightness levels (0–255)
constexpr uint8_t BL_BRIGHT_ACTIVE = 255;
constexpr uint8_t BL_BRIGHT_IDLE   = 80;
constexpr uint8_t BL_BRIGHT_OFF    = 0;

// ------------ Audio (I2S Chime Queue) ------------
#include "driver/i2s.h"
#define I2S_PORT I2S_NUM_0

// 先宣告型別，避免 Arduino 自動原型干擾
struct AudioReq { uint16_t freq; uint16_t ms; };

const int AUDIO_Q_MAX = 8;
AudioReq audioQ[AUDIO_Q_MAX];
volatile int audioHead = 0, audioTail = 0;

// 不用結構參數傳遞，避免再次被自動原型搞到
AudioReq audioCur;

bool audioEnqueue(uint16_t f, uint16_t durMs) {
  int next = (audioHead + 1) % AUDIO_Q_MAX;
  if (next == audioTail) return false;
  audioQ[audioHead] = {f, durMs};
  audioHead = next;
  return true;
}

bool audioDequeue() {
  if (audioTail == audioHead) return false;
  audioCur = audioQ[audioTail];
  audioTail = (audioTail + 1) % AUDIO_Q_MAX;
  return true;
}

static inline void i2sInit() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 22050,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = 0,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .bck_io_num   = I2S_BCLK,
    .ws_io_num    = I2S_LRCK,
    .data_out_num = I2S_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
}

static inline void playTone(uint16_t freq, uint16_t durMs) {
  const int SR = 22050;
  int total = (SR * durMs) / 1000;
  for (int i = 0; i < total; ++i) {
    float s = sinf(2.0f * PI * freq * (float)i / (float)SR);
    int16_t sample = (int16_t)(s * 16000);
    size_t w;
    i2s_write(I2S_PORT, (const char*)&sample, sizeof(sample), &w, portMAX_DELAY);
  }
}

void audioTask() {
  if (audioDequeue()) {
    playTone(audioCur.freq, audioCur.ms);
  }
}


// ------------ Touch Handling & Notifications ------------
void handleTouch(uint16_t currentTouched) {
  uint16_t changed = currentTouched ^ lastTouched;
  auto isPressed = [&](int i){ return (currentTouched & (1 << i)) && (changed & (1 << i)); };
  if (isPressed(0)) { gCard = CARD_WEATHER; audioEnqueue(1200, 60); refreshUI(); }
  if (isPressed(2)) { gCard = CARD_AIR;     audioEnqueue(1000, 60); refreshUI(); }
  if (isPressed(1)) { gCard = CARD_IDLE;    audioEnqueue(800, 60);  refreshUI(); }
  lastTouched = currentTouched;
}
void setNotify(const String& msg) { gNotifyMsg = msg; gCard = CARD_NOTIFY; audioEnqueue(600, 120); refreshUI(); }

// ------------ Setup ------------
void setup() {
  Serial.begin(115200);
  //ledcSetup(0, 5000, 8); ledcAttachPin(TFT_BL, 0);
  pinMode(TFT_BL, OUTPUT);
  setBacklight(BL_BRIGHT_IDLE);   // 初始亮度
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS); tft.init(240, 240, SPI_MODE0);
  tft.setRotation(2); tft.fillScreen(ST77XX_BLACK); tft.setCursor(10, 110);
  tft.setTextSize(2); tft.print("Booting...");
  
  Wire.begin(SDA, SCL);
  vl53_ok = tof.begin(); if (!vl53_ok) setNotify("VL53L0X Fail");
  mpr_ok = cap.begin(0x5A); if (!mpr_ok) setNotify("MPR121 Fail");
  bme_ok = bme.begin();
  if (bme_ok) { /* BME settings here */ } else { setNotify("BME680 Fail"); }

  i2sInit();
  wifiConnect();
  if (wifiConnected) { syncTimeNTP(); fetchWeather(); } else { setNotify("WiFi Connect Failed"); }
  
  gCard = CARD_IDLE; lastPresentMs = millis(); wakeFromPowerSave();
}

// ------------ Main Loop ------------
uint32_t tPoll_100ms = 0, tPoll_1s = 0, tPoll_10s = 0, tPoll_15s = 0, tPoll_30s = 0;
void loop() {
  uint32_t now = millis();
  
  if (now - tPoll_1s >= 1000) {
    tPoll_1s = now;
    if (vl53_ok) {
       VL53L0X_RangingMeasurementData_t m; tof.rangingTest(&m, false);
       if (m.RangeStatus == 0 && m.RangeMilliMeter < DIST_THRESHOLD_MM) lastPresentMs = now;
    }
  }
  
  if (now - lastPresentMs > AWAY_TIMEOUT_MS) enterPowerSave(); else wakeFromPowerSave();
  if (inSleep) { audioTask(); delay(50); return; }

  if (now - tPoll_100ms >= 100) {
    tPoll_100ms = now;
    if (mpr_ok) { uint16_t current = cap.touched(); if (current != lastTouched) handleTouch(current); }
    if (gCard == CARD_IDLE) refreshUI();
  }
  if (bme_ok && now - tPoll_10s >= 10000) {
      tPoll_10s = now;
      if (bme.performReading()) { /* update gAir struct */ if (gCard == CARD_AIR) refreshUI(); }
  }
  if (wifiConnected && now - tPoll_15s >= 15000) {
      tPoll_15s = now;
      if (weatherCacheExpired()) { if (fetchWeather() && (gCard == CARD_IDLE || gCard == CARD_WEATHER)) refreshUI(); }
  }
  if (now - tPoll_30s >= 30000) {
      tPoll_30s = now;
      if (WiFi.status() != WL_CONNECTED) { wifiConnected = false; setNotify("WiFi Disconnected"); }
  }
  audioTask();
}

// Full weather fetch function
bool fetchWeather() {
  if (!wifiConnected) return false;
  String url = "https://opendata.cwa.gov.tw/api/v1/rest/datastore/F-C0032-001";
  url += "?Authorization=" + String(CWA_API_KEY) + "&locationName=" + urlEncodeUTF8(CWA_LOCATION);
  HTTPClient http; WiFiClientSecure client; client.setInsecure();
  http.begin(client, url);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String payload = http.getString();
  http.end();
  StaticJsonDocument<8192> doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return false;
  JsonObject location = doc["records"]["location"][0];
  if (location.isNull()) return false;
  auto getParamValue = [&](const char* name, String* startTime = nullptr, String* endTime = nullptr) -> JsonVariant {
    for (JsonObject elem : location["weatherElement"].as<JsonArray>()) {
      if (strcmp(elem["elementName"], name) == 0) {
        JsonObject time0 = elem["time"][0];
        if (startTime) *startTime = time0["startTime"].as<String>();
        if (endTime) *endTime = time0["endTime"].as<String>();
        return time0["parameter"]["parameterName"];
      }
    }
    return JsonVariant();
  };
  String startT, endT;
  gWeather.wx = getParamValue("Wx", &startT, &endT).as<String>();
  if (gWeather.wx.isEmpty()) return false;
  gWeather.pop = getParamValue("PoP").as<int>();
  gWeather.minT = getParamValue("MinT").as<float>();
  gWeather.maxT = getParamValue("MaxT").as<float>();
  gWeather.ci = getParamValue("CI").as<String>();
  gWeather.tempC = (gWeather.minT + gWeather.maxT) / 2.0f;
  gWeather.period = startT + " ~ " + endT;
  gWeather.fetchedAt = millis();
  weatherTTL = WEATHER_TTL_MS_MIN + (millis() % (WEATHER_TTL_MS_MAX - WEATHER_TTL_MS_MIN));
  return true;
}