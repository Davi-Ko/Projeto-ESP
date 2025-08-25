#include <ESP8266WiFi.h>
extern "C" {
  #include <espnow.h>
}
#include <EEPROM.h>

// ================= CONFIG =================
#define RELAY_ACTIVE_HIGH true
#define RELAY_PIN 0    // GPIO0 - Pino físico 5 do ESP-01
#define LED_PIN 2      // GPIO2 - Pino físico 3 do ESP-01 (LED onboard)

// *** CONFIGURE AQUI O ID ÚNICO DO SEU PORTÃO ***
#define UNIQUE_DEVICE_ID 101   // MUDE ESTE NÚMERO PARA CADA PAR (101, 102, 103, etc.)
// PORTARIA deve ter o MESMO NÚMERO!

#define CHANNEL 1
#define EEPROM_SIZE 64
#define EEPROM_PEER_ADDR 0

#define HELLO_RESPONSE_INTERVAL 500
#define ACK_DELAY 200           
#define RELAY_PULSE_TIME 1000   
#define READY_DELAY 50          
#define READY_PULSE_INTERVAL 5000  
#define COMMAND_COOLDOWN 2000     
// ==========================================

uint8_t peerMac[6];
bool paired = false;
bool relayState = false;
uint32_t lastHelloResponse = 0;
uint32_t lastReadyPulse = 0;
uint32_t lastCommandTime = 0;

typedef struct {
    uint16_t uniqueId;        // ID único do dispositivo
    uint8_t deviceType;       // 1=Portão, 2=Portaria
    uint8_t command;          // Comando
    uint8_t relayState;       // Estado do relé
    char message[28];         // Reduzido para caber o uniqueId
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
  // Salva também o ID único para validação
  EEPROM.write(EEPROM_PEER_ADDR + 6, (UNIQUE_DEVICE_ID >> 8) & 0xFF);
  EEPROM.write(EEPROM_PEER_ADDR + 7, UNIQUE_DEVICE_ID & 0xFF);
  EEPROM.commit();
  Serial.printf("Peer salvo: %s (ID: %d)\n", macToStr(mac).c_str(), UNIQUE_DEVICE_ID);
}

bool loadPeer() {
  EEPROM.begin(EEPROM_SIZE);
  bool empty = true;
  
  // Carrega MAC
  for (int i = 0; i < 6; i++) {
    peerMac[i] = EEPROM.read(EEPROM_PEER_ADDR + i);
    if (peerMac[i] != 0xFF && peerMac[i] != 0x00) empty = false;
  }
  
  // Verifica ID salvo
  uint16_t savedId = (EEPROM.read(EEPROM_PEER_ADDR + 6) << 8) | EEPROM.read(EEPROM_PEER_ADDR + 7);
  
  if (!empty && savedId == UNIQUE_DEVICE_ID) {
    Serial.printf("Peer carregado: %s (ID válido: %d)\n", macToStr(peerMac).c_str(), savedId);
    return true;
  } else if (!empty) {
    Serial.printf("Peer encontrado mas ID diferente: %d (esperado: %d) - Limpando...\n", 
                  savedId, UNIQUE_DEVICE_ID);
    clearPeer();
  }
  
  return false;
}

void clearPeer() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < 8; i++) { // Limpa MAC + ID
    EEPROM.write(EEPROM_PEER_ADDR + i, 0xFF);
  }
  EEPROM.commit();
  paired = false;
  digitalWrite(LED_PIN, LOW);
  Serial.println("Pareamento limpo");
}

void ensurePeerAdded(uint8_t *mac) {
  esp_now_del_peer(mac);
  int result = esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, CHANNEL, NULL, 0);
  Serial.printf("Peer %s %s\n", macToStr(mac).c_str(), 
                result == 0 ? "adicionado" : "ERRO ao adicionar");
}

void sendMessage(uint8_t *mac, esp_now_message *msg) {
  ensurePeerAdded(mac);
  int result = esp_now_send(mac, (uint8_t*)msg, sizeof(esp_now_message));
  Serial.printf("Enviando para %s - Resultado: %s\n", 
                macToStr(mac).c_str(), result == 0 ? "OK" : "ERRO");
}

