// ===================== PORTARIA (ESP8266/ESP-01) =====================
// - Cria AP "Portaria-ESP" com Web UI para acionar o portão
// - Pareamento automático via ESP-NOW (compatível com Portão simplificado)
// - Salva pareamento na EEPROM; /unpair limpa e reinicia descoberta
// - /open envia comando e aguarda ACK com retentativas
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
#define AP_CHANNEL         1              // Mesmo canal do Portão
#define AP_MAX_CONN        2

#define HELLO_INTERVAL_MS  3000           // Aumentado para 3 segundos
#define ACK_TIMEOUT_MS     3000           // Aumentado para 3 segundos
#define MAX_RETRY_ATTEMPTS 3
#define COMMAND_DELAY      300            // Delay antes de enviar comando
typedef struct {
    uint8_t deviceId;
    uint8_t command;
    uint8_t relayState;
    char message[32];
} esp_now_message;
// ============== Estruturas de Pareamento =============
struct PairStore {
  uint32_t magic;
  uint8_t  peer[6];
  uint8_t  channel;
  uint8_t  flags;
  uint32_t timestamp;
  uint8_t  reserved[16];
};

#define EEPROM_SIZE  64
#define MAGIC_WORD   0xABCD1234

// =================== Estado Global ===================
PairStore gPair{};
bool gPaired = false;
volatile bool gDiscoveryMode = true;

ESP8266WebServer server(80);
uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t myMac[6]{};
volatile bool gAckReceived = false;
uint32_t lastHello = 0;

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
            transition: color 0.3s ease;
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
        </div>
        <button class="main-button" id="openBtn" onclick="openGate()">
            <span id="btnText">🚪 Abrir Portao</span>
        </button>
        <div class="response-area" id="response">Sistema pronto para uso</div>
        <div class="footer">
            <a href="/unpair" class="footer-link">🔓 Desemparelhar Dispositivos</a>
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
                        if (data.peer && data.peer.trim() !== '') {
                            document.getElementById('peerMac').textContent = data.peer;
                            document.getElementById('connectionStatus').textContent = 'Conectado';
                            document.getElementById('connectionStatus').className = 'connection-status connected';
                            isConnected = true;
                        } else {
                            document.getElementById('peerMac').textContent = 'Aguardando...';
                            document.getElementById('connectionStatus').textContent = 'Desconectado';
                            document.getElementById('connectionStatus').className = 'connection-status disconnected';
                            isConnected = false;
                        }
                        document.getElementById('openBtn').disabled = !isConnected;
                    } catch (e) { setResponse('Erro JSON', 'error'); }
                }
            };
            xhr.send();
        }
        function openGate() {
            if (!isConnected) { setResponse('Erro: Portao nao pareado', 'error'); return; }
            var btn = document.getElementById('openBtn');
            var btnText = document.getElementById('btnText');
            var originalText = btnText.innerHTML;
            btn.disabled = true; btn.classList.add('pulse');
            btnText.innerHTML = '<span class="loading"></span> Acionando...';
            var xhr = new XMLHttpRequest();
            xhr.open('GET', '/open', true);
            xhr.onreadystatechange = function() {
                if (xhr.readyState == 4) {
                    var message = xhr.responseText;
                    setResponse((xhr.status == 200 ? '✅ ' : '❌ ') + message, 
                               xhr.status == 200 ? 'success' : 'error');
                    setTimeout(function() {
                        btn.disabled = !isConnected; btn.classList.remove('pulse');
                        btnText.innerHTML = originalText;
                    }, 1000);
                }
            };
            xhr.send();
        }
        function setResponse(message, type) {
            var el = document.getElementById('response');
            el.textContent = message; el.className = 'response-area ' + (type || '');
        }
        window.onload = function() { loadStatus(); setInterval(loadStatus, 5000); };
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
  EEPROM.put(0, gPair);
  EEPROM.commit();
  Serial.println("Pareamento salvo na EEPROM");
}

