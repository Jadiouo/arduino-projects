#include <Arduino.h>
#include <Wire.h>
#include <VL53L0X.h>
#include <math.h>

// ==== PIN 設定（依實際接線修改） ====
const int PIN_IR_L   = 34;   // 左邊 FC-51 OUT
const int PIN_IR_R   = 35;   // 右邊 FC-51 OUT

const int PIN_ML_IN1 = 25;   // 左馬達 IN1
const int PIN_ML_IN2 = 26;   // 左馬達 IN2
const int PIN_MR_IN1 = 27;   // 右馬達 IN1
const int PIN_MR_IN2 = 14;   // 右馬達 IN2

// ==== 幾何與安全參數 ====
const float TOF_ANGLE_RAD = 30.0 * PI / 180.0;
const float COS_TOF       = cos(TOF_ANGLE_RAD);
const float DFRONT_CM     = 5.0;   // TOF 到車頭的水平距離（自己量）

const float Di_SAFE_CM    = 10.0;  // 接近這裡開始當作「危險區」
const float Di_STOP_CM    = 4.0;   // 低於這個啟動 emergencyBrakeAndRetreat

// ==== IR 安全度計算參數（FC-51 用數位訊號） ====
// 這裡 md = 0~100，先簡單用「有偵測到 → 0；沒偵測到 → 100」
const float mdSafeValue   = 100.0; // 沒偵測到邊緣
const float mdDangerValue = 0.0;   // 偵測到邊緣 / 障礙
const float mdDiffThresh  = 8.0;   // 左右安全度差異門檻（其實只要 >0 就有差）

// ==== 速度 / 轉彎設定 ====
const int   MAX_SPEED     = 220;   // 最高速度（先不要 255，留一點 margin）
const int   MIN_SPEED     = 80;    // 最慢還在動的速度
const int   BASE_STEER    = 40;    // 基本轉彎力量
const int   EXTRA_STEER   = 70;    // 風險高時額外轉彎力量

// steerSign 更新節奏（左右差不多時，多久抽籤換一次方向）
const unsigned long STEER_UPDATE_INTERVAL_MS = 800;

// ==== 全域變數 ====
int steerSign = 0;   // -1 左, 0 直行, +1 右
unsigned long lastSteerUpdate = 0;

// 供計算 dDi 用
float Di_prev = 0.0;
bool  Di_prev_valid = false;

// TOF 物件
VL53L0X tof;

// ========== 感測器相關 ==========

// 讀 TOF 距離（cm）
float readTofDistanceCm() {
  uint16_t mm = tof.readRangeContinuousMillimeters();
  if (tof.timeoutOccurred()) {
    // 讀不到就回一個很大的值，讓程式當作「超遠 / 無障礙」
    return 200.0;
  }
  return mm / 10.0;  // 轉成 cm
}

// FC-51：讀數位值（0 or 1）
int readIrDigital(int pin) {
  return digitalRead(pin);
}

// 數位 IR → 安全度（0~100，越大越安全）
// 多數 FC-51：OUT = LOW → 有偵測 / 很近；OUT = HIGH → 安全
float mapIrDigitalToSpace(int dig) {
  if (dig == LOW) {
    // 偵測到障礙 / 地面近 → 危險
    return mdDangerValue;
  } else {
    // 沒偵測到 → 安全
    return mdSafeValue;
  }
}

// ========== 馬達底層控制 ==========

// speed: 0~255, dir: 1 前進, -1 後退, 0 停
void setMotor(int in1, int in2, int speed, int dir) {
  speed = constrain(speed, 0, 255);
  if (dir > 0) {
    analogWrite(in1, speed);
    analogWrite(in2, 0);
  } else if (dir < 0) {
    analogWrite(in1, 0);
    analogWrite(in2, speed);
  } else {
    analogWrite(in1, 0);
    analogWrite(in2, 0);
  }
}

// 快速短路煞車（Brake 模式）
void setBrakeMode() {
  // DRV8833：兩腳同電位 = brake（這裡用 LOW，也可以改 HIGH）
  digitalWrite(PIN_ML_IN1, LOW);
  digitalWrite(PIN_ML_IN2, LOW);
  digitalWrite(PIN_MR_IN1, LOW);
  digitalWrite(PIN_MR_IN2, LOW);
}

// 同方向前進的曲線運動：左右輪速度略有差異
void driveCurve(int baseSpeed, int steerSign, int steerPower) {
  int left  = baseSpeed;
  int right = baseSpeed;

  if (steerSign < 0) {        // 左轉 → 右輪快、左輪慢
    left  -= steerPower;
    right += steerPower;
  } else if (steerSign > 0) { // 右轉 → 左輪快、右輪慢
    left  += steerPower;
    right -= steerPower;
  }

  left  = constrain(left,  0, 255);
  right = constrain(right, 0, 255);

  setMotor(PIN_ML_IN1, PIN_ML_IN2, left,  1);
  setMotor(PIN_MR_IN1, PIN_MR_IN2, right, 1);
}

// 往後退（可以稍微偏向 safeDir）
void driveBackward(int speed, int safeDir) {
  int left  = speed;
  int right = speed;

  int curve = 30;  // 往安全方向微微帶一下弧度

  if (safeDir < 0) {        // 往左比較安全 → 後退時朝左偏
    left  -= curve;
    right += curve;
  } else if (safeDir > 0) { // 往右比較安全
    left  += curve;
    right -= curve;
  }

  left  = constrain(left,  0, 255);
  right = constrain(right, 0, 255);

  setMotor(PIN_ML_IN1, PIN_ML_IN2, left,  -1);
  setMotor(PIN_MR_IN1, PIN_MR_IN2, right, -1);
}

