// =============================================================================
//  Mirror Hint Sensor — Desk Drawer (A Mermaid's Tale)
//  Board: ESP8266 (NodeMCU/CH340) + VL53L0X time-of-flight sensor
//
//  Mirror sits in front of the sensor. When the mirror is REMOVED for
//  5 consecutive reads, publish "solved" (NOT retained) on:
//      MermaidsTale/DeskDrawer/mirrorhint
//  and pull OUTPUT_PIN low.
//
//  PINS (verified by I2C scan 2026-08-24 — the physical SDA/SCL wires are
//  swapped vs the silk labels; the sensor ACKs at 0x29 ONLY on SDA=5/SCL=4):
//      VL53L0X SDA -> GPIO5  (board label D1)
//      VL53L0X SCL -> GPIO4  (board label D2)
//      Output      -> GPIO12 (board label D6)
// =============================================================================

#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// ==== Wi-Fi ====
const char* WIFI_SSID     = "AlchemyGuest";
const char* WIFI_PASSWORD = "VoodooVacation5601";

// ==== MQTT ====
const char* MQTT_SERVER = "10.1.10.115";
const int   MQTT_PORT   = 1883;
const char* MQTT_TOPIC  = "MermaidsTale/DeskDrawer/mirrorhint";

// ==== Pins (raw GPIO numbers) ====
#define SDA_PIN    5    // GPIO5 = D1  <-- verified by scan
#define SCL_PIN    4    // GPIO4 = D2  <-- verified by scan
#define OUTPUT_PIN 12   // GPIO12 = D6

// ==== Sensor settings ====
#define OBJECT_DISTANCE_MM     200
#define MISSING_COUNT_REQUIRED 5

Adafruit_VL53L0X lox;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

bool objectHasBeenSeen = false;
bool puzzleSolved = false;
int missingCount = 0;
unsigned long lastMQTTReconnectAttempt = 0;

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  if (mqttClient.connected()) return;
  if (millis() - lastMQTTReconnectAttempt < 5000) return;
  lastMQTTReconnectAttempt = millis();

  Serial.print("Connecting to MQTT...");
  String clientID = "MirrorHint-";
  clientID += String(ESP.getChipId(), HEX);

  if (mqttClient.connect(clientID.c_str())) {
    Serial.println("CONNECTED");
  } else {
    Serial.print("FAILED, rc=");
    Serial.println(mqttClient.state());
  }
}

void solvePuzzle() {
  if (puzzleSolved) return;
  puzzleSolved = true;

  digitalWrite(OUTPUT_PIN, LOW);

  Serial.println();
  Serial.println("============================");
  Serial.println("MIRROR REMOVED");
  Serial.println("D6 -> LOW");
  Serial.println("PUZZLE SOLVED");
  Serial.println("============================");

  // NOT retained — a retained "solved" would replay into every
  // reconnect after GameReset (see retained-message incidents).
  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_TOPIC, "solved");
    Serial.print("MQTT SENT: ");
    Serial.print(MQTT_TOPIC);
    Serial.println(" = solved");
  } else {
    Serial.println("WARNING: MQTT not connected.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("Mirror Hint Sensor Starting...");

  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, HIGH);
  Serial.println("D6 -> HIGH");

  Wire.begin(SDA_PIN, SCL_PIN);

  Serial.println("Starting VL53L0X...");
  if (!lox.begin()) {
    Serial.println("ERROR: VL53L0X NOT FOUND");
    while (1) { delay(100); }
  }
  Serial.println("VL53L0X connected.");

  connectWiFi();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  reconnectMQTT();

  Serial.println();
  Serial.println("System ready.");
}

void loop() {
  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();

  if (puzzleSolved) return;

  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);

  if (measure.RangeStatus != 4) {
    int distance = measure.RangeMilliMeter;
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.println(" mm");

    if (distance < OBJECT_DISTANCE_MM) {
      objectHasBeenSeen = true;
      missingCount = 0;
      digitalWrite(OUTPUT_PIN, HIGH);
      Serial.println("OBJECT PRESENT -> D6 HIGH");
    } else if (objectHasBeenSeen) {
      missingCount++;
      Serial.print("Object missing count: ");
      Serial.println(missingCount);
      if (missingCount >= MISSING_COUNT_REQUIRED) solvePuzzle();
    }
  } else {
    Serial.println("OUT OF RANGE");
    if (objectHasBeenSeen) {
      missingCount++;
      Serial.print("Object missing count: ");
      Serial.println(missingCount);
      if (missingCount >= MISSING_COUNT_REQUIRED) solvePuzzle();
    }
  }

  delay(100);
}
