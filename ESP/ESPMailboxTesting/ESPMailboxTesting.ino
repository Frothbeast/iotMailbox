#include <WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include "driver/gpio.h" // Required for gpio_hold functions

#define HALL_SENSOR_PIN 3
#define ONE_WIRE_BUS 2
#define RGB_LED_PIN 8 
#define NUM_PIXELS 1
#define MAX_PACKETS 10 

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
    // 1. Release the hold immediately so the pin can be used
    gpio_hold_dis((gpio_num_t)HALL_SENSOR_PIN);

    Serial.begin(115200);
    delay(1000); 
    Serial.println("\n--- DEVICE WAKEUP ---");

    pixels.begin();
    pixels.setBrightness(50);
    pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);
    
    int hallState = digitalRead(HALL_SENSOR_PIN);
    Serial.print("Current Hall Sensor State: ");
    Serial.println(hallState == LOW ? "LOW (Magnet Present)" : "HIGH (No Magnet)");

    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    uint8_t currentTrigger = 1; 

    if (reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
        Serial.println("Wake Reason: Power On / Reset");
        currentTrigger = 0;
    } 
    else if (reason == ESP_SLEEP_WAKEUP_GPIO) {
        Serial.println("Wake Reason: Hall Sensor Pin Change");
        currentTrigger = (hallState == LOW) ? 2 : 3;
    }
    else if (reason == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("Wake Reason: 60s Timer");
    }

    if (hallState == LOW) {
        pixels.setPixelColor(0, pixels.Color(0, 255, 0));
        pixels.show();
    }

    if (currentTrigger == 0) {
        sendZeroPacket();
    }

    sensors.begin();
    sensors.requestTemperatures();
    float rawTemp = sensors.getTempCByIndex(0);

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

    // 2. CONFIGURE NEXT WAKEUP
    int wakeLevel = (hallState == LOW) ? 1 : 0; 
    
    // Ensure pull-up is active
    pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);
    
    Serial.print("Configuring Wakeup for Level: ");
    Serial.println(wakeLevel);
    
    esp_deep_sleep_enable_gpio_wakeup(1ULL << HALL_SENSOR_PIN, (esp_deepsleep_gpio_wake_up_mode_t)wakeLevel);
    
    // 3. LOCK THE PULL-UP STATE
    gpio_hold_en((gpio_num_t)HALL_SENSOR_PIN);
    
    esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);
    
    Serial.println("Entering Deep Sleep...");
    Serial.flush();
    esp_deep_sleep_start();
}

void sendZeroPacket() {
    Serial.print("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
        delay(500);
        timeout++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected.");
        lastValidRSSI = (int16_t)WiFi.RSSI();
        WiFiClient client;
        if (client.connect(SERVER_IP, SERVER_PORT)) {
            Serial.println("TX: 000000000000");
            client.println("000000000000");
            unsigned long start = millis();
            while (!client.available() && millis() - start * 1000);
            client.stop();
            Serial.println("Socket Closed.");
        }
    }
}

void sendBufferedData() {
    Serial.print("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int wifiTimeout = 0;
    while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
        delay(500);
        wifiTimeout++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected.");
        lastValidRSSI = (int16_t)WiFi.RSSI();
        WiFiClient client;
        if (client.connect(SERVER_IP, SERVER_PORT)) {
            client.setTimeout(2000);
            for (int i = 0; i < packetCount; i++) {
                char hexMsg[21];
                sprintf(hexMsg, "%02X%02X%04X%04X", 
                        buffer[i].id, buffer[i].trigger, 
                        (uint16_t)buffer[i].temp, (uint16_t)buffer[i].rssi);

                Serial.print("TX: ");
                Serial.println(hexMsg);
                client.println(hexMsg);
                
                unsigned long start = millis();
                bool gotAck = false;
                while (millis() - start < 10000) {
                    if (client.available()) {
                        String response = client.readStringUntil('\n');
                        Serial.print("RX: ");
                        Serial.println(response);
                        if (response.indexOf("ACK") >= 0) {
                            gotAck = true;
                            break;
                        }
                    }
                    delay(50);
                }
                if (!gotAck) {
                    Serial.println("Error: No ACK");
                    break;
                }
            }
            client.stop();
            Serial.println("Socket Closed.");
        }
        packetCount = 0;
    }
}

void loop() {}