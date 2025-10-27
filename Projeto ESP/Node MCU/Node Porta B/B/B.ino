#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// ====================== CONFIGURAÇÕES ======================
#define DEVICE_NAME "PORTA_B"  // 👉 altere para "PORTA_B" ou "PORTEIRO"
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
bool bypassMode = false;
bool portaAberta = false;
unsigned long relayStart = 0;
bool relayAtivo = false;
bool discoveryDone = false;

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

void processMessage(const String& msg) {
  if (msg.startsWith("DISCOVERY|")) {
    int i1 = msg.indexOf('|') + 1;
    int i2 = msg.indexOf('|', i1);
    String dev = msg.substring(i1, i2);
    String ip = msg.substring(i2 + 1);

    if (dev == DEVICE_NAME) return;

    bool exists = false;
    for (int i = 0; i < 5; i++) {
      if (knownNames[i] == dev) exists = true;
    }
    if (!exists) {
      for (int i = 0; i < 5; i++) {
        if (knownNames[i] == "") {
          knownNames[i] = dev;
          knownIPs[i] = ip;
          Serial.println("[DISCOVERY] Novo device: " + dev + " -> " + ip);
          break;
        }
      }
    }

    // Se todos foram descobertos, para de mandar broadcast
    int discovered = 0;
    for (int i = 0; i < 5; i++) {
      if (knownNames[i] != "") discovered++;
    }
    if (discovered >= 2 && !discoveryDone) { // Exclui o próprio
      discoveryDone = true;
      Serial.println("🟢 Todos os dispositivos descobertos! Broadcast encerrado.");
    }
  }

  else if (msg.startsWith("OPEN|")) {
    String target = msg.substring(5);
    if (target == DEVICE_NAME) {
      if (!bypassMode) {
        // simulação: impede se a própria porta já está aberta
        if (portaAberta) {
          Serial.println("🚫 Intertravamento ativo, não pode abrir!");
          return;
        }
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

  // Envia broadcast de descoberta até todos serem encontrados
  static unsigned long lastDiscovery = 0;
  if (!discoveryDone && millis() - lastDiscovery > 2000) {
    sendBroadcast("DISCOVERY|" + String(DEVICE_NAME) + "|" + localIP.toString());
    lastDiscovery = millis();
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
