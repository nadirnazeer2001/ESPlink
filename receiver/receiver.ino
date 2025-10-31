#include <ESP32Servo.h>
#include <WiFi.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

typedef struct {
  uint8_t mode;
  uint8_t linear;
  uint8_t angular;
} TxData;

TxData receivedData;
Servo linearServo, angularServo;

// Shared variables
uint8_t currentLinear = 50;
uint8_t currentAngular = 50;
uint8_t targetLinear = 50;
uint8_t targetAngular = 50;
unsigned long lastDataTime = 0;
const unsigned long TIMEOUT_MS = 500;

void setupServos() {
  linearServo.attach(18, 1000, 2000);
  angularServo.attach(19, 1000, 2000);
  linearServo.writeMicroseconds(1500);
  angularServo.writeMicroseconds(1500);
}

void OnDataRecv(const esp_now_recv_info_t * esp_now_info, const uint8_t *incomingData, int len) {
  if (len == sizeof(TxData)) {
    memcpy(&receivedData, incomingData, len);
    targetLinear = receivedData.linear;
    targetAngular = receivedData.angular;
    lastDataTime = millis();
  }
}

void updateValues() {
  // If timeout, gradually reset to center
  if (millis() - lastDataTime > TIMEOUT_MS) {
    // Smoothly move toward center
    if (currentLinear > 50) currentLinear--;
    else if (currentLinear < 50) currentLinear++;
    
    if (currentAngular > 50) currentAngular--;
    else if (currentAngular < 50) currentAngular++;
  } else {
    // Normal operation - use received values directly
    currentLinear = targetLinear;
    currentAngular = targetAngular;
  }
}

// Main control task
void controlTask(void *parameter) {
  unsigned long lastPrint = 0;
  bool wasTimedOut = false;
  
  for (;;) {
    // Update values (with timeout handling)
    updateValues();
    
    // Update servos at 50Hz
    linearServo.writeMicroseconds(map(currentLinear, 0, 100, 1000, 2000));
    angularServo.writeMicroseconds(map(currentAngular, 0, 100, 1000, 2000));
    
    // Print status
    if (millis() - lastPrint > 200) {
      bool isTimedOut = (millis() - lastDataTime > TIMEOUT_MS);
      
      if (isTimedOut != wasTimedOut) {
        if (isTimedOut) {
          Serial.println("*** TIMEOUT - Returning to center ***");
        } else {
          Serial.println("*** DATA RECEIVED - Normal operation ***");
        }
        wasTimedOut = isTimedOut;
      }
      
      Serial.printf("Linear:%d Angular:%d %s\n", 
                   currentLinear, currentAngular, 
                   isTimedOut ? "[TIMEOUT]" : "");
      lastPrint = millis();
    }
    
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  setupServos();
  lastDataTime = millis();
  
  WiFi.mode(WIFI_STA);
  esp_now_init();
  esp_now_register_recv_cb(OnDataRecv);
  
  xTaskCreate(controlTask, "Control Task", 4096, NULL, 1, NULL);
  
  Serial.println("System Ready - 0.5s timeout with smooth reset");
}

void loop() {
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}
