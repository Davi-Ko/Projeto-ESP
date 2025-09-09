// ===================== PORTARIA (ESP8266/ESP-01) =====================
// Sistema com ID único para pareamento específico
// =====================================================================

#include <ESP8266WiFi.h>
extern "C" {
  #include <espnow.h>
}
#include <EEPROM.h>
#include <ESP8266WebServer.h>

// ===================== CONFIG =====================
#define AP_SSID            "Portaria-ESP"
#define AP_PASS            "12345678"
#define AP_CHANNEL         1
#define AP_MAX_CONN        2

// *** CONFIGURE AQUI O MESMO ID DO PORTÃO ***
#define UNIQUE_DEVICE_ID 101   // DEVE SER IGUAL AO DO PORTÃO!

#define HELLO_INTERVAL_MS  3000           
#define ACK_TIMEOUT_MS     2000           
#define MAX_RETRY_ATTEMPTS 2              
#define COMMAND_DELAY      500            
#define HELLO_TIMEOUT      20000          
#define RECONNECT_INTERVAL 30000          

typedef struct {
    uint16_t uniqueId;        // ID único do dispositivo
    uint8_t deviceType;       // 1=Portão, 2=Portaria
    uint8_t command;          // Comando
    uint8_t relayState;       // Estado do relé
    char message[28];         // Reduzido para caber o uniqueId
} esp_now_message;

// ============== Estruturas de Pareamento =============
struct PairStore {
  uint32_t magic;
  uint8_t  peer[6];
  uint8_t  channel;
  uint8_t  flags;
  uint32_t timestamp;
  uint16_t uniqueId;        // ID único salvo
  uint8_t  reserved[14];    // Reduzido para caber uniqueId
};

#define EEPROM_SIZE  64
#define MAGIC_WORD   0xABCD1234

// =================== Estado Global ===================
PairStore gPair{};
bool gPaired = false;
volatile bool gDiscoveryMode = true;
volatile bool gReadyReceived = false;

ESP8266WebServer server(80);
uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t myMac[6]{};
volatile bool gAckReceived = false;
uint32_t lastHello = 0;
uint32_t lastReconnectAttempt = 0;
volatile uint32_t lastReadyReceived = 0;

