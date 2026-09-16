/*
  ESP32-C3 SuperMini Desktop Pet v6.0 (離線隨機版)
  - 移除 Wi-Fi / 網頁伺服器
  - 開機直接顯示表情
  - 每 5 秒隨機切換心情

  [x] Adafruit SSD1306

  [x] Adafruit GFX Library

  [x] Adafruit BusIO

*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ====== 腳位設定 ======
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9

// OLED 設定
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ====== 狀態變數 ======
enum MoodState { NORMAL, TIRED, LOVE, ANGRY };
MoodState currentMood = NORMAL;

// 眨眼控制
unsigned long lastBlinkTime = 0;
unsigned long nextBlinkInterval = 3000;
bool isBlinking = false;
unsigned long blinkStartTime = 0;

// 隨機切換控制
unsigned long lastMoodChangeTime = 0;
const unsigned long MOOD_CHANGE_INTERVAL = 5000; // 5秒換一次

// ====== 繪圖邏輯 ======
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
  
  // 眼皮遮罩
  if (eyelidTop > 0) {
    int coverH = (int)(h * eyelidTop);
    display.fillRect(x - w/2, y - h/2, w, coverH, SSD1306_BLACK);
  }
}

void drawFacePage() {
  unsigned long now = millis();

  // 眨眼邏輯 (只有在 Normal 或 Tired 狀態下才眨眼)
  if (currentMood == NORMAL || currentMood == TIRED) {
    if (!isBlinking && (now - lastBlinkTime > nextBlinkInterval)) {
      isBlinking = true;
      blinkStartTime = now;
    }
  }

  float blinkState = 0.0; 
  if (isBlinking) {
    unsigned long blinkDuration = now - blinkStartTime;
    if (blinkDuration < 100) blinkState = 1.0;      // 閉眼
    else if (blinkDuration < 200) blinkState = 0.0; // 張眼
    else {
      isBlinking = false;
      lastBlinkTime = now;
      nextBlinkInterval = random(2000, 6000); // 下次眨眼隨機時間
    }
  }

  int lx = 40, ly = 32, rx = 88, ry = 32, ew = 36, eh = 40;
  display.clearDisplay();

  // --- 繪製不同表情 ---
  if (currentMood == LOVE) {
    // 愛心眼
    drawHeart(lx, ly + 5, 18); 
    drawHeart(rx, ry + 5, 18);
    // 微笑嘴
    display.fillTriangle(64, 45, 60, 40, 68, 40, SSD1306_WHITE);
  
  } else if (currentMood == ANGRY) {
    // 生氣眼 (上眼皮壓低)
    drawEye(lx, ly, ew, eh, 0, 0, 0.6); 
    drawEye(rx, ry, ew, eh, 0, 0, 0.6);
    // 生氣眉毛
    display.drawLine(lx-15, ly-15, lx+10, ly-5, SSD1306_WHITE);
    display.drawLine(rx+15, ry-15, rx-10, ry-5, SSD1306_WHITE);
    // 生氣嘴 (橫線)
    display.drawLine(60, 50, 68, 50, SSD1306_WHITE);

  } else if (currentMood == TIRED) {
    // 睏倦眼 (眼皮半垂)
    drawEye(lx, ly, ew, eh, 0, 0.2, 0.55); 
    drawEye(rx, ry, ew, eh, 0, 0.2, 0.55);
    // 嘴巴
    display.drawLine(60, 50, 68, 50, SSD1306_WHITE);

  } else {
    // 正常狀態 (NORMAL)
    float currentEyelid = (blinkState > 0) ? 1.0 : 0.0;
    
    // 讓眼珠稍微動一下
    float lookX = 0; 
    int sec = (now / 1000) % 10; 
    if(sec == 0 || sec == 1) lookX = -0.4; // 往左看
    if(sec == 5 || sec == 6) lookX = 0.4;  // 往右看
    
    drawEye(lx, ly, ew, eh, lookX, 0, currentEyelid); 
    drawEye(rx, ry, ew, eh, lookX, 0, currentEyelid);
    
    // 微笑嘴
    display.drawLine(60, 48, 64, 50, SSD1306_WHITE); 
    display.drawLine(64, 50, 68, 48, SSD1306_WHITE);
  }
}

// ====== 隨機切換邏輯 ======
void checkMoodChange() {
  unsigned long now = millis();
  if (now - lastMoodChangeTime > MOOD_CHANGE_INTERVAL) {
    lastMoodChangeTime = now;
    
    // 隨機選一個 0~3 的數字
    int randVal = random(0, 4);
    
    switch(randVal) {
      case 0: currentMood = NORMAL; Serial.println("Mood: Normal"); break;
      case 1: currentMood = LOVE;   Serial.println("Mood: Love"); break;
      case 2: currentMood = ANGRY;  Serial.println("Mood: Angry"); break;
      case 3: currentMood = TIRED;  Serial.println("Mood: Tired"); break;
    }
  }
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);
  
  // 初始化 I2C 與 OLED
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  // 如果你的 OLED 顯示不正常，試著把 SSD1306_SWITCHCAPVCC 改成 SSD1306_EXTERNALVCC
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  
  // 開機動畫 (選用)
  display.setTextSize(2); 
  display.setCursor(20, 25); 
  display.println("Hello!");
  display.display();
  delay(1000); // 只顯示 1 秒
  
  // 初始化隨機數種子
  randomSeed(analogRead(0)); 
}

// ====== LOOP ======
void loop() {
  // 1. 檢查是否該換表情了
  checkMoodChange();

  // 2. 繪製表情
  drawFacePage();
  
  // 3. 更新螢幕
  display.display();
}