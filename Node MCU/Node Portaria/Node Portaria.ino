#include <ESP8266WiFi.h>
#include <espnow.h>

#define RELAY_PIN D1
#define FACIAL_PIN D2
#define SENSOR_PIN D3

// Identificador único do nó
#define NODE_ID 1  

// Estrutura de mensagem
typedef struct struct_message {
  int sender;
  int command;  
} struct_message;

struct_message incomingMsg;
struct_message outgoingMsg;

uint8_t porta2MAC[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}; // será atualizado automaticamente
uint8_t centralMAC[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

bool doorLocked = true;
unsigned long openTimer = 0;

void onDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  memcpy(&incomingMsg, incomingData, sizeof(incomingMsg));

  // Central pediu abertura
  if(incomingMsg.command == 1 && doorLocked) {
    // só abre se Porta 2 estiver fechada
    if(digitalRead(SENSOR_PIN) == HIGH) {
      digitalWrite(RELAY_PIN, LOW);  
      doorLocked = false;
      openTimer = millis();
    }
  }

  // Central enviou atualização de MAC
  if(incomingMsg.command == 100 + NODE_ID) {
    memcpy(porta2MAC, mac, 6);
  }
}

void sendMessage(uint8_t *target, int cmd) {
  outgoingMsg.sender = NODE_ID;
  outgoingMsg.command = cmd;
  esp_now_send(target, (uint8_t *) &outgoingMsg, sizeof(outgoingMsg));
}

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Relay desligado
  pinMode(FACIAL_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != 0) {
    Serial.println("Erro ao iniciar ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  // envia broadcast para central
  esp_now_add_peer(centralMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
}

void loop() {
  // Se facial acionado
  if(digitalRead(FACIAL_PIN) == LOW && doorLocked) {
    sendMessage(centralMAC, 10);  // pede abertura via central
    delay(500);
  }

  // Fecha porta automaticamente após 5s
  if(!doorLocked && millis() - openTimer > 5000) {
    digitalWrite(RELAY_PIN, HIGH);
    doorLocked = true;
  }
}