bool loadPairFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, gPair);
  if (gPair.magic == MAGIC_WORD && gPair.channel >= 1 && gPair.channel <= 13) {
    Serial.printf("Pareamento carregado - Canal: %d\n", gPair.channel);
    return true;
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
  if (len != sizeof(esp_now_message)) return;

  esp_now_message msg;
  memcpy(&msg, data, sizeof(msg));

  Serial.printf("=== RESPOSTA RECEBIDA ===\n");
  Serial.printf("deviceId=%d, cmd=%d, estado=%d, msg=%s\n",
              msg.deviceId, msg.command, msg.relayState, msg.message);
  Serial.printf("De: %s\n", macToStr(mac).c_str());

  // Resposta do portão durante pareamento
  if (!gPaired || gDiscoveryMode) {
    if (strcmp(msg.message, "READY") == 0) {
      Serial.println("READY recebido - Estabelecendo pareamento...");
      memcpy(gPair.peer, mac, 6);
      gPair.channel = AP_CHANNEL;

      ensurePeerExists(gPair.peer);
      savePairToEEPROM();
      gPaired = true;
      gDiscoveryMode = false;
      Serial.printf("PAREADO COM SUCESSO: %s\n", macToStr(gPair.peer).c_str());
    }
  }
  
  // ACK de comando
  if (gPaired && strcmp(msg.message, "ACK") == 0) {
    gAckReceived = true;
    Serial.println("ACK DE COMANDO RECEBIDO - Sucesso!");
  }
}

// =================== Descoberta ====================
void sendHelloBroadcast() {
  if (!gDiscoveryMode && gPaired) return;

  const char *hello = "HELLO";
  Serial.printf("Enviando HELLO broadcast (canal: %d)...\n", AP_CHANNEL);
  
  int result = esp_now_send(bcast, (uint8_t*)hello, strlen(hello));
  Serial.printf("Resultado HELLO: %s\n", result == 0 ? "OK" : "ERRO");
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
  json += "\"channel\":" + String(gPair.channel) + ",";
  json += "\"uptime\":" + String(millis()/1000) + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap());
  json += "}";
  server.send(200, "application/json", json);
}

void handleOpen(){
  if (!gPaired) {
    server.send(500, "text/plain", "Erro: Portao nao pareado. Aguardando conexao...");
    return;
  }

  Serial.println("=== INICIANDO COMANDO ABRIR ===");
  
  esp_now_message msg;
  msg.deviceId = 1;                 // ID do ESP do portão
  msg.command = 1;                  // 1 = abrir
  msg.relayState = 0;               // Estado não é importante aqui
  strcpy(msg.message, "ABRIR");     // Texto só para log/debug

  gAckReceived = false;
  bool success = false;

  for (int attempt = 1; attempt <= MAX_RETRY_ATTEMPTS && !success; attempt++) {
    Serial.printf("--- Tentativa %d/%d ---\n", attempt, MAX_RETRY_ATTEMPTS);
    
    // Garante que o peer está adicionado
    ensurePeerExists(gPair.peer);
    
    // Delay antes do comando para estabilidade
    delay(COMMAND_DELAY);
    
    // Envia o comando
    int result = esp_now_send(gPair.peer, (uint8_t*)&msg, sizeof(msg));
    Serial.printf("Comando enviado para %s - Resultado: %s\n", 
                  macToStr(gPair.peer).c_str(), result == 0 ? "OK" : "ERRO");
    
    if (result != 0) {
      Serial.printf("ERRO no envio (tentativa %d)\n", attempt);
      delay(500); // Mais tempo antes de tentar novamente
      continue;
    }

    // Aguarda ACK com timeout
    uint32_t startTime = millis();
    Serial.printf("Aguardando ACK por %d ms...\n", ACK_TIMEOUT_MS);
    
    while (millis() - startTime < ACK_TIMEOUT_MS) {
      if (gAckReceived) { 
        Serial.println("✅ ACK RECEBIDO COM SUCESSO!");
        success = true; 
        break; 
      }
      delay(10);
      yield(); // Importante para não travar o sistema
    }
    
    if (!success) {
      Serial.printf("❌ TIMEOUT aguardando ACK (tentativa %d)\n", attempt);
    }
    
    gAckReceived = false; // Reset para próxima tentativa

    if (!success && attempt < MAX_RETRY_ATTEMPTS) {
      Serial.println("Aguardando antes da próxima tentativa...");
      delay(500); // Intervalo entre tentativas
    }
  }

  if (success) {
    Serial.println("=== ✅ COMANDO EXECUTADO COM SUCESSO ===");
    server.send(200, "text/plain", "Portao acionado com sucesso!");
  } else {
    Serial.println("=== ❌ FALHA APÓS TODAS AS TENTATIVAS ===");
    server.send(504, "text/plain", "Falha: Portao nao respondeu apos " + String(MAX_RETRY_ATTEMPTS) + " tentativas");
  }
}

