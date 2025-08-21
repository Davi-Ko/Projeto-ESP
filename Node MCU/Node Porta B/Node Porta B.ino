#include <ESP8266WiFi.h>
#include <espnow.h>

#define RELAY_PIN D1
#define FACIAL_PIN D2
#define SENSOR_PIN D3

#define NODE_ID 2  

typedef struct struct_message {
  int sender;
  int command;
} struct_message;

struct_message incomingMsg;
struct_message outgoingMsg;

uint8_t porta1MAC[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t centralMAC[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

bool doorLocked = true;
unsigned long openTimer = 0;

void onDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  memcpy(&incomingMsg, incomingData, sizeof(incomingMsg));

  if(incomingMsg.command == 1 && doorLocked) {
    if(digitalRead(SENSOR_PIN) == HIGH) {
      digitalWrite(RELAY_PIN, LOW);
      doorLocked = false;
      openTimer = millis();
    }
  }

  if(incomingMsg.command == 100 + NODE_ID) {
    memcpy(porta1MAC, mac, 6);
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
  digitalWrite(RELAY_PIN, HIGH);
  pinMode(FACIAL_PIN, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != 0) {
    Serial.println("Erro ao iniciar ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);
  esp_now_add_peer(centralMAC, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
}

void loop() {
  if(digitalRead(FACIAL_PIN) == LOW && doorLocked) {
    sendMessage(centralMAC, 20);  
    delay(500);
  }

  if(!doorLocked && millis() - openTimer > 5000) {
    digitalWrite(RELAY_PIN, HIGH);
    doorLocked = true;
  }
}
