/* ===========================================================
   Arduino UNO + SSD1306 + Joystick + TTP22 + MQ-135 (Demo)
   Add "Cat Eyes" page:
   - Blinks every 2s
   - On touch (only on this page): eyes become hearts for 1.5s
   =========================================================== */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---- Pins ----
const int PIN_MQ135 = A0;
const int PIN_VRX   = A1;
const int PIN_VRY   = A2;
const int PIN_JSW   = 2;   // joystick switch (LOW when pressed)
const int PIN_TOUCH = 3;   // TTP22/223 digital out (HIGH when touched)

// ---- Joystick thresholds (0..1023) ----
const int TH_RIGHT = 800;   // >TH_RIGHT => right
const int TH_LEFT  = 200;   // <TH_LEFT  => left
const unsigned long JOY_DEBOUNCE_MS = 250;

// ---- Touch debounce ----
const unsigned long TOUCH_DEBOUNCE_MS = 250;

// ---- Idle / Power-save ----
const unsigned long IDLE_SLEEP_MS = 30000;  // 30s to displayOff

// ---- Timers ----
const unsigned long MQ135_PERIOD_MS = 500;
const unsigned long UI_REFRESH_MS   = 200;

// ---- State ----
enum Page : uint8_t {
  PAGE_IDLE=0, PAGE_WEATHER=1, PAGE_AIR=2, PAGE_NOTIFY=3, PAGE_CAT=4, PAGE_COUNT=5
};
volatile Page currentPage = PAGE_IDLE;

unsigned long t_lastInput = 0;
unsigned long t_lastJoy   = 0;
unsigned long t_lastTouch = 0;
unsigned long t_lastMQ    = 0;
unsigned long t_lastUI    = 0;

bool displaySleeping = false;

// ---- MQ-135 reading ----
int   mq135_raw = 0;
uint8_t iaq_level = 3; // 1(good) .. 5(bad) rough

// ---- Fake API/cache placeholders ----
struct WeatherCache {
  char city[12] = "Taichung";
  int  tempC = 24;      // mock
  int  pop   = 20;      // %
  int  tMin  = 22;
  int  tMax  = 28;
  char desc[12] = "Cloudy";
  unsigned long updated_ms = 0;
} wx;

// ---- Cat eyes animation state ----
bool catBlinkClosed = false;               // true: closed (blink frame)
unsigned long t_lastBlink = 0;
const unsigned long BLINK_PERIOD_MS = 2000; // blink every 2s

bool catHeartActive = false;
unsigned long t_heartUntil = 0;
const unsigned long HEART_DURATION_MS = 1500;

// ----------------- Helpers -----------------
void wakeDisplayIfNeeded() {
  if (displaySleeping) {
    display.ssd1306_command(SSD1306_DISPLAYON);
    displaySleeping = false;
  }
  t_lastInput = millis();
}

void maybeSleepDisplay() {
  if (!displaySleeping && (millis() - t_lastInput >= IDLE_SLEEP_MS)) {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    displaySleeping = true;
  }
}

void drawHeader(const char* title) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.print(title);
  display.drawLine(0,10,127,10,SSD1306_WHITE);
}

// ---------- UI Pages ----------
void renderIdle() {
  drawHeader("Idle");
  display.setCursor(0,16);
  display.print("City: ");
  display.print(wx.city);
  display.setCursor(0,28);
  display.print("Temp: ");
  display.print(wx.tempC);
  display.print(" C");
  display.setCursor(0,40);
  display.print("Desc: ");
  display.print(wx.desc);
  display.setCursor(0,52);
  display.print("Use joystick to flip");
}

void renderWeather() {
  drawHeader("Weather (Mock)");
  display.setCursor(0,16);
  display.print("City: "); display.print(wx.city);
  display.setCursor(0,28);
  display.print("Now: "); display.print(wx.tempC); display.print("C");
  display.setCursor(0,40);
  display.print("Min~Max: "); display.print(wx.tMin); display.print("~"); display.print(wx.tMax);
  display.setCursor(0,52);
  display.print("PoP: "); display.print(wx.pop); display.print("%  "); display.print(wx.desc);
}

