#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "driver/gpio.h"

#define HALL_SENSOR_PIN 3
#define ONE_WIRE_BUS 2
#define SENSOR_POWER_PIN 4 
#define RGB_LED_PIN 8 
#define NUM_PIXELS 1
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
Adafruit_NeoPixel pixels(NUM_PIXELS, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
    setCpuFrequencyMhz(80);

    gpio_hold_dis((gpio_num_t)HALL_SENSOR_PIN);
    gpio_hold_dis((gpio_num_t)SENSOR_POWER_PIN);
    
    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    delay(200); 

    pixels.begin();
    pixels.setBrightness(50);
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

    if (hallState == LOW) {
        pixels.setPixelColor(0, pixels.Color(0, 255, 0));
        pixels.show();
    }

    sensors.begin();
    float rawTemp = -127.0;
    if (sensors.getDeviceCount() > 0) {
        sensors.requestTemperatures();
        delay(800); 
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

    if (hallState == LOW) {
        delay(2000);
    }

    pixels.clear();
    pixels.show();

    int wakeLevel = (hallState == LOW) ? 1 : 0; 
    pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);
    esp_deep_sleep_enable_gpio_wakeup(1ULL << HALL_SENSOR_PIN, (esp_deepsleep_gpio_wake_up_mode_t)wakeLevel);
    
    digitalWrite(SENSOR_POWER_PIN, LOW);
    pinMode(SENSOR_POWER_PIN, INPUT); 
    gpio_hold_en((gpio_num_t)SENSOR_POWER_PIN);
    gpio_hold_en((gpio_num_t)HALL_SENSOR_PIN);
    
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    btStop();

    esp_sleep_enable_timer_wakeup(600ULL * 1000000ULL); 
    esp_deep_sleep_start();
}

void initWiFi() {
    WiFi.config(local_IP, gateway, subnet, dns);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void sendBufferedData() {
    initWiFi();
    int wifiTimeout = 0;
    while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
        delay(500);
        wifiTimeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        lastValidRSSI = (int16_t)WiFi.RSSI(); // Update RSSI before sending
        
        WiFiClient client;
        if (client.connect(SERVER_IP, SERVER_PORT)) {
            client.setTimeout(2000);
            for (int i = 0; i < packetCount; i++) {
                // If this is the first packet of a session, update its RSSI with the live value
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