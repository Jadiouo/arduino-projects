#include <Ultrasonic.h>  // Load the library
#include <Wire.h>
#include "SparkFun_VCNL4040_Arduino_Library.h" 

VCNL4040 proximitySensor;

// Arduino pins connected to each sensor's Trig and Echo pins
int RIGHT_TRIGGER_PIN = 7;
int RIGHT_ECHO_PIN    = 6;
int LEFT_TRIGGER_PIN  = 5;
int LEFT_ECHO_PIN     = 4;

// Motor shield control pins


// Threshold to determine if an object is close (in cm)
int CLOSE = 10; 

// Declare the two ultrasonic sensor objects
Ultrasonic uleft(LEFT_TRIGGER_PIN, LEFT_ECHO_PIN);
Ultrasonic uright(RIGHT_TRIGGER_PIN, RIGHT_ECHO_PIN);

void setup() {
  Serial.begin(9600); // Initialize serial port
  Wire.begin();                            // 啟動 I2C（漏掉了）
  if (proximitySensor.begin() == false) {  // 初始化感測器（漏掉了）
    Serial.println("VCNL4040 找不到，檢查接線！");
    while (1);
  }
  pinMode(12, OUTPUT);  // DIRA (left motor direction)
  pinMode(13, OUTPUT);  // DIRB (right motor direction)
  pinMode(3, OUTPUT);   // ENA (left motor enable)
  pinMode(11, OUTPUT);  // ENB (right motor enable)
}

void loop() {
  // Read the distances in centimeters and save to variables
  int left_dist  = uleft.read();
  int right_dist = uright.read();
  unsigned int proxValue = proximitySensor.getProximity();

  // Print both readings to the serial port
  Serial.print("L: ");
  Serial.print(left_dist);
  Serial.print(" cm   R: ");
  Serial.print(right_dist);
  Serial.println(" cm");

  Serial.print("Proximity: ");
  Serial.println(proxValue);

  // Print notices if something is close to either sensor
  if (left_dist <= CLOSE) {
    Serial.println("Object close to left sensor!");
  }
  if (right_dist <= CLOSE) {
    Serial.println("Object close to right sensor!");
  }

  delay(100);  // Wait 100 ms between readings

  if (proxValue > 40){
    digitalWrite(12, HIGH);  // Left motor forward
    digitalWrite(13, HIGH);  // Right motor forward
    digitalWrite(3, HIGH);   // Left motor on
    digitalWrite(11, HIGH);  // Right motor on
  }

  else if (left_dist < 20 ){
    digitalWrite(12, HIGH);  // Left motor forward
    digitalWrite(13, LOW);  // Right motor forward
    digitalWrite(3, HIGH);   // Left motor on
    digitalWrite(11, HIGH);  // Right motor on
  }
  else if (right_dist < 20 ){
    digitalWrite(12, LOW);  // Left motor forward
    digitalWrite(13, HIGH);  // Right motor forward
    digitalWrite(3, HIGH);   // Left motor on
    digitalWrite(11, HIGH);  // Right motor on
  }
  else{
    digitalWrite(12, LOW);  // Left motor forward
    digitalWrite(13, LOW);  // Right motor forward
    digitalWrite(3, HIGH);   // Left motor on
    digitalWrite(11, HIGH);  // Right motor on
    }


  // Adjust HIGH/LOW below according to your polarity notes





}