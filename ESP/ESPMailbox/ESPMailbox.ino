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

// Array of all valid GPIO pins on the ESP32-C3-WROOM-02 to configure before sleep
const gpio_num_t unusedPins[] = {
    GPIO_NUM_0, GPIO_NUM_1, GPIO_NUM_5, GPIO_NUM_6, 
    GPIO_NUM_7, GPIO_NUM_10, GPIO_NUM_18, GPIO_NUM_19
};
const int numUnusedPins = sizeof(unusedPins) / sizeof(unusedPins[0]);

void setup() {
    setCpuFrequencyMhz(80);

    // Un-isolate pins upon wakeup to restore software control
    gpio_hold_dis((gpio_num_t)HALL_SENSOR_PIN);
    gpio_hold_dis((gpio_num_t)SENSOR_POWER_PIN);
    
    // Phase 1: Initialize switched power and wait for physical DS18B20 power up
    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    delay(500); // Increased from 200 to allow full internal POR stabilization

    pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);
    
    int hallState = digitalRead(HALL_SENSOR_PIN);
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    uint8_t currentTrigger = 1;

    if (reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
        currentTrigger = 0;
    } 
    else if (reason == ESP_SLEEP_WAKEUP_GPIO) {
        currentTrigger = (hallState == LOW) ? 2 : 3;
    }

    // Phase 2: Complete the 1-Wire transaction sequence
    sensors.begin();
    float rawTemp = -127.0;
    if (sensors.getDeviceCount() > 0) {
        sensors.requestTemperatures(); // Sends conversion request command
        // DallasTemperature library handles conversion delays internally if set to block.
        // Explicit delay added here to guarantee high-resolution completion window.
        delay(750); 
        rawTemp = sensors.getTempCByIndex(0);
    }

    if (packetCount < MAX_PACKETS) {
        buffer[packetCount].id = 0x01;
        buffer[packetCount].trigger = currentTrigger;
        buffer[packetCount].temp = (int16_t)(rawTemp * 100);
        buffer[packetCount].rssi = lastValidRSSI; 
        packetCount++;
    }

    if (currentTrigger == 0 || currentTrigger == 2 || currentTrigger == 3 || packetCount >= 6) {
        sendBufferedData();
    }

    // Configure target interrupt wake state (inverse of current state)
    int wakeLevel = (hallState == LOW) ? 1 : 0; 
    pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << HALL_SENSOR_PIN, (esp_deepsleep_gpio_wake_up_mode_t)wakeLevel);
    
    // Shut down sensor power rail to eliminate idle current leaks
    digitalWrite(SENSOR_POWER_PIN, LOW);
    pinMode(SENSOR_POWER_PIN, OUTPUT); 
    gpio_hold_en((gpio_num_t)SENSOR_POWER_PIN); // Lock low state during sleep
    
    // HALL_SENSOR_PIN hold removed to keep the pin connected to the RTC wake matrix
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    btStop();

    // Configure all unused pins with internal pull-downs to eliminate leakage current
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