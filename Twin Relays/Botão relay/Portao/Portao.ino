#include <ESP8266WiFi.h>
extern "C" {
  #include <espnow.h>
}
#include <EEPROM.h>

// ================= CONFIG =================
#define RELAY_ACTIVE_HIGH true
#define RELAY_PIN 0    // GPIO0 - Pino físico 5 do ESP-01
#define LED_PIN 2      // GPIO2 - Pino físico 3 do ESP-01 (LED onboard)
#define DEVICE_ID 1
#define CHANNEL 1
#define EEPROM_SIZE 64
#define EEPROM_PEER_ADDR 0

#define HELLO_RESPONSE_INTERVAL 500
#define ACK_DELAY 200  // Delay antes de enviar ACK
#define RELAY_PULSE_TIME 1000  // Tempo do pulso do relé em ms
// ==========================================

uint8_t peerMac[6];
bool paired = false;
bool relayState = false;
uint32_t lastHelloResponse = 0;

typedef struct {
    uint8_t deviceId;
    uint8_t command;
    uint8_t relayState;
    char message[32];
} esp_now_message;


String macToStr(const uint8_t mac[6]) {
  char buf[20];
  sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", 
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

void savePeer(uint8_t *mac) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 6; i++) {
    EEPROM.write(EEPROM_PEER_ADDR + i, mac[i]);
  }
  EEPROM.commit();
  Serial.printf("Peer salvo: %s\n", macToStr(mac).c_str());
}

bool loadPeer() {
  EEPROM.begin(EEPROM_SIZE);
  bool empty = true;
  for (int i = 0; i < 6; i++) {
    peerMac[i] = EEPROM.read(EEPROM_PEER_ADDR + i);
    if (peerMac[i] != 0xFF && peerMac[i] != 0x00) empty = false;
  }
  if (!empty) {
    Serial.printf("Peer carregado: %s\n", macToStr(peerMac).c_str());
  }
  return !empty;
}

void clearPeer() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 6; i++) {
    EEPROM.write(EEPROM_PEER_ADDR + i, 0xFF);
  }
  EEPROM.commit();
  paired = false;
  digitalWrite(LED_PIN, LOW);
  Serial.println("Pareamento limpo");
}

void sendMessage(uint8_t *mac, const char *msg) {
  // Garante que o peer está adicionado antes de enviar
  esp_now_del_peer(mac);
  int addResult = esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, CHANNEL, NULL, 0);
  
  if (addResult == 0) {
    int result = esp_now_send(mac, (uint8_t*)msg, strlen(msg));
    Serial.printf("Enviando '%s' para %s - Add: %s, Send: %s\n", 
                  msg, macToStr(mac).c_str(), 
                  addResult == 0 ? "OK" : "ERRO",
                  result == 0 ? "OK" : "ERRO");
  } else {
    Serial.printf("ERRO ao adicionar peer %s: %d\n", macToStr(mac).c_str(), addResult);
  }
}

void onDataSent(uint8_t *mac, uint8_t status) {
  Serial.printf("Confirmacao de envio para %s - Status: %s\n", 
                macToStr(mac).c_str(), status == 0 ? "SUCESSO" : "FALHA");
}

void onDataRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len != sizeof(esp_now_message)) {
    Serial.printf("Tamanho inválido (%d bytes), esperado %d\n", len, sizeof(esp_now_message));
    return;
  }

  esp_now_message msg;
  memcpy(&msg, data, sizeof(msg));

  Serial.printf("=== COMANDO RECEBIDO ===\n");
  Serial.printf("De: %s\n", macToStr(mac).c_str());
  Serial.printf("deviceId=%d, command=%d, relayState=%d, msg=%s\n",
                msg.deviceId, msg.command, msg.relayState, msg.message);

  // Se não for pra mim, ignora
  if (msg.deviceId != MEU_ID) {
    Serial.println("Mensagem não é para mim, ignorando...");
    return;
  }

  // --- PAREAMENTO ---
  if (msg.command == 0 && strcmp(msg.message, "HELLO") == 0) {
    if (millis() - lastHelloResponse < HELLO_RESPONSE_INTERVAL) {
      Serial.println("HELLO ignorado (rate limit)");
      return;
    }
    lastHelloResponse = millis();

    if (!paired) {
      memcpy(peerMac, mac, 6);
      savePeer(mac);
      paired = true;
      digitalWrite(LED_PIN, HIGH);
      Serial.printf("PAREAMENTO INICIAL com %s\n", macToStr(mac).c_str());
    } else {
      Serial.printf("HELLO de peer conhecido: %s\n", macToStr(mac).c_str());
    }

    // Responde com READY
    delay(50);
    esp_now_message resp;
    resp.deviceId = MEU_ID;
    resp.command = 0;
    resp.relayState = digitalRead(RELAY_PIN);
    strcpy(resp.message, "READY");
    esp_now_send(mac, (uint8_t*)&resp, sizeof(resp));
    return;
  }

  // --- COMANDO ABRIR ---
  if (msg.command == 1) {
    Serial.println("=== PROCESSANDO COMANDO ABRIR ===");

    if (!paired) {
      memcpy(peerMac, mac, 6);
      savePeer(mac);
      paired = true;
      digitalWrite(LED_PIN, HIGH);
      Serial.printf("AUTO-PAREAMENTO com %s\n", macToStr(mac).c_str());
    }

    // Aciona o relé
    Serial.println("ACIONANDO RELÉ...");
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? HIGH : LOW);
    delay(RELAY_PULSE_TIME);
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    Serial.println("RELÉ DESLIGADO");

    // Pisca LED
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, LOW);
      delay(100);
      digitalWrite(LED_PIN, HIGH);
      delay(100);
    }

    // Responde com ACK
    delay(ACK_DELAY);
    esp_now_message ack;
    ack.deviceId = MEU_ID;
    ack.command = 0;
    ack.relayState = digitalRead(RELAY_PIN);
    strcpy(ack.message, "ACK");
    esp_now_send(mac, (uint8_t*)&ack, sizeof(ack));

    Serial.println("=== COMANDO ABRIR FINALIZADO ===");
    return;
  }

  Serial.printf("Comando não reconhecido (cmd=%d, msg=%s)\n", msg.command, msg.message);
}


void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("===========================================");
  Serial.println("ESP-01 PORTÃO - Sistema de Controle");
  Serial.println("===========================================");

  // Configuração dos pinos
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  // Estado inicial dos pinos
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH); // Relé desligado
  digitalWrite(LED_PIN, LOW); // LED apagado inicialmente
  
  Serial.printf("Pino do Relé: GPIO%d (físico: %s)\n", RELAY_PIN, RELAY_PIN == 0 ? "5" : "desconhecido");
  Serial.printf("Pino do LED: GPIO%d (físico: %s)\n", LED_PIN, LED_PIN == 2 ? "3" : "desconhecido");
  Serial.printf("Estado inicial do relé: %s\n", RELAY_ACTIVE_HIGH ? "LOW (desligado)" : "HIGH (desligado)");

  // WiFi em modo STA (necessário para ESP-NOW)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  // Força o canal do WiFi
  wifi_set_channel(CHANNEL);

  uint8_t myMac[6];
  wifi_get_macaddr(STATION_IF, myMac);
  Serial.printf("MAC do Portão: %s\n", macToStr(myMac).c_str());
  Serial.printf("Canal forçado: %d\n", CHANNEL);

  // Inicializa ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("ERRO CRÍTICO: Falha ao inicializar ESP-NOW!");
    while(true) {
      Serial.println("Sistema em loop de erro - Reinicie o ESP");
      delay(5000);
    }
  }
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  // Carrega pareamento salvo da EEPROM
  if (loadPeer()) {
    paired = true;
    digitalWrite(LED_PIN, HIGH);
    Serial.printf("SISTEMA RESTAURADO - Pareado com: %s\n", macToStr(peerMac).c_str());
    Serial.println("Aguardando comandos...");
  } else {
    Serial.println("NENHUM PAREAMENTO - Aguardando descoberta via HELLO...");
  }

  Serial.println("===========================================");
  Serial.println("SISTEMA DO PORTÃO INICIADO E PRONTO!");
  Serial.printf("Configuração do Relé: %s\n", 
                RELAY_ACTIVE_HIGH ? "ATIVO_ALTO" : "ATIVO_BAIXO");
  Serial.printf("Tempo de pulso: %d ms\n", RELAY_PULSE_TIME);
  Serial.println("===========================================");
  
  // Teste inicial do relé
  Serial.println("Realizando teste do relé...");
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? HIGH : LOW);
  delay(200);
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
  Serial.println("Teste do relé concluído!");
}

void loop() {
  // Sistema baseado em eventos - aguarda comandos via ESP-NOW
  // Status do LED:
  // - LED apagado: não pareado
  // - LED aceso: pareado e pronto
  // - LED piscando: comando sendo executado
  
  yield(); // Permite que o sistema execute outras tarefas
}