#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
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
#define MOTOR_STEPS 315 * 3       // X27 motor total steps
#define BLE_SCAN_TIME 5           // BLE scan duration (seconds)
#define OLED_TIMEOUT 5000         // OLED display timeout (milliseconds)
#define FRIDGE_DEVICE_NAME "FridgeSensor"  // BLE device name of fridge sensor

// ===== Humidity Range and Alarm Thresholds =====
#define HUMIDITY_MIN 50.0
#define HUMIDITY_MAX 100.0
#define HUMIDITY_ALARM_HIGH 93.0
#define HUMIDITY_ALARM_LOW 80.0

// ===== Test Mode Configuration =====
#define TEST_MODE true            // Set to false when fridge sensor is ready
#define TEST_UPDATE_INTERVAL 3000 // Test data update interval (milliseconds)

// ===== Object Initialization =====
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
SwitecX25 motor(MOTOR_STEPS, MOTOR_PIN1, MOTOR_PIN2, MOTOR_PIN3, MOTOR_PIN4);
BLEScan* pBLEScan;

// ===== State Variables =====
float temperature = 0.0;
float humidity = 0.0;
bool dataReceived = false;
unsigned long lastDataTime = 0;
unsigned long oledOnTime = 0;
bool oledActive = false;
bool lastButtonState = HIGH;

// ===== Test Mode Variables =====
unsigned long lastTestUpdate = 0;
int testStep = 0;
// Test humidity values: normal -> high alarm -> normal -> low alarm -> repeat
float testHumidityValues[] = {85.0, 88.0, 91.0, 95.0, 97.0, 90.0, 85.0, 78.0, 75.0, 70.0, 75.0, 80.0};
int testValueCount = sizeof(testHumidityValues) / sizeof(testHumidityValues[0]);

// ===== BLE Callback Class =====
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Check if this is our fridge sensor
    if (advertisedDevice.getName() == FRIDGE_DEVICE_NAME) {
      // Parse temperature and humidity from Manufacturer Data
      if (advertisedDevice.haveManufacturerData()) {
        std::string data = advertisedDevice.getManufacturerData();
        if (data.length() >= 4) {
          // Data format: [temp_high][temp_low][humidity_high][humidity_low]
          int16_t tempRaw = ((uint8_t)data[0] << 8) | (uint8_t)data[1];
          int16_t humRaw = ((uint8_t)data[2] << 8) | (uint8_t)data[3];
          temperature = tempRaw / 100.0;
          humidity = humRaw / 100.0;
          dataReceived = true;
          lastDataTime = millis();
          
          Serial.printf("Data received - Temp: %.1f C, Humidity: %.1f%%\n", temperature, humidity);
        }
      }
    }
  }
};

// ===== Function Declarations =====
void updateMotor();
void updateLED();
void checkButton();
void showOLED();
void hideOLED();
void updateTestData();

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n===== Fridge Humidity Gauge =====");
  
  if (TEST_MODE) {
    Serial.println("*** TEST MODE ENABLED ***");
  }

  // Initialize I2C
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED: OK");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    if (TEST_MODE) {
      display.println("TEST MODE");
      display.println("Simulating data...");
    } else {
      display.println("Waiting for");
      display.println("BLE signal...");
    }
    display.display();
  }

  // Initialize NeoPixel
  pixel.begin();
  pixel.setBrightness(50);
  pixel.setPixelColor(0, pixel.Color(0, 0, 255));  // Blue = waiting
  pixel.show();
  Serial.println("NeoPixel: OK");

  // Initialize motor and zero position
  motor.zero();
  Serial.println("Motor: OK");

  // Initialize button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println("Button: OK");

  // Initialize BLE (only in normal mode)
  if (!TEST_MODE) {
    BLEDevice::init("HumidityGauge");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    Serial.println("BLE: OK");
    Serial.println("System ready, scanning BLE...");
  } else {
    Serial.println("System ready in TEST MODE");
  }
}

void loop() {
  if (TEST_MODE) {
    // Test mode: generate simulated data
    updateTestData();
  } else {
    // Normal mode: BLE scan
    BLEScanResults results = pBLEScan->start(BLE_SCAN_TIME, false);
    pBLEScan->clearResults();
  }

  // Update outputs
  updateMotor();
  updateLED();
  checkButton();
  
  // Auto-hide OLED after timeout
  if (oledActive && (millis() - oledOnTime > OLED_TIMEOUT)) {
    hideOLED();
  }

  // Update motor position (must be called frequently)
  motor.update();
  
  // Small delay in test mode to prevent CPU hogging
  if (TEST_MODE) {
    delay(10);
  }
}

