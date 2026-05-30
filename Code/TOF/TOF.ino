#include <ESP8266WiFi.h>
#include <Wire.h>
#include <adafruit_vl53l0x.h>
#include <PubSubClient.h>

// ==== Wi-Fi Credentials ====
const char* ssid = "QuantumLoop";
const char* password = "Alchemy5601";

// ==== MQTT Broker ====
const char* mqtt_server = "10.1.10.115";

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

#define TRIGGER_DISTANCE 200  // Threshold distance in mm
bool isTriggered = false;  //one-shot latch; reset on MermaidsTale/GameStart = Triggered

void setup_wifi() {
    delay(10);
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, password);

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
        delay(500);
        Serial.print(".");
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWi-Fi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nFailed to connect to Wi-Fi");
        Serial.print("WiFi.status() = ");
        Serial.println(WiFi.status());
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    char message[64];
    if (length >= sizeof(message)) length = sizeof(message) - 1;
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    Serial.println(message);

    if (strcmp(topic, "MermaidsTale/GameStart") == 0 && strcmp(message, "Triggered") == 0) {
        if (isTriggered) {
            Serial.println("GameStart received — re-arming motion sensor");
            client.publish("MermaidsTale/ShipMotion2", "Searching");
        }
        isTriggered = false;
    }
}

void reconnect() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect("ESP8266Client_01")) {
            Serial.println("connected");
            client.publish("MermaidsTale/ShipMotion2", "Connected!");
            client.subscribe("MermaidsTale/#");

            // Initial publish now that we're definitely connected
            client.publish("MermaidsTale/ShipMotion2", "Searching");
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(". Trying again in 5 seconds.");
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    Wire.begin();

    if (!lox.begin()) {
        Serial.println("Failed to initialize VL53L0X!");
        while (1);
    }

    setup_wifi();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

void loop() {
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    if (isTriggered) {
        delay(100);
        return;
    }

    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false);

    if (measure.RangeStatus != 4) {
        if (measure.RangeMilliMeter < TRIGGER_DISTANCE) {
            Serial.println("MermaidsTale/ShipMotion2 triggered");
            Serial.print("Distance (mm): ");
            Serial.println(measure.RangeMilliMeter);

            client.publish("MermaidsTale/ShipMotion2", "triggered");
            client.publish("MermaidsTale/StarChart/MotionSensor", "Triggered");
            client.loop();

            isTriggered = true;
        } else {
            Serial.println("Out of range");
        }
    } else {
        Serial.println("Out of range");
    }

    delay(100);
}
