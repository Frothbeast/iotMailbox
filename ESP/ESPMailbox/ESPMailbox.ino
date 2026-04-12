#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"

#define HALL_SENSOR_PIN 3
#define ONE_WIRE_BUS 2
#define MAX_PACKETS 10 // Store up to 10 hours of data

#define DEBUG 1 // Set to 0 to "silence" the chip and save every millisecond of battery

// RTC Data - persists during sleep
RTC_DATA_ATTR struct Packet {
    uint8_t id;
    uint8_t trigger;
    int16_t temp; // Stored as temp * 100
    int16_t rssi;
} buffer[MAX_PACKETS];

RTC_DATA_ATTR int packetCount = 0;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void setup() {
    #if DEBUG
      Serial.begin(115200);
    #endif
    
    // 1. Identify Trigger
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    uint8_t currentTrigger = (reason == ESP_SLEEP_WAKEUP_GPIO) ? 1 : 2;

    // 2. Sample Data
    sensors.begin();
    sensors.requestTemperatures();
    float rawTemp = sensors.getTempCByIndex(0);
    
    // 3. Store in RTC Buffer
    if (packetCount < MAX_PACKETS) {
        buffer[packetCount].id = 0x01; // Your Device ID
        buffer[packetCount].trigger = currentTrigger;
        buffer[packetCount].temp = (int16_t)(rawTemp * 100);
        buffer[packetCount].rssi = (int16_t)WiFi.RSSI();
        packetCount++;
    }

    // 4. Transmission Logic (Every 6 hours OR Door Open)
    if (currentTrigger == 1 || packetCount >= 6) {
        sendBufferedData();
    }

    // 5. Reset for Sleep
    esp_deep_sleep_enable_gpio_wakeup(1ULL << HALL_SENSOR_PIN, ESP_GPIO_WAKEUP_LOW_LEVEL);
    esp_sleep_enable_timer_wakeup(3600ULL * 1000000ULL);
    esp_deep_sleep_start();
    #if DEBUG
      Serial.println("Waking up...");
    #endif
}

void sendBufferedData() {
    WiFi.begin(WIFI_SSID, WIFI_PASS, 0, NULL, true); // WPA3
    
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
        delay(500);
        timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        for (int i = 0; i < packetCount; i++) {
            if (client.connect(SERVER_IP, SERVER_PORT)) {
                // Construct Hex String
                char hexMsg[21];
                sprintf(hexMsg, "%02X%02X%04X%04X", 
                        buffer[i].id, buffer[i].trigger, 
                        (uint16_t)buffer[i].temp, (uint16_t)buffer[i].rssi);
                
                client.println(hexMsg);
                
                // Wait for ACK before moving to next packet
                unsigned long start = millis();
                while (!client.available() && millis() - start < 1000);
                if (client.available()) client.readStringUntil('\n'); 
                
                client.stop();
            }
        }
        packetCount = 0; // Clear buffer after successful transmission
    }
}

void loop() {}