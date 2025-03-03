
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <WiFiManager.h>
#include <esp_task_wdt.h> // For watchdog

// WebSocket server details
const char *server = "socket.jpfashion.in";
const int port = 3000;

// Define GPIO pins for gate relays and indicators
const int gate1Pin = 5;
const int gate2Pin = 18;
const int ledPin = 12;    // LED indicates WiFi/Internet issues
const int buttonPin = 13; // Button to reset WiFi credentials

// Timing variables for non-blocking relay control
unsigned long gate1OffTime = 0;
unsigned long gate2OffTime = 0;
const unsigned long offDuration = 5000; // 5 seconds

// Internet recheck delay (if WiFi is up but Internet is down)
const unsigned long internetRetryDelay = 15000;
unsigned long internetLossStart = 0;

WiFiClientSecure secureClient;
WebSocketsClient webSocket;

//---------------------------------------------------------------------
// Puts both relays into a safe state (OFF)
void setRelaysSafe()
{
  digitalWrite(gate1Pin, LOW);
  digitalWrite(gate2Pin, LOW);
  Serial.println("[INFO] Safe state: Both relays are OFF.");
}

//---------------------------------------------------------------------
// Checks Internet connectivity by trying to connect to a known host.
bool isInternetConnected()
{
  WiFiClient testClient;
  const char *testHost = "www.google.com";
  if (testClient.connect(testHost, 80))
  {
    testClient.stop();
    return true;
  }
  return false;
}

void reconnectWebSocket()
{
  Serial.println("[INFO] Attempting to reconnect WebSocket...");
  webSocket.beginSSL(server, port, "/");
}

//---------------------------------------------------------------------
// Uses WiFiManager to establish a WiFi connection. This function
// blocks until a connection is made.
void setupWiFi()
{
  Serial.println("[INFO] Launching WiFiManager portal...");
  WiFiManager wifiManager;
  // Optionally, you can set a timeout:
  // wifiManager.setConnectTimeout(30);
  if (!wifiManager.autoConnect("GateLockAP"))
  {
    Serial.println("[ERROR] WiFiManager failed to connect. Restarting...");
    ESP.restart();
  }
  Serial.println("[INFO] WiFiManager connected.");
  Serial.print("[INFO] Connected SSID: ");
  Serial.println(WiFi.SSID());
  Serial.print("[INFO] IP Address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(ledPin, LOW);
  setRelaysSafe();
  internetLossStart = 0;
}

//---------------------------------------------------------------------
// WebSocket event handler.
void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_CONNECTED:
    Serial.println("[INFO] Connected to WebSocket server.");
    webSocket.sendTXT(String("Hello Server From ESP32, connected SSID: ") + WiFi.SSID());
    digitalWrite(gate2Pin, HIGH);
    digitalWrite(gate1Pin, HIGH);
    break;

  case WStype_DISCONNECTED:
    Serial.println("[ERROR] Disconnected from WebSocket server.");
    setRelaysSafe();
    break;

  case WStype_TEXT:
  {
    String message = String((char *)payload);
    Serial.println("[INFO] WebSocket message received: " + message);
    if (message == "GATE1_ON")
    {
      digitalWrite(gate1Pin, HIGH);
      Serial.println("[INFO] Gate 1 turned ON.");
      gate1OffTime = 0;
    }
    else if (message == "GATE1_OFF")
    {
      digitalWrite(gate1Pin, LOW);
      Serial.println("[INFO] Gate 1 turned OFF for 5 seconds.");
      gate1OffTime = millis();
    }
    else if (message == "GATE2_ON")
    {
      digitalWrite(gate2Pin, HIGH);
      Serial.println("[INFO] Gate 2 turned ON.");
      gate2OffTime = 0;
    }
    else if (message == "GATE2_OFF")
    {
      digitalWrite(gate2Pin, LOW);
      Serial.println("[INFO] Gate 2 turned OFF for 5 seconds.");
      gate2OffTime = millis();
    }
    else if (message == "ping")
    {
      webSocket.sendTXT("pong");
    }
    break;
  }

  default:
    break;
  }
}

//---------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(1000);

  // Initialize pins.
  pinMode(gate1Pin, OUTPUT);
  pinMode(gate2Pin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  setRelaysSafe();
  digitalWrite(ledPin, LOW);

  // Connect via WiFiManager.
  setupWiFi();

  // Register the current (main) task with the watchdog.
  TaskHandle_t mainTask = xTaskGetCurrentTaskHandle();
  if (esp_task_wdt_add(mainTask) != ESP_OK)
  {
    Serial.println("[WARN] Main task already registered with watchdog or failed to add.");
  }

  // Setup secure client and WebSocket.
  secureClient.setInsecure();
  webSocket.beginSSL(server, port, "/");
  webSocket.onEvent(webSocketEvent);

  // Check Internet connectivity at startup.
  if (!isInternetConnected())
  {
    Serial.println("[WARN] Internet not reachable at startup.");
    digitalWrite(ledPin, HIGH);
  }
}

//---------------------------------------------------------------------
void loop()
{
  // Reset the watchdog timer.
  esp_task_wdt_reset();

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck >= 100000)
  {
    lastCheck = millis();
    if (!webSocket.isConnected())
    {
      reconnectWebSocket();
    }
  }
  // end hua hai
  webSocket.loop();

  // Every 10 seconds, check connectivity.
  // static unsigned long lastCheck = 0;
  if (millis() - lastCheck >= 10000)
  {
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED || !isInternetConnected())
    {
      Serial.println("[ERROR] WiFi or Internet disconnected. Reconnecting...");
      digitalWrite(ledPin, HIGH);
      setRelaysSafe();
      WiFi.disconnect(true);
      setupWiFi();
      webSocket.beginSSL(server, port, "/");
    }
    else
    {
      digitalWrite(ledPin, LOW);
    }
  }

  // Check if reset button is pressed.
  if (digitalRead(buttonPin) == LOW)
  {
    Serial.println("[INFO] Button pressed, resetting WiFi credentials...");
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    ESP.restart();
  }

  // Non-blocking check to restore relay state after off duration.
  unsigned long currentMillis = millis();
  if (gate1OffTime != 0 && currentMillis - gate1OffTime >= offDuration)
  {
    digitalWrite(gate1Pin, HIGH);
    Serial.println("[INFO] Gate 1 automatically turned ON after delay.");
    gate1OffTime = 0;
  }
  if (gate2OffTime != 0 && currentMillis - gate2OffTime >= offDuration)
  {
    digitalWrite(gate2Pin, HIGH);
    Serial.println("[INFO] Gate 2 automatically turned ON after delay.");
    gate2OffTime = 0;
  }

  delay(250); // Brief delay to reduce CPU load.
}
