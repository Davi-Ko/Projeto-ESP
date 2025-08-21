// filepath: ESP-01/ESP-01.ino
/*
 client_esp01.ino
 Generic ESP-01 firmware (single binary for all nodes)
 Roles supported: "rele", "sensor", "botoeira", "interlock"
 - Saves role and logical id to EEPROM on set_role
 - Announces self to gateway at startup
 - Receives "set_role" or "config" messages via ESP-NOW
 - Broadcasts "event" messages (sensor/button/relay change)
 - Executes "command" messages targeted to its logical id
 Notes:
  - GPIO0: BUTTON (careful with boot)
  - GPIO2: RELAY control (active HIGH assumed)
  - EEPROM size small but enough for simple JSON store
 Requirements:
  - ArduinoJson 6.x
  - EEPROM library
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

#define EEPROM_SIZE 1024

// pins for ESP-01
#define PIN_BUTTON 0   // use with caution (pull-up), triggers on LOW
#define PIN_RELAY 2    // drives a transistor/driver to switch relay
#define PIN_SENSOR 0   // if sensor uses same pin as button adjust accordingly

String myRole = "";      // loaded from EEPROM or set by gateway
String myLogical = "";   // loaded from EEPROM or set by gateway
String gatewayMac = "";  // learned from announce

// config table: logical -> mac
struct ConfigEntry { String logical; String mac; String role; };
#define MAX_CFG 16
ConfigEntry cfgTable[MAX_CFG];
int cfgCount = 0;

// cached states: logical -> state
struct StateEntry { String logical; String state; unsigned long lastSeen; };
#define MAX_STATES 32
StateEntry stateTable[MAX_STATES];
int stateCount = 0;

String ownMacStr() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  char buf[18]; sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
                        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  return String(buf);
}

void addOrUpdateCfg(const String &logical, const String &mac, const String &role) {
  for (int i=0;i<cfgCount;i++) {
    if (cfgTable[i].logical == logical) {
      cfgTable[i].mac = mac;
      cfgTable[i].role = role;
      return;
    }
  }
  if (cfgCount < MAX_CFG) {
    cfgTable[cfgCount].logical = logical;
    cfgTable[cfgCount].mac = mac;
    cfgTable[cfgCount].role = role;
    cfgCount++;
  }
}

String findMacByLogical(const String &logical) {
  for (int i=0;i<cfgCount;i++) if (cfgTable[i].logical == logical) return cfgTable[i].mac;
  return "";
}

void addOrUpdateState(const String &logical, const String &state) {
  for (int i=0;i<stateCount;i++) {
    if (stateTable[i].logical == logical) {
      stateTable[i].state = state;
      stateTable[i].lastSeen = millis();
      return;
    }
  }
  if (stateCount < MAX_STATES) {
    stateTable[stateCount].logical = logical;
    stateTable[stateCount].state = state;
    stateTable[stateCount].lastSeen = millis();
    stateCount++;
  }
}

String getState(const String &logical) {
  for (int i=0;i<stateCount;i++) if (stateTable[i].logical == logical) return stateTable[i].state;
  return "";
}

// EEPROM save/load (stores JSON with role, logical and config table)
void saveToEEPROM() {
  StaticJsonDocument<512> doc;
  doc["role"] = myRole;
  doc["logical"] = myLogical;
  JsonArray arr = doc.createNestedArray("table");
  for (int i=0;i<cfgCount;i++) {
    JsonObject o = arr.createNestedObject();
    o["logical"] = cfgTable[i].logical;
    o["mac"] = cfgTable[i].mac;
    o["role"] = cfgTable[i].role;
  }
  String out; serializeJson(doc, out);
  EEPROM.begin(EEPROM_SIZE);
  for (unsigned int i=0;i<out.length() && i < EEPROM_SIZE-1;i++) EEPROM.write(i, out[i]);
  EEPROM.write(out.length(), 0);
  EEPROM.commit();
  EEPROM.end();
  Serial.printf("Saved EEPROM: %s\n", out.c_str());
}

void loadFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  String s;
  for (int i=0;i<EEPROM_SIZE;i++) {
    char c = EEPROM.read(i);
    if (c == 0) break;
    s += c;
  }
  EEPROM.end();
  if (s.length() == 0) return;
  StaticJsonDocument<512> doc;
  auto err = deserializeJson(doc, s);
  if (err) {
    Serial.println("EEPROM parse error");
    return;
  }
  myRole = String((const char*)doc["role"]);
  myLogical = String((const char*)doc["logical"]);
  if (doc.containsKey("table")) {
    cfgCount = 0;
    for (JsonObject o : doc["table"].as<JsonArray>()) {
      addOrUpdateCfg(String((const char*)o["logical"]), String((const char*)o["mac"]), String((const char*)o["role"]));
    }
  }
  Serial.printf("Loaded from EEPROM role=%s logical=%s cfgCount=%d\n", myRole.c_str(), myLogical.c_str(), cfgCount);
}

// ESP-NOW helpers
void espSendBroadcast(const String &payload) {
  uint8_t bc[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  esp_now_send(bc, (uint8_t*)payload.c_str(), payload.length());
}
void espSendToMac(const String &macStr, const String &payload) {
  uint8_t mac[6];
  if (sscanf(macStr.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
             &mac[0],&mac[1],&mac[2],&mac[3],&mac[4],&mac[5]) != 6) {
    Serial.println("bad mac");
    return;
  }
  esp_now_send(mac, (uint8_t*)payload.c_str(), payload.length());
}

// receive callback
void onDataRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  String src;
  for (int i=0;i<6;i++) {
    char tmp[4]; sprintf(tmp, "%02X", mac[i]);
    src += tmp; if (i<5) src += ":";
  }
  String msg;
  for (int i=0;i<len;i++) msg += (char)data[i];
  Serial.printf("RX from %s : %s\n", src.c_str(), msg.c_str());

  StaticJsonDocument<512> doc;
  auto err = deserializeJson(doc, msg);
  if (err) { Serial.println("JSON parse err"); return; }
  const char* type = doc["type"];
  if (!type) return;
  String t = String(type);

  if (t == "announce") {
    const char* gw = doc["gw"];
    if (gw) {
      gatewayMac = String(gw);
      Serial.printf("Got gateway mac: %s\n", gatewayMac.c_str());
    }
  } else if (t == "set_role") {
    const char* role = doc["role"];
    const char* logical = doc["logical"];
    if (role) myRole = String(role);
    if (logical) myLogical = String(logical);
    saveToEEPROM();
    Serial.printf("Set role=%s logical=%s\n", myRole.c_str(), myLogical.c_str());
  } else if (t == "config") {
    // full config table
    if (doc.containsKey("table")) {
      cfgCount = 0;
      for (JsonObject o : doc["table"].as<JsonArray>()) {
        String macs = String((const char*)o["mac"]);
        String role = String((const char*)o["role"]);
        String logical = String((const char*)o["logical"]);
        addOrUpdateCfg(logical, macs, role);
      }
      saveToEEPROM();
      Serial.printf("Config updated from gateway (entries=%d)\n", cfgCount);
    }
  } else if (t == "event") {
    const char* logical = doc["logical"];
    const char* state = doc["state"];
    if (logical && state) {
      addOrUpdateState(String(logical), String(state));
      // If this client is interlock node, it may act on events to change local logic
    }
  } else if (t == "command") {
    const char* action = doc["action"];
    const char* logical = doc["logical"];
    if (logical && action) {
      String target = String(logical);
      // execute only if matched myLogical
      if (target == myLogical) {
        String act = String(action);
        if (myRole == "rele") {
          if (act == "open" || act == "on" || act == "abrir") {
            digitalWrite(PIN_RELAY, HIGH);
            addOrUpdateState(myLogical, "on");
            // broadcast state change
            StaticJsonDocument<128> ev; ev["type"] = "event"; ev["logical"] = myLogical; ev["state"] = "on"; ev["src"] = ownMacStr();
            String out; serializeJson(ev, out); espSendBroadcast(out);
          } else if (act == "close" || act == "off" || act == "fechar") {
            digitalWrite(PIN_RELAY, LOW);
            addOrUpdateState(myLogical, "off");
            StaticJsonDocument<128> ev; ev["type"] = "event"; ev["logical"] = myLogical; ev["state"] = "off"; ev["src"] = ownMacStr();
            String out; serializeJson(ev, out); espSendBroadcast(out);
          }
        }
      }
    }
  }
}

void sendAnnounce() {
  StaticJsonDocument<128> d;
  d["type"] = "announce";
  d["src"] = ownMacStr();
  String out; serializeJson(d, out);
  espSendBroadcast(out);
}

void sendEvent(const String &logical, const String &state) {
  StaticJsonDocument<192> d;
  d["type"] = "event";
  d["logical"] = logical;
  d["state"] = state;
  d["src"] = ownMacStr();
  String out; serializeJson(d, out);
  espSendBroadcast(out);
}

// parse numeric 'door' id from logical like "door1_button" or "door2_sensor"
int parseDoorId(const String &logical) {
  int p = logical.indexOf("door");
  if (p < 0) return -1;
  int q = p + 4;
  if (q < logical.length()) {
    char c = logical[q];
    if (isDigit(c)) return c - '0';
  }
  return -1;
}

// Evaluate interlock when button pressed (local decision)
void handleButtonPressLocal() {
  if (myLogical.length() == 0) return;
  int door = parseDoorId(myLogical);
  if (door < 0) return;
  int other = (door == 1) ? 2 : 1;
  String otherSensor = String("door") + String(other) + "_sensor";
  String otherState = getState(otherSensor);
  Serial.printf("Interlock check: other=%s state=%s\n", otherSensor.c_str(), otherState.c_str());
  bool allow = !(otherState == "open" || otherState == "aberta" || otherState == "1");
  if (allow) {
    String relayLogical = String("door") + String(door) + "_relay";
    String relayMac = findMacByLogical(relayLogical);
    StaticJsonDocument<256> cmd;
    cmd["type"] = "command";
    cmd["action"] = "open";
    cmd["logical"] = relayLogical;
    cmd["src"] = ownMacStr();
    String out; serializeJson(cmd, out);
    if (relayMac.length()) {
      espSendToMac(relayMac, out);
      Serial.printf("Sent unicast command to %s\n", relayMac.c_str());
    } else {
      espSendBroadcast(out);
      Serial.println("Relay MAC unknown, broadcasted command");
    }
  } else {
    Serial.println("Interlock: denied");
    StaticJsonDocument<128> ev; ev["type"]="event"; ev["logical"]=myLogical; ev["state"]="interlock_denied"; ev["src"]=ownMacStr();
    String out; serializeJson(ev,out); espSendBroadcast(out);
  }
}

unsigned long lastAnn = 0;
unsigned long lastBtnDebounce = 0;
bool lastBtnState = HIGH;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Client starting...");
  EEPROM.begin(EEPROM_SIZE);
  loadFromEEPROM();
  EEPROM.end();

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // default role for first run helpful for testing
  if (myRole.length() == 0) {
    myRole = "botoeira";
    myLogical = "door1_button";
  }

  // init esp-now
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  if (esp_now_init() != 0) {
    Serial.println("esp_now_init failed");
  } else {
    esp_now_register_recv_cb(onDataRecv);
    Serial.println("esp_now initialized");
  }

  // announce self
  sendAnnounce();
  delay(200);
  // request config by broadcast (gateway will reply with config)
  StaticJsonDocument<128> req; req["type"]="request_config"; req["src"]=ownMacStr();
  String rq; serializeJson(req, rq);
  espSendBroadcast(rq);
}

void loop() {
  // periodic announce if no gateway known
  if (gatewayMac.length() == 0 && millis() - lastAnn > 10000) {
    lastAnn = millis();
    sendAnnounce();
  }

  // handle button if role==botoeira
  if (myRole == "botoeira") {
    bool st = digitalRead(PIN_BUTTON);
    if (st != lastBtnState) {
      if (millis() - lastBtnDebounce > 50) {
        lastBtnDebounce = millis();
        if (st == LOW) { // pressed (assuming pull-up)
          Serial.println("Button pressed!");
          addOrUpdateState(myLogical, "pressed");
          sendEvent(myLogical, "pressed");
          delay(10);
          // evaluate interlock & send command if allowed
          handleButtonPressLocal();
        }
      }
    }
    lastBtnState = st;
  }

  // sensor role reading (if sensor uses same pin)
  if (myRole == "sensor") {
    bool s = digitalRead(PIN_BUTTON); // using PIN_BUTTON as example
    String st = s ? "closed" : "open";
    String prev = getState(myLogical);
    if (prev != st) {
      addOrUpdateState(myLogical, st);
      sendEvent(myLogical, st);
      Serial.printf("Sensor changed %s -> %s\n", myLogical.c_str(), st.c_str());
    }
  }

  // periodical report for relé
  if (myRole == "rele") {
    static unsigned long lastRep = 0;
    if (millis() - lastRep > 15000) {
      lastRep = millis();
      String state = digitalRead(PIN_RELAY) ? "on" : "off";
      addOrUpdateState(myLogical, state);
      sendEvent(myLogical, state);
    }
  }

  delay(10);
}