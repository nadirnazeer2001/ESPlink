#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <esp_now.h>

// ---------------- Display ----------------
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ---------------- Pins ----------------
#define BTN_UP     27
#define BTN_DOWN   26
#define BTN_LEFT   12
#define BTN_RIGHT  14
#define BTN_OK     25

const int J1X_PIN = 34; // Angular
const int J2X_PIN = 32; // Linear

// ---------------- ESP-NOW Receiver MAC ----------------
uint8_t receiverMAC[] = {0x14, 0x08, 0x08, 0xA6, 0x8E, 0x68};

// ---------------- Data To Send ----------------
typedef struct {
  uint8_t mode;     // 0 Serial, 1 PWM, 2 CAN
  uint8_t linear;   // 0-100
  uint8_t angular;  // 0-100
} __attribute__((packed)) TxData;

TxData txPacket;

// ---------------- Shared Variables ----------------
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

volatile float angularValue = 50.0;
volatile float linearValue  = 50.0;
volatile int trimX = 50;
volatile int trimY = 50;

volatile int centerJ1 = 2048, centerJ2 = 2048;
volatile int minJ1 = 4095, maxJ1 = 0;
volatile int minJ2 = 4095, maxJ2 = 0;

enum Screen { MAIN_MENU, TRIM_MENU, TRIM_X, TRIM_Y, MODE_MENU, VALUE_SCREEN };
volatile Screen screen = MAIN_MENU;

volatile int menuIndex = 0;
volatile int modeIndex = 1; // default PWM

// ---------------- Parameters ----------------
const int CALIB_SAMPLES = 200;
const int DEADZONE_ADC = 50;
const float SMOOTH_ALPHA = 0.25;

// ---------------- Extended Range Parameters ----------------
const int EXTENDED_RANGE = 800; // How far beyond current min/max we allow
const int MIN_RANGE = 500;      // Minimum required range from center

// ---------------- Button Debouncing ----------------
unsigned long lastButtonPressTime = 0;
const unsigned long BUTTON_DEBOUNCE = 150;

// ---------------- Display Refresh Control ----------------
volatile bool screenDirty = true;
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_MIN_INTERVAL = 33;

// ---------------- Simple Button Reading ----------------
bool btnPressed(int pin) {
  if (digitalRead(pin) == LOW) {
    unsigned long currentTime = millis();
    if (currentTime - lastButtonPressTime > BUTTON_DEBOUNCE) {
      lastButtonPressTime = currentTime;
      return true;
    }
  }
  return false;
}

// Special function for trim buttons with repeat
bool btnPressedTrim(int pin) {
  static unsigned long lastTrimTime = 0;
  static unsigned long lastRepeatTime = 0;
  static bool isRepeating = false;
  const unsigned long INITIAL_DELAY = 500;
  const unsigned long REPEAT_INTERVAL = 100;
  
  if (digitalRead(pin) == LOW) {
    unsigned long currentTime = millis();
    
    if (!isRepeating) {
      if (currentTime - lastButtonPressTime > BUTTON_DEBOUNCE) {
        lastButtonPressTime = currentTime;
        lastTrimTime = currentTime;
        isRepeating = true;
        return true;
      }
    } else {
      if (currentTime - lastTrimTime > INITIAL_DELAY) {
        if (currentTime - lastRepeatTime > REPEAT_INTERVAL) {
          lastRepeatTime = currentTime;
          return true;
        }
      }
    }
  } else {
    isRepeating = false;
  }
  
  return false;
}

// ---------------- Improved Map Function with Extended Range ----------------
int mapLearn(int raw, int minV, int centerV, int maxV) {
  // Apply deadzone around center first
  if (abs(raw - centerV) <= DEADZONE_ADC) {
    return 50;
  }
  
  // Calculate effective min and max with extended range
  int effectiveMin = minV;
  int effectiveMax = maxV;
  
  // Ensure minimum range from center for better resolution
  if (centerV - effectiveMin < MIN_RANGE) {
    effectiveMin = centerV - MIN_RANGE;
  }
  if (effectiveMax - centerV < MIN_RANGE) {
    effectiveMax = centerV + MIN_RANGE;
  }
  
  // Allow extended range beyond current min/max
  effectiveMin = min(effectiveMin, centerV - EXTENDED_RANGE);
  effectiveMax = max(effectiveMax, centerV + EXTENDED_RANGE);
  
  // Constrain to ADC limits
  effectiveMin = max(effectiveMin, 0);
  effectiveMax = min(effectiveMax, 4095);
  
  if (raw > centerV) {
    // Map from just outside deadzone to extended max
    int rangeStart = centerV + DEADZONE_ADC;
    return constrain(map(raw, rangeStart, effectiveMax, 51, 100), 51, 100);
  } else {
    // Map from extended min to just outside deadzone
    int rangeEnd = centerV - DEADZONE_ADC;
    return constrain(map(raw, effectiveMin, rangeEnd, 0, 49), 0, 49);
  }
}

