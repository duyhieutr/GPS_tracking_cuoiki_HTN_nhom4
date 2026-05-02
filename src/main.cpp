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

float latitude = 0;
float longitude = 0;

bool alarmActive = false;
bool sensorAlarm = false;

typedef struct {
  int type;
} Event_t;

QueueHandle_t eventQueue;

void sendToFirebase(String path, float value) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(DATABASE_URL) + path + ".json?auth=" + DATABASE_SECRET;

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.PUT(String(value));
  http.end();
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }
}

float getDistance() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  long duration = pulseIn(ECHO, HIGH, 30000);
  return duration * 0.034 / 2;
}

void IRAM_ATTR sosISR() {
  Event_t e = {1};
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(eventQueue, &e, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void IRAM_ATTR stopISR() {
  Event_t e = {2};
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(eventQueue, &e, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void taskSensor(void *pv) {
  bool trig = false;

  while (1) {
    float d = getDistance();

    if (d > 0 && d < 10) {
      if (!trig) {
        Event_t e = {3};
        xQueueSend(eventQueue, &e, 0);
        trig = true;
      }
    } else {
      trig = false;
    }

    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

void taskEvent(void *pv) {
  Event_t e;

  while (1) {
    if (xQueueReceive(eventQueue, &e, portMAX_DELAY)) {

      if (e.type == 1) {
        alarmActive = true;
        digitalWrite(BUZZER, HIGH);
        sendToFirebase("/gps/xe01/sos", 1);
      }

      if (e.type == 2) {
        alarmActive = false;
        sensorAlarm = false;
        digitalWrite(BUZZER, LOW);
        sendToFirebase("/gps/xe01/sos", 0);
        sendToFirebase("/gps/xe01/alarm", 0);
      }

      if (e.type == 3) {
        if (!alarmActive && !sensorAlarm) {
          sensorAlarm = true;
          digitalWrite(BUZZER, HIGH);
          sendToFirebase("/gps/xe01/alarm", 1);
        }
      }
    }
  }
}

void taskGPS(void *pv) {
  float lastLat = 0;
  float lastLng = 0;

  while (1) {
    while (gpsSerial.available()) {
      gps.encode(gpsSerial.read());
    }

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

      if (abs(latitude - lastLat) > 0.00001 || abs(longitude - lastLng) > 0.00001) {
        sendToFirebase("/gps/xe01/lat", latitude);
        sendToFirebase("/gps/xe01/lng", longitude);
        lastLat = latitude;
        lastLng = longitude;
      }
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);
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

  xTaskCreatePinnedToCore(taskSensor, "sensor", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskEvent, "event", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(taskGPS, "gps", 4096, NULL, 1, NULL, 0);
}

void loop() {}