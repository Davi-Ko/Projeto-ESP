/*
 gateway.ino
 ESP8266 Gateway: WiFi AP + Web server + ESP-NOW
 - Shows discovered nodes (MAC)
 - Lets you assign "role" and "logical id" to each MAC
 - Sends set_role to the specific ESP-01 via ESP-NOW
 - Broadcasts "announce" and full "config" periodically so new nodes get config
 Requirements:
  - ArduinoJson 6.x
  - ESP8266 core
 Notes:
  - Keep MAX_NODES >= number of clients (we use 12 as safe guard)
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>           // ESP-NOW functions (ESP8266 core)
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>

#define AP_SSID "Intertravamento_AP"
#define AP_PASS "12345678"
#define HTTP_PORT 80

ESP8266WebServer server(HTTP_PORT);

const char* ROLES[] = {
  "rele_ima1",     // ESP para liberar ímã 1
  "rele_ima2",     // ESP para liberar ímã 2
  "sensor1",       // ESP sensor 1
  "sensor2",       // ESP sensor 2
  "interlock",     // ESP intertravamento
  "botoeira1",     // ESP botoeira 1
  "botoeira2"      // ESP botoeira 2
};

const char* LOGICALS[] = {
  "ima1", "ima2", "sensor1", "sensor2", "interlock", "botoeira1", "botoeira2"
};


struct NodeInfo {
  String mac;
  String role;     // e.g., "rele", "sensor", "botoeira", "interlock"
  String logical;  // e.g., "door1_relay", "door1_sensor", "door1_button"
  String lastMsg;
  unsigned long lastSeen;
};

#define MAX_NODES 12
NodeInfo nodes[MAX_NODES];
int nodeCount = 0;

String macToString(const uint8_t *mac) {
  char buf[18];
  sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  return String(buf);
}

int findNodeByMac(const String &mac) {
  for (int i=0;i<nodeCount;i++) if (nodes[i].mac == mac) return i;
  return -1;
}

void addOrUpdateNode(const String &mac, const String &msg) {
  int idx = findNodeByMac(mac);
  if (idx >= 0) {
    nodes[idx].lastMsg = msg;
    nodes[idx].lastSeen = millis();
    return;
  }
  if (nodeCount >= MAX_NODES) return;
  nodes[nodeCount].mac = mac;
  nodes[nodeCount].role = "";
  nodes[nodeCount].logical = "";
  nodes[nodeCount].lastMsg = msg;
  nodes[nodeCount].lastSeen = millis();
  nodeCount++;
}

// Build full config JSON to broadcast to all clients
String buildFullConfigJson() {
  StaticJsonDocument<2048> doc;
  doc["type"] = "config";
  JsonArray arr = doc.createNestedArray("table");
  for (int i=0;i<nodeCount;i++) {
    if (nodes[i].role.length() || nodes[i].logical.length()) {
      JsonObject o = arr.createNestedObject();
      o["mac"] = nodes[i].mac;
      o["role"] = nodes[i].role;
      o["logical"] = nodes[i].logical;
    }
  }
  String out; serializeJson(doc, out);
  return out;
}

// Send broadcast (ff:ff:ff:ff:ff:ff)
void espNowSendBroadcast(const String &payload) {
  uint8_t bc[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  esp_now_send(bc, (uint8_t*)payload.c_str(), payload.length());
}

// Send to specific MAC
void espNowSendToMac(const String &macStr, const String &payload) {
  uint8_t mac[6];
  if (sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
             &mac[0],&mac[1],&mac[2],&mac[3],&mac[4],&mac[5]) != 6) {
    Serial.println("Invalid MAC");
    return;
  }
  esp_now_send(mac, (uint8_t*)payload.c_str(), payload.length());
}

// ESP-NOW receive callback
void onDataRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  String m = macToString(mac);
  String msg;
  for (int i=0;i<len;i++) msg += (char)data[i];
  Serial.printf("RX from %s : %s\n", m.c_str(), msg.c_str());
  addOrUpdateNode(m, msg);

  // parse incoming JSON for events that we should display/log
  StaticJsonDocument<512> doc;
  auto err = deserializeJson(doc, msg);
  if (!err) {
    const char* type = doc["type"];
    if (type) {
      String t = String(type);
      if (t == "announce") {
        // node announcing - respond by sending current config entry for it (if exists)
        int idx = findNodeByMac(m);
        if (idx >= 0 && (nodes[idx].role.length() || nodes[idx].logical.length())) {
          StaticJsonDocument<256> setdoc;
          setdoc["type"] = "set_role";
          setdoc["role"] = nodes[idx].role;
          setdoc["logical"] = nodes[idx].logical;
          String out; serializeJson(setdoc, out);
          espNowSendToMac(m, out);
          Serial.printf("Sent set_role to %s\n", m.c_str());
        } else {
          // send full config so node knows network mapping
          String cfg = buildFullConfigJson();
          if (cfg.length()) espNowSendToMac(m, cfg);
        }
      } else if (t == "event") {
        // event from node: update UI (we already stored lastMsg)
      }
    }
  }
}

String buildRoleSelect(const String& selected) {
  String html = "<select name='role'>";
  html += "<option value=''>--</option>";
  for (int i = 0; i < 7; i++) {
    html += "<option value='" + String(ROLES[i]) + "'";
    if (selected == ROLES[i]) html += " selected";
    html += ">" + String(ROLES[i]) + "</option>";
  }
  html += "</select>";
  return html;
}

String buildLogicalSelect(const String& selected) {
  String html = "<select name='logical'>";
  html += "<option value=''>--</option>";
  for (int i = 0; i < 7; i++) {
    html += "<option value='" + String(LOGICALS[i]) + "'";
    if (selected == LOGICALS[i]) html += " selected";
    html += ">" + String(LOGICALS[i]) + "</option>";
  }
  html += "</select>";
  return html;
}

// HTTP handlers
void handleRoot() {
  String html;
  html += "<html><head><meta charset='utf-8'><title>Gateway</title></head><body>";
  html += "<h2>Gateway - Intertravamento</h2>";
  html += "<p>AP SSID: " + String(AP_SSID) + "</p>";
  html += "<h3>Discovered nodes</h3>";
  html += "<table border='1' cellpadding='6'><tr><th>MAC</th><th>Role</th><th>Logical</th><th>Last Msg</th><th>Action</th></tr>";
  for (int i=0;i<nodeCount;i++) {
    html += "<tr>";
    html += "<td>" + nodes[i].mac + "</td>";
    html += "<td>" + nodes[i].role + "</td>";
    html += "<td>" + nodes[i].logical + "</td>";
    html += "<td><pre style='font-family:monospace;max-width:400px;overflow:auto'>" + nodes[i].lastMsg + "</pre></td>";
    html += "<td>";
    html += "<form method='POST' action='/assign' style='display:inline-block'>";
    html += "<input type='hidden' name='mac' value='" + nodes[i].mac + "'>";
    html += "role: " + buildRoleSelect(nodes[i].role);
    html += " logical: " + buildLogicalSelect(nodes[i].logical);
    html += "<input type='submit' value='Salvar'></form>";
    html += "</td>";
    html += "</tr>";
  }
  html += "</table>";
  html += "<h3>Quick commands</h3>";
  html += "<form method='POST' action='/sendcmd'>";
  html += "Target MAC (empty=broadcast): <input name='target' size=20> ";
  html += "action: <input name='action' size=8> logical: " + buildLogicalSelect("") + "";
  html += "<input type='submit' value='Send'></form>";
  html += "<p>API: GET /status (json)  POST /assign (mac,role,logical) POST /sendcmd (target,action,logical)</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleStatus() {
  StaticJsonDocument<2048> doc;
  JsonArray arr = doc.createNestedArray("nodes");
  for (int i=0;i<nodeCount;i++) {
    JsonObject o = arr.createNestedObject();
    o["mac"] = nodes[i].mac;
    o["role"] = nodes[i].role;
    o["logical"] = nodes[i].logical;
    o["lastMsg"] = nodes[i].lastMsg;
    o["lastSeenMs"] = (unsigned long)nodes[i].lastSeen;
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleAssign() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Use POST"); return; }
  String mac = server.arg("mac");
  String role = server.arg("role");
  String logical = server.arg("logical");

  int idx = findNodeByMac(mac);
  if (idx >= 0) {
    nodes[idx].role = role;
    nodes[idx].logical = logical;
  } else {
    if (nodeCount < MAX_NODES) {
      nodes[nodeCount].mac = mac;
      nodes[nodeCount].role = role;
      nodes[nodeCount].logical = logical;
      nodes[nodeCount].lastMsg = "";
      nodes[nodeCount].lastSeen = millis();
      nodeCount++;
    } else {
      server.send(500, "text/plain", "node list full");
      return;
    }
  }

  // Send set_role message to the assigned node
  StaticJsonDocument<256> doc;
  doc["type"] = "set_role";
  doc["role"] = role;
  doc["logical"] = logical;
  String out; serializeJson(doc, out);
  espNowSendToMac(mac, out);

  // Broadcast full config to all nodes (so everyone knows mapping)
  String cfg = buildFullConfigJson();
  if (cfg.length()) espNowSendBroadcast(cfg);

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

void handleSendCmd() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Use POST"); return; }
  String target = server.arg("target");
  String action = server.arg("action");
  String logical = server.arg("logical");

  StaticJsonDocument<256> doc;
  doc["type"] = "command";
  doc["action"] = action;
  if (logical.length()) doc["logical"] = logical;
  String out; serializeJson(doc, out);

  if (target.length() == 0) {
    espNowSendBroadcast(out);
  } else {
    espNowSendToMac(target, out);
  }

  server.sendHeader("Location", "/");
  server.send(302, "text/plain", "");
}

unsigned long lastAnnounce = 0;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Gateway starting...");

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("AP started: %s\n", AP_SSID);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  uint8_t mac[6]; WiFi.macAddress(mac);
  Serial.print("Gateway MAC: "); Serial.println(macToString(mac));

  // init ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("esp_now_init failed");
  } else {
    esp_now_register_recv_cb(onDataRecv);
    Serial.println("esp_now initialized");
  }

  // web endpoints
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/assign", HTTP_POST, handleAssign);
  server.on("/sendcmd", HTTP_POST, handleSendCmd);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  // prune stale nodes (>2 minutes)
  for (int i=0;i<nodeCount;i++) {
    if (millis() - nodes[i].lastSeen > 120000) {
      for (int j=i;j<nodeCount-1;j++) nodes[j] = nodes[j+1];
      nodeCount--; i--;
    }
  }

  // periodic announce & push config
  if (millis() - lastAnnounce > 5000) {
    lastAnnounce = millis();
    StaticJsonDocument<128> doc;
    doc["type"] = "announce";
    doc["gw"] = macToString((const uint8_t*)WiFi.macAddress().c_str());
    String out; serializeJson(doc, out);
    espNowSendBroadcast(out);

    String cfg = buildFullConfigJson();
    if (cfg.length()) espNowSendBroadcast(cfg);
  }
}
