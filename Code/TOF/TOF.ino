// =============================================================================
//  Alyssa Front Desk — Wireless Motion Sensor
//  Board: ESP8266 + VL53L0X time-of-flight distance sensor (battery shield)
//
//  Purpose: sits at the front desk and tells Alyssa (our virtual receptionist)
//  when a customer walks up. Publishes ONE MQTT message per arrival:
//
//      topic:   Alyssa/FrontDesk/message
//      payload: MotionDetected
//
//  Lifecycle / health chatter goes on a SEPARATE topic so that anything
//  listening for arrivals never fires on a boot or cooldown message:
//
//      topic:   Alyssa/FrontDesk/status
//      payload: Connected | Searching | Cooldown
// =============================================================================

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <PubSubClient.h>

// ==== Wi-Fi Credentials ====
const char* ssid     = "AlchemyGuest";
const char* password = "VoodooVacation5601";

// ==== MQTT Broker ====
const char* mqtt_server = "10.1.10.115";
const char* mqtt_client_id = "AlyssaFrontDeskSensor";

// ==== MQTT Topics (keep in sync with the AI Character program) ====
const char* TOPIC_MESSAGE = "Alyssa/FrontDesk/message";   // arrivals only
const char* TOPIC_STATUS  = "Alyssa/FrontDesk/status";    // health / lifecycle
const char* TOPIC_COMMAND = "Alyssa/FrontDesk/command";   // optional: RESET / ARM

const char* PAYLOAD_MOTION = "MotionDetected";

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

#define TRIGGER_DISTANCE 200                    // mm — closer than this = someone is at the desk
const unsigned long cooldownDuration = 120000;  // 2 min before Alyssa can be pinged again
unsigned long triggerStartTime = 0;
bool isTriggered = false;

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

// Optional remote control: "RESET" or "ARM" on Alyssa/FrontDesk/command ends the
// cooldown early so the sensor is ready for the next guest.
void callback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    Serial.println(msg);

    if (strcmp(topic, TOPIC_COMMAND) == 0 && (msg == "RESET" || msg == "ARM")) {
        isTriggered = false;
        client.publish(TOPIC_STATUS, "Searching");
        Serial.println("Cooldown cleared by command. Searching.");
    }
}

void reconnect() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect(mqtt_client_id)) {
            Serial.println("connected");
            client.subscribe(TOPIC_COMMAND);

            // Health only — NEVER publish to TOPIC_MESSAGE here, or Alyssa
            // would greet an empty lobby every time the sensor reboots.
            client.publish(TOPIC_STATUS, "Connected");
            client.publish(TOPIC_STATUS, "Searching");
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

    unsigned long currentTime = millis();

    if (isTriggered) {
        if (currentTime - triggerStartTime >= cooldownDuration) {
            Serial.println("Cooldown complete. Resuming search.");
            client.publish(TOPIC_STATUS, "Searching");
            client.loop();
            isTriggered = false;
        }
        delay(100);
        return;
    }

    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false);

    if (measure.RangeStatus != 4 && measure.RangeMilliMeter < TRIGGER_DISTANCE) {
        Serial.print("Customer detected at front desk. Distance (mm): ");
        Serial.println(measure.RangeMilliMeter);

        client.publish(TOPIC_MESSAGE, PAYLOAD_MOTION);   // <-- the one message Alyssa acts on
        client.loop(); // Ensure message gets sent
        delay(500);

        isTriggered = true;
        triggerStartTime = currentTime;

        client.publish(TOPIC_STATUS, "Cooldown");
        client.loop();
    } else {
        Serial.println("Out of range");
    }

    delay(100);
}