void renderAir() {
  drawHeader("Air (MQ-135)");
  display.setCursor(0,16);
  display.print("Raw: "); display.print(mq135_raw);
  display.setCursor(0,28);
  display.print("IAQ: ");
  display.print(iaq_level);
  display.print("  (1 good ~ 5 bad)");
  int barW = map(iaq_level, 1, 5, 20, 120);
  display.drawRect(0,42,124,12,SSD1306_WHITE);
  display.fillRect(2,44,barW-4,8,SSD1306_WHITE);
  display.setCursor(0,56);
  display.print("Touch = note / wake");
}

void renderNotify() {
  drawHeader("Notify");
  display.setCursor(0,16);
  display.print("Interaction events show here.");
  display.setCursor(0,28);
  display.print("API/Speaker reserved.");
  display.setCursor(0,40);
  display.print("UNO: no Wi-Fi / I2S.");
  display.setCursor(0,52);
  display.print("Use Right/Left to cycle");
}

// ---- Cat Eyes drawing primitives ----
void drawEyeOpen(int cx, int cy, int rOuter, int rPupil, int pupilOffsetX) {
  // outer eye (simple circle as stylized eye)
  display.drawCircle(cx, cy, rOuter, SSD1306_WHITE);
  // pupil
  display.fillCircle(cx + pupilOffsetX, cy, rPupil, SSD1306_WHITE);
  // small "sparkle"
  display.drawPixel(cx + pupilOffsetX - 1, cy - 2, SSD1306_WHITE);
}

void drawEyeClosed(int cx, int cy, int w) {
  // closed lid: two lines to look like eyelid
  display.drawLine(cx - w/2, cy, cx + w/2, cy, SSD1306_WHITE);
  display.drawLine(cx - w/2, cy+1, cx + w/2, cy+1, SSD1306_WHITE);
}

void drawHeart(int cx, int cy, int s) {
  // heart made of two circles + triangle
  int r = s/2;
  // top lobes
  display.fillCircle(cx - r/2, cy - r/2, r/2, SSD1306_WHITE);
  display.fillCircle(cx + r/2, cy - r/2, r/2, SSD1306_WHITE);
  // bottom triangle
  display.fillTriangle(cx - s/2, cy - r/2, cx + s/2, cy - r/2, cx, cy + s/2, SSD1306_WHITE);
}

void renderCatEyes() {
  drawHeader("Cat Eyes");
  // Eye layout
  const int eyeY = 36;      // vertical center of eyes
  const int eyeLX = 42;     // left eye center
  const int eyeRX = 86;     // right eye center
  const int eyeR  = 12;     // outer radius
  const int pupilR = 4;
  const int eyeWidthForClosed = 22;

  if (catHeartActive) {
    // Heart eyes
    drawHeart(eyeLX, eyeY, 16);
    drawHeart(eyeRX, eyeY, 16);
    display.setCursor(24, 54);
    display.print("<3 Meow!");
    return;
  }

  if (catBlinkClosed) {
    // closed/blink frame
    drawEyeClosed(eyeLX, eyeY, eyeWidthForClosed);
    drawEyeClosed(eyeRX, eyeY, eyeWidthForClosed);
    display.setCursor(28, 54);
    display.print("Blink...");
  } else {
    // open eyes, pupils slightly inward for cute look
    drawEyeOpen(eyeLX, eyeY, eyeR, pupilR, +1);
    drawEyeOpen(eyeRX, eyeY, eyeR, pupilR, -1);
    display.setCursor(18, 54);
    display.print("Touch to send hearts!");
  }
}

