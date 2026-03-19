#include <Arduino.h>
#include <Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// 引脚定义
#define SDA_PIN D4
#define SCL_PIN D5

// BLE UUID
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_TEMP_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_HUM_UUID       "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CHAR_TIME_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26aa"

// 测量参数
const int SAMPLE_COUNT = 10;
const unsigned long SAMPLE_INTERVAL = 20000;  // 20秒

// BLE 全局变量
BLECharacteristic *pTempChar;
BLECharacteristic *pHumChar;
BLECharacteristic *pTimeChar;
bool deviceConnected = false;
unsigned long startTime;

// 滤波用的历史值
float lastTemp = -999;
float lastHum = -999;

// 滤波结果结构
struct FilterResult {
    float value;
    float stdDev;
    float changeRate;
    bool stable;
};

// BLE 连接回调
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("Client connected");
    }
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("Client disconnected");
        pServer->getAdvertising()->start();
    }
};

// 读取温度
float readTempRaw() {
    Wire.beginTransmission(0x40);
    Wire.write(0xE3);
    Wire.endTransmission();
    delay(50);
    
    Wire.requestFrom(0x40, 2);
    if (Wire.available() == 2) {
        uint16_t raw = Wire.read() << 8 | Wire.read();
        raw &= 0xFFFC;
        return -46.85 + 175.72 * raw / 65536.0;
    }
    return -999;
}

// 读取湿度
float readHumRaw() {
    Wire.beginTransmission(0x40);
    Wire.write(0xE5);
    Wire.endTransmission();
    delay(50);
    
    Wire.requestFrom(0x40, 2);
    if (Wire.available() == 2) {
        uint16_t raw = Wire.read() << 8 | Wire.read();
        raw &= 0xFFFC;
        return -6.0 + 125.0 * raw / 65536.0;
    }
    return -999;
}

// 计算标准差
float calcStdDev(float* arr, int size, float mean) {
    float sum = 0;
    for (int i = 0; i < size; i++) {
        sum += (arr[i] - mean) * (arr[i] - mean);
    }
    return sqrt(sum / size);
}

// 智能滤波
FilterResult smartFilter(float* samples, int size, float lastValue) {
    FilterResult result;
    
    // 排序
    float sorted[SAMPLE_COUNT];
    memcpy(sorted, samples, size * sizeof(float));
    for (int i = 0; i < size - 1; i++) {
        for (int j = 0; j < size - i - 1; j++) {
            if (sorted[j] > sorted[j + 1]) {
                float t = sorted[j];
                sorted[j] = sorted[j + 1];
                sorted[j + 1] = t;
            }
        }
    }
    
    // 计算均值
    float sum = 0;
    for (int i = 0; i < size; i++) {
        sum += samples[i];
    }
    float mean = sum / size;
    
    // 计算标准差和变化率
    result.stdDev = calcStdDev(samples, size, mean);
    result.changeRate = (lastValue == -999) ? 0 : fabs(mean - lastValue);
    
    // 判断稳定性
    result.stable = (result.stdDev < 0.5) && (result.changeRate < 2.0);
    
    // 选择滤波策略
    if (result.stable) {
        // 稳定：中值滤波
        result.value = sorted[size / 2];
    } else {
        // 变化中：截尾平均（去掉最高2个和最低2个）
        float trimSum = 0;
        for (int i = 2; i < size - 2; i++) {
            trimSum += sorted[i];
        }
        result.value = trimSum / (size - 4);
    }
    
    return result;
}

