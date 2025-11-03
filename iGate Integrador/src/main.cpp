#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <LoRa.h>
#include <algorithm>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define LED_PIN 25 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const char* callsign = "Ti0tec5-7";   
const char* passcode = "26556";       
const char* server   = "rotate.aprs2.net";
const int   port     = 14580;

const char* ssid     = "Ubnt1_Casa_4";
const char* password = "cartago4";

#define LORA_SCK     5
#define LORA_MISO    19
#define LORA_MOSI    27
#define LORA_CS      18
#define LORA_RST     14
#define LORA_IRQ     26
#define LORA_BAND    433.775E6

const float BEACON_LAT = 9.8599407;
const float BEACON_LON = -83.9063452;
const char* BEACON_COMMENT = "Escuela de Ingeniería Electrónica - ITCR";
const unsigned long BEACON_INTERVAL = 180000;
unsigned long lastBeaconTime = 0;

WiFiClient aprsClient;
unsigned long packetsReceived = 0;
unsigned long packetsSentToAPRSIS = 0;
unsigned long packetsReceivedFromAPRSIS = 0;
unsigned long packetsSentToLoRa = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 30000;

bool wifiConnected = false;
unsigned long lastWifiReconnectAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL = 30000;

unsigned long lastAPRSTrafficTime = 0;
const unsigned long APRS_TIMEOUT = 120000;
unsigned long lastServerPing = 0;
const unsigned long SERVER_PING_INTERVAL = 60000;

String getTimestamp() {
  unsigned long seconds = millis() / 1000;
  unsigned long minutes = seconds / 60;
  seconds %= 60;
  minutes %= 60;
  char timestamp[12];
  sprintf(timestamp, "[%02lu:%02lu] ", minutes, seconds);
  return String(timestamp);
}

bool connectToWiFi() {
  Serial.print(getTimestamp());
  Serial.print("Conectando a WiFi: ");
  Serial.println(ssid);
  
  WiFi.disconnect(true);
  delay(1000);
  WiFi.begin(ssid, password);
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n" + getTimestamp() + "✓ WiFi conectado!");
    Serial.print(getTimestamp() + "IP address: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_PIN, HIGH);
    wifiConnected = true;
    return true;
  } else {
    Serial.println("\n" + getTimestamp() + "✗ Fallo conexión WiFi");
    digitalWrite(LED_PIN, LOW);
    wifiConnected = false;
    return false;
  }
}

bool connectToAPRSIS() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(getTimestamp() + "No hay conexión WiFi, no se puede conectar a APRS-IS");
    return false;
  }
  
  if (aprsClient.connected()) aprsClient.stop();
  delay(1000);
  
  Serial.print(getTimestamp());
  Serial.print("Conectando a ");
  Serial.print(server);
  Serial.print(":");
  Serial.println(port);
  
  if (aprsClient.connect(server, port)) {
    Serial.println(getTimestamp() + "✓ Conectado a APRS-IS");
    delay(1000);
    
    while (aprsClient.available()) {
      String response = aprsClient.readStringUntil('\n');
      Serial.println(getTimestamp() + "SRV_INIT: " + response);
    }
    
    String auth = "user " + String(callsign) +
                  " pass " + String(passcode) +
                  " vers TTGO-LoRa-iGate 1.0 " +
                  "filter r/9.85/-83.90/200\n";
    aprsClient.print(auth);
    Serial.print(getTimestamp() + "AUTH_SEND: ");
    Serial.print(auth);

    delay(2000);
    bool authSuccess = false;
    unsigned long authStart = millis();
    while (millis() - authStart < 5000) {
      if (aprsClient.available()) {
        String response = aprsClient.readStringUntil('\n');
        Serial.println(getTimestamp() + "AUTH_RESP: " + response);
        if (response.indexOf("verified") >= 0 || response.indexOf("logresp") >= 0) authSuccess = true;
      }
      delay(100);
    }
    
    if (authSuccess) {
      Serial.println(getTimestamp() + "✓ Autenticación exitosa, esperando tráfico...");
      lastAPRSTrafficTime = millis();
      return true;
    } else {
      Serial.println(getTimestamp() + "✗ Problema con autenticación");
      aprsClient.stop();
      return false;
    }
  } else {
    Serial.println(getTimestamp() + "✗ Fallo conexión APRS-IS");
    return false;
  }
}