void stopMotor() {
  setMotor(PIN_ML_IN1, PIN_ML_IN2, 0, 0);
  setMotor(PIN_MR_IN1, PIN_MR_IN2, 0, 0);
}

// ========== 高速狀態下的「最後防線」：急煞 + 反轉拉回來 ==========

// safeDir: -1 左, 0 直退, +1 右
void emergencyBrakeAndRetreat(int safeDir) {
  // 1) 先短路煞車，咬住一點動能
  setBrakeMode();
  delay(80);   // 80ms 可調

  // 2) 再小力反轉一小段時間（直覺上的「嚇到猛退一下」）
  int backSpeed = 80;   // MAX 255 的 1/3 左右
  driveBackward(backSpeed, safeDir);
  delay(150);           // 0.15 秒

  stopMotor();
}

// ========== 方向偏好 / 中線保持 ==========

// 依 mdL / mdR + 隨機，更新 steerSign
void updateSteerSign(float mdL, float mdR) {
  float diff = mdR - mdL;  // >0 表示右邊比較安全

  if (fabs(diff) > mdDiffThresh) {
    // 很明顯哪邊較安全，就長期往那邊彎
    steerSign = (diff > 0) ? +1 : -1;
  } else {
    // 兩邊差不多 → 偶爾抽籤換方向，避免軌跡太死
    unsigned long now = millis();
    if (now - lastSteerUpdate > STEER_UPDATE_INTERVAL_MS) {
      int r = random(-1, 2); // -1, 0, 1
      steerSign = r;
      lastSteerUpdate = now;
    }
  }
}

// ========== setup / loop ==========

void setup() {
  Serial.begin(115200);

  pinMode(PIN_IR_L, INPUT);   // FC-51 OUT，預設板上有拉電阻，一般不用 PULLUP
  pinMode(PIN_IR_R, INPUT);

  pinMode(PIN_ML_IN1, OUTPUT);
  pinMode(PIN_ML_IN2, OUTPUT);
  pinMode(PIN_MR_IN1, OUTPUT);
  pinMode(PIN_MR_IN2, OUTPUT);

  Wire.begin();          // ESP32 I2C 啟動
  tof.setTimeout(500);
  if (!tof.init()) {
    Serial.println("Failed to detect and initialize VL53L0X!");
    while (1) { delay(100); }
  }
  tof.startContinuous(); // 連續量測模式

  randomSeed(analogRead(PIN_IR_L));
  stopMotor();
}

void loop() {
  // 1. 讀感測器
  float tofDist = readTofDistanceCm();
  int   irLVal  = readIrDigital(PIN_IR_L);
  int   irRVal  = readIrDigital(PIN_IR_R);

  float Di  = tofDist * COS_TOF - DFRONT_CM;        // 正前方可行距離估計
  float mdL = mapIrDigitalToSpace(irLVal);          // 左側安全度 0~100
  float mdR = mapIrDigitalToSpace(irRVal);          // 右側安全度 0~100

  // 初始化上一輪 Di
  if (!Di_prev_valid) {
    Di_prev = Di;
    Di_prev_valid = true;
  }

  // 2. 計算風險值 risk（0~1）
  float risk = 0.0;

  // 2-1 前方距離風險：Di < 20 才開始算
  if (Di < 20.0) {
    risk += (20.0 - Di) / 20.0;   // Di=20 → +0, Di=0 → +1
  }

  // 2-2 左右邊緣風險（取比較危險的那一邊）
  float mdMin = (mdL < mdR) ? mdL : mdR;
  float edgeRisk = (100.0 - mdMin) / 100.0;   // mdMin=100 → 0, mdMin=0 → 1
  risk += 0.5 * edgeRisk;                     // 權重 0.5，可調

  // 2-3 距離變化率風險：高速衝向邊緣時提前收油
  float dDi = Di - Di_prev;   // 單位 ≈ 每次 loop 的 cm 差
  if (dDi < -3.0) {           // 這次比上次少 >3cm，代表衝很快
    risk += 0.5;              // 再加一點風險
  }
  Di_prev = Di;

  // 2-4 限制 risk 在 0~1
  if (risk < 0.0) risk = 0.0;
  if (risk > 1.0) risk = 1.0;

  // 3. 更新轉向偏好（目標：長期讓車跑在桌面中間）
  updateSteerSign(mdL, mdR);

  // 4. 緊急保護：最後防線（Di 很小時直接急煞 + 後退）
  if (Di <= Di_STOP_CM) {
    int safeDir = 0;
    if (mdL > mdR + mdDiffThresh) safeDir = -1;     // 左邊安全 → 往左退
    else if (mdR > mdL + mdDiffThresh) safeDir = 1; // 右邊安全 → 往右退

    emergencyBrakeAndRetreat(safeDir);
    return;   // 這圈 loop 到此為止，下一圈再重來
  }

  // 5. 用 risk 算速度：risk=0 → full, risk=1 → min
  int speed = MIN_SPEED + (int)((1.0 - risk) * (MAX_SPEED - MIN_SPEED));

  // 6. 用 risk 調整轉彎力量：越危險 → 轉彎越兇
  int steerPower = BASE_STEER + (int)(EXTRA_STEER * risk);

  // 7. 根據目前的 steerSign + speed + steerPower 開車
  driveCurve(speed, steerSign, steerPower);

  // Debug 先打開看數據，調完再關
  /*
  Serial.print("Di="); Serial.print(Di);
  Serial.print(" risk="); Serial.print(risk);
  Serial.print(" mdL="); Serial.print(mdL);
  Serial.print(" mdR="); Serial.print(mdR);
  Serial.print(" steer="); Serial.print(steerSign);
  Serial.print(" speed="); Serial.println(speed);
  */

  delay(20);  // 約 50Hz 更新
}