void onDataSent(uint8_t *mac, uint8_t status) {
  Serial.printf("Confirmacao de envio para %s - Status: %s\n", 
                macToStr(mac).c_str(), status == 0 ? "SUCESSO" : "FALHA");
}

void onDataRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  Serial.printf("=== MENSAGEM RECEBIDA ===\n");
  Serial.printf("De: %s, Tamanho: %d bytes\n", macToStr(mac).c_str(), len);

  if (len != sizeof(esp_now_message)) {
    Serial.printf("Tamanho incorreto: %d (esperado: %d) - Ignorando\n", len, sizeof(esp_now_message));
    return;
  }

  esp_now_message msg;
  memcpy(&msg, data, sizeof(msg));

  Serial.printf("uniqueId=%d, deviceType=%d, command=%d, relayState=%d, msg=%s\n",
                msg.uniqueId, msg.deviceType, msg.command, msg.relayState, msg.message);

  // *** VERIFICAÇÃO DE ID ÚNICO ***
  if (msg.uniqueId != UNIQUE_DEVICE_ID) {
    Serial.printf("ID INCOMPATÍVEL: %d (esperado: %d) - IGNORANDO MENSAGEM\n", 
                  msg.uniqueId, UNIQUE_DEVICE_ID);
    return;
  }

  // Verifica se é de uma Portaria (deviceType = 2)
  if (msg.deviceType != 2) {
    Serial.printf("Tipo de dispositivo incorreto: %d (esperado: 2=Portaria) - Ignorando\n", msg.deviceType);
    return;
  }

  // --- PAREAMENTO (HELLO) ---
  if (msg.command == 0 && strcmp(msg.message, "HELLO") == 0) {
    Serial.printf("=== HELLO RECEBIDO (ID: %d) ===\n", msg.uniqueId);
    
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
      Serial.printf("PAREAMENTO INICIAL com %s (ID: %d)\n", macToStr(mac).c_str(), msg.uniqueId);
    } else {
      Serial.printf("HELLO de peer conhecido: %s (ID: %d)\n", macToStr(mac).c_str(), msg.uniqueId);
    }

    // Responde com READY
    delay(READY_DELAY);
    esp_now_message resp;
    resp.uniqueId = UNIQUE_DEVICE_ID;
    resp.deviceType = 1;  // 1 = Portão
    resp.command = 0;
    resp.relayState = digitalRead(RELAY_PIN);
    strcpy(resp.message, "READY");
    sendMessage(mac, &resp);
    
    return;
  }

  // --- COMANDO ABRIR ---
  if (msg.command == 1) {
    Serial.printf("=== PROCESSANDO COMANDO ABRIR (ID: %d) ===\n", msg.uniqueId);

    // Verifica cooldown para evitar comandos duplicados
    if (millis() - lastCommandTime < COMMAND_COOLDOWN) {
      Serial.printf("COMANDO IGNORADO - Cooldown ativo (%lu ms restantes)\n", 
                   COMMAND_COOLDOWN - (millis() - lastCommandTime));
      
      // Mesmo assim envia ACK
      esp_now_message ack;
      ack.uniqueId = UNIQUE_DEVICE_ID;
      ack.deviceType = 1;  // 1 = Portão
      ack.command = 0;
      ack.relayState = digitalRead(RELAY_PIN);
      strcpy(ack.message, "ACK");
      sendMessage(mac, &ack);
      return;
    }
    
    lastCommandTime = millis();

    if (!paired) {
      memcpy(peerMac, mac, 6);
      savePeer(mac);
      paired = true;
      digitalWrite(LED_PIN, HIGH);
      Serial.printf("AUTO-PAREAMENTO com %s (ID: %d)\n", macToStr(mac).c_str(), msg.uniqueId);
    }

    // ENVIA ACK IMEDIATAMENTE
    Serial.println("ENVIANDO ACK...");
    
    esp_now_message ack;
    ack.uniqueId = UNIQUE_DEVICE_ID;
    ack.deviceType = 1;  // 1 = Portão
    ack.command = 0;
    ack.relayState = digitalRead(RELAY_PIN);
    strcpy(ack.message, "ACK");
    
    int result = esp_now_send(mac, (uint8_t*)&ack, sizeof(ack));
    Serial.printf("ACK enviado - Resultado: %s\n", result == 0 ? "SUCESSO" : "ERRO");
    
    delay(100);

    // Aciona o relé
    Serial.println("ACIONANDO RELÉ...");
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? HIGH : LOW);
    
    // Pisca LED uma vez
    digitalWrite(LED_PIN, LOW);
    delay(200);
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
    digitalWrite(LED_PIN, HIGH);
    
    delay(RELAY_PULSE_TIME);
    digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
    Serial.println("RELÉ DESLIGADO");

    Serial.printf("=== COMANDO ABRIR FINALIZADO (ID: %d) ===\n", msg.uniqueId);
    return;
  }

  Serial.printf("Comando não reconhecido (cmd=%d, msg=%s)\n", msg.command, msg.message);
}

