/*
 * ESP-01 RELAY COM AUTO-DISCOVERY
 * Este ESP responde automaticamente a broadcasts de descoberta
 * e se conecta com o servidor sem configuração manual
 */

#include <ESP8266WiFi.h>
#include <espnow.h>

// Pino do relay (GPIO 0 no ESP-01)
#define RELAY_PIN 0

// LED interno para indicação visual (GPIO 2)
#define LED_PIN 2

// Configurações do timer
#define TIMER_DURATION 5000  // 5 segundos em milissegundos

// Estrutura das mensagens (deve ser igual ao servidor)
typedef struct {
  uint8_t msgType;  // 1=discovery, 2=discovery_response, 3=relay_command
  uint8_t deviceType; // 1=server, 2=relay
  char deviceName[32];
  bool relayState;
  int command;
} ESPMessage;

ESPMessage incomingMsg;
ESPMessage outgoingMsg;

// Status atual do relay
bool currentRelayState = false;
unsigned long lastCommandTime = 0;
int commandCount = 0;
bool connectedToServer = false;
uint8_t serverMAC[6];
unsigned long lastDiscoveryResponse = 0;

// Variáveis do timer para acionamento temporizado
bool timerActive = false;
unsigned long timerStart = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP-01 Relay com Auto-Discovery ===");
  
  // Configurar pinos
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  // Estado inicial: relay desligado
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, HIGH); // LED interno invertido (HIGH = apagado)
  
  // Configurar WiFi para ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  Serial.print("📡 MAC Address deste ESP: ");
  Serial.println(WiFi.macAddress());
  Serial.println("🔍 Aguardando descoberta automática...");
  
  // Inicializar ESP-NOW
  initESPNow();
  
  // Piscar LED para indicar inicialização
  for (int i = 0; i < 6; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(200);
    digitalWrite(LED_PIN, HIGH);
    delay(200);
  }
  
  Serial.println("✅ Sistema pronto - aguardando comandos...");
}

void loop() {
  // Verificar timer ativo
  if (timerActive && (millis() - timerStart >= TIMER_DURATION)) {
    Serial.println("⏰ Timer expirado - desligando relay");
    timerActive = false;
    controlRelay(false);
  }
  
  // LED de status - piscar lento se desconectado, rápido se conectado
  static unsigned long lastBlink = 0;
  unsigned long blinkInterval = connectedToServer ? 2000 : 500;
  
  if (millis() - lastBlink > blinkInterval) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    lastBlink = millis();
    
    // Status periódico
    static int statusCount = 0;
    statusCount++;
    if (statusCount >= (connectedToServer ? 5 : 10)) {
      showStatus();
      statusCount = 0;
    }
  }
  
  delay(50);
}

void initESPNow() {
  if (esp_now_init() != 0) {
    Serial.println("❌ Erro ao inicializar ESP-NOW");
    delay(1000);
    ESP.restart();
    return;
  }
  
  Serial.println("✅ ESP-NOW inicializado com sucesso");
  
  // Configurar como COMBO para poder enviar e receber
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);
  
  // Adicionar broadcast para receber descobertas
  uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_add_peer(broadcastMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
  
  Serial.println("🎧 Escutando broadcasts de descoberta...");
}

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  if (sendStatus == 0) {
    Serial.println("✅ Resposta de descoberta enviada");
  } else {
    Serial.println("❌ Falha ao enviar resposta: " + String(sendStatus));
  }
}

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  memcpy(&incomingMsg, incomingData, sizeof(incomingMsg));
  
  // Processar descoberta do servidor
  if (incomingMsg.msgType == 1 && incomingMsg.deviceType == 1) {
    Serial.println("\n🎯 Descoberta recebida do servidor!");
    Serial.print("   Nome: ");
    Serial.println(incomingMsg.deviceName);
    Serial.print("   MAC: ");
    printMAC(mac);
    
    // Responder apenas uma vez a cada 2 segundos para evitar spam
    if (millis() - lastDiscoveryResponse > 2000) {
      respondToDiscovery(mac);
      lastDiscoveryResponse = millis();
    }
    return;
  }
  
  // Processar comando de relay
  if (incomingMsg.msgType == 3) {
    commandCount++;
    lastCommandTime = millis();
    
    // Marcar como conectado se não estava
    if (!connectedToServer) {
      memcpy(serverMAC, mac, 6);
      connectedToServer = true;
      Serial.println("🔗 Conectado ao servidor!");
    }
    
    Serial.println("\n📡 Comando recebido:");
    Serial.print("   MAC Origem: ");
    printMAC(mac);
    Serial.println("   Estado: " + String(incomingMsg.relayState ? "LIGAR" : "DESLIGAR"));
    Serial.println("   Comando: " + String(incomingMsg.command));
    Serial.println("   Total comandos: " + String(commandCount));
    
    // Verificar se é comando temporizado
    if (incomingMsg.command == 5) {
      Serial.println("⏰ Comando temporizado detectado - " + String(TIMER_DURATION / 1000) + " segundos");
      timerActive = true;
      timerStart = millis();
    }
    
    // Controlar o relay
    controlRelay(incomingMsg.relayState);
  }
}

