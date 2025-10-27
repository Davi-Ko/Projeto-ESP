#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// ====================== CONFIGURAÇÕES ======================
#define DEVICE_NAME "PORTEIRO"  // 👉 altere para "PORTA_B" ou "PORTEIRO"
const char* ssid = "Evosystems&Wires Visitante";
const char* password = "Wifi2025";

#define UDP_PORT 4210
#define RELAY_TIME 5000  // 5 segundos

// ===================== PINOS ================================
#define BTN1_PIN 5    // D1 - facial / botão 1 do porteiro
#define BTN2_PIN 4    // D2 - botão 2 (apenas porteiro)
#define BYPASS_PIN 0  // D3 - interruptor bypass (apenas porteiro)
#define SENSOR_PIN 14 // D5 - sensor da porta
#define RELAY_PIN 12  // D6 - LED ímã
#define PUPE_PIN 13   // D7 - LED puxe/empurre

// ===================== VARIÁVEIS ============================
WiFiUDP udp;
IPAddress localIP;
String knownNames[5];
String knownIPs[5];
unsigned long lastPing[5];
bool bypassMode = false;
bool portaAberta = false;
unsigned long relayStart = 0;
bool relayAtivo = false;
bool discoveryDone = false;
unsigned long lastDiscovery = 0;
unsigned long lastPingSent = 0;

// ===================== FUNÇÕES =============================
void sendBroadcast(const String& msg) {
  udp.beginPacket("255.255.255.255", UDP_PORT);
  udp.print(msg);
  udp.endPacket();
  Serial.println("[UDP] Broadcast enviado: " + msg);
}

void sendStatus() {
  String estado = portaAberta ? "OPEN" : "CLOSED";
  sendBroadcast("STATUS|" + String(DEVICE_NAME) + "|" + estado);
}

void abrirPorta() {
  if (relayAtivo) return;
  Serial.println("🔓 Porta abrindo...");
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(PUPE_PIN, HIGH);
  relayStart = millis();
  relayAtivo = true;
  portaAberta = true;
  sendStatus();
}

void addDevice(const String& dev, const String& ip) {
  if (dev == DEVICE_NAME) return;

  for (int i = 0; i < 5; i++) {
    if (knownNames[i] == dev) {
      knownIPs[i] = ip;
      lastPing[i] = millis();
      return;
    }
  }

  for (int i = 0; i < 5; i++) {
    if (knownNames[i] == "") {
      knownNames[i] = dev;
      knownIPs[i] = ip;
      lastPing[i] = millis();
      Serial.println("[DISCOVERY] Novo device: " + dev + " -> " + ip);
      break;
    }
  }

  // Envia confirmação mútua
  sendBroadcast("CONFIRM|" + String(DEVICE_NAME) + "|" + localIP.toString());
}

int countKnownDevices() {
  int count = 0;
  for (int i = 0; i < 5; i++) {
    if (knownNames[i] != "") count++;
  }
  return count;
}

void processMessage(const String& msg) {
  if (msg.startsWith("DISCOVERY|")) {
    int i1 = msg.indexOf('|') + 1;
    int i2 = msg.indexOf('|', i1);
    String dev = msg.substring(i1, i2);
    String ip = msg.substring(i2 + 1);
    addDevice(dev, ip);
  }

  else if (msg.startsWith("CONFIRM|")) {
    int i1 = msg.indexOf('|') + 1;
    int i2 = msg.indexOf('|', i1);
    String dev = msg.substring(i1, i2);
    String ip = msg.substring(i2 + 1);
    addDevice(dev, ip);
  }

  else if (msg.startsWith("PING|")) {
    int i1 = msg.indexOf('|') + 1;
    int i2 = msg.indexOf('|', i1);
    String dev = msg.substring(i1, i2);
    String ip = msg.substring(i2 + 1);
    addDevice(dev, ip);
    sendBroadcast("PONG|" + String(DEVICE_NAME) + "|" + localIP.toString());
  }

  else if (msg.startsWith("PONG|")) {
    int i1 = msg.indexOf('|') + 1;
    int i2 = msg.indexOf('|', i1);
    String dev = msg.substring(i1, i2);
    for (int i = 0; i < 5; i++) {
      if (knownNames[i] == dev) {
        lastPing[i] = millis();
      }
    }
  }

  else if (msg.startsWith("OPEN|")) {
    String target = msg.substring(5);
    if (target == DEVICE_NAME) {
      if (!bypassMode && portaAberta) {
        Serial.println("🚫 Intertravamento ativo, não pode abrir!");
        return;
      }
      abrirPorta();
    }
  }

  else if (msg.startsWith("BYPASS|")) {
    bypassMode = msg.endsWith("ON");
    Serial.println(bypassMode ? "⚠️ Bypass ativado!" : "🔒 Bypass desativado.");
  }

  else if (msg.startsWith("STATUS|")) {
    Serial.println("[STATUS recebido] " + msg);
  }
}