// 格式化运行时间
String formatUptime(unsigned long ms) {
    unsigned long seconds = ms / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;
    
    if (days > 0) {
        return String(days) + "d " + String(hours % 24) + "h";
    } else if (hours > 0) {
        return String(hours) + "h " + String(minutes % 60) + "m";
    } else {
        return String(minutes) + "m " + String(seconds % 60) + "s";
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    startTime = millis();
    
    // 初始化 I2C
    Wire.begin(SDA_PIN, SCL_PIN);
    Serial.println("HTU21D initializing...");
    
    // 测试传感器
    float testTemp = readTempRaw();
    if (testTemp == -999) {
        Serial.println("ERROR: HTU21D not found!");
    } else {
        Serial.println("HTU21D OK, temp: " + String(testTemp, 1) + "C");
    }
    
    // 初始化 BLE
    BLEDevice::init("TempHum_Sensor");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    BLEService *pService = pServer->createService(SERVICE_UUID);
    
    // 温度特征
    pTempChar = pService->createCharacteristic(
        CHAR_TEMP_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pTempChar->addDescriptor(new BLE2902());
    
    // 湿度特征
    pHumChar = pService->createCharacteristic(
        CHAR_HUM_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pHumChar->addDescriptor(new BLE2902());
    
    // 运行时间特征
    pTimeChar = pService->createCharacteristic(
        CHAR_TIME_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    pTimeChar->addDescriptor(new BLE2902());
    
    pService->start();
    
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->start();
    
    Serial.println("BLE Server ready: TempHum_Sensor");
    Serial.println("---");
}

void loop() {
    // 采集样本
    float tempSamples[SAMPLE_COUNT];
    float humSamples[SAMPLE_COUNT];
    
    Serial.println("Sampling...");
    for (int i = 0; i < SAMPLE_COUNT; i++) {
        tempSamples[i] = readTempRaw();
        humSamples[i] = readHumRaw();
        Serial.print("  #");
        Serial.print(i);
        Serial.print(": T=");
        Serial.print(tempSamples[i], 1);
        Serial.print("C, H=");
        Serial.print(humSamples[i], 1);
        Serial.println("%");
        delay(100);
    }
    
    // 智能滤波
    FilterResult tempResult = smartFilter(tempSamples, SAMPLE_COUNT, lastTemp);
    FilterResult humResult = smartFilter(humSamples, SAMPLE_COUNT, lastHum);
    
    lastTemp = tempResult.value;
    lastHum = humResult.value;
    
    String uptime = formatUptime(millis() - startTime);
    
    // 打印结果
    Serial.println("--- Result ---");
    Serial.print("Temp: ");
    Serial.print(tempResult.value, 1);
    Serial.print("C (std:");
    Serial.print(tempResult.stdDev, 2);
    Serial.print(", chg:");
    Serial.print(tempResult.changeRate, 2);
    Serial.print(", ");
    Serial.print(tempResult.stable ? "STABLE" : "CHANGING");
    Serial.println(")");
    
    Serial.print("Hum: ");
    Serial.print(humResult.value, 1);
    Serial.print("% (std:");
    Serial.print(humResult.stdDev, 2);
    Serial.print(", chg:");
    Serial.print(humResult.changeRate, 2);
    Serial.print(", ");
    Serial.print(humResult.stable ? "STABLE" : "CHANGING");
    Serial.println(")");
    
    Serial.print("Uptime: ");
    Serial.println(uptime);
    
    // BLE 发送
    if (deviceConnected) {
        String tempStr = String(tempResult.value, 1) + "C";
        if (!tempResult.stable) tempStr += "*";
        
        String humStr = String(humResult.value, 1) + "%";
        if (!humResult.stable) humStr += "*";
        
        pTempChar->setValue(tempStr.c_str());
        pTempChar->notify();
        
        pHumChar->setValue(humStr.c_str());
        pHumChar->notify();
        
        pTimeChar->setValue(uptime.c_str());
        pTimeChar->notify();
        
        Serial.println("BLE sent: " + tempStr + ", " + humStr + ", " + uptime);
    } else {
        Serial.println("BLE: No client connected");
    }
    
    Serial.println("Sleeping 20s...");
    Serial.println("---");
    
    delay(SAMPLE_INTERVAL);
}