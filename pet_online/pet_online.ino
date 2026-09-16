/*
  ESP32-C3 SuperMini Desktop Pet v7.0 (網頁分頁控制版)
  - 網頁控制：上一頁/下一頁/摸摸/惹惱
  - Page 0: 互動表情 (眨眼/變換心情)
  - Page 1: 網路時鐘 (NTP 自動校時)
  - Page 2: 天氣資訊 (目前為靜態樣板)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ====== Wi-Fi 設定 (填在 secrets.h，參考 secrets.h.example) ======
#include "secrets.h"

// ====== 腳位設定 ======
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9

// OLED 設定
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Web Server
WebServer server(80);

// NTP 時間設定 (台灣時區 UTC+8)
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET_SEC = 8 * 3600;
const int   DAYLIGHT_OFFSET_SEC = 0;

// ====== 狀態變數 ======
// 頁面定義
enum PageState { PAGE_FACE, PAGE_CLOCK, PAGE_WEATHER };
const int TOTAL_PAGES = 3;
int currentPageInt = 0; // 0: Face, 1: Clock, 2: Weather

// 心情定義
enum MoodState { NORMAL, TIRED, LOVE, ANGRY };
MoodState currentMood = NORMAL;

// 互動計時
unsigned long lastInteractionMs = 0;
const unsigned long MOOD_RESET_MS = 5000; // 互動後5秒回復正常

// 眨眼控制
unsigned long lastBlinkTime = 0;
unsigned long nextBlinkInterval = 3000;
bool isBlinking = false;
unsigned long blinkStartTime = 0;

// ====== 網頁 HTML (儲存在 Flash) ======
const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 Pet Control</title>
<style>
 body { background-color: #222; color: #fff; font-family: sans-serif; text-align: center; padding: 20px; display: flex; flex-direction: column; align-items: center; min-height: 90vh; margin: 0; }
 h2 { margin-bottom: 10px; }
 .status-box { color: #aaa; font-size: 0.9rem; margin-bottom: 20px; height: 20px; }
 .control-group { background: #333; padding: 15px; border-radius: 20px; width: 100%; max-width: 320px; margin-bottom: 20px; }
 .btn { width: 100%; padding: 12px; font-size: 1.1rem; margin: 5px 0; border-radius: 10px; border: none; cursor: pointer; color: white; transition: transform 0.1s; }
 .btn:active { transform: scale(0.95); }
 .pat { background: linear-gradient(135deg, #ff4081, #ff80ab); }
 .annoy { background: linear-gradient(135deg, #536dfe, #8c9eff); }
 .nav-row { display: flex; justify-content: space-between; gap: 10px; }
 .nav-btn { flex: 1; background: #00C853; font-size: 1.5rem; padding: 15px 0; }
 .label { font-size: 0.8rem; color: #888; margin-bottom: 5px; text-align: left; width: 100%; }
</style>
</head>
<body>
 <h2>Desktop Pet v7.0</h2>
 <div id="status" class="status-box">Ready</div>

 <div class="control-group">
   <div class="label">切換頁面 (Switch Page)</div>
   <div class="nav-row">
     <button class="btn nav-btn" onclick="send('prev')">◀</button>
     <button class="btn nav-btn" onclick="send('next')">▶</button>
   </div>
 </div>

 <div class="control-group">
   <div class="label">互動 (Interact - Face Only)</div>
   <button class="btn pat" onclick="send('pat')">❤️ 摸摸 (Pat)</button>
   <button class="btn annoy" onclick="send('annoy')">⚡ 惹惱 (Annoy)</button>
 </div>

 <script>
  function send(cmd) {
    var statusDiv = document.getElementById('status');
    statusDiv.innerHTML = "Sending...";
    fetch('/cmd?val=' + cmd, { 
        method: 'GET',
        headers: { 'Connection': 'close' }
    })
    .then(res => {
      if(res.ok) {
        statusDiv.innerHTML = "✅ OK: " + cmd;
        statusDiv.style.color = "#00ff00";
      } else {
        statusDiv.innerHTML = "❌ Error";
        statusDiv.style.color = "#ff0000";
      }
    })
    .catch(e => {
      statusDiv.innerHTML = "⚠️ Disconnected";
      statusDiv.style.color = "#ff0000";
    });
  }
 </script>
</body>
</html>
)rawliteral";

// ====== 繪圖輔助函式 ======
void drawHeart(int x, int y, int size) {
  int r = size / 2;
  display.fillCircle(x - r/2, y - r/2, r/2 + 1, SSD1306_WHITE);
  display.fillCircle(x + r/2, y - r/2, r/2 + 1, SSD1306_WHITE);
  display.fillTriangle(x - size, y - r/2, x + size, y - r/2, x, y + size/2 + 2, SSD1306_WHITE);
}

void drawEye(int x, int y, int w, int h, float pupilX, float pupilY, float eyelidTop) {
  display.fillRoundRect(x - w/2, y - h/2, w, h, 4, SSD1306_WHITE);
  int pR = w / 3; 
  int pX = x + (int)(pupilX * (w/4)); 
  int pY = y + (int)(pupilY * (h/4));
  display.fillCircle(pX, pY, pR, SSD1306_BLACK);
  if (eyelidTop > 0) {
    int coverH = (int)(h * eyelidTop);
    display.fillRect(x - w/2, y - h/2, w, coverH, SSD1306_BLACK);
  }
}

// ====== 頁面繪製函式 ======

// 1. 表情頁面
void drawFacePage() {
  unsigned long now = millis();
  
  // 互動超時回復
  if (currentMood != NORMAL && (now - lastInteractionMs > MOOD_RESET_MS)) { 
    currentMood = NORMAL; 
  }

  // 眨眼邏輯
  if (currentMood == NORMAL || currentMood == TIRED) {
    if (!isBlinking && (now - lastBlinkTime > nextBlinkInterval)) { isBlinking = true; blinkStartTime = now; }
  }
  float blinkState = 0.0; 
  if (isBlinking) {
    unsigned long blinkDuration = now - blinkStartTime;
    if (blinkDuration < 100) blinkState = 1.0; else if (blinkDuration < 200) blinkState = 0.0; 
    else { isBlinking = false; lastBlinkTime = now; nextBlinkInterval = random(2000, 6000); }
  }

  int lx = 40, ly = 32, rx = 88, ry = 32, ew = 36, eh = 40;
  display.clearDisplay();
  
  if (currentMood == LOVE) {
    drawHeart(lx, ly + 5, 18); drawHeart(rx, ry + 5, 18);
    display.fillTriangle(64, 45, 60, 40, 68, 40, SSD1306_WHITE);
  } else if (currentMood == TIRED || currentMood == ANGRY) {
    float lid = (currentMood == ANGRY) ? 0.6 : 0.55;
    drawEye(lx, ly, ew, eh, 0, -0.3, lid); drawEye(rx, ry, ew, eh, 0, -0.3, lid);
    display.drawLine(60, 50, 68, 50, SSD1306_WHITE);
    if(currentMood == ANGRY) { // 生氣眉毛
       display.drawLine(lx-15, ly-15, lx+10, ly-5, SSD1306_WHITE);
       display.drawLine(rx+15, ry-15, rx-10, ry-5, SSD1306_WHITE);
    }
  } else {
    float currentEyelid = (blinkState > 0) ? 1.0 : 0.0;
    float lookX = 0; int sec = (now / 1000) % 10; if(sec == 0) lookX = -0.4; if(sec == 5) lookX = 0.4;  
    drawEye(lx, ly, ew, eh, lookX, 0.3, currentEyelid); drawEye(rx, ry, ew, eh, lookX, 0.3, currentEyelid);
    display.drawLine(60, 48, 64, 50, SSD1306_WHITE); display.drawLine(64, 50, 68, 48, SSD1306_WHITE);
  }
}

// 2. 時鐘頁面 (NTP)
void drawClockPage() {
  display.clearDisplay();
  
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    display.setCursor(10, 30);
    display.setTextSize(1);
    display.println("Syncing Time...");
    return;
  }

  // 畫外框
  display.drawRoundRect(0, 0, 128, 64, 4, SSD1306_WHITE);

  // 日期
  display.setTextSize(1); 
  display.setCursor(6, 6); 
  display.printf("%02d/%02d", timeinfo.tm_mon + 1, timeinfo.tm_mday);
  
  // 星期
  display.setCursor(90, 6); 
  const char* wday[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"}; 
  display.print(wday[timeinfo.tm_wday]);

  // 時間 (大字)
  display.setTextSize(3); 
  display.setCursor(18, 20); 
  display.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

  // 秒數進度條
  int secWidth = map(timeinfo.tm_sec, 0, 60, 0, 116); 
  display.drawRect(6, 52, 116, 6, SSD1306_WHITE); 
  display.fillRect(8, 54, secWidth, 2, SSD1306_WHITE);
}

// 3. 天氣頁面 (靜態模擬)
void drawWeatherPage() {
  display.clearDisplay();
  
  // 畫個太陽
  display.fillCircle(30, 32, 10, SSD1306_WHITE);
  for(int i=0; i<8; i++) { 
    float angle = i * 45 * 3.14 / 180; 
    display.drawLine(30 + cos(angle)*14, 32 + sin(angle)*14, 30 + cos(angle)*18, 32 + sin(angle)*18, SSD1306_WHITE); 
  }

  // 文字資訊
  display.setTextSize(1); 
  display.setCursor(60, 10); display.print("Taichung");
  
  display.setTextSize(3); 
  display.setCursor(60, 25); display.print("24"); 
  display.setTextSize(1); display.print("C");

  display.setCursor(60, 52); display.print("Hum: 65%");
  
  // 頁面指示點
  display.fillCircle(60, 60, 2, SSD1306_WHITE);
  display.fillCircle(68, 60, 2, SSD1306_WHITE);
  display.fillCircle(76, 60, 2, SSD1306_WHITE);
}

// ====== HTTP 指令處理 ======
void handleCommand() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Connection", "close");

  if (server.hasArg("val")) {
    String cmd = server.arg("val");
    Serial.println("CMD: " + cmd);

    // 翻頁邏輯
    if(cmd == "next") { 
      currentPageInt++; 
      if(currentPageInt >= TOTAL_PAGES) currentPageInt = 0; 
    }
    if(cmd == "prev") { 
      currentPageInt--; 
      if(currentPageInt < 0) currentPageInt = TOTAL_PAGES - 1; 
    }
    
    // 表情互動
    if(cmd == "pat")   { currentMood = LOVE; currentPageInt = 0; lastInteractionMs = millis(); }
    if(cmd == "annoy") { currentMood = TIRED; currentPageInt = 0; lastInteractionMs = millis(); }

    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Args");
  }
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.setTextColor(SSD1306_WHITE);

  // 開機畫面
  display.clearDisplay();
  display.setCursor(0, 20); display.println("Connecting WiFi...");
  display.display();

  // 連接 WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  
  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());

  // 顯示 IP
  display.clearDisplay();
  display.setCursor(0, 10); display.println("WiFi OK!");
  display.setCursor(0, 30); display.println(WiFi.localIP());
  display.display();
  
  // 校時 (這會讓時鐘頁面生效)
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  // 啟動 Web Server
  server.on("/", [](){ server.send_P(200, "text/html", MAIN_page); });
  server.on("/cmd", handleCommand);
  server.begin();
  
  delay(2000); // 讓你看一下 IP
}

// ====== LOOP ======
void loop() {
  server.handleClient(); // 處理網頁請求

  switch(currentPageInt) {
    case 0: drawFacePage(); break;
    case 1: drawClockPage(); break;
    case 2: drawWeatherPage(); break;
  }
  
  display.display();
}