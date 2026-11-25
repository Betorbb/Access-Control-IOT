#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <SPI.h>
#include <MFRC522.h>

// ==================== CONFIGURAÇÕES ====================
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

const char* MQTT_SERVER = "broker.hivemq.com";
const int MQTT_PORT = 1883;
const char* MQTT_USER = "";
const char* MQTT_PASSWORD = "";

const char* DEVICE_ID = "ESP32-AC-001";
// Cole aqui a URL ativa exibida no terminal do cloudflared, mantendo /api/access no final
const char* API_URL = "https://concerning-units-princeton-tide.trycloudflare.com/api/access";

// Pinos
#define LED_VERDE 32
#define LED_VERMELHO 33
#define BUZZER 25
#define RELAY_PIN 26
#define DHT_PIN 15
#define PIR_PIN 13

// Pinos SPI RFID
#define RFID_SS 5
#define RFID_RST 27

#define DHTTYPE DHT22

// ==================== OBJETOS GLOBAIS ====================
WiFiClient espClient;
PubSubClient mqttClient(espClient);
DHT dht(DHT_PIN, DHTTYPE);
MFRC522 rfid(RFID_SS, RFID_RST);

unsigned long telemetryInterval = 30000;
unsigned long lastTelemetry = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastRFIDCheck = 0;
unsigned long lastRFIDReadTime = 0;
unsigned long lastReconnect = 0;
unsigned long lastWifiAttempt = 0;

bool mqttConnected = false;
bool wifiConnected = false;
String lastUID = "";
bool doorOpen = false;
unsigned long doorOpenTime = 0;
unsigned long doorOpenDuration = 5000;

// ==================== PROTÓTIPOS ====================
void connectWiFi();
void maintainConnections();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleCommand(JsonDocument& doc);
void handleConfig(JsonDocument& doc);
void sendTelemetry();
void sendTelemetryHTTP();
void sendHeartbeat();
void publishStatus(const char* status);
void publishEvent(const char* eventType, const char* details);
void checkRFID();
void verifyRFIDAccess(String uid);
void openDoor();
void closeDoor();
void feedbackNegado();
void checkPIR();
String getTimestamp();
void blinkLED(int pin, int times, int delayMs);

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== Access Control IoT - Entrega 2 ===");
  Serial.printf("Device ID: %s\n", DEVICE_ID);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, LOW);
  digitalWrite(BUZZER, LOW);
  digitalWrite(RELAY_PIN, LOW);

  dht.begin();
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID RC522 OK");

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(2048);

  connectWiFi();

  Serial.println("Setup completo!");
}

// ==================== LOOP PRINCIPAL ====================
void loop() {
  maintainConnections();
  mqttClient.loop();

  if (millis() - lastTelemetry > telemetryInterval) {
    sendTelemetry();
    lastTelemetry = millis();
  }

  if (millis() - lastHeartbeat > 10000) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }

  if (millis() - lastRFIDCheck > 100) {
    checkRFID();
    lastRFIDCheck = millis();
  }

  checkPIR();

  if (doorOpen && millis() - doorOpenTime > doorOpenDuration) {
    closeDoor();
    doorOpenDuration = 5000;
  }

  delay(10);
}

// ==================== WIFI & MQTT ====================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.printf("\nWiFi conectado! IP: %s (RSSI: %d)\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    blinkLED(LED_VERDE, 3, 100);
  } else {
    wifiConnected = false;
    Serial.printf("\nFalha ao conectar WiFi! Status: %d\n", WiFi.status());
  }
}

void maintainConnections() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    if (millis() - lastWifiAttempt >= 10000 || lastWifiAttempt == 0) {
      lastWifiAttempt = millis();
      connectWiFi();
    }
    return;
  }

  wifiConnected = true;
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
}

void reconnectMQTT() {
  if (millis() - lastReconnect < 5000) return;

  Serial.print("Conectando MQTT...");
  String clientId = "ESP32-" + String(DEVICE_ID);

  if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
    mqttConnected = true;
    Serial.println(" Conectado!");

    String cmdTopic = "access-control/" + String(DEVICE_ID) + "/commands";
    mqttClient.subscribe(cmdTopic.c_str());

    String configTopic = "access-control/" + String(DEVICE_ID) + "/config";
    mqttClient.subscribe(configTopic.c_str());

    publishStatus("online");
    blinkLED(LED_VERDE, 2, 100);
  } else {
    mqttConnected = false;
    Serial.printf(" Falhou, rc=%d\n", mqttClient.state());
    lastReconnect = millis();
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.printf("MQTT [%s]: %s\n", topic, message.c_str());

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    Serial.println("JSON parse error!");
    return;
  }

  String topicStr = String(topic);
  if (topicStr.endsWith("/commands")) {
    handleCommand(doc);
  } else if (topicStr.endsWith("/config")) {
    handleConfig(doc);
  }
}

