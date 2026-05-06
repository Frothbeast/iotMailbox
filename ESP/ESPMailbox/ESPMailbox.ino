#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"

#define HALL_SENSOR_PIN 3
#define ONE_WIRE_BUS 2
#define MAX_PACKETS 10 

#define DEBUG 1 

// RTC Data - persists during sleep[cite: 1]
RTC_DATA_ATTR struct Packet {
    uint8_t id;
    uint8_t trigger;
    int16_t temp; 
    int16_t rssi;
} buffer[MAX_PACKETS];

RTC_DATA_ATTR int packetCount = 0;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void setup() {
    #if DEBUG
      Serial.begin(115200);
    #endif

    // 1. Identify Trigger Logic[cite: 1]
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    uint8_t currentTrigger = 1; // Default to Heartbeat (1)[cite: 1]

    if (reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
        currentTrigger = 0; // Power-up[cite: 1]
    } 
    else if (reason == ESP_SLEEP_WAKEUP_GPIO) {
        // Distinguish between Opened (Low) and Reset (High)[cite: 1]
        pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);
        if (digitalRead(HALL_SENSOR_PIN) == LOW) {
            currentTrigger = 2; // Door Opened
        } else {
            currentTrigger = 3; // Door Reset
        }
    }
    else if (reason == ESP_SLEEP_WAKEUP_TIMER) {
        currentTrigger = 1; // Heartbeat[cite: 1]
    }

    // 2. Initial Wake Routine (Send 000000000000)[cite: 1]
    if (currentTrigger == 0) {
        #if DEBUG
          Serial.println("Initial power-up detected. Sending zero packet...");
        #endif
        sendZeroPacket();
    }

    // 3. Sample Data[cite: 1]
    sensors.begin();
    sensors.requestTemperatures();
    float rawTemp = sensors.getTempCByIndex(0);
    
    // 4. Store in RTC Buffer[cite: 1]
    if (packetCount < MAX_PACKETS) {
        buffer[packetCount].id = 0x01; 
        buffer[packetCount].trigger = currentTrigger;
        buffer[packetCount].temp = (int16_t)(rawTemp * 100);
        
        // RSSI is only valid if WiFi was previously on; 
        // otherwise, it will record 0 or last known until next connect.
        buffer[packetCount].rssi = (int16_t)WiFi.RSSI();
        packetCount++;
    }

    // 5. Transmission Logic (Immediate for Events 0, 2, 3 OR every 6 packets)[cite: 1]
    if (currentTrigger == 0 || currentTrigger == 2 || currentTrigger == 3 || packetCount >= 6) {
        sendBufferedData();
    }

    // 6. Reset for Sleep[cite: 1]
    // Use GPIO_CHANGE to wake on both door open and door close
    esp_deep_sleep_enable_gpio_wakeup(1ULL << HALL_SENSOR_PIN, ESP_GPIO_WAKEUP_GPIO_CHANGE);
    esp_sleep_enable_timer_wakeup(3600ULL * 1000000ULL);
    esp_deep_sleep_start();
}

void sendZeroPacket() {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
        delay(500);
        timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        if (client.connect(SERVER_IP, SERVER_PORT)) {
            client.println("000000000000"); //[cite: 1]
            
            unsigned long start = millis();
            while (!client.available() && millis() - start < 1000);
            if (client.available()) client.readStringUntil('\n'); 
            
            client.stop();
        }
    }
}

void sendBufferedData() {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
        delay(500);
        timeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        for (int i = 0; i < packetCount; i++) {
            if (client.connect(SERVER_IP, SERVER_PORT)) {
                char hexMsg[21];
                sprintf(hexMsg, "%02X%02X%04X%04X", 
                        buffer[i].id, buffer[i].trigger, 
                        (uint16_t)buffer[i].temp, (uint16_t)buffer[i].rssi);
                
                client.println(hexMsg); //[cite: 1]
                
                unsigned long start = millis();
                while (!client.available() && millis() - start < 1000);
                if (client.available()) client.readStringUntil('\n'); 
                
                client.stop();
            }
        }
        packetCount = 0; //[cite: 1]
    }
}

void loop() {}