void respondToDiscovery(uint8_t* serverMac) {
  // Adicionar servidor como peer se não foi adicionado
  if (!connectedToServer) {
    esp_now_add_peer(serverMac, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
    Serial.println("🔄 Servidor adicionado como peer");
  }
  
  // Preparar resposta
  outgoingMsg.msgType = 2; // discovery_response
  outgoingMsg.deviceType = 2; // relay
  strcpy(outgoingMsg.deviceName, "ESP-Relay-01");
  outgoingMsg.relayState = currentRelayState;
  outgoingMsg.command = 0;
  
  // Enviar resposta
  uint8_t result = esp_now_send(serverMac, (uint8_t*)&outgoingMsg, sizeof(outgoingMsg));
  
  if (result == 0) {
    Serial.println("📤 Resposta de descoberta enviada");
  } else {
    Serial.println("❌ Erro ao enviar resposta: " + String(result));
  }
}

void controlRelay(bool state) {
  currentRelayState = state;
  
  // Controlar relay
  digitalWrite(RELAY_PIN, state ? HIGH : LOW);
  
  // Feedback visual no LED (invertido no ESP-01)
  if (state) {
    // Relay ligado = LED piscando rápido
    for (int i = 0; i < 6; i++) {
      digitalWrite(LED_PIN, LOW);
      delay(100);
      digitalWrite(LED_PIN, HIGH);
      delay(100);
    }
  } else {
    // Relay desligado = LED apagado
    digitalWrite(LED_PIN, HIGH);
  }
  
  // Feedback serial
  Serial.println(state ? "⚡ RELAY LIGADO" : "🔴 RELAY DESLIGADO");
  Serial.println("   GPIO 0: " + String(state ? "HIGH" : "LOW"));
  Serial.println("   Status LED atualizado");
  Serial.println();
}

void printMAC(uint8_t* mac) {
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
}

void showStatus() {
  Serial.println("📊 STATUS DO SISTEMA:");
  Serial.println("   🔗 Conectado: " + String(connectedToServer ? "SIM" : "NÃO"));
  Serial.println("   ⚡ Relay: " + String(currentRelayState ? "LIGADO" : "DESLIGADO"));
  
  if (timerActive) {
    unsigned long remaining = TIMER_DURATION - (millis() - timerStart);
    Serial.println("   ⏰ Timer: " + String(remaining / 1000) + "s restantes");
  }
  
  Serial.println("   📡 Comandos: " + String(commandCount));
  
  if (connectedToServer) {
    Serial.println("   🕐 Último comando: " + String((millis() - lastCommandTime) / 1000) + "s atrás");
  }
  
  Serial.println("   📶 MAC: " + WiFi.macAddress());
  Serial.println("   ⏱️ Uptime: " + String(millis() / 1000) + "s");
  Serial.println();
}