// ===== Generate Test Data =====
void updateTestData() {
  if (millis() - lastTestUpdate > TEST_UPDATE_INTERVAL) {
    lastTestUpdate = millis();
    
    // Cycle through test values
    humidity = testHumidityValues[testStep];
    temperature = -5.0 + (random(0, 50) / 10.0);  // Random temp between -5 and 0
    
    dataReceived = true;
    lastDataTime = millis();
    
    Serial.printf("[TEST] Temp: %.1f C, Humidity: %.1f%%", temperature, humidity);
    
    // Print status
    if (humidity > HUMIDITY_ALARM_HIGH) {
      Serial.println(" -> TOO HUMID!");
    } else if (humidity < HUMIDITY_ALARM_LOW) {
      Serial.println(" -> TOO DRY!");
    } else {
      Serial.println(" -> Normal");
    }
    
    // Move to next test value
    testStep = (testStep + 1) % testValueCount;
    
    // Update OLED if it's active
    if (oledActive) {
      showOLED();
    }
  }
}

// ===== Update Motor Position Based on Humidity =====
void updateMotor() {
  if (!dataReceived) return;

  // Map humidity (50-100%) to motor steps (0-MOTOR_STEPS)
  float clampedHumidity = constrain(humidity, HUMIDITY_MIN, HUMIDITY_MAX);
  int targetPosition = map(clampedHumidity * 100, HUMIDITY_MIN * 100, HUMIDITY_MAX * 100, 0, MOTOR_STEPS);
  
  motor.setPosition(targetPosition);
  motor.update();
}

// ===== Update LED Color Based on Humidity Status =====
void updateLED() {
  if (!dataReceived) {
    // Blue = waiting for connection
    pixel.setPixelColor(0, pixel.Color(0, 0, 255));
  } else if (!TEST_MODE && millis() - lastDataTime > 60000) {
    // Purple = connection lost (no data for over 1 minute)
    pixel.setPixelColor(0, pixel.Color(128, 0, 128));
  } else if (humidity > HUMIDITY_ALARM_HIGH) {
    // Red = too humid
    pixel.setPixelColor(0, pixel.Color(255, 0, 0));
  } else if (humidity < HUMIDITY_ALARM_LOW) {
    // Orange = too dry
    pixel.setPixelColor(0, pixel.Color(255, 100, 0));
  } else {
    // Green = normal
    pixel.setPixelColor(0, pixel.Color(0, 255, 0));
  }
  pixel.show();
}

// ===== Check Button State =====
void checkButton() {
  bool currentState = digitalRead(BUTTON_PIN);
  
  if (lastButtonState == HIGH && currentState == LOW) {
    // Button pressed
    if (oledActive) {
      hideOLED();
    } else {
      showOLED();
    }
    delay(50);  // Debounce
  }
  
  lastButtonState = currentState;
}

// ===== Show OLED Display =====
void showOLED() {
  oledActive = true;
  oledOnTime = millis();
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  if (!dataReceived) {
    display.println("No data yet");
    if (TEST_MODE) {
      display.println("Starting test...");
    } else {
      display.println("Scanning BLE...");
    }
  } else {
    // Title
    if (TEST_MODE) {
      display.println("== TEST MODE ==");
    } else {
      display.println("=== Fridge Status ===");
    }
    display.println();
    
    // Temperature
    display.print("Temp: ");
    display.print(temperature, 1);
    display.println(" C");
    
    // Humidity
    display.print("Humidity: ");
    display.print(humidity, 1);
    display.println(" %");
    
    display.println();
    
    // Status
    if (humidity > HUMIDITY_ALARM_HIGH) {
      display.println("!! TOO HUMID !!");
    } else if (humidity < HUMIDITY_ALARM_LOW) {
      display.println("!! TOO DRY !!");
    } else {
      display.println("Status: Normal");
    }
    
    // Last update time
    display.println();
    unsigned long ago = (millis() - lastDataTime) / 1000;
    display.print("Updated: ");
    display.print(ago);
    display.println("s ago");
  }
  
  display.display();
}

// ===== Hide OLED Display =====
void hideOLED() {
  oledActive = false;
  display.clearDisplay();
  display.display();
}