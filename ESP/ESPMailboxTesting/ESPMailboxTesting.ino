#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "config.h"

#define HALL_SENSOR_PIN 3
#define ONE_WIRE_BUS 2
#define SENSOR_POWER_PIN 4 
#define MAX_PACKETS 10 

IPAddress local_IP(DEVICE_IP);
IPAddress gateway(DEVICE_GATEWAY);
IPAddress subnet(DEVICE_SUBNET);
IPAddress dns(DEVICE_DNS);

struct Packet {
    uint8_t id;
    uint8_t trigger;
    int16_t temp;
    int16_t rssi;
} buffer[MAX_PACKETS];

int packetCount = 0;
int16_t lastValidRSSI = 0; 
int lastHallState = HIGH;
float globalTemp = -127.0;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

unsigned long lastSensorRequest = 0;
unsigned long lastReportTime = 0;
bool conversionInProgress = false;

const unsigned long requestInterval = 10000; 
const unsigned long conversionDelay = 750;   
const unsigned long reportInterval = 60000;  

void setup() {
    setCpuFrequencyMhz(80);
    Serial.begin(115200);

    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    delay(500); 
    
    pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);
    lastHallState = digitalRead(HALL_SENSOR_PIN);

    sensors.begin();
    sensors.setWaitForConversion(false); 

    // Force an immediate cold-boot transmission (Trigger 0)
    sendSinglePacket(0x01, 0, -12700);
}

void loop() {
    unsigned long currentMillis = millis();

    // 1. Monitor Hall Sensor continuously
    int currentHallState = digitalRead(HALL_SENSOR_PIN);
    if (currentHallState != lastHallState) {
        // Trigger 2 for LOW (magnet present), Trigger 3 for HIGH (no magnet)
        uint8_t triggerType = (currentHallState == LOW) ? 2 : 3;
        int16_t currentTempPayload = (int16_t)(globalTemp * 100);
        
        sendSinglePacket(0x01, triggerType, currentTempPayload);
        lastHallState = currentHallState;
    }

    // 2. Non-blocking DS18B20 State Machine
    if (!conversionInProgress && (currentMillis - lastSensorRequest >= requestInterval)) {
        sensors.requestTemperatures(); 
        lastSensorRequest = currentMillis;
        conversionInProgress = true;
    }

    if (conversionInProgress && (currentMillis - lastSensorRequest >= conversionDelay)) {
        globalTemp = sensors.getTempCByIndex(0);
        conversionInProgress = false;

        if (packetCount < MAX_PACKETS) {
            buffer[packetCount].id = 0x01;
            buffer[packetCount].trigger = 1; 
            buffer[packetCount].temp = (int16_t)(globalTemp * 100);
            buffer[packetCount].rssi = lastValidRSSI;
            packetCount++;
        }
    }

    // 3. Report buffered data once per minute
    if (currentMillis - lastReportTime >= reportInterval) {
        if (packetCount > 0) {
            sendBufferedData();
        }
        lastReportTime = currentMillis;
    }
}

void initWiFi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA); // Force radio hardware layer out of low-power OFF mode
    WiFi.config(local_IP, gateway, subnet, dns);
    WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CHANNEL, WIFI_BSSID);
}

void sendSinglePacket(uint8_t id, uint8_t trigger, int16_t temp) {
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
            
            char hexMsg[21];
            sprintf(hexMsg, "%02X%02X%04X%04X", id, trigger, (uint16_t)temp, (uint16_t)lastValidRSSI);
            client.println(hexMsg);
            
            unsigned long start = millis();
            while (millis() - start < 5000) {
                if (client.available()) {
                    String response = client.readStringUntil('\n');
                    if (response.indexOf("ACK") >= 0) {
                        break;
                    }
                }
                delay(50);
            }
            client.stop();
        }
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
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
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}