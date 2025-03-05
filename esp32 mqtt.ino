esp32 mqtt

#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <esp_task_wdt.h>

// MQTT Credentials
const char *mqttServer = "91.108.110.250";
const int mqttPort = 4000;
const char *mqttUser = "omlibrary_gate_1";
const char *mqttPassword = "Sagar@2025";
const char *mqttTopic = "home/gate";

// WiFi & MQTT Clients
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// GPIO Pins
const int gate1Pin = 5;
const int gate2Pin = 18;
const int ledPin = 12;
const int buttonPin = 13;

// Timing Variables for Gate Control
unsigned long gate1OffTime = 0;
unsigned long gate2OffTime = 0;
const unsigned long offDuration = 5000;  // 5 seconds

// Internet Loss Detection
unsigned long internetLossStart = 0;
const unsigned long internetRetryDelay = 15000;  // 15 seconds

//---------------------------------------------------------------------
void setRelaysSafe() {
  digitalWrite(gate1Pin, LOW);
  digitalWrite(gate2Pin, LOW);
  Serial.println("[INFO] Safe state: Both relays OFF.");
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
void setupWiFi() {
  Serial.println("[INFO] Connecting WiFi...");
  WiFiManager wifiManager;
 wifiManager.setConnectTimeout(300); // This line is for to wait for avlaible wifi 5 minutes if not then launch wifi manger 
  if (!wifiManager.autoConnect("GateLockAP")) {
    Serial.println("[ERROR] WiFiManager failed. Restarting...");
    ESP.restart();
  }
  Serial.println("[INFO] WiFi Connected: " + WiFi.SSID());
  Serial.println("[INFO] IP Address: " + WiFi.localIP().toString());
  digitalWrite(ledPin, LOW);
  internetLossStart = 0;
}

//---------------------------------------------------------------------
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.println("[INFO] Connecting to MQTT...");
    if (mqttClient.connect("ESP32_Client", mqttUser, mqttPassword)) {
      Serial.println("[INFO] Connected to MQTT Broker.");
      mqttClient.subscribe(mqttTopic);
      // mqttClient.publish(mqttTopic, "Websocket Reconnected");
       String message = String("Hello Server From NodeMCU, connected SSID: ") + WiFi.SSID();
      mqttClient.publish(mqttTopic, message.c_str());
      
      Serial.println("[INFO] MQTT Message Sent: " + message);
    } else {
      Serial.println("[ERROR] MQTT Failed. Retrying...");
      delay(5000);
    }
  }
}

//---------------------------------------------------------------------
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println("[INFO] MQTT Message: " + message);

  if (message == "GATE1_ON") {
    digitalWrite(gate1Pin, HIGH);
    Serial.println("[INFO] Gate 1 ON.");
    gate1OffTime = 0;
  } else if (message == "GATE1_OFF") {
    digitalWrite(gate1Pin, LOW);
    mqttClient.publish(mqttTopic, "Gate1_off");
    Serial.println("[INFO] Gate 1 OFF for 5 sec.");
    gate1OffTime = millis();
  } else if (message == "GATE2_ON") {
    digitalWrite(gate2Pin, HIGH);
    Serial.println("[INFO] Gate 2 ON.");
    gate2OffTime = 0;
  } else if (message == "GATE2_OFF") {
    digitalWrite(gate2Pin, LOW);
    mqttClient.publish(mqttTopic, "Gate1 turned off for 5 Sec");
    Serial.println("[INFO] Gate 2 OFF for 5 sec.");
    gate2OffTime = millis();
  } else if (message == "ping") {
    mqttClient.publish(mqttTopic, "pong");
  }
}

//---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(gate1Pin, OUTPUT);
  pinMode(gate2Pin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  setRelaysSafe();
  digitalWrite(ledPin, LOW);

  setupWiFi();

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(mqttCallback);
  reconnectMQTT();

  TaskHandle_t mainTask = xTaskGetCurrentTaskHandle();
  esp_task_wdt_add(mainTask);
}
unsigned long lastLoopTime = 0;
const unsigned long loopInterval = 250;
//---------------------------------------------------------------------
void loop() {
  esp_task_wdt_reset();

  if (millis() - lastLoopTime >= loopInterval) {
    lastLoopTime = millis();


    if (!mqttClient.connected()) {
      reconnectMQTT();
    }
    mqttClient.loop();

    static unsigned long lastCheck = 0;
    if (millis() - lastCheck >= 10000) {
      lastCheck = millis();
      if (WiFi.status() != WL_CONNECTED || !isInternetConnected()) {
        Serial.println("[ERROR] WiFi or Internet lost. Reconnecting...");
        digitalWrite(ledPin, HIGH);
        setRelaysSafe();
        // WiFi.disconnect(true); this function is used to reset saved wifi dont use it 
        setupWiFi();
        reconnectMQTT();
      } else {
        digitalWrite(ledPin, LOW);
      }
    }

    if (digitalRead(buttonPin) == LOW) {
      Serial.println("[INFO] Resetting WiFi...");
      WiFiManager wifiManager;
      wifiManager.resetSettings();
      ESP.restart();
    }

    unsigned long currentMillis = millis();
    if (gate1OffTime != 0 && currentMillis - gate1OffTime >= offDuration) {
      digitalWrite(gate1Pin, HIGH);
      Serial.println("[INFO] Gate 1 auto ON.");
      mqttClient.publish(mqttTopic, "Show_checkout");
      gate1OffTime = 0;
    }
    if (gate2OffTime != 0 && currentMillis - gate2OffTime >= offDuration) {
      digitalWrite(gate2Pin, HIGH);
      Serial.println("[INFO] Gate 2 auto ON.");
      gate2OffTime = 0;
    }
  }
}
