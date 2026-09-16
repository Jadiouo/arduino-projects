# arduino-projects

個人 Arduino / ESP32 sketch 集合（Arduino IDE sketchbook）。

| Sketch | 開發板 | 硬體 | 說明 |
|---|---|---|---|
| `auto_car/` | Arduino UNO + 馬達擴充板 | 2× HC-SR04 超音波、SparkFun VCNL4040 近距感測 | 避障自走車：讀左右超音波距離與前方 proximity，控制馬達前進/轉向 |
| `pet/` | ESP32-C3 SuperMini | SSD1306 OLED (I2C) | 桌面寵物 v6.0：開機顯示表情、眨眼動畫、每 5 秒隨機切換心情 |
| `pet_offline/` | ESP32-C3 SuperMini | SSD1306 OLED | `pet` 的離線版（附所需函式庫清單） |
| `pet_online/` | ESP32-C3 SuperMini | SSD1306 OLED + Wi-Fi | 桌面寵物 v7.0：網頁分頁控制（表情 / NTP 時鐘 / 天氣），內建 WebServer |
| `rat/` | ESP32 + DRV8833 | VL53L0X ToF、2× FC-51 紅外線 | 老鼠機器人：ToF 測前方距離、IR 判斷地面/障礙安全度，含急煞與反轉脫困 |
| `sidekit/` | ESP32-S3 | ST7789 (SPI)、VL53L0X、MPR121 觸控、BME680、MAX98357A (I2S) | 桌面環境主控台：Wi-Fi 校時、中央氣象署天氣、空氣品質 IAQ、人靠近亮屏、I2S 提示音 |
| `sketch_oct22a/` | Arduino UNO | SSD1306 OLED、搖桿、TTP223 觸控、MQ-135 | 多頁面 UI Demo：搖桿切頁、貓咪眼睛動畫（觸摸變愛心眼）、MQ-135 空氣讀值 |

## 使用方式

1. 把這個 repo clone 成 Arduino IDE 的 sketchbook 目錄（或把需要的 sketch 資料夾複製過去）。
2. 需要 Wi-Fi / API 金鑰的 sketch（`pet_online`、`sidekit`）：把該資料夾內的 `secrets.h.example` 複製成 `secrets.h` 並填入自己的值。`secrets.h` 已被 git 忽略，不會被推上去。
3. 用 Library Manager 安裝需要的函式庫（`libraries/` 不在版控內）：

| 函式庫 | 用於 |
|---|---|
| Adafruit GFX Library、Adafruit SSD1306、Adafruit BusIO | pet / pet_offline / pet_online / sketch_oct22a |
| Adafruit ST7735 and ST7789 Library | sidekit |
| Adafruit VL53L0X | sidekit |
| VL53L0X (Pololu) | rat |
| Adafruit MPR121、Adafruit BME680、Adafruit Unified Sensor | sidekit |
| ArduinoJson | sidekit |
| Ultrasonic、SparkFun VCNL4040 Proximity Sensor Library | auto_car |

## 開發板套件

- ESP32 系列：Arduino IDE 需安裝 `esp32` by Espressif（Boards Manager）。