struct AX25Packet {
  String destination;
  String source;
  String path;
  String info;
};

AX25Packet parseAX25(const String& packet) {
  AX25Packet ax;
  int sep1 = packet.indexOf('>');
  int sep2 = packet.indexOf(':');
  
  if (sep1 < 0 || sep2 < 0 || sep2 <= sep1) {
    ax.info = packet;
    return ax;
  }
  
  ax.destination = packet.substring(0, sep1);
  
  String rest = packet.substring(sep1 + 1, sep2);
  int commaIndex = rest.indexOf(',');
  
  if (commaIndex >= 0) {
    ax.source = rest.substring(0, commaIndex);
    ax.path   = rest.substring(commaIndex + 1);
  } else {
    ax.source = rest;
    ax.path = "";
  }
  
  ax.info = packet.substring(sep2 + 1);
  return ax;
}

void forwardLoRaToAPRSIS() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String loraPacket = "";
    while (LoRa.available()) loraPacket += (char)LoRa.read();
    
    packetsReceived++;
    
    AX25Packet ax = parseAX25(loraPacket);
    
    Serial.println(getTimestamp() + "📡 LoRa_RX [" + String(packetsReceived) + "]: " + loraPacket);
    Serial.println("   → Destino: " + ax.destination + " | Fuente: " + ax.source + " | Path: " + ax.path + " | Info: " + ax.info);
    
    if (ax.destination != callsign && aprsClient.connected()) {
      int bytesSent = aprsClient.print(loraPacket + "\n");
      if (bytesSent > 0) {
        packetsSentToAPRSIS++;
        Serial.println(getTimestamp() + "➡️ Reenviado a APRS-IS [" + String(packetsSentToAPRSIS) + "]");
      } else {
        Serial.println(getTimestamp() + "✗ Error reenviando a APRS-IS");
      }
    }
  }
}

void forwardAPRStoLoRa(const String& aprsPacket) {
  LoRa.beginPacket();
  LoRa.print(aprsPacket);
  LoRa.endPacket();
  Serial.println(getTimestamp() + "⬅️ APRS-IS_TX→LoRa: " + aprsPacket);
  packetsSentToLoRa++;
}

void processAPRSTraffic() {
  static String buffer = "";
  while (aprsClient.available()) {
    char c = aprsClient.read();
    buffer += c;
    if (c == '\n') {
      buffer.trim();
      if (buffer.length() > 0) {
        if (buffer.charAt(0) == '#') {
          Serial.println(getTimestamp() + "SRV_SYS: " + buffer);
        } else {
          packetsReceivedFromAPRSIS++;
          Serial.println(getTimestamp() + "🎯 APRS_RX [" + String(packetsReceivedFromAPRSIS) + "]: " + buffer);
          lastAPRSTrafficTime = millis();
          forwardAPRStoLoRa(buffer);
        }
        buffer = "";
      }
    }
  }
}

void sendBeacon() {
  if (!aprsClient.connected()) return;
  
  Serial.println(getTimestamp() + "Preparando beacon...");
  
  int lat_deg = abs((int)BEACON_LAT);
  float lat_min = (abs(BEACON_LAT) - lat_deg) * 60.0;
  char lat_dir = BEACON_LAT >= 0 ? 'N' : 'S';
  
  int lon_deg = abs((int)BEACON_LON);
  float lon_min = (abs(BEACON_LON) - lon_deg) * 60.0;
  char lon_dir = BEACON_LON >= 0 ? 'E' : 'W';
  
  String beaconPacket = String(callsign) + ">APRS,TCPIP:=";
  char position[30];
  sprintf(position, "%02d%05.2f%c/%03d%05.2f%c", lat_deg, lat_min, lat_dir, lon_deg, lon_min, lon_dir);
  beaconPacket += String(position) + "&" + BEACON_COMMENT + "\n";
  
  Serial.print(getTimestamp() + "BEACON_TX: ");
  Serial.print(beaconPacket);
  
  int bytesSent = aprsClient.print(beaconPacket);
  Serial.print(getTimestamp() + "BEACON_BYTES: ");
  Serial.println(bytesSent);
  
  if (bytesSent > 0) {
    packetsSentToAPRSIS++;
    Serial.println(getTimestamp() + "✓ Beacon enviado correctamente");
    lastAPRSTrafficTime = millis();
  }
  
  lastBeaconTime = millis();
}