// ================= ESP-NOW CALLBACK =================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {}

// ================= Send Task =================
void sendTask(void *pv) {
  TickType_t lastWakeTime = xTaskGetTickCount();
  
  for (;;) {
    portENTER_CRITICAL(&mux);
    txPacket.mode = modeIndex;
    txPacket.linear = (uint8_t)round(linearValue);
    txPacket.angular = (uint8_t)round(angularValue);
    portEXIT_CRITICAL(&mux);

    esp_now_send(receiverMAC, (uint8_t*)&txPacket, sizeof(txPacket));
    
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(25));
  }
}

// ================= Improved Joystick Task with Extended Range =================
void joystickTask(void *pv) {
  // Calibration
  long s1 = 0, s2 = 0;
  for (int i = 0; i < CALIB_SAMPLES; i++) {
    s1 += analogRead(J1X_PIN);
    s2 += analogRead(J2X_PIN);
    delay(3);
  }

  portENTER_CRITICAL(&mux);
  centerJ1 = s1 / CALIB_SAMPLES;
  centerJ2 = s2 / CALIB_SAMPLES;
  
  // Set initial min/max with extended range for better resolution
  minJ1 = centerJ1 - MIN_RANGE;
  maxJ1 = centerJ1 + MIN_RANGE;
  minJ2 = centerJ2 - MIN_RANGE;
  maxJ2 = centerJ2 + MIN_RANGE;
  
  // Reset values to exact center
  angularValue = 50.0;
  linearValue = 50.0;
  portEXIT_CRITICAL(&mux);

  TickType_t lastWakeTime = xTaskGetTickCount();
  
  for (;;) {
    int r1 = analogRead(J1X_PIN);
    int r2 = analogRead(J2X_PIN);

    portENTER_CRITICAL(&mux);
    
    // Update min/max gradually to allow full range exploration
    if (r1 < minJ1) minJ1 = r1;
    if (r1 > maxJ1) maxJ1 = r1;
    if (r2 < minJ2) minJ2 = r2;
    if (r2 > maxJ2) maxJ2 = r2;
    
    // Apply mapping with extended range
    int a = mapLearn(r1, minJ1, centerJ1, maxJ1);
    int l = mapLearn(r2, minJ2, centerJ2, maxJ2);

    // Apply trim
    a = constrain(a + (trimY - 50), 0, 100);
    l = constrain(l + (trimX - 50), 0, 100);

    // Apply smoothing only if not in center deadzone
    if (abs(r1 - centerJ1) <= DEADZONE_ADC) {
      angularValue = 50.0;
    } else {
      angularValue = SMOOTH_ALPHA * a + (1 - SMOOTH_ALPHA) * angularValue;
    }
    
    if (abs(r2 - centerJ2) <= DEADZONE_ADC) {
      linearValue = 50.0;
    } else {
      linearValue = SMOOTH_ALPHA * l + (1 - SMOOTH_ALPHA) * linearValue;
    }
    
    if (screen == VALUE_SCREEN) {
      screenDirty = true;
    }
    
    portEXIT_CRITICAL(&mux);

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(20));
  }
}

// ================= UI FUNCTIONS =================
void drawMainMenu() {
  const char* items[] = {"Trim", "Mode", "Value"};
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  for(int i=0;i<3;i++){
    if(i==menuIndex) u8g2.drawStr(0, 12+i*12, "> ");
    u8g2.drawStr(12, 12+i*12, items[i]);
  }
  u8g2.sendBuffer();
}

void drawTrimMenu() {
  const char* items[] = {"TrimX", "TrimY", "Back"};
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  for(int i=0;i<3;i++){
    if(i==menuIndex) u8g2.drawStr(0, 12+i*12, "> ");
    u8g2.drawStr(12, 12+i*12, items[i]);
  }
  u8g2.sendBuffer();
}

void drawModeMenu() {
  const char* modes[] = {"Serial", "PWM", "CAN", "Back"};
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  for(int i=0;i<4;i++){
    if(i==menuIndex) u8g2.drawStr(0, 12+i*12, "> ");
    u8g2.drawStr(12, 12+i*12, modes[i]);
  }
  u8g2.sendBuffer();
}

void drawTrimAdjust(const char *label, int value){
  u8g2.clearBuffer(); 
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0,12,label);
  u8g2.setCursor(0,30);
  u8g2.print("Value: "); u8g2.print(value);
  u8g2.drawStr(0,55,"LEFT - / RIGHT +");
  u8g2.sendBuffer();
}

void drawValueScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(0,12,"Live Values");
  u8g2.setCursor(0,30);
  
  int linear, angular;
  portENTER_CRITICAL(&mux);
  linear = (int)linearValue;
  angular = (int)angularValue;
  portEXIT_CRITICAL(&mux);
  
  u8g2.print("Linear : "); u8g2.print(linear);
  u8g2.setCursor(0,45);
  u8g2.print("Angular: "); u8g2.print(angular);
  u8g2.drawStr(0,62,"OK Back");
  u8g2.sendBuffer();
}

// ================= UI TASK =================
void uiTask(void *pv) {
  for (;;) {
    unsigned long currentTime = millis();
    
    if (screenDirty && (currentTime - lastDisplayUpdate >= DISPLAY_MIN_INTERVAL)) {
      switch(screen){
        case MAIN_MENU: drawMainMenu(); break;
        case TRIM_MENU: drawTrimMenu(); break;
        case TRIM_X: drawTrimAdjust("Trim X", trimX); break;
        case TRIM_Y: drawTrimAdjust("Trim Y", trimY); break;
        case MODE_MENU: drawModeMenu(); break;
        case VALUE_SCREEN: drawValueScreen(); break;
      }
      screenDirty = false;
      lastDisplayUpdate = currentTime;
    }

    switch(screen){
      case MAIN_MENU:
        if(btnPressed(BTN_UP))   { menuIndex = max(0, menuIndex-1); screenDirty = true; }
        if(btnPressed(BTN_DOWN)) { menuIndex = min(2, menuIndex+1); screenDirty = true; }
        if(btnPressed(BTN_OK)) {
          if(menuIndex==0){ screen = TRIM_MENU; menuIndex = 0; }
          else if(menuIndex==1){ screen = MODE_MENU; menuIndex = modeIndex; }
          else screen = VALUE_SCREEN;
          screenDirty = true;
        }
        break;

      case TRIM_MENU:
        if(btnPressed(BTN_UP))   { menuIndex = max(0, menuIndex-1); screenDirty = true; }
        if(btnPressed(BTN_DOWN)) { menuIndex = min(2, menuIndex+1); screenDirty = true; }
        if(btnPressed(BTN_OK)) {
          if(menuIndex==0) screen = TRIM_X;
          else if(menuIndex==1) screen = TRIM_Y;
          else { screen = MAIN_MENU; menuIndex =0; }
          screenDirty = true;
        }
        break;

      case TRIM_X:
        if(btnPressedTrim(BTN_LEFT))  { trimX = max(0, trimX-1); screenDirty = true; }
        if(btnPressedTrim(BTN_RIGHT)) { trimX = min(100, trimX+1); screenDirty = true; }
        if(btnPressed(BTN_OK)) { screen = TRIM_MENU; screenDirty = true; }
        break;

      case TRIM_Y:
        if(btnPressedTrim(BTN_LEFT))  { trimY = max(0, trimY-1); screenDirty = true; }
        if(btnPressedTrim(BTN_RIGHT)) { trimY = min(100, trimY+1); screenDirty = true; }
        if(btnPressed(BTN_OK)) { screen = TRIM_MENU; screenDirty = true; }
        break;

      case MODE_MENU:
        if(btnPressed(BTN_UP))   { menuIndex = max(0, menuIndex-1); screenDirty = true; }
        if(btnPressed(BTN_DOWN)) { menuIndex = min(3, menuIndex+1); screenDirty = true; }
        if(btnPressed(BTN_OK)) {
          if(menuIndex<3) modeIndex = menuIndex;
          screen = MAIN_MENU; menuIndex = 0;
          screenDirty = true;
        }
        break;

      case VALUE_SCREEN:
        if(btnPressed(BTN_OK)) { screen = MAIN_MENU; menuIndex=0; screenDirty = true; }
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);

  Wire.begin(21, 22);
  u8g2.begin();

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, receiverMAC, 6);
  peer.channel = 0;  
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  xTaskCreatePinnedToCore(joystickTask, "joy", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(uiTask,       "ui",  4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(sendTask,     "tx",  3072, NULL, 1, NULL, 0);
}

void loop() {
  vTaskDelete(NULL);
}
