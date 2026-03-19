#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <SwitecX25.h>

// ===== Pin Definitions =====
#define MOTOR_PIN1 D0
#define MOTOR_PIN2 D1
#define MOTOR_PIN3 D2
#define MOTOR_PIN4 D3
#define SDA_PIN D4
#define SCL_PIN D5
#define BUTTON_PIN D6
#define NEOPIXEL_PIN D7

// ===== Configuration =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define MOTOR_STEPS 945
#define OLED_TIMEOUT 8000

// ===== BLE UUIDs (must match sensor) =====
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_TEMP_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_HUM_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CHAR_UPTIME_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26aa"
#define SENSOR_NAME         "TempHum_Sensor"

// ===== Temperature Display Range =====
#define TEMP_MIN -10.0
#define TEMP_MAX 30.0

// ===== Alarm Thresholds =====
#define TEMP_ALARM_HIGH 10.0
#define TEMP_ALARM_LOW 0.0
#define HUM_ALARM_HIGH 90.0
#define HUM_ALARM_LOW 30.0

// ===== Objects =====
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
SwitecX25 motor(MOTOR_STEPS, MOTOR_PIN1, MOTOR_PIN2, MOTOR_PIN3, MOTOR_PIN4);

// ===== BLE Objects =====
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pTempChar = nullptr;
BLERemoteCharacteristic* pHumChar = nullptr;
BLERemoteCharacteristic* pUptimeChar = nullptr;
BLEAdvertisedDevice* targetDevice = nullptr;
BLEScan* pBLEScan = nullptr;

// ===== State Variables =====
float temperature = 0.0;
float humidity = 0.0;
String uptime = "";
bool tempChanging = false;
bool humChanging = false;
bool dataReceived = false;
bool deviceFound = false;
bool isConnected = false;

unsigned long lastDataTime = 0;
unsigned long oledOnTime = 0;
bool oledActive = false;
bool lastButtonState = HIGH;

// ===== Breathing LED Variables =====
uint8_t breathBrightness = 0;
int8_t breathDirection = 1;
unsigned long lastBreathUpdate = 0;
#define BREATH_INTERVAL 15
#define BREATH_MIN 5
#define BREATH_MAX 60
#define BREATH_MAX_ACTIVE 255

// ===== LED Colors =====
enum LedState { 
  LED_WAITING,      // Blue - scanning/connecting
  LED_NORMAL,       // Green - all good
  LED_TEMP_HIGH,    // Red - temperature too high
  LED_TEMP_LOW,     // Cyan - temperature too low  
  LED_HUM_HIGH,     // Red - humidity too high
  LED_HUM_LOW,      // Orange - humidity too low
  LED_CHANGING,     // Magenta - data changing
  LED_DISCONNECTED  // Purple - connection lost
};
LedState currentLedState = LED_WAITING;

// ===== Function Declarations =====
void initMotor();
void updateMotor();
void updateLED();
void updateBreathing();
void checkButton();
void showOLED();
void hideOLED();
bool connectToSensor();
void parseCharacteristicData();
void setLedColor(LedState state);
uint32_t getStateColor(LedState state);

// ===== BLE Scan Callback =====
class MyScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.getName() == SENSOR_NAME) {
      Serial.println("Found sensor!");
      targetDevice = new BLEAdvertisedDevice(advertisedDevice);
      deviceFound = true;
      pBLEScan->stop();
    }
  }
};

// ===== BLE Client Callbacks =====
class MyClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient* client) {
    Serial.println("Connected to sensor");
    isConnected = true;
  }
  
  void onDisconnect(BLEClient* client) {
    Serial.println("Disconnected from sensor");
    isConnected = false;
    dataReceived = false;
  }
};

// ===== Notify Callbacks =====
void tempNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  String value = String((char*)pData).substring(0, length);
  
  // Parse "25.3C" or "25.3C*"
  tempChanging = value.endsWith("*");
  if (tempChanging) {
    value = value.substring(0, value.length() - 2); // Remove "C*"
  } else {
    value = value.substring(0, value.length() - 1); // Remove "C"
  }
  temperature = value.toFloat();
  dataReceived = true;
  lastDataTime = millis();
  
  Serial.printf("Temp: %.1fC %s\n", temperature, tempChanging ? "(CHANGING)" : "");
}

void humNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  String value = String((char*)pData).substring(0, length);
  
  // Parse "45.2%" or "45.2%*"
  humChanging = value.endsWith("*");
  if (humChanging) {
    value = value.substring(0, value.length() - 2); // Remove "%*"
  } else {
    value = value.substring(0, value.length() - 1); // Remove "%"
  }
  humidity = value.toFloat();
  
  Serial.printf("Humidity: %.1f%% %s\n", humidity, humChanging ? "(CHANGING)" : "");
}

void uptimeNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  uptime = String((char*)pData).substring(0, length);
  Serial.printf("Uptime: %s\n", uptime.c_str());
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n===== Fridge Display Unit =====");

  // Initialize I2C
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED: OK");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Initializing...");
    display.display();
  } else {
    Serial.println("OLED: FAILED");
  }

  // Initialize NeoPixel
  pixel.begin();
  pixel.setBrightness(BREATH_MIN);
  pixel.setPixelColor(0, pixel.Color(0, 0, 255));
  pixel.show();
  Serial.println("NeoPixel: OK");

  // Initialize button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println("Button: OK");

  // Initialize motor
  initMotor();
  Serial.println("Motor: OK");

  // Initialize BLE
  BLEDevice::init("FridgeDisplay");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyScanCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  Serial.println("BLE: OK");

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Scanning for");
  display.println("sensor...");
  display.display();

  Serial.println("System ready, scanning...");
}

// ===== Main Loop =====
void loop() {
  // BLE connection management
  if (!isConnected) {
    if (!deviceFound) {
      // Scan for sensor
      Serial.println("Scanning...");
      pBLEScan->start(5, false);
      pBLEScan->clearResults();
    } else {
      // Try to connect
      if (connectToSensor()) {
        Serial.println("Connection established!");
      } else {
        Serial.println("Connection failed, will retry...");
        deviceFound = false;
        delete targetDevice;
        targetDevice = nullptr;
        delay(2000);
      }
    }
  }

  // Update outputs
  updateBreathing();
  updateLED();
  updateMotor();
  checkButton();

  // Auto-hide OLED after timeout
  if (oledActive && (millis() - oledOnTime > OLED_TIMEOUT)) {
    hideOLED();
  }

  // Motor needs frequent updates
  motor.update();
  
  delay(10);
}

// ===== Connect to Sensor =====
bool connectToSensor() {
  if (pClient == nullptr) {
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallbacks());
  }

  Serial.println("Connecting to sensor...");
  
  if (!pClient->connect(targetDevice)) {
    Serial.println("Failed to connect");
    return false;
  }

  // Get service
  BLERemoteService* pService = pClient->getService(BLEUUID(SERVICE_UUID));
  if (pService == nullptr) {
    Serial.println("Service not found");
    pClient->disconnect();
    return false;
  }

  // Get characteristics and register for notifications
  pTempChar = pService->getCharacteristic(BLEUUID(CHAR_TEMP_UUID));
  if (pTempChar && pTempChar->canNotify()) {
    pTempChar->registerForNotify(tempNotifyCallback);
    Serial.println("Subscribed to temperature");
  }

  pHumChar = pService->getCharacteristic(BLEUUID(CHAR_HUM_UUID));
  if (pHumChar && pHumChar->canNotify()) {
    pHumChar->registerForNotify(humNotifyCallback);
    Serial.println("Subscribed to humidity");
  }

  pUptimeChar = pService->getCharacteristic(BLEUUID(CHAR_UPTIME_UUID));
  if (pUptimeChar && pUptimeChar->canNotify()) {
    pUptimeChar->registerForNotify(uptimeNotifyCallback);
    Serial.println("Subscribed to uptime");
  }

  return true;
}

// ===== Initialize Motor (Zero Position) =====
void initMotor() {
  Serial.println("Motor zeroing...");
  motor.zero();
  motor.setPosition(0);
  
  // Wait for motor to reach zero
  while (motor.currentStep != motor.targetStep) {
    motor.update();
    delay(1);
  }
  delay(500);
  
  // Sweep test
  Serial.println("Motor sweep test...");
  motor.setPosition(MOTOR_STEPS);
  while (motor.currentStep != motor.targetStep) {
    motor.update();
    delay(1);
  }
  delay(300);
  
  motor.setPosition(0);
  while (motor.currentStep != motor.targetStep) {
    motor.update();
    delay(1);
  }
  delay(300);
  
  Serial.println("Motor ready");
}

// ===== Update Motor Position Based on Temperature =====
void updateMotor() {
  if (!dataReceived) return;

  // Map temperature to motor position
  float clampedTemp = constrain(temperature, TEMP_MIN, TEMP_MAX);
  int targetPos = map(clampedTemp * 10, TEMP_MIN * 10, TEMP_MAX * 10, 0, MOTOR_STEPS);
  
  motor.setPosition(targetPos);
}