// ==================== HANDLERS ====================
void handleCommand(JsonDocument& doc) {
  String command = doc["command"] | "";
  JsonObject payload = doc["payload"];

  Serial.printf("Comando recebido: %s\n", command.c_str());

  if (command == "open_door") {
    openDoor();
    publishEvent("door_opened", "Acesso remoto concedido");
  }
  else if (command == "close_door") {
    closeDoor();
    publishEvent("door_closed", "Porta fechada remotamente");
  }
  else if (command == "reboot") {
    publishEvent("rebooting", "Reiniciando por comando remoto");
    delay(1000);
    ESP.restart();
  }
  else if (command == "update_config") {
    if (!payload.isNull() && payload["telemetry_interval"].is<unsigned long>()) {
      telemetryInterval = payload["telemetry_interval"];
    }
    publishEvent("config_updated", "Configuração atualizada");
  }
  else if (command == "unlock") {
    doorOpenDuration = (!payload.isNull() && payload["duration"].is<unsigned long>())
                       ? payload["duration"].as<unsigned long>()
                       : 5000;
    openDoor();
    String msg = "Porta destravada por " + String(doorOpenDuration) + "ms";
    publishEvent("door_unlocked", msg.c_str());
  }
}

void handleConfig(JsonDocument& doc) {
  Serial.println("Configuração recebida");
}

// ==================== TELEMETRIA ====================
void sendTelemetry() {
  if (!mqttClient.connected()) {
    sendTelemetryHTTP();
    return;
  }

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int battery = constrain(map(analogRead(34), 0, 4095, 0, 100), 0, 100);
  int signal = constrain(map(WiFi.RSSI(), -100, -30, 0, 100), 0, 100);

  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["temperature"] = isnan(temperature) ? 0 : temperature;
  doc["humidity"] = isnan(humidity) ? 0 : humidity;
  doc["battery"] = battery;
  doc["signal_strength"] = signal;
  doc["timestamp"] = getTimestamp();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;

  String topic = "access-control/" + String(DEVICE_ID) + "/telemetry";
  String payload;
  serializeJson(doc, payload);

  if (mqttClient.publish(topic.c_str(), payload.c_str())) {
    Serial.println("Telemetria enviada via MQTT");
    blinkLED(LED_VERDE, 1, 50);
  } else {
    Serial.println("Falha ao publicar telemetria");
    sendTelemetryHTTP();
  }
}

void sendTelemetryHTTP() {
  if (!wifiConnected) return;

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;
  if (!http.begin(secureClient, API_URL)) {
    Serial.println("Falha ao iniciar HTTPClient na telemetria");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["temperature"] = isnan(temperature) ? 0 : temperature;
  doc["humidity"] = isnan(humidity) ? 0 : humidity;
  doc["battery"] = constrain(map(analogRead(34), 0, 4095, 0, 100), 0, 100);
  doc["signal_strength"] = constrain(map(WiFi.RSSI(), -100, -30, 0, 100), 0, 100);
  doc["timestamp"] = getTimestamp();

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    Serial.printf("Telemetria HTTP enviada: %d\n", httpCode);
  } else {
    Serial.printf("Falha HTTP: %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
  }
  http.end();
}

void sendHeartbeat() {
  if (!mqttClient.connected()) return;

  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["status"] = "online";
  doc["free_heap"] = ESP.getFreeHeap();
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;
  doc["timestamp"] = getTimestamp();

  String payload;
  serializeJson(doc, payload);
  mqttClient.publish(("access-control/" + String(DEVICE_ID) + "/status").c_str(), payload.c_str());
}

void publishStatus(const char* status) {
  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["status"] = status;
  doc["timestamp"] = getTimestamp();

  String payload;
  serializeJson(doc, payload);
  mqttClient.publish(("access-control/" + String(DEVICE_ID) + "/status").c_str(), payload.c_str());
}

void publishEvent(const char* eventType, const char* details) {
  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["event_type"] = eventType;
  doc["details"] = details;
  doc["timestamp"] = getTimestamp();

  String payload;
  serializeJson(doc, payload);
  mqttClient.publish(("access-control/" + String(DEVICE_ID) + "/events").c_str(), payload.c_str());
}

// ==================== RFID ====================
void checkRFID() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uid += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  if (uid == lastUID && millis() - lastRFIDReadTime < 3000) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }
  lastUID = uid;
  lastRFIDReadTime = millis();

  Serial.printf("RFID detectado: %s\n", uid.c_str());

  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["event_type"] = "rfid_detected";
  doc["rfid_uid"] = uid;
  doc["timestamp"] = getTimestamp();

  String payload;
  serializeJson(doc, payload);
  mqttClient.publish(("access-control/" + String(DEVICE_ID) + "/events").c_str(), payload.c_str());

  verifyRFIDAccess(uid);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void verifyRFIDAccess(String uid) {
  if (!wifiConnected) {
    feedbackNegado();
    return;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;
  String url = String(API_URL);
  if (url.endsWith("/telemetry")) {
    url.replace("/telemetry", "/access");
  }

  if (!http.begin(secureClient, url)) {
    Serial.println("Falha ao iniciar HTTPClient no acesso RFID");
    feedbackNegado();
    return;
  }

  http.addHeader("Content-Type", "application/json");

  String payload = "{\"uid\":\"" + uid + "\"}";
  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.printf("HTTP codigo: %d\n", httpCode);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, response);

    bool granted = (!err && doc["granted"].is<bool>())
                     ? doc["granted"].as<bool>()
                     : false;

    String user = (!err && !doc["user"].isNull())
                    ? doc["user"].as<String>()
                    : "Usuário";

    if (granted) {
      Serial.printf("Acesso LIBERADO para: %s\n", user.c_str());
      openDoor();
      String msg = "Acesso concedido para " + user;
      publishEvent("access_granted", msg.c_str());
    } else {
      Serial.println("Acesso NEGADO");
      feedbackNegado();
      String msg = "Acesso negado para UID: " + uid;
      publishEvent("access_denied", msg.c_str());
    }
  } else {
    Serial.printf("Erro HTTP: %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
    feedbackNegado();
  }
  http.end();
}

// ==================== PORTA & FEEDBACK ====================
void openDoor() {
  digitalWrite(RELAY_PIN, HIGH);
  doorOpen = true;
  doorOpenTime = millis();
  digitalWrite(BUZZER, HIGH);
  blinkLED(LED_VERDE, 2, 100);
  digitalWrite(BUZZER, LOW);
  Serial.println("Porta ABERTA");
}

void closeDoor() {
  digitalWrite(RELAY_PIN, LOW);
  doorOpen = false;
  Serial.println("Porta FECHADA");
}

void feedbackNegado() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_VERMELHO, HIGH);
    digitalWrite(BUZZER, HIGH);
    delay(100);
    digitalWrite(LED_VERMELHO, LOW);
    digitalWrite(BUZZER, LOW);
    delay(100);
  }
}

// ==================== PIR ====================
void checkPIR() {
  static bool lastPIRState = false;
  bool currentPIR = digitalRead(PIR_PIN);

  if (currentPIR && !lastPIRState) {
    Serial.println("Movimento detectado!");
    publishEvent("motion_detected", "Movimento detectado pelo sensor PIR");
    blinkLED(LED_VERMELHO, 2, 100);
  }
  lastPIRState = currentPIR;
}

// ==================== UTILITÁRIOS ====================
String getTimestamp() {
  unsigned long secs = millis() / 1000;
  unsigned int days = secs / 86400;
  unsigned int hours = (secs % 86400) / 3600;
  unsigned int minutes = (secs % 3600) / 60;
  unsigned int secs_rem = secs % 60;

  char buffer[32];
  sprintf(buffer, "2026-11-%02dT%02d:%02d:%02dZ", (days % 30) + 1, hours, minutes, secs_rem);
  return String(buffer);
}

void blinkLED(int pin, int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(delayMs);
    digitalWrite(pin, LOW);
    if (i < times - 1) delay(delayMs);
  }
}