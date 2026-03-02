#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <SwitecX25.h>

// ===== 引脚定义 =====
// X27 步进电机
#define MOTOR_PIN1 D0
#define MOTOR_PIN2 D1
#define MOTOR_PIN3 D2
#define MOTOR_PIN4 D3

// OLED SSD1306
#define OLED_SDA D4
#define OLED_SCL D5

// 按钮
#define BUTTON_PIN D6

// NeoPixel LED
#define NEOPIXEL_PIN D7
#define NEOPIXEL_COUNT 1

// ===== 对象初始化 =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define MOTOR_STEPS 945
SwitecX25 motor(MOTOR_STEPS, MOTOR_PIN1, MOTOR_PIN2, MOTOR_PIN3, MOTOR_PIN4);

Adafruit_NeoPixel pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ===== 变量 =====
int motorPosition = 0;
bool lastButtonState = HIGH;
int testMode = 0;

void runTest(int mode);

void setup() {
  Serial.begin(115200);
  delay(1000);

    Serial.begin(115200);
  delay(1000);
  
  Wire.begin(D4, D5);  // SDA, SCL
  
  Serial.println("I2C 扫描中...");
  
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("找到设备: 0x");
      Serial.println(addr, HEX);
    }
  }
  
  Serial.println("扫描完成");

  Serial.println("===== 设备测试开始 =====");

  // 初始化按钮
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // 初始化I2C和OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED 初始化失败!");
  } else {
    Serial.println("OLED ✓");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("System Ready!");
    display.println("Press button to test");
    display.display();
  }

  // 初始化NeoPixel
  pixel.begin();
  pixel.setBrightness(50);
  pixel.setPixelColor(0, pixel.Color(0, 255, 0)); // 绿色表示就绪
  pixel.show();
  Serial.println("NeoPixel ✓");

  // 初始化电机并归零
  motor.zero();
  Serial.println("X27 Motor ✓");

  Serial.println("按下按钮切换测试模式");
}

void loop() {
  // 必须持续调用
  motor.update();

  // 读取按钮
  bool buttonState = digitalRead(BUTTON_PIN);
  
  // 检测按钮按下（下降沿）
  if (lastButtonState == HIGH && buttonState == LOW) {
    delay(50); // 消抖
    testMode = (testMode + 1) % 4;
    runTest(testMode);
  }
  lastButtonState = buttonState;
}

void runTest(int mode) {
  display.clearDisplay();
  display.setCursor(0, 0);

  switch (mode) {
    case 0:
      // 测试OLED
      Serial.println("测试: OLED显示");
      display.setTextSize(2);
      display.println("OLED OK!");
      display.setTextSize(1);
      display.println("");
      display.println("Mode 0: Display Test");
      pixel.setPixelColor(0, pixel.Color(255, 255, 255)); // 白色
      break;

    case 1:
      // 测试NeoPixel - 红绿蓝循环
      Serial.println("测试: NeoPixel LED");
      display.println("Mode 1: LED Test");
      display.println("");
      display.println("LED = RED");
      pixel.setPixelColor(0, pixel.Color(255, 0, 0));
      pixel.show();
      delay(500);
      display.println("LED = GREEN");
      pixel.setPixelColor(0, pixel.Color(0, 255, 0));
      pixel.show();
      delay(500);
      display.println("LED = BLUE");
      pixel.setPixelColor(0, pixel.Color(0, 0, 255));
      break;

    case 2:
      // 测试电机 - 移动到中间位置
      Serial.println("测试: X27电机 -> 50%");
      display.println("Mode 2: Motor Test");
      display.println("");
      display.println("Moving to 50%...");
      motorPosition = MOTOR_STEPS / 2;
      motor.setPosition(motorPosition);
      pixel.setPixelColor(0, pixel.Color(255, 165, 0)); // 橙色
      break;

    case 3:
      // 测试电机 - 归零
      Serial.println("测试: X27电机 -> 归零");
      display.println("Mode 3: Motor Zero");
      display.println("");
      display.println("Returning to 0...");
      motorPosition = 0;
      motor.setPosition(motorPosition);
      pixel.setPixelColor(0, pixel.Color(0, 255, 255)); // 青色
      break;
  }

  display.println("");
  display.print("Next: Mode ");
  display.println((mode + 1) % 4);
  display.display();
  pixel.show();
}