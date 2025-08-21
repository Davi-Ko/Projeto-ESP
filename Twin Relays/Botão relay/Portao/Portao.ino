#include <ESP8266WiFi.h>
extern "C" {
  #include <espnow.h>
}
#include <EEPROM.h>

// ================= CONFIG =================
#define RELAY_ACTIVE_HIGH true
#define RELAY_PIN 0    // GPIO0 - Pino físico 5 do ESP-01
#define LED_PIN 2      // GPIO2 - Pino físico 3 do ESP-01 (LED onboard)
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
  char msg[32] = {0};
  if (len > sizeof(msg) - 1) len = sizeof(msg) - 1;
  memcpy(msg, data, len);
  msg[len] = '\0';

  Serial.printf("=== COMANDO RECEBIDO ===\n");
  Serial.printf("Mensagem: '%s'\n", msg);
  Serial.printf("De: %s\n", macToStr(mac).c_str());
  Serial.printf("Tamanho: %d bytes\n", len);

  // Resposta ao HELLO para pareamento
  if (strcmp(msg, "HELLO") == 0) {
    // Limita a frequência de respostas para evitar spam
    if (millis() - lastHelloResponse < HELLO_RESPONSE_INTERVAL) {
      Serial.println("HELLO ignorado (rate limit)");
      return;
    }
    lastHelloResponse = millis();

    Serial.println("Processando HELLO...");
    
    if (!paired) {
      // Primeiro pareamento
      memcpy(peerMac, mac, 6);
      savePeer(mac);
      paired = true;
      digitalWrite(LED_PIN, HIGH);
      Serial.printf("PAREAMENTO INICIAL com %s\n", macToStr(mac).c_str());
    } else {
      Serial.printf("HELLO de peer conhecido: %s\n", macToStr(mac).c_str());
    }
    
    // Responde com READY
    delay(50); // Pequeno delay para estabilidade
    sendMessage(mac, "READY");
    return;
  }

  // Comando para abrir o portão
  if (strcmp(msg, "ABRIR") == 0) {
    Serial.println("=== PROCESSANDO COMANDO ABRIR ===");
    
    // Se não pareado ainda, faz pareamento automático
    if (!paired) {
      memcpy(peerMac, mac, 6);
      savePeer(mac);
      paired = true;
      digitalWrite(LED_PIN, HIGH);
      Serial.printf("AUTO-PAREAMENTO com %s\n", macToStr(mac).c_str());
    }

    // Aciona o relé com pulso
    Serial.println("ACIONANDO RELÉ...");
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? HIGH : LOW);
    Serial.printf("RELÉ LIGADO (pino %d = %s)\n", RELAY_PIN, RELAY_ACTIVE_HIGH ? "HIGH" : "LOW");
    
    // Mantém o relé acionado pelo tempo definido
    delay(RELAY_PULSE_TIME);
    
    // Desliga o relé
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    Serial.printf("RELÉ DESLIGADO (pino %d = %s)\n", RELAY_PIN, RELAY_ACTIVE_HIGH ? "LOW" : "HIGH");
    
    // Pisca o LED para indicar acionamento
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, LOW);
      delay(100);
      digitalWrite(LED_PIN, HIGH);
      delay(100);
    }
    
    // Delay antes de enviar ACK para garantir estabilidade
    delay(ACK_DELAY);
    
    // Envia ACK
    Serial.println("Enviando ACK...");
    sendMessage(mac, "ACK");
    
    Serial.println("=== COMANDO ABRIR FINALIZADO ===");
    return;
  }

  Serial.printf("Comando não reconhecido: '%s'\n", msg);
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