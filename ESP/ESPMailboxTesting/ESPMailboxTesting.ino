#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"

#define HALL_SENSOR_PIN 3
#define ONE_WIRE_BUS 2
#define MAX_PACKETS 10 

#define DEBUG 1 

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

void setup() {
    #if DEBUG
      Serial.begin(115200);
      delay(500); 
      Serial.println("\n--- DEBUG START ---");
      Serial.printf("Current Packet Count: %d", packetCount);
      Serial.printf("Last Stored RSSI: %d", lastValidRSSI);
    #endif

    pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);
    int hallState = digitalRead(HALL_SENSOR_PIN);

    #if DEBUG
      Serial.printf("Hall Sensor Pin (%d) State: %s", HALL_SENSOR_PIN, hallState == LOW ? "LOW" : "HIGH");
    #endif

    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    uint8_t currentTrigger = 1; 

    #if DEBUG
      Serial.printf("Wakeup Reason Code: %d", reason);
    #endif

    if (reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
        currentTrigger = 0; 
    } 
    else if (reason == ESP_SLEEP_WAKEUP_GPIO) {
        currentTrigger = (hallState == LOW) ? 2 : 3;
    }
    else if (reason == ESP_SLEEP_WAKEUP_TIMER) {
        currentTrigger = 1; 
    }

    #if DEBUG
      Serial.printf("Current Trigger Calculated: %d", currentTrigger);
    #endif

    if (currentTrigger == 0) {
        #if DEBUG
          Serial.println("Initial power-up detected.");
        #endif
        sendZeroPacket();
    }

    sensors.begin();
    sensors.requestTemperatures();
    float rawTemp = sensors.getTempCByIndex(0);
    
    #if DEBUG
      Serial.printf("Raw Temperature: %.2f C", rawTemp);
    #endif

    if (packetCount < MAX_PACKETS) {
        buffer[packetCount].id = 0x01; 
        buffer[packetCount].trigger = currentTrigger;
        buffer[packetCount].temp = (int16_t)(rawTemp * 100);
        buffer[packetCount].rssi = lastValidRSSI;
        
        #if DEBUG
          Serial.printf("Buffering Packet[%d]: ID=0x%02X, Trig=%d, TempScaled=%d, RSSI=%d", 
                        packetCount, buffer[packetCount].id, buffer[packetCount].trigger, 
                        buffer[packetCount].temp, buffer[packetCount].rssi);
        #endif
        packetCount++;
    }

    if (currentTrigger == 0 || currentTrigger == 2 || currentTrigger == 3 || packetCount >= 6) {
        #if DEBUG
          Serial.println("Transmission criteria met. Starting sendBufferedData()...");
        #endif
        sendBufferedData();
    }

    int nextLevel = (digitalRead(HALL_SENSOR_PIN) == LOW) ? 1 : 0; 
    
    #if DEBUG
      Serial.printf("Configuring GPIO Wakeup: Pin %d, Wake level %d", HALL_SENSOR_PIN, nextLevel);
      Serial.println("Entering Deep Sleep (60s timer)...");
      Serial.println("--- DEBUG END ---");
      Serial.flush();
    #endif

    esp_deep_sleep_enable_gpio_wakeup(1ULL << HALL_SENSOR_PIN, (esp_deepsleep_gpio_wake_up_mode_t)nextLevel);
    esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);
    esp_deep_sleep_start();
}

void sendZeroPacket() {
    #if DEBUG
      Serial.printf("Connecting to WiFi: %s", WIFI_SSID);
    #endif
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
        delay(500);
        timeout++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        lastValidRSSI = (int16_t)WiFi.RSSI(); 
        #if DEBUG
          Serial.printf("WiFi Connected. RSSI: %d. Connecting to Server: %s:%d", lastValidRSSI, SERVER_IP, SERVER_PORT);
        #endif
        WiFiClient client;
        if (client.connect(SERVER_IP, SERVER_PORT)) {
            client.println("000000000000"); 
            unsigned long start = millis();
            while (!client.available() && millis() - start < 1000);
            if (client.available()) {
                String resp = client.readStringUntil('\n');
                #if DEBUG
                  Serial.printf("Zero Packet Response: %s", resp.c_str());
                #endif
            }
            client.stop();
        }
    } else {
        #if DEBUG
          Serial.println("WiFi Connection Failed for Zero Packet.");
        #endif
    }
}

void sendBufferedData() {
    #if DEBUG
      Serial.printf("Connecting to WiFi for buffer sync. SSID: %s", WIFI_SSID);
    #endif
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int wifiTimeout = 0;
    while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
        delay(500);
        wifiTimeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        lastValidRSSI = (int16_t)WiFi.RSSI();
        WiFiClient client;
        
        #if DEBUG
          Serial.printf("WiFi Connected. RSSI: %d. Server: %s:%d", lastValidRSSI, SERVER_IP, SERVER_PORT);
        #endif

        if (client.connect(SERVER_IP, SERVER_PORT)) {
            client.setTimeout(10); 

            for (int i = 0; i < packetCount; i++) {
                char hexMsg[21];
                sprintf(hexMsg, "%02X%02X%04X%04X", 
                        buffer[i].id, buffer[i].trigger, 
                        (uint16_t)buffer[i].temp, (uint16_t)buffer[i].rssi);
                
                #if DEBUG
                  Serial.printf("Sending Packet %d/%d: %s", i + 1, packetCount, hexMsg);
                #endif

                client.println(hexMsg); 

                unsigned long start = millis();
                bool gotAck = false;
                while (millis() - start < 10000) {
                    if (client.available()) {
                        String response = client.readStringUntil('\n');
                        response.trim();
                        #if DEBUG
                          Serial.printf("Server Response: %s", response.c_str());
                        #endif
                        if (response.indexOf("ACK") >= 0) { 
                            gotAck = true;
                            break;
                        }
                    }
                    delay(10); 
                }

                if (!gotAck) {
                    #if DEBUG
                      Serial.println("ACK mismatch or timeout. Aborting buffer.");
                    #endif
                    break; 
                }
            }
            client.stop();
        } else {
            #if DEBUG
              Serial.println("Failed to connect to server.");
            #endif
        }
        packetCount = 0; 
    } else {
        #if DEBUG
          Serial.println("WiFi connection failed.");
        #endif
    }
}

void loop() {}