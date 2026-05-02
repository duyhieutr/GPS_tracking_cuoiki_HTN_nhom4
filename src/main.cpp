#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <TinyGPS++.h>
#include <HTTPClient.h>
#include <LiquidCrystal_I2C.h>
#define WIFI_SSID "Duy Hieu"
#define WIFI_PASS "28042004"
#define DATABASE_URL "https://polynomial-coda-389612-default-rtdb.firebaseio.com"
#define DATABASE_SECRET "EyjryNGI1PMwWwDEBzSwFrkVxc8Fi03K4RY1KbWc"
#define GPS_RX 16
#define GPS_TX 17
#define TRIG 5
#define ECHO 18
#define SOS_BTN 27
#define STOP_BTN 14
#define BUZZER 25
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);
LiquidCrystal_I2C lcd(0x27, 16, 2);
bool sosActive = false;
bool obstacleState = false;
uint32_t lastObstacleTime = 0;
float latitude = 0;
float longitude = 0;
typedef struct {
  int type;
} Event_t;
QueueHandle_t eventQueue;

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(300);
}
void sendToFirebase(String path, float value) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String(DATABASE_URL) + path + ".json?auth=" + DATABASE_SECRET;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.PUT(String(value));
  http.end();
}
float readOnce() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long duration = pulseIn(ECHO, HIGH, 30000);
  if (duration == 0) return -1;
  float d = duration * 0.034 / 2;
  if (d < 2 || d > 400) return -1;
  return d;
}
float getDistance() {
  float sum = 0;
  int count = 0;

  for (int i = 0; i < 3; i++) {
    float d = readOnce();
    if (d > 0) {
      sum += d;
      count++;
    }
    delay(10);
  }
  if (count == 0) return -1;
  return sum / count;
}
void IRAM_ATTR sosISR() {
  Event_t e = {1};
  xQueueSendFromISR(eventQueue, &e, NULL);
}
void IRAM_ATTR stopISR() {
  Event_t e = {2};
  xQueueSendFromISR(eventQueue, &e, NULL);
}
void taskSensor(void *pv) {
  while (1) {
    float d = getDistance();
    uint32_t now = millis();
    if (d > 0 && d < 10) {
      lastObstacleTime = now;
      if (!obstacleState) {
        obstacleState = true;
        Serial.println("ENTER ZONE");

        digitalWrite(BUZZER, HIGH);
        delay(120);
        digitalWrite(BUZZER, LOW);
        sendToFirebase("/gps/xe01/alarm", 1);
      }
    }
    if (obstacleState && d > 15) {
      if (now - lastObstacleTime > 800) {
        obstacleState = false;
        Serial.println("EXIT ZONE");
        sendToFirebase("/gps/xe01/alarm", 0);
      }
    }
    if (!sosActive && !obstacleState) {
      digitalWrite(BUZZER, LOW);
    }
    delay(120);
  }
}
void taskEvent(void *pv) {
  Event_t e;
  while (1) {
    if (xQueueReceive(eventQueue, &e, portMAX_DELAY)) {
      switch (e.type) {
        case 1:
          sosActive = true;
          digitalWrite(BUZZER, HIGH);
          sendToFirebase("/gps/xe01/sos", 1);
          break;
        case 2:
          sosActive = false;
          obstacleState = false;
          digitalWrite(BUZZER, LOW);
          sendToFirebase("/gps/xe01/sos", 0);
          sendToFirebase("/gps/xe01/alarm", 0);
          break;
      }
    }
  }
}
void taskGPS(void *pv) {
  while (1) {
    while (gpsSerial.available())
      gps.encode(gpsSerial.read());
    if (gps.location.isUpdated()) {
      latitude = gps.location.lat();
      longitude = gps.location.lng();
      lcd.setCursor(0, 0);
      lcd.print("LAT:");
      lcd.print(latitude, 4);
      lcd.print("   ");
      lcd.setCursor(0, 1);
      lcd.print("LNG:");
      lcd.print(longitude, 4);
      lcd.print("   ");
      sendToFirebase("/gps/xe01/lat", latitude);
      sendToFirebase("/gps/xe01/lng", longitude);
    }

    delay(1000);
  }
}
void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);
  pinMode(SOS_BTN, INPUT_PULLUP);
  pinMode(STOP_BTN, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);
  eventQueue = xQueueCreate(10, sizeof(Event_t));
  attachInterrupt(digitalPinToInterrupt(SOS_BTN), sosISR, FALLING);
  attachInterrupt(digitalPinToInterrupt(STOP_BTN), stopISR, FALLING);
  connectWiFi();
  Serial.println("SYSTEM READY");
  xTaskCreatePinnedToCore(taskSensor, "sensor", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskEvent, "event", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskGPS, "gps", 4096, NULL, 1, NULL, 0);
}
void loop() {}