bool checkAPRSISConnectionHealth() {
  if (!aprsClient.connected()) return false;
  if (millis() - lastAPRSTrafficTime > APRS_TIMEOUT) {
    Serial.println(getTimestamp() + "✗ Timeout APRS-IS (sin tráfico)");
    return false;
  }
  return true;
}

void sendServerPing() {
  if (aprsClient.connected()) {
    aprsClient.print("# Ping TTGO-iGate " + String(millis()) + "\n");
    Serial.println(getTimestamp() + "Ping enviado al servidor");
    lastServerPing = millis();
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConnected) {
      Serial.println(getTimestamp() + "WiFi desconectado");
      wifiConnected = false;
    }
    if (millis() - lastWifiReconnectAttempt > WIFI_RECONNECT_INTERVAL) {
      lastWifiReconnectAttempt = millis();
      connectToWiFi();
    }
  } else if (!wifiConnected) {
    wifiConnected = true;
    lastReconnectAttempt = 0;
  }
}

void checkAPRSISConnection() {
  bool needsReconnect = (WiFi.status() == WL_CONNECTED) && (!aprsClient.connected() || !checkAPRSISConnectionHealth());
  if (needsReconnect && millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
    lastReconnectAttempt = millis();
    if (connectToAPRSIS()) lastBeaconTime = 0;
  }
  if (aprsClient.connected() && millis() - lastServerPing > SERVER_PING_INTERVAL) sendServerPing();
}

void updateOLEDStatus() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  
  display.println("WiFi: " + String(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "DESCONECTADO"));
  display.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
  display.println("Srv: " + String(aprsClient.connected() ? server : "DESCONECTADO"));
  display.println("LoRa RX/TX: " + String(packetsReceived) + "/" + String(packetsSentToLoRa));
  display.println("APRS TX/RX: " + String(packetsSentToAPRSIS) + "/" + String(packetsReceivedFromAPRSIS));
  display.println("Estado: " + String((WiFi.status() == WL_CONNECTED && aprsClient.connected()) ? "OPERATIVO" : (WiFi.status() == WL_CONNECTED ? "WIFI-SOLO" : "OFFLINE")));
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Wire.begin(21, 22);
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("❌ No se encontró OLED en dirección 0x3C");
    for(;;);
  }
  display.clearDisplay();
  display.println("Hola LilyGO LoRa32!");
  display.display();

  Serial.println("\n" + getTimestamp() + "=== INICIANDO iGATE APRS ===");
  connectToWiFi();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setSpreadingFactor(7);
  LoRa.setCodingRate4(5);
  if (!LoRa.begin(LORA_BAND)) {
    Serial.println(getTimestamp() + "✗ Error iniciando LoRa!");
    return;
  }
  Serial.println(getTimestamp() + "✓ LoRa iniciado");

  if (connectToAPRSIS()) sendBeacon();
  lastAPRSTrafficTime = millis();
}

void loop() {
  checkWiFiConnection();
  checkAPRSISConnection();

  if (aprsClient.connected()) {
    processAPRSTraffic();
    if (millis() - lastBeaconTime > BEACON_INTERVAL) sendBeacon();
  }

  forwardLoRaToAPRSIS();

  static unsigned long lastOLEDUpdate = 0;
  if (millis() - lastOLEDUpdate > 1000) {
    updateOLEDStatus();
    lastOLEDUpdate = millis();
  }

  delay(50);
}