// =================== Interface Web ===================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Controle do Portao - Portaria</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh; display: flex; align-items: center;
            justify-content: center; padding: 20px;
        }
        .container {
            background: rgba(255, 255, 255, 0.95);
            backdrop-filter: blur(10px); border-radius: 20px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
            padding: 40px; max-width: 500px; width: 100%;
            text-align: center; border: 1px solid rgba(255, 255, 255, 0.2);
        }
        .title {
            font-size: 2.5em; font-weight: 700; color: #333;
            margin-bottom: 10px; text-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
        }
        .subtitle { color: #666; font-size: 1.1em; font-weight: 300; }
        .device-id {
            background: #e3f2fd; border: 2px solid #2196F3; border-radius: 10px;
            padding: 10px; margin: 15px 0; font-weight: bold; color: #1976D2;
        }
        .status-card {
            background: #f8f9ff; border-radius: 15px; padding: 25px;
            margin: 30px 0; border-left: 5px solid #667eea;
        }
        .status-item {
            display: flex; justify-content: space-between;
            align-items: center; margin: 15px 0; padding: 10px 0;
            border-bottom: 1px solid rgba(0, 0, 0, 0.1);
        }
        .status-item:last-child { border-bottom: none; }
        .status-label { font-weight: 600; color: #555; }
        .status-value {
            font-family: 'Courier New', monospace; font-size: 0.9em;
            color: #333; background: rgba(255, 255, 255, 0.8);
            padding: 5px 10px; border-radius: 8px;
        }
        .connection-status {
            display: inline-block; padding: 6px 15px; border-radius: 20px;
            font-size: 0.85em; font-weight: 600; text-transform: uppercase;
        }
        .connected {
            background: linear-gradient(135deg, #4CAF50, #45a049); color: white;
        }
        .disconnected {
            background: linear-gradient(135deg, #f44336, #d32f2f); color: white;
        }
        .discovering {
            background: linear-gradient(135deg, #ff9800, #f57c00); color: white;
        }
        .main-button {
            background: linear-gradient(135deg, #667eea, #764ba2);
            border: none; border-radius: 50px; padding: 20px 40px;
            font-size: 1.4em; font-weight: 700; color: white;
            cursor: pointer; transition: all 0.3s ease;
            box-shadow: 0 8px 25px rgba(102, 126, 234, 0.3);
            margin: 20px 0; width: 100%; text-transform: uppercase;
            letter-spacing: 1px;
        }
        .main-button:hover {
            transform: translateY(-3px);
            box-shadow: 0 12px 35px rgba(102, 126, 234, 0.4);
            background: linear-gradient(135deg, #764ba2, #667eea);
        }
        .main-button:disabled {
            background: #cccccc; cursor: not-allowed;
            transform: none; box-shadow: none;
        }
        .response-area {
            background: #f8f9ff; border-radius: 12px; padding: 15px;
            margin: 20px 0; border-left: 4px solid #667eea;
            font-weight: 500; color: #555;
        }
        .success {
            border-left-color: #4CAF50; background: #e8f5e8; color: #2e7d2e;
        }
        .error {
            border-left-color: #f44336; background: #ffeaea; color: #c62828;
        }
        .footer {
            margin-top: 30px; padding-top: 20px;
            border-top: 1px solid rgba(0, 0, 0, 0.1);
        }
        .footer-link {
            color: #667eea; text-decoration: none; font-weight: 600;
            transition: color 0.3s ease; margin: 0 10px;
        }
        .footer-link:hover { color: #764ba2; text-decoration: underline; }
        .loading {
            display: inline-block; width: 20px; height: 20px;
            border: 3px solid rgba(255, 255, 255, 0.3); border-radius: 50%;
            border-top-color: #fff; animation: spin 1s ease-in-out infinite;
        }
        .pulse { animation: pulse 2s infinite; }
        @keyframes spin { to { transform: rotate(360deg); } }
        @keyframes pulse {
            0% { box-shadow: 0 0 0 0 rgba(102, 126, 234, 0.7); }
            70% { box-shadow: 0 0 0 10px rgba(102, 126, 234, 0); }
            100% { box-shadow: 0 0 0 0 rgba(102, 126, 234, 0); }
        }
        @media (max-width: 600px) {
            .container { padding: 30px 20px; margin: 10px; }
            .title { font-size: 2em; }
            .main-button { padding: 18px 30px; font-size: 1.2em; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1 class="title">🏠 Portaria</h1>
            <p class="subtitle">Controle do Portao Eletronico</p>
            <div class="device-id">ID Único: )" + String(UNIQUE_DEVICE_ID) + R"rawliteral(</div>
        </div>
        <div class="status-card">
            <div class="status-item">
                <span class="status-label">MAC Portaria:</span>
                <span class="status-value" id="myMac">Carregando...</span>
            </div>
            <div class="status-item">
                <span class="status-label">Status:</span>
                <span class="connection-status disconnected" id="connectionStatus">Desconectado</span>
            </div>
            <div class="status-item">
                <span class="status-label">MAC Portao:</span>
                <span class="status-value" id="peerMac">Nao pareado</span>
            </div>
            <div class="status-item">
                <span class="status-label">Ultimo READY:</span>
                <span class="status-value" id="lastReady">Nunca</span>
            </div>
        </div>
        <button class="main-button" id="openBtn" onclick="openGate()">
            <span id="btnText">🚪 Abrir Portao</span>
        </button>
        <div class="response-area" id="response">Sistema pronto - Procurando Portao ID )" + String(UNIQUE_DEVICE_ID) + R"rawliteral(</div>
        <div class="footer">
            <a href="/unpair" class="footer-link">🔓 Desemparelhar</a>
            <a href="/reconnect" class="footer-link">🔄 Reconectar</a>
            <a href="/info" class="footer-link">ℹ️ Info</a>
        </div>
    </div>
    <script>
        var isConnected = false;
        function loadStatus() {
            var xhr = new XMLHttpRequest();
            xhr.open('GET', '/status', true);
            xhr.onreadystatechange = function() {
                if (xhr.readyState == 4 && xhr.status == 200) {
                    try {
                        var data = JSON.parse(xhr.responseText);
                        document.getElementById('myMac').textContent = data.my || 'Erro';
                        var statusEl = document.getElementById('connectionStatus');
                        
                        var lastReadyEl = document.getElementById('lastReady');
                        if (data.lastReady && data.lastReady > 0) {
                            var readyAge = Math.floor((data.uptime - data.lastReady/1000));
                            lastReadyEl.textContent = readyAge + 's atrás';
                        } else {
                            lastReadyEl.textContent = 'Nunca';
                        }
                        
                        if (data.peer && data.peer.trim() !== '') {
                            document.getElementById('peerMac').textContent = data.peer;
                            
                            var timeSinceReady = data.uptime*1000 - (data.lastReady || 0);
                            if (timeSinceReady < 15000) {
                                statusEl.textContent = 'Conectado';
                                statusEl.className = 'connection-status connected';
                                isConnected = true;
                            } else {
                                statusEl.textContent = 'Timeout';
                                statusEl.className = 'connection-status disconnected';
                                isConnected = false;
                            }
                        } else if (data.discovery) {
                            document.getElementById('peerMac').textContent = 'Procurando...';
                            statusEl.textContent = 'Descobrindo';
                            statusEl.className = 'connection-status discovering';
                            isConnected = false;
                        } else {
                            document.getElementById('peerMac').textContent = 'Aguardando...';
                            statusEl.textContent = 'Desconectado';
                            statusEl.className = 'connection-status disconnected';
                            isConnected = false;
                        }
                        document.getElementById('openBtn').disabled = !isConnected;
                    } catch (e) { setResponse('Erro JSON', 'error'); }
                }
            };
            xhr.send();
        }
        function openGate() {
            if (!isConnected) { 
                setResponse('Erro: Portao nao pareado', 'error'); 
                return; 
            }
            
            var btn = document.getElementById('openBtn');
            var btnText = document.getElementById('btnText');
            var originalText = btnText.innerHTML;

            btn.disabled = true; 
            btn.classList.add('pulse');
            btnText.innerHTML = '<span class="loading"></span> Acionando...';

            // Envia comando ABRIR
            var xhrOpen = new XMLHttpRequest();
            xhrOpen.open('GET', '/open', true);
            xhrOpen.onreadystatechange = function() {
                if (xhrOpen.readyState == 4) {
                    setResponse((xhrOpen.status == 200 ? '✅ ' : '❌ ') + xhrOpen.responseText,
                                xhrOpen.status == 200 ? 'success' : 'error');

                    // Aguarda 5 segundos antes de enviar FECHAR
                    setTimeout(function() {
                        var xhrClose = new XMLHttpRequest();
                        xhrClose.open('GET', '/close', true);
                        xhrClose.onreadystatechange = function() {
                            if (xhrClose.readyState == 4) {
                                setResponse((xhrClose.status == 200 ? '✅ ' : '❌ ') + xhrClose.responseText,
                                            xhrClose.status == 200 ? 'success' : 'error');

                                // Reativa botão
                                btn.disabled = !isConnected; 
                                btn.classList.remove('pulse');
                                btnText.innerHTML = originalText;
                            }
                        };
                        xhrClose.send();
                    }, 5000);
                }
            };
            xhrOpen.send();
        }
        function setResponse(message, type) {
            var el = document.getElementById('response');
            el.textContent = message; el.className = 'response-area ' + (type || '');
        }
        window.onload = function() { loadStatus(); setInterval(loadStatus, 2000); };
        document.addEventListener('keydown', function(e) {
            if (e.code === 'Space' || e.key === 'Enter') {
                e.preventDefault(); openGate();
            }
        });
    </script>
</body>
</html>
)rawliteral";

// =================== Utilitários ===================
String macToStr(const uint8_t mac[6]) {
  char buf[20];
  sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  return String(buf);
}

void savePairToEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  gPair.magic = MAGIC_WORD;
  gPair.timestamp = millis();
  gPair.uniqueId = UNIQUE_DEVICE_ID;  // Salva o ID único
  EEPROM.put(0, gPair);
  EEPROM.commit();
  Serial.printf("Pareamento salvo na EEPROM (ID: %d)\n", gPair.uniqueId);
}

bool loadPairFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, gPair);
  if (gPair.magic == MAGIC_WORD && gPair.channel >= 1 && gPair.channel <= 13) {
    // Verifica se o ID salvo corresponde ao atual
    if (gPair.uniqueId == UNIQUE_DEVICE_ID) {
      Serial.printf("Pareamento carregado - Canal: %d, ID: %d\n", gPair.channel, gPair.uniqueId);
      return true;
    } else {
      Serial.printf("ID diferente encontrado: %d (esperado: %d) - Limpando pareamento\n", 
                    gPair.uniqueId, UNIQUE_DEVICE_ID);
    }
  }
  memset(&gPair, 0, sizeof(gPair));
  return false;
}

void clearPairing() {
  Serial.println("Limpando pareamento...");
  esp_now_del_peer(gPair.peer);
  memset(&gPair, 0, sizeof(gPair));
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, gPair);
  EEPROM.commit();
  gPaired = false;
  gDiscoveryMode = true;
  gReadyReceived = false;
  lastReadyReceived = 0;
}

void ensurePeerExists(uint8_t *mac) {
  esp_now_del_peer(mac);
  int result = esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, gPair.channel, NULL, 0);
  Serial.printf("Peer %s %s\n", macToStr(mac).c_str(), 
                result == 0 ? "adicionado" : "ERRO ao adicionar");
}

// ================== ESPNOW Callbacks ===============
void onDataSent(uint8_t *mac, uint8_t status) {
  Serial.printf("Confirmacao para %s - Status: %s\n", 
                macToStr(mac).c_str(), status == 0 ? "SUCESSO" : "FALHA");
}

void onDataRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  if (len != sizeof(esp_now_message)) {
    Serial.printf("Tamanho incorreto: %d (esperado: %d) - Ignorando\n", len, sizeof(esp_now_message));
    return;
  }

  esp_now_message msg;
  memcpy(&msg, data, sizeof(msg));

  Serial.printf("=== MENSAGEM RECEBIDA ===\n");
  Serial.printf("De: %s\n", macToStr(mac).c_str());
  Serial.printf("uniqueId=%d, deviceType=%d, cmd=%d, estado=%d, msg=%s\n",
                msg.uniqueId, msg.deviceType, msg.command, msg.relayState, msg.message);

  // *** VERIFICAÇÃO DE ID ÚNICO ***
  if (msg.uniqueId != UNIQUE_DEVICE_ID) {
    Serial.printf("ID INCOMPATÍVEL: %d (esperado: %d) - IGNORANDO MENSAGEM\n", 
                  msg.uniqueId, UNIQUE_DEVICE_ID);
    return;
  }

  // Verifica se é de um Portão (deviceType = 1)
  if (msg.deviceType != 1) {
    Serial.printf("Tipo de dispositivo incorreto: %d (esperado: 1=Portão) - Ignorando\n", msg.deviceType);
    return;
  }

  // Resposta do portão durante pareamento
  if (!gPaired || gDiscoveryMode) {
    if (strcmp(msg.message, "READY") == 0) {
      Serial.printf("READY (ID: %d) recebido - Estabelecendo pareamento...\n", msg.uniqueId);
      lastReadyReceived = millis();
      memcpy(gPair.peer, mac, 6);
      gPair.channel = AP_CHANNEL;

      ensurePeerExists(gPair.peer);
      savePairToEEPROM();
      gPaired = true;
      gDiscoveryMode = false;
      gReadyReceived = true;
      Serial.printf("PAREADO COM SUCESSO: %s (ID: %d)\n", macToStr(gPair.peer).c_str(), msg.uniqueId);
    }
  }
  
  // READY de dispositivo já pareado
  if (gPaired && strcmp(msg.message, "READY") == 0) {
    lastReadyReceived = millis();
    Serial.printf("READY recebido - Conexão mantida! (ID: %d)\n", msg.uniqueId);
  }
  
  // ACK de comando
  if (gPaired && strcmp(msg.message, "ACK") == 0) {
    // Verifica se é do peer pareado
    if (memcmp(gPair.peer, mac, 6) == 0) {
      gAckReceived = true;
      Serial.printf("ACK DE COMANDO RECEBIDO - Sucesso! (ID: %d)\n", msg.uniqueId);
    } else {
      Serial.printf("ACK de MAC não pareado: %s (esperado: %s)\n", 
                   macToStr(mac).c_str(), macToStr(gPair.peer).c_str());
    }
  }
}

// =================== Descoberta ====================
void sendHelloBroadcast() {
  esp_now_message hello;
  hello.uniqueId = UNIQUE_DEVICE_ID;
  hello.deviceType = 2;  // 2 = Portaria
  hello.command = 0;     // Comando de pareamento
  hello.relayState = 0;
  strcpy(hello.message, "HELLO");

  Serial.printf("Enviando HELLO broadcast (ID: %d, canal: %d)...\n", UNIQUE_DEVICE_ID, AP_CHANNEL);
  
  int result = esp_now_send(bcast, (uint8_t*)&hello, sizeof(hello));
  Serial.printf("Resultado HELLO: %s\n", result == 0 ? "OK" : "ERRO");
}

void checkConnectionHealth() {
  if (gPaired && lastReadyReceived > 0) {
    uint32_t timeSinceReady = millis() - lastReadyReceived;
    
    if (timeSinceReady > 15000) {
      Serial.printf("TIMEOUT: %lu ms sem READY - Iniciando reconexão\n", timeSinceReady);
      gDiscoveryMode = true;
      lastReconnectAttempt = millis();
    }
  }
}

// =================== Web Handlers ===================
String peerStrOrEmpty(){
  return gPaired ? macToStr(gPair.peer) : String("");
}

void handleRoot() { 
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus(){
  String json = "{";
  json += "\"my\":\"" + macToStr(myMac) + "\",";
  json += "\"peer\":\"" + peerStrOrEmpty() + "\",";
  json += "\"paired\":" + String(gPaired ? "true" : "false") + ",";
  json += "\"discovery\":" + String(gDiscoveryMode ? "true" : "false") + ",";
  json += "\"channel\":" + String(gPair.channel) + ",";
  json += "\"uniqueId\":" + String(UNIQUE_DEVICE_ID) + ",";
  json += "\"uptime\":" + String(millis()/1000) + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"lastReady\":" + String(lastReadyReceived);
  json += "}";
  server.send(200, "application/json", json);
}

void handleOpen(){
  if (!gPaired) {
    server.send(500, "text/plain", "Erro: Portao nao pareado. Aguardando conexao...");
    return;
  }

  uint32_t timeSinceReady = millis() - lastReadyReceived;
  if (timeSinceReady > 15000) {
    server.send(500, "text/plain", "Erro: Portao desconectado. Aguarde reconexao...");
    gDiscoveryMode = true;
    return;
  }

  Serial.printf("=== INICIANDO COMANDO ABRIR (ID: %d) ===\n", UNIQUE_DEVICE_ID);
  
  esp_now_message msg;
  msg.uniqueId = UNIQUE_DEVICE_ID;
  msg.deviceType = 2;  // 2 = Portaria
  msg.command = 1;     // 1 = abrir
  msg.relayState = 0;
  strcpy(msg.message, "ABRIR");

  gAckReceived = false;
  bool success = false;

  for (int attempt = 1; attempt <= MAX_RETRY_ATTEMPTS && !success; attempt++) {
    Serial.printf("--- Tentativa %d/%d ---\n", attempt, MAX_RETRY_ATTEMPTS);
    
    ensurePeerExists(gPair.peer);
    delay(COMMAND_DELAY);
    
    int result = esp_now_send(gPair.peer, (uint8_t*)&msg, sizeof(msg));
    Serial.printf("Comando enviado para %s - Resultado: %s\n", 
                  macToStr(gPair.peer).c_str(), result == 0 ? "OK" : "ERRO");
    
    if (result != 0) {
      Serial.printf("ERRO no envio (tentativa %d)\n", attempt);
      delay(1000);
      continue;
    }

    uint32_t startTime = millis();
    Serial.printf("Aguardando ACK por %d ms...\n", ACK_TIMEOUT_MS);
    
    while (millis() - startTime < ACK_TIMEOUT_MS) {
      if (gAckReceived) { 
        Serial.println("✅ ACK RECEBIDO COM SUCESSO!");
        success = true; 
        break; 
      }
      delay(20);
      yield();
    }
    
    if (!success) {
      Serial.printf("⌛ TIMEOUT aguardando ACK (tentativa %d)\n", attempt);
    }
    
    gAckReceived = false;

    if (!success && attempt < MAX_RETRY_ATTEMPTS) {
      Serial.println("Aguardando antes da próxima tentativa...");
      delay(1500);
    }
  }

  if (success) {
    Serial.println("=== ✅ COMANDO EXECUTADO COM SUCESSO ===");
    server.send(200, "text/plain", "Portao acionado com sucesso!");
  } else {
    Serial.println("=== ⌛ FALHA APÓS TODAS AS TENTATIVAS ===");
    server.send(504, "text/plain", "Falha: Portao nao respondeu apos " + String(MAX_RETRY_ATTEMPTS) + " tentativas");
  }
}

void handleClose() {
    if (!gPaired) {
        server.send(500, "text/plain", "Erro: Portao nao pareado. Aguardando conexao...");
        return;
    }

    uint32_t timeSinceReady = millis() - lastReadyReceived;
    if (timeSinceReady > 15000) {
        server.send(500, "text/plain", "Erro: Portao desconectado. Aguarde reconexao...");
        gDiscoveryMode = true;
        return;
    }

    Serial.printf("=== INICIANDO COMANDO FECHAR (ID: %d) ===\n", UNIQUE_DEVICE_ID);

    esp_now_message msg;
    msg.uniqueId = UNIQUE_DEVICE_ID;
    msg.deviceType = 2;  // 2 = Portaria
    msg.command = 2;     // 2 = fechar
    msg.relayState = 0;
    strcpy(msg.message, "FECHAR");

    gAckReceived = false;
    bool success = false;

    for (int attempt = 1; attempt <= MAX_RETRY_ATTEMPTS && !success; attempt++) {
        Serial.printf("--- Tentativa %d/%d ---\n", attempt, MAX_RETRY_ATTEMPTS);
        
        ensurePeerExists(gPair.peer);
        delay(COMMAND_DELAY);

        int result = esp_now_send(gPair.peer, (uint8_t*)&msg, sizeof(msg));
        Serial.printf("Comando enviado para %s - Resultado: %s\n", 
                      macToStr(gPair.peer).c_str(), result == 0 ? "OK" : "ERRO");

        if (result != 0) {
            Serial.printf("ERRO no envio (tentativa %d)\n", attempt);
            delay(1000);
            continue;
        }

        uint32_t startTime = millis();
        while (millis() - startTime < ACK_TIMEOUT_MS) {
            if (gAckReceived) { 
                Serial.println("✅ ACK RECEBIDO COM SUCESSO!");
                success = true; 
                break; 
            }
            delay(20);
            yield();
        }

        if (!success) {
            Serial.printf("⌛ TIMEOUT aguardando ACK (tentativa %d)\n", attempt);
        }

        gAckReceived = false;

        if (!success && attempt < MAX_RETRY_ATTEMPTS) {
            delay(1500);
        }
    }

    if (success) {
        Serial.println("=== ✅ COMANDO FECHAR EXECUTADO COM SUCESSO ===");
        server.send(200, "text/plain", "Portao fechado com sucesso!");
    } else {
        Serial.println("=== ⌛ FALHA APÓS TODAS AS TENTATIVAS ===");
        server.send(504, "text/plain", "Falha: Portao nao respondeu apos " + String(MAX_RETRY_ATTEMPTS) + " tentativas");
    }
}

void handleUnpair(){
  clearPairing();
  server.send(200, "text/plain", "Pareamento removido. O sistema iniciara nova descoberta automaticamente.");
}

void handleReconnect(){
  Serial.println("Forçando reconexão...");
  if (gPaired) {
    gDiscoveryMode = true;
    lastReconnectAttempt = millis();
    sendHelloBroadcast();
  }
  server.send(200, "text/plain", "Tentativa de reconexao iniciada.");
}

void handleInfo(){
  String info = "ESP-01 Portaria - Status do Sistema\n";
  info += "===================================\n";
  info += "ID Único: " + String(UNIQUE_DEVICE_ID) + "\n";
  info += "MAC: " + macToStr(myMac) + "\n";
  info += "Canal: " + String(AP_CHANNEL) + "\n";
  info += "Pareado: " + String(gPaired ? "Sim" : "Nao") + "\n";
  if (gPaired) {
    info += "MAC Portao: " + macToStr(gPair.peer) + "\n";
    info += "Canal Peer: " + String(gPair.channel) + "\n";
    info += "ID Salvo: " + String(gPair.uniqueId) + "\n";
    info += "Timestamp: " + String(gPair.timestamp) + "\n";
    if (lastReadyReceived > 0) {
      uint32_t timeSinceReady = millis() - lastReadyReceived;
      info += "Ultimo READY: " + String(timeSinceReady/1000) + "s atras\n";
      info += "Status Conexao: " + String(timeSinceReady < 15000 ? "ATIVA" : "TIMEOUT") + "\n";
    } else {
      info += "Ultimo READY: Nunca\n";
      info += "Status Conexao: INATIVA\n";
    }
  }
  info += "Discovery Mode: " + String(gDiscoveryMode ? "Ativo" : "Inativo") + "\n";
  info += "Uptime: " + String(millis()/1000) + "s\n";
  info += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n";

  server.send(200, "text/plain", info);
}

// =================== Setup/Loop ====================
void setup(){
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("===========================================");
  Serial.printf("ESP-01 PORTARIA - ID ÚNICO: %d\n", UNIQUE_DEVICE_ID);
  Serial.println("===========================================");

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, false, AP_MAX_CONN);
  WiFi.disconnect();
  
  wifi_set_channel(AP_CHANNEL);
  wifi_get_macaddr(STATION_IF, myMac);

  Serial.printf("ID Único: %d\n", UNIQUE_DEVICE_ID);
  Serial.printf("Tipo: 2 (Portaria)\n");
  Serial.printf("AP Criado: %s\n", AP_SSID);
  Serial.printf("Canal: %d\n", AP_CHANNEL);
  Serial.printf("IP do AP: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("MAC STA: %s\n", macToStr(myMac).c_str());

  if (esp_now_init() != 0) {
    Serial.println("ERRO CRÍTICO: esp_now_init falhou!");
    while(true) {
      Serial.println("Sistema em loop de erro - Reinicie o ESP");
      delay(5000);
    }
  }
  
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  esp_now_del_peer(bcast);
  int bcastResult = esp_now_add_peer(bcast, ESP_NOW_ROLE_COMBO, AP_CHANNEL, NULL, 0);
  Serial.printf("Broadcast peer: %s\n", bcastResult == 0 ? "OK" : "ERRO");

  gPaired = loadPairFromEEPROM();
  if (gPaired) {
    Serial.printf("PAREAMENTO RESTAURADO: %s (ID: %d)\n", 
                  macToStr(gPair.peer).c_str(), gPair.uniqueId);
    ensurePeerExists(gPair.peer);
    gDiscoveryMode = false;
    
    Serial.printf("Testando conexão com Portão ID: %d...\n", UNIQUE_DEVICE_ID);
    sendHelloBroadcast();
    
    uint32_t testStart = millis();
    gReadyReceived = false;
    while (millis() - testStart < HELLO_TIMEOUT && !gReadyReceived) {
      yield();
      delay(100);
    }
    
    if (gReadyReceived) {
      Serial.printf("Portão ID %d respondeu - Conexão OK!\n", UNIQUE_DEVICE_ID);
      lastReadyReceived = millis();
    } else {
      Serial.printf("Portão ID %d não respondeu - Ativando descoberta\n", UNIQUE_DEVICE_ID);
      gDiscoveryMode = true;
    }
  } else {
    Serial.printf("NENHUM PAREAMENTO - Procurando Portão ID: %d\n", UNIQUE_DEVICE_ID);
    gDiscoveryMode = true;
  }

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/open", handleOpen);
  server.on("/close", handleClose);
  server.on("/unpair", handleUnpair);
  server.on("/reconnect", handleReconnect);
  server.on("/info", handleInfo);
  server.begin();

  Serial.println("===========================================");
  Serial.println("SERVIDOR WEB: http://192.168.4.1");
  Serial.printf("SISTEMA PORTARIA PRONTO! Aceita apenas ID: %d\n", UNIQUE_DEVICE_ID);
  Serial.println("===========================================");
}

void loop(){
  server.handleClient();

  if (gDiscoveryMode && millis() - lastHello > HELLO_INTERVAL_MS) {
    lastHello = millis();
    sendHelloBroadcast();
  }

  static uint32_t lastHealthCheck = 0;
  if (millis() - lastHealthCheck > 5000) {
    lastHealthCheck = millis();
    checkConnectionHealth();
  }

  if (gPaired && gDiscoveryMode && 
      millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
    Serial.printf("Tentativa automática de reconexão (ID: %d)...\n", UNIQUE_DEVICE_ID);
    lastReconnectAttempt = millis();
    sendHelloBroadcast();
  }

  yield();
}