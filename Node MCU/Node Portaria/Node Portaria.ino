#include <ESP8266WiFi.h>
#include <espnow.h>

#define BTN_PORTA1 D5
#define BTN_PORTA2 D6

typedef struct struct_message {
  int sender;
  int command;
} struct_message;

struct_message incomingMsg;
struct_message outgoingMsg;

uint8_t porta1MAC[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t porta2MAC[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

void onDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  memcpy(&incomingMsg, incomingData, sizeof(incomingMsg));

  if(incomingMsg.command == 10) {
    esp_now_send(porta1MAC, (uint8_t *)&incomingMsg, sizeof(incomingMsg));
  }
  if(incomingMsg.command == 20) {
    esp_now_send(porta2MAC, (uint8_t *)&incomingMsg, sizeof(incomingMsg));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PORTA1, INPUT_PULLUP);
  pinMode(BTN_PORTA2, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != 0) {
    Serial.println("Erro ao iniciar ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);
}

void loop() {
  if(digitalRead(BTN_PORTA1) == LOW) {
    outgoingMsg.sender = 0;
    outgoingMsg.command = 1; 
    esp_now_send(porta1MAC, (uint8_t *)&outgoingMsg, sizeof(outgoingMsg));
    delay(500);
  }

  if(digitalRead(BTN_PORTA2) == LOW) {
    outgoingMsg.sender = 0;
    outgoingMsg.command = 1;
    esp_now_send(porta2MAC, (uint8_t *)&outgoingMsg, sizeof(outgoingMsg));
    delay(500);
  }
}
