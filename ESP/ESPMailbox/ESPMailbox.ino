#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"
#include "driver/gpio.h"

#define HALL_SENSOR_PIN 3
#define ONE_WIRE_BUS 2
#define SENSOR_POWER_PIN 4 
#define MAX_PACKETS 10 

IPAddress local_IP(DEVICE_IP);
IPAddress gateway(DEVICE_GATEWAY);
IPAddress subnet(DEVICE_SUBNET);
IPAddress dns(DEVICE_DNS);

RTC_DATA_ATTR struct Packet {
    uint8_t id;
    uint8_t trigger;
    int16_t temp;
    int16_t rssi;
} buffer[MAX_PACKETS];

RTC_DATA_ATTR int packetCount = 0;
RTC_DATA_ATTR int16_t lastValidRSSI = 0; 

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

const gpio_num_t unusedPins[] = {
    GPIO_NUM_0, GPIO_NUM_1, GPIO_NUM_6, 
    GPIO_NUM_7, GPIO_NUM_10, GPIO_NUM_18, GPIO_NUM_19
};
const int numUnusedPins = sizeof(unusedPins) / sizeof(unusedPins[0]);

void setup() {
    setCpuFrequencyMhz(80);

    // 1. Release all hardware locks immediately upon boot
    gpio_hold_dis((gpio_num_t)HALL_SENSOR_PIN);
    gpio_hold_dis((gpio_num_t)SENSOR_POWER_PIN);

    pinMode(SENSOR_POWER_PIN, INPUT);
    pinMode(HALL_SENSOR_PIN, INPUT_PULLUP); // Standardize on a constant baseline pull-up
    delay(10); 

    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    
    // Hardware Debounce: Let physical switch stabilization complete
    delay(50); 
    
    int currentHallState = digitalRead(HALL_SENSOR_PIN);
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    
    sensors.begin();
    sensors.setWaitForConversion(false);
    sensors.setResolution(9); // Set DS18B20 to 9-bit resolution 
    float rawTemp = -127.0;
    if (sensors.getDeviceCount() > 0) {
        sensors.requestTemperatures();
        delay(94); 
        rawTemp = sensors.getTempCByIndex(0);
    }
    int16_t currentTempPayload = (int16_t)(rawTemp * 100);

    bool forceSend = false;

    if (reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
        if (packetCount < MAX_PACKETS) {
            buffer[packetCount].id = 0x01;
            buffer[packetCount].trigger = 0;
            buffer[packetCount].temp = currentTempPayload;
            buffer[packetCount].rssi = lastValidRSSI;
            packetCount++;
        }
        forceSend = true;
    } 
    else if (reason == ESP_SLEEP_WAKEUP_TIMER) {
        if (packetCount < MAX_PACKETS) {
            buffer[packetCount].id = 0x01;
            buffer[packetCount].trigger = 1;
            buffer[packetCount].temp = currentTempPayload;
            buffer[packetCount].rssi = lastValidRSSI;
            packetCount++;
        }
    } 
    else if (reason == ESP_SLEEP_WAKEUP_GPIO) {
        // Correct Mapping: LOW = Magnet Present (Trigger 2), HIGH = Magnet Removed (Trigger 3)
        uint8_t initialTrigger = (currentHallState == LOW) ? 2 : 3; 
        if (packetCount < MAX_PACKETS) {
            buffer[packetCount].id = 0x01;
            buffer[packetCount].trigger = initialTrigger;
            buffer[packetCount].temp = currentTempPayload;
            buffer[packetCount].rssi = lastValidRSSI;
            packetCount++;
        }

        // Active Monitor Window: Watch for fast immediate follow-up transitions
        int lastObservedState = currentHallState;
        unsigned long monitorStart = millis();
        while (millis() - monitorStart < 3000) {
            int liveState = digitalRead(HALL_SENSOR_PIN);
            if (liveState != lastObservedState) {
                delay(20); // Debounce physical switch contacts
                liveState = digitalRead(HALL_SENSOR_PIN); 
                if (liveState != lastObservedState) {
                    uint8_t secondaryTrigger = (liveState == LOW) ? 2 : 3;
                    if (packetCount < MAX_PACKETS) {
                        buffer[packetCount].id = 0x01;
                        buffer[packetCount].trigger = secondaryTrigger;
                        buffer[packetCount].temp = currentTempPayload;
                        buffer[packetCount].rssi = lastValidRSSI;
                        packetCount++;
                    }
                    lastObservedState = liveState;
                }
            }
            delay(10);
        }
        forceSend = true;
    }

    // Capture finalized physical position before entering sleep configuration
    int finalHallState = digitalRead(HALL_SENSOR_PIN);

    if (forceSend || packetCount >= 6) {
        sendBufferedData();
    }

    // 2. Set static hardware environment and dynamic wakeup logic
    gpio_pulldown_dis((gpio_num_t)HALL_SENSOR_PIN);
    gpio_pullup_en((gpio_num_t)HALL_SENSOR_PIN);
    
    // Arm next wake state based on the current settled configuration
    int wakeLevel = (finalHallState == HIGH) ? 0 : 1; 
    esp_deep_sleep_enable_gpio_wakeup(1ULL << HALL_SENSOR_PIN, (esp_deepsleep_gpio_wake_up_mode_t)wakeLevel);
    
    // Lock pin configuration constraints into the low-power hardware domain
    gpio_hold_en((gpio_num_t)HALL_SENSOR_PIN);

    digitalWrite(SENSOR_POWER_PIN, LOW);
    pinMode(SENSOR_POWER_PIN, OUTPUT); 
    gpio_hold_en((gpio_num_t)SENSOR_POWER_PIN); 
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    btStop();

    for (int i = 0; i < numUnusedPins; i++) {
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << unusedPins[i]);
        io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
    }

    esp_sleep_enable_timer_wakeup(600ULL * 1000000ULL); 
    esp_deep_sleep_start();
}

void initWiFi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA); 
    WiFi.config(local_IP, gateway, subnet, dns);
    WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CHANNEL, WIFI_BSSID);
}

void sendBufferedData() {
    initWiFi();
    int wifiTimeout = 0;
    while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
        delay(500);
        wifiTimeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        lastValidRSSI = (int16_t)WiFi.RSSI();
        WiFiClient client;
        if (client.connect(SERVER_IP, SERVER_PORT)) {
            client.setTimeout(2000);
            for (int i = 0; i < packetCount; i++) {
                if (i == packetCount - 1) {
                    buffer[i].rssi = lastValidRSSI;
                }

                char hexMsg[21];
                sprintf(hexMsg, "%02X%02X%04X%04X", 
                        buffer[i].id, buffer[i].trigger, 
                        (uint16_t)buffer[i].temp, (uint16_t)buffer[i].rssi);
                client.println(hexMsg);
                
                unsigned long start = millis();
                bool gotAck = false;
                while (millis() - start < 10000) {
                    if (client.available()) {
                        String response = client.readStringUntil('\n');
                        if (response.indexOf("ACK") >= 0) {
                            gotAck = true;
                            break;
                        }
                    }
                    delay(50);
                }
                if (!gotAck) break;
            }
            client.stop();
        }
        packetCount = 0; 
    }
}

void loop() {}