void drawPage(Page p) {
  display.clearDisplay();
  switch (p) {
    case PAGE_IDLE:    renderIdle();    break;
    case PAGE_WEATHER: renderWeather(); break;
    case PAGE_AIR:     renderAir();     break;
    case PAGE_NOTIFY:  renderNotify();  break;
    case PAGE_CAT:     renderCatEyes(); break;
    default:           renderIdle();    break;
  }
  display.display();
}

void nextPage() {
  currentPage = static_cast<Page>((currentPage + 1) % PAGE_COUNT);
}

void prevPage() {
  currentPage = static_cast<Page>((currentPage + PAGE_COUNT - 1) % PAGE_COUNT);
}

// ------------- Input Handlers --------------
void handleJoystick() {
  int x = analogRead(PIN_VRX);
  unsigned long now = millis();

  if (now - t_lastJoy >= JOY_DEBOUNCE_MS) {
    if (x > TH_RIGHT) {
      nextPage();
      t_lastJoy = now;
      wakeDisplayIfNeeded();
    } else if (x < TH_LEFT) {
      prevPage();
      t_lastJoy = now;
      wakeDisplayIfNeeded();
    }
  }

  if (digitalRead(PIN_JSW) == LOW) {
    if (now - t_lastJoy >= JOY_DEBOUNCE_MS) {
      currentPage = PAGE_NOTIFY;
      t_lastJoy = now;
      wakeDisplayIfNeeded();
    }
  }
}

void handleTouch() {
  unsigned long now = millis();
  bool touched = (digitalRead(PIN_TOUCH) == HIGH);

  if (touched && (now - t_lastTouch >= TOUCH_DEBOUNCE_MS)) {
    wakeDisplayIfNeeded();

    // ① 先做原本的互動：在貓咪頁觸發愛心眼（不在也一樣觸發）
    catHeartActive = true;
    t_heartUntil   = now + HEART_DURATION_MS;

    // ② 新增：無論目前在哪一頁，都強制跳到「貓咪頁」
    currentPage = PAGE_CAT;

    // ③ 讓畫面更快更新（可選）
    t_lastUI = 0;

    t_lastTouch = now;
  }
}


// ------------- Sensors & Animations -------------
void readMQ135() {
  mq135_raw = analogRead(PIN_MQ135);
  if      (mq135_raw < 200) iaq_level = 1;
  else if (mq135_raw < 350) iaq_level = 2;
  else if (mq135_raw < 500) iaq_level = 3;
  else if (mq135_raw < 700) iaq_level = 4;
  else                      iaq_level = 5;
}

void updateCatAnimations() {
  unsigned long now = millis();

  // Blink toggle every 2s (only when on Cat page and not in heart mode)
  if (currentPage == PAGE_CAT && !catHeartActive) {
    if (now - t_lastBlink >= BLINK_PERIOD_MS) {
      catBlinkClosed = !catBlinkClosed;
      t_lastBlink = now;
    }
  } else {
    // reset blink when leaving page
    catBlinkClosed = false;
  }

  // Heart timeout
  if (catHeartActive && now >= t_heartUntil) {
    catHeartActive = false;
  }
}

// ----------------- Setup / Loop -----------------
void setup() {
  pinMode(PIN_JSW, INPUT_PULLUP);
  pinMode(PIN_TOUCH, INPUT);
  pinMode(PIN_MQ135, INPUT);

  Wire.begin(); // UNO: SDA=A4, SCL=A5
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;); // halt if display init fails
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Booting UNO Console...");
  display.display();

  wx.updated_ms = millis();

  t_lastInput = millis();
  t_lastUI = millis();
  t_lastBlink = millis();
}

void loop() {
  unsigned long now = millis();

  handleJoystick();
  handleTouch();

  // Sensors
  if (now - t_lastMQ >= MQ135_PERIOD_MS) {
    readMQ135();
    t_lastMQ = now;
  }

  // Animations (cat eyes)
  updateCatAnimations();

  // UI
  if (now - t_lastUI >= UI_REFRESH_MS) {
    drawPage(currentPage);
    t_lastUI = now;
  }

  // Power save
  maybeSleepDisplay();
}