void handleUnpair(){
  clearPairing();
  server.send(200, "text/plain", "Pareamento removido. O sistema iniciara nova descoberta automaticamente.");
}

void handleInfo(){
  String info = "ESP-01 Portaria - Status do Sistema\n";
  info += "===================================\n";
  info += "MAC: " + macToStr(myMac) + "\n";
  info += "Canal: " + String(AP_CHANNEL) + "\n";
  info += "Pareado: " + String(gPaired ? "Sim" : "Nao") + "\n";
  if (gPaired) {
    info += "MAC Portao: " + macToStr(gPair.peer) + "\n";
    info += "Canal Peer: " + String(gPair.channel) + "\n";
    info += "Timestamp: " + String(gPair.timestamp) + "\n";
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
  Serial.println("ESP-01 PORTARIA - Sistema de Controle");
  Serial.println("===========================================");

  // WiFi AP + STA mode
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, false, AP_MAX_CONN);
  WiFi.disconnect(); // Desconecta de qualquer rede STA
  
  // Força o canal WiFi
  wifi_set_channel(AP_CHANNEL);
  
  wifi_get_macaddr(STATION_IF, myMac);

  Serial.printf("AP Criado: %s\n", AP_SSID);
  Serial.printf("Senha: %s\n", AP_PASS);
  Serial.printf("Canal: %d\n", AP_CHANNEL);
  Serial.printf("IP do AP: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("MAC STA: %s\n", macToStr(myMac).c_str());

  // ESPNOW
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

  // Peer broadcast para descoberta
  esp_now_del_peer(bcast);
  int bcastResult = esp_now_add_peer(bcast, ESP_NOW_ROLE_COMBO, AP_CHANNEL, NULL, 0);
  Serial.printf("Broadcast peer: %s\n", bcastResult == 0 ? "OK" : "ERRO");

  // Carrega pareamento salvo
  gPaired = loadPairFromEEPROM();
  if (gPaired) {
    Serial.printf("PAREAMENTO RESTAURADO: %s (canal %d)\n", 
                  macToStr(gPair.peer).c_str(), gPair.channel);
    ensurePeerExists(gPair.peer);
    gDiscoveryMode = false;
    
    Serial.println("Sistema pronto - Enviando HELLO para confirmar conexão...");
    // Envia um HELLO inicial para confirmar que o portão ainda está ativo
    delay(1000);
    gDiscoveryMode = true; // Temporariamente ativa descoberta para testar
    sendHelloBroadcast();
    delay(2000);
    if (!gPaired) {
      Serial.println("Portão não respondeu - Continuando em modo descoberta");
    }
  } else {
    Serial.println("NENHUM PAREAMENTO - Iniciando descoberta...");
    gDiscoveryMode = true;
  }

  // Servidor Web
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/open", handleOpen);
  server.on("/unpair", handleUnpair);
  server.on("/info", handleInfo);
  server.begin();

  Serial.println("===========================================");
  Serial.println("SERVIDOR WEB: http://192.168.4.1");
  Serial.println("SISTEMA PORTARIA PRONTO!");
  Serial.println("===========================================");
}

void loop(){
  server.handleClient();

  // Descoberta automática se não pareado ou em modo descoberta
  if (gDiscoveryMode && millis() - lastHello > HELLO_INTERVAL_MS) {
    lastHello = millis();
    sendHelloBroadcast();
  }

  yield();
}