// ===================== SETUP ===============================
void setup() {
  Serial.begin(115200);
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(BYPASS_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(PUPE_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(PUPE_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi conectado!");
  localIP = WiFi.localIP();
  Serial.println("IP: " + localIP.toString());

  udp.begin(UDP_PORT);
  sendBroadcast("DISCOVERY|" + String(DEVICE_NAME) + "|" + localIP.toString());
}

// ===================== LOOP ================================
void loop() {
  // Receber pacotes UDP
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char buffer[255];
    int len = udp.read(buffer, 255);
    if (len > 0) buffer[len] = '\0';
    processMessage(String(buffer));
  }

  // Envia broadcast de descoberta até todos confirmarem
  if (!discoveryDone && millis() - lastDiscovery > 2000) {
    sendBroadcast("DISCOVERY|" + String(DEVICE_NAME) + "|" + localIP.toString());
    lastDiscovery = millis();
  }

  // Envia PING a cada 10s
  if (millis() - lastPingSent > 10000) {
    sendBroadcast("PING|" + String(DEVICE_NAME) + "|" + localIP.toString());
    lastPingSent = millis();
  }

  // Remove dispositivos inativos (sem PONG há 30s)
  for (int i = 0; i < 5; i++) {
    if (knownNames[i] != "" && millis() - lastPing[i] > 30000) {
      Serial.println("⚠️ Dispositivo inativo removido: " + knownNames[i]);
      knownNames[i] = "";
      knownIPs[i] = "";
      discoveryDone = false; // força reativação do broadcast
    }
  }

  // Atualiza flag se todos estão confirmados
  if (countKnownDevices() >= 2 && !discoveryDone) {
    discoveryDone = true;
    Serial.println("🟢 Todos os dispositivos confirmados! Broadcast encerrado.");
  }

  // Lógica de botões
  if (String(DEVICE_NAME) == "PORTEIRO") {
    if (digitalRead(BTN1_PIN) == LOW) { sendBroadcast("OPEN|PORTA_A"); delay(300); }
    if (digitalRead(BTN2_PIN) == LOW) { sendBroadcast("OPEN|PORTA_B"); delay(300); }

    bool bypassState = (digitalRead(BYPASS_PIN) == LOW);
    static bool lastBypass = !bypassState;
    if (bypassState != lastBypass) {
      sendBroadcast("BYPASS|" + String(bypassState ? "ON" : "OFF"));
      lastBypass = bypassState;
      delay(300);
    }
  } else {
    if (digitalRead(BTN1_PIN) == LOW) { abrirPorta(); delay(300); }
  }

  // Atualiza estado do sensor
  portaAberta = (digitalRead(SENSOR_PIN) == LOW);

  // Desliga o relé após 5s
  if (relayAtivo && millis() - relayStart >= RELAY_TIME) {
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(PUPE_PIN, LOW);
    relayAtivo = false;
    portaAberta = false;
    sendStatus();
    Serial.println("🔒 Porta fechada novamente.");
  }
}