void sendReadyPulse() {
  if (paired && millis() - lastReadyPulse > READY_PULSE_INTERVAL) {
    lastReadyPulse = millis();
    
    Serial.printf("=== ENVIANDO READY PULSE (ID: %d) ===\n", UNIQUE_DEVICE_ID);
    
    esp_now_message resp;
    resp.uniqueId = UNIQUE_DEVICE_ID;
    resp.deviceType = 1;  // 1 = Portão
    resp.command = 0;
    resp.relayState = digitalRead(RELAY_PIN);
    strcpy(resp.message, "READY");
    sendMessage(peerMac, &resp);
    
    Serial.printf("READY pulse enviado para %s\n", macToStr(peerMac).c_str());
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("===========================================");
  Serial.printf("ESP-01 PORTÃO - ID ÚNICO: %d\n", UNIQUE_DEVICE_ID);
  Serial.println("===========================================");

  // Configuração dos pinos
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(LED_PIN, LOW);
  
  Serial.printf("ID Único do Dispositivo: %d\n", UNIQUE_DEVICE_ID);
  Serial.printf("Tipo: 1 (Portão)\n");
  Serial.printf("Pino do Relé: GPIO%d\n", RELAY_PIN);
  Serial.printf("Pino do LED: GPIO%d\n", LED_PIN);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(CHANNEL);

  uint8_t myMac[6];
  wifi_get_macaddr(STATION_IF, myMac);
  Serial.printf("MAC do Portão: %s\n", macToStr(myMac).c_str());

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

  if (loadPeer()) {
    paired = true;
    digitalWrite(LED_PIN, HIGH);
    Serial.printf("SISTEMA RESTAURADO - Pareado com: %s\n", macToStr(peerMac).c_str());
    
    // Envia READY inicial
    delay(1000);
    esp_now_message resp;
    resp.uniqueId = UNIQUE_DEVICE_ID;
    resp.deviceType = 1;
    resp.command = 0;
    resp.relayState = digitalRead(RELAY_PIN);
    strcpy(resp.message, "READY");
    sendMessage(peerMac, &resp);
    
    lastReadyPulse = millis();
    
  } else {
    Serial.printf("NENHUM PAREAMENTO - Aguardando Portaria com ID: %d\n", UNIQUE_DEVICE_ID);
  }

  Serial.println("===========================================");
  Serial.printf("PORTÃO PRONTO! Aceita apenas ID: %d\n", UNIQUE_DEVICE_ID);
  Serial.println("===========================================");
  
  // Teste do relé
  Serial.println("Teste do relé...");
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? HIGH : LOW);
  delay(100);
  digitalWrite(RELAY_PIN, RELAY_ACTIVE_HIGH ? LOW : HIGH);
  
  // Pisca LED 3 vezes
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(200);
    digitalWrite(LED_PIN, LOW);
    delay(200);
  }
  
  if (paired) {
    digitalWrite(LED_PIN, HIGH);
  }
}

void loop() {
  sendReadyPulse();
  yield();
  delay(100);
}