// ===== Update Breathing Effect =====
void updateBreathing() {
  if (millis() - lastBreathUpdate < BREATH_INTERVAL) return;
  lastBreathUpdate = millis();

  uint8_t maxBright = oledActive ? BREATH_MAX_ACTIVE : BREATH_MAX;
  uint8_t minBright = oledActive ? BREATH_MAX_ACTIVE : BREATH_MIN;

  if (oledActive) {
    // Constant bright when OLED is on
    breathBrightness = BREATH_MAX_ACTIVE;
  } else {
    // Breathing effect when OLED is off
    breathBrightness += breathDirection * 2;
    
    if (breathBrightness >= BREATH_MAX) {
      breathBrightness = BREATH_MAX;
      breathDirection = -1;
    } else if (breathBrightness <= BREATH_MIN) {
      breathBrightness = BREATH_MIN;
      breathDirection = 1;
    }
  }
  
  pixel.setBrightness(breathBrightness);
}

// ===== Update LED Color =====
void updateLED() {
  LedState newState;

  if (!isConnected) {
    newState = LED_WAITING;
  } else if (!dataReceived) {
    newState = LED_WAITING;
  } else if (millis() - lastDataTime > 60000) {
    newState = LED_DISCONNECTED;
  } else if (tempChanging || humChanging) {
    newState = LED_CHANGING;
  } else if (temperature > TEMP_ALARM_HIGH) {
    newState = LED_TEMP_HIGH;
  } else if (temperature < TEMP_ALARM_LOW) {
    newState = LED_TEMP_LOW;
  } else if (humidity > HUM_ALARM_HIGH) {
    newState = LED_HUM_HIGH;
  } else if (humidity < HUM_ALARM_LOW) {
    newState = LED_HUM_LOW;
  } else {
    newState = LED_NORMAL;
  }

  currentLedState = newState;
  pixel.setPixelColor(0, getStateColor(newState));
  pixel.show();
}

// ===== Get Color for LED State =====
uint32_t getStateColor(LedState state) {
  switch (state) {
    case LED_WAITING:      return pixel.Color(0, 0, 255);      // Blue
    case LED_NORMAL:       return pixel.Color(0, 255, 0);      // Green
    case LED_TEMP_HIGH:    return pixel.Color(255, 0, 0);      // Red
    case LED_TEMP_LOW:     return pixel.Color(0, 255, 255);    // Cyan
    case LED_HUM_HIGH:     return pixel.Color(255, 0, 0);      // Red
    case LED_HUM_LOW:      return pixel.Color(255, 128, 0);    // Orange
    case LED_CHANGING:     return pixel.Color(255, 0, 255);    // Magenta (新颜色)
    case LED_DISCONNECTED: return pixel.Color(128, 0, 128);    // Purple
    default:               return pixel.Color(255, 255, 255);  // White
  }
}

// ===== Check Button =====
void checkButton() {
  bool currentState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && currentState == LOW) {
    // Button pressed
    if (oledActive) {
      hideOLED();
    } else {
      showOLED();
    }
    delay(50);
  }

  lastButtonState = currentState;
}

// ===== Show OLED =====
void showOLED() {
  oledActive = true;
  oledOnTime = millis();

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (!isConnected) {
    display.println("Not connected");
    display.println();
    display.println("Searching for");
    display.println("sensor...");
  } else if (!dataReceived) {
    display.println("Connected");
    display.println();
    display.println("Waiting for");
    display.println("data...");
  } else {
    // Title
    display.println("=== Fridge ===");
    display.println();

    // Temperature
    display.print("Temp: ");
    display.print(temperature, 1);
    display.print("C");
    if (tempChanging) display.print(" ~");
    display.println();

    // Humidity
    display.print("Hum:  ");
    display.print(humidity, 1);
    display.print("%");
    if (humChanging) display.print(" ~");
    display.println();

    display.println();

    // Status line
    if (tempChanging || humChanging) {
      display.println("Stabilizing...");
    } else if (temperature > TEMP_ALARM_HIGH) {
      display.println("! TEMP HIGH !");
    } else if (temperature < TEMP_ALARM_LOW) {
      display.println("! TEMP LOW !");
    } else if (humidity > HUM_ALARM_HIGH) {
      display.println("! HUM HIGH !");
    } else if (humidity < HUM_ALARM_LOW) {
      display.println("! HUM LOW !");
    } else {
      display.println("Status: OK");
    }

    // Uptime
    display.println();
    display.print("Up: ");
    display.println(uptime);
  }

  display.display();
}

// ===== Hide OLED =====
void hideOLED() {
  oledActive = false;
  display.clearDisplay();
  display.display();
}