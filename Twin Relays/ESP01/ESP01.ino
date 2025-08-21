#include <ESP8266WiFi.h>
#include <espnow.h>
#include <EEPROM.h>

#define RELAY_PIN 0
#define LED_PIN 2
#define DEVICE_ID 2  // Mude para 2 no segundo ESP01

// Configuração dos twin relays
#define TWIN_DEVICE_ID 1  // ID do dispositivo gêmeo (mude para 1 no segundo ESP01)
#define SYNC_MODE_TOGGLE 0    // Modo: quando um muda, o outro inverte
#define SYNC_MODE_MIRROR 1    // Modo: quando um muda, o outro copia o mesmo estado
#define CURRENT_SYNC_MODE SYNC_MODE_TOGGLE  // Altere aqui o modo desejado

typedef struct {
    uint8_t deviceId;
    uint8_t command;
    uint8_t relayState;
    uint8_t syncMode;
    bool isAutoSync;  // Flag para identificar sincronização automática
    char message[32];
} esp_now_message;

bool relayState = false;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t twinMacAddress[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // MAC do twin (será descoberto)
bool twinDiscovered = false;
unsigned long lastHeartbeat = 0;
unsigned long lastSyncAttempt = 0;
const unsigned long HEARTBEAT_INTERVAL = 30000;
const unsigned long SYNC_RETRY_INTERVAL = 5000;

void setup() {
    Serial.begin(115200);

    pinMode(RELAY_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(LED_PIN, LOW);

    EEPROM.begin(512);
    relayState = EEPROM.read(0);
    digitalWrite(RELAY_PIN, relayState);
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != 0) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    
    esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);

    Serial.println("Twin Relay ESP01 initialized successfully");
    Serial.print("Device ID: ");
    Serial.println(DEVICE_ID);
    Serial.print("Twin Device ID: ");
    Serial.println(TWIN_DEVICE_ID);
    Serial.print("Sync Mode: ");
    Serial.println(CURRENT_SYNC_MODE == SYNC_MODE_TOGGLE ? "TOGGLE" : "MIRROR");

    // Sinalização de inicialização
    for(int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(200);
        digitalWrite(LED_PIN, LOW);
        delay(200);
    }

    // Tentar descobrir o twin
    discoverTwin();
}

void loop() {
    // Heartbeat periódico
    if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
        sendHeartbeat();
        lastHeartbeat = millis();
    }

    // Tentar redescobrir o twin se não foi encontrado
    if (!twinDiscovered && millis() - lastSyncAttempt > SYNC_RETRY_INTERVAL) {
        discoverTwin();
        lastSyncAttempt = millis();
    }

    delay(100);
}

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
    Serial.print("Sending status: ");
    Serial.println(sendStatus == 0 ? "Success" : "Failed");
}

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
    esp_now_message message;
    memcpy(&message, incomingData, sizeof(message));

    Serial.print("Received from: ");
    printMacAddress(mac);
    Serial.print(" Device ID: ");
    Serial.print(message.deviceId);
    Serial.print(", Command: ");
    Serial.print(message.command);
    Serial.print(", AutoSync: ");
    Serial.println(message.isAutoSync ? "YES" : "NO");

    // Processar descoberta do twin
    if (message.deviceId == TWIN_DEVICE_ID && !twinDiscovered) {
        memcpy(twinMacAddress, mac, 6);
        twinDiscovered = true;
        Serial.println("Twin discovered!");
        printMacAddress(twinMacAddress);
        
        // Adicionar o twin como peer específico
        esp_now_add_peer(twinMacAddress, ESP_NOW_ROLE_COMBO, 1, NULL, 0);
    }

    // Processar comandos
    if (message.deviceId == DEVICE_ID || message.deviceId == 0) {
        processCommand(message.command, mac, message.isAutoSync);
    }
    
    // Processar sincronização automática do twin
    if (message.deviceId == TWIN_DEVICE_ID && message.isAutoSync) {
        processTwinSync(message);
    }
}

void processCommand(uint8_t command, uint8_t* senderMac, bool isAutoSync) {
    esp_now_message response;
    response.deviceId = DEVICE_ID;
    response.relayState = relayState;
    response.isAutoSync = false;
    response.syncMode = CURRENT_SYNC_MODE;
    strcpy(response.message, "TWIN_RELAY");

    bool stateChanged = false;
    bool shouldSyncTwin = false;

    switch (command) {
        case 0: // OFF
            if (relayState != false) {
                relayState = false;
                stateChanged = true;
                shouldSyncTwin = !isAutoSync; // Só sincroniza se não for auto-sync
            }
            digitalWrite(RELAY_PIN, LOW);
            digitalWrite(LED_PIN, HIGH);
            saveRelayState();
            response.command = 10;
            Serial.println("Relay OFF");
            break;
        
        case 1: // ON
            if (relayState != true) {
                relayState = true;
                stateChanged = true;
                shouldSyncTwin = !isAutoSync;
            }
            digitalWrite(RELAY_PIN, HIGH);
            digitalWrite(LED_PIN, LOW);
            saveRelayState();
            response.command = 10;
            Serial.println("Relay ON");
            break;
        
        case 2: // TOGGLE
            relayState = !relayState;
            stateChanged = true;
            shouldSyncTwin = !isAutoSync;
            digitalWrite(RELAY_PIN, relayState);
            digitalWrite(LED_PIN, !relayState);
            saveRelayState();
            response.command = 10;
            Serial.println("Relay TOGGLE");
            break;

        case 3: // STATUS
            response.command = 10;
            Serial.println("Status request received");
            break;
        
        case 4: // DISCOVERY
            response.command = 11;
            Serial.println("Discovery request received");
            break;

        default:
            return;
    }
    
    response.relayState = relayState;
    esp_now_send(senderMac, (uint8_t *)&response, sizeof(response));

    // Sincronizar com o twin se o estado mudou e não foi auto-sync
    if (stateChanged && shouldSyncTwin && twinDiscovered) {
        syncWithTwin();
    }
}

void processTwinSync(esp_now_message twinMessage) {
    Serial.print("Processing twin sync. Twin state: ");
    Serial.print(twinMessage.relayState);
    Serial.print(", Sync mode: ");
    Serial.println(twinMessage.syncMode);

    bool newState = relayState;
    
    if (twinMessage.syncMode == SYNC_MODE_MIRROR) {
        // Modo espelho: copia o estado do twin
        newState = twinMessage.relayState;
        Serial.println("Mirror mode: copying twin state");
    } else if (twinMessage.syncMode == SYNC_MODE_TOGGLE) {
        // Modo toggle: inverte o estado atual
        newState = !relayState;
        Serial.println("Toggle mode: inverting current state");
    }

    if (newState != relayState) {
        relayState = newState;
        digitalWrite(RELAY_PIN, relayState);
        digitalWrite(LED_PIN, !relayState);
        saveRelayState();
        
        Serial.print("Twin sync completed. New state: ");
        Serial.println(relayState ? "ON" : "OFF");
        
        // Piscar LED para indicar sincronização
        for(int i = 0; i < 2; i++) {
            digitalWrite(LED_PIN, HIGH);
            delay(100);
            digitalWrite(LED_PIN, !relayState);
            delay(100);
        }
    }
}

void syncWithTwin() {
    if (!twinDiscovered) {
        Serial.println("Twin not discovered, cannot sync");
        return;
    }

    esp_now_message syncMessage;
    syncMessage.deviceId = DEVICE_ID;
    syncMessage.command = 13; // Comando de sincronização
    syncMessage.relayState = relayState;
    syncMessage.syncMode = CURRENT_SYNC_MODE;
    syncMessage.isAutoSync = true;
    strcpy(syncMessage.message, "TWIN_SYNC");

    esp_now_send(twinMacAddress, (uint8_t *)&syncMessage, sizeof(syncMessage));
    Serial.println("Sync message sent to twin");
}

void discoverTwin() {
    esp_now_message message;
    message.deviceId = DEVICE_ID;
    message.command = 14; // Comando de descoberta de twin
    message.relayState = relayState;
    message.syncMode = CURRENT_SYNC_MODE;
    message.isAutoSync = false;
    strcpy(message.message, "TWIN_DISCOVERY");

    esp_now_send(broadcastAddress, (uint8_t *)&message, sizeof(message));
    Serial.println("Twin discovery message sent");
}

void sendHeartbeat() {
    esp_now_message message;
    message.deviceId = DEVICE_ID;
    message.command = 12;
    message.relayState = relayState;
    message.syncMode = CURRENT_SYNC_MODE;
    message.isAutoSync = false;
    strcpy(message.message, "TWIN_HEARTBEAT");

    esp_now_send(broadcastAddress, (uint8_t *) &message, sizeof(message));
    Serial.println("Heartbeat sent");
}

void saveRelayState() {
    EEPROM.write(0, relayState);
    EEPROM.commit();
}

void printMacAddress(uint8_t *mac) {
    for (int i = 0; i < 6; i++){
        Serial.print(mac[i], HEX);
        if (i < 5) {
            Serial.print(":");
        }
    }
    Serial.println();
}