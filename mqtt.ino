mqtt 

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

// MQTT Server details
const char *mqttServer = "91.108.110.250";
const int mqttPort = 4000;
const char *mqttUser = "omlibrary_gate_1";
const char *mqttPassword = "Sagar@2025";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Define GPIO pins for gate relays and indicators
const int gate1Pin = 5;
const int gate2Pin = 4;
const int ledPin = 2;
const int buttonPin = 0;
const int builtInLedPin = LED_BUILTIN;

// Timing variables
unsigned long gate1OffTime = 0;
unsigned long gate2OffTime = 0;
const unsigned long offDuration = 500;

unsigned long internetLossStart = 0;
const unsigned long internetRetryDelay = 15000;

//---------------------------------------------------------------------
void setRelaysSafe() {
    digitalWrite(builtInLedPin, LOW);
    digitalWrite(gate1Pin, LOW);
    digitalWrite(gate2Pin, LOW);
    Serial.println("[INFO] Safe state: Both relays are OFF.");
}

//---------------------------------------------------------------------
bool isInternetConnected() {
    WiFiClient testClient;
    if (testClient.connect("www.google.com", 80)) {
        testClient.stop();
        return true;
    }
    return false;
}

//---------------------------------------------------------------------
void reconnectMQTT() {
    while (!mqttClient.connected()) {
        Serial.println("[INFO] Connecting to MQTT...");
        if (mqttClient.connect("ESP8266Client", mqttUser, mqttPassword)) {
            Serial.println("[INFO] Connected to MQTT");
            mqttClient.subscribe("home/gate");
            mqttClient.publish("home/gate", "MQTT Reconected");
        } else {
            Serial.print("[ERROR] MQTT connection failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" Retrying in 5 seconds...");
            delay(5000);
        }
    }
}

//---------------------------------------------------------------------
void setupWiFi() {
    Serial.println("[INFO] Launching WiFiManager portal...");
    WiFiManager wifiManager;
    if (!wifiManager.autoConnect("GateLockAP")) {
        Serial.println("[ERROR] WiFiManager failed to connect. Restarting...");
        ESP.restart();
    }
    Serial.println("[INFO] WiFiManager connected.");
    Serial.print("[INFO] IP Address: ");
    Serial.println(WiFi.localIP());
    digitalWrite(ledPin, LOW);
    setRelaysSafe();
    internetLossStart = 0;
}

//---------------------------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String message = "";
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.println("[INFO] MQTT message received: " + message);

    if (message == "GATE1_ON") {
        digitalWrite(gate1Pin, HIGH);
        Serial.println("[INFO] Gate 1 turned ON.");
        gate1OffTime = 0;
    } else if (message == "GATE1_OFF") {
        digitalWrite(gate1Pin, LOW);
        Serial.println("[INFO] Gate 1 turned OFF for 5 seconds.");
        gate1OffTime = millis();
    } else if (message == "GATE2_ON") {
        digitalWrite(gate2Pin, HIGH);
        Serial.println("[INFO] Gate 2 turned ON.");
        gate2OffTime = 0;
    } else if (message == "LIGHT") {
        digitalWrite(gate2Pin, LOW);
        digitalWrite(builtInLedPin, LOW);
        mqttClient.publish("home/gate", "LIGHT turned off for 5 seconds");
        Serial.println("[INFO] Gate 2 turned OFF for 5 seconds.");
        gate2OffTime = millis();
    } else if (message == "ping") {
        mqttClient.publish("home/gate", "pong nodemcu");
    }
}

//---------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(gate1Pin, OUTPUT);
    pinMode(gate2Pin, OUTPUT);
    pinMode(builtInLedPin, OUTPUT);
    pinMode(ledPin, OUTPUT);
    pinMode(buttonPin, INPUT_PULLUP);

    setRelaysSafe();
    digitalWrite(ledPin, LOW);

    setupWiFi();
    mqttClient.setServer(mqttServer, mqttPort);
    mqttClient.setCallback(mqttCallback);
    reconnectMQTT();
}

//---------------------------------------------------------------------
void loop() {
    if (!mqttClient.connected()) {
        reconnectMQTT();
    }
    mqttClient.loop();

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck >= 10000) {
        lastCheck = millis();
        if (WiFi.status() != WL_CONNECTED || !isInternetConnected()) {
            Serial.println("[ERROR] WiFi or Internet disconnected. Reconnecting...");
            digitalWrite(ledPin, HIGH);
            setRelaysSafe();
            WiFi.disconnect(true);
            setupWiFi();
            reconnectMQTT();
        } else {
            digitalWrite(ledPin, LOW);
        }
    }

    unsigned long currentMillis = millis();
    if (gate1OffTime != 0 && currentMillis - gate1OffTime >= offDuration) {
        digitalWrite(gate1Pin, HIGH);
        Serial.println("[INFO] Gate 1 automatically turned ON after delay.");
        gate1OffTime = 0;
    }
    if (gate2OffTime != 0 && currentMillis - gate2OffTime >= offDuration) {
        digitalWrite(gate2Pin, HIGH);
        digitalWrite(builtInLedPin, HIGH);
        mqttClient.publish("home/gate", "LIGHT turned on after delay");
        Serial.println("[INFO] Gate 2 automatically turned ON after delay.");
        gate2OffTime = 0;
    }

    if (digitalRead(buttonPin) == LOW) {
        Serial.println("[INFO] Button pressed, resetting WiFi credentials...");
        WiFiManager wifiManager;
        wifiManager.resetSettings();
        ESP.restart();
    }
}
