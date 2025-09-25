#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <LoRa.h>
#include <algorithm>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===== LED de usuario =====
#define LED_PIN 25 

// ===== CONFIGURACIÓN OLED =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== CONFIGURACIÓN APRS =====
const char* callsign = "Ti0tec5-7";   
const char* passcode = "26556";       
const char* server   = "rotate.aprs2.net";
const int   port     = 14580;

// ===== CONFIGURACIÓN WIFI =====
const char* ssid     = "Ubnt1_Casa_4";
const char* password = "cartago4";

// ===== CONFIGURACIÓN LoRa =====
#define LORA_SCK     5
#define LORA_MISO    19
#define LORA_MOSI    27
#define LORA_CS      18
#define LORA_RST     14
#define LORA_IRQ     26
#define LORA_BAND    433.775E6

// ===== CONFIGURACIÓN BEACON =====
const float BEACON_LAT = 9.8599407;
const float BEACON_LON = -83.9063452;
const char* BEACON_COMMENT = "iGATE Brainer";
const unsigned long BEACON_INTERVAL = 180000; // 3 min
const unsigned long STATUS_INTERVAL = 180000;
unsigned long lastBeaconTime = 0;
unsigned long lastStatusTime = 0;

// ===== VARIABLES DE CONEXIÓN =====
WiFiClient aprsClient;
unsigned long packetsReceived = 0;
unsigned long packetsSentToAPRSIS = 0;
unsigned long packetsReceivedFromAPRSIS = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 30000; // 30 segundos

// ===== VARIABLES PARA RECONEXIÓN WIFI =====
unsigned long lastWifiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 10000; // Revisar WiFi cada 10 segundos
bool wifiConnected = false;
unsigned long lastWifiReconnectAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL = 30000; // Intentar reconectar cada 30 segundos

// ===== VARIABLES PARA DETECCIÓN DE CONEXIÓN APRS-IS =====
unsigned long lastAPRSTrafficTime = 0;
const unsigned long APRS_TIMEOUT = 120000; // 2 minutos sin tráfico = desconectado
unsigned long lastServerPing = 0;
const unsigned long SERVER_PING_INTERVAL = 60000; // Ping cada 1 minuto

// ===== Función timestamp =====
String getTimestamp() {
  unsigned long seconds = millis() / 1000;
  unsigned long minutes = seconds / 60;
  seconds %= 60;
  minutes %= 60;
  char timestamp[12];
  sprintf(timestamp, "[%02lu:%02lu] ", minutes, seconds);
  return String(timestamp);
}

// ===== Conexión WiFi con reconexión automática =====
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
    digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Parpadear LED durante conexión
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

// ===== Conexión APRS-IS =====
bool connectToAPRSIS() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(getTimestamp() + "No hay conexión WiFi, no se puede conectar a APRS-IS");
    return false;
  }
  
  // Cerrar conexión anterior si existe
  if (aprsClient.connected()) {
    aprsClient.stop();
    delay(1000);
  }
  
  Serial.print(getTimestamp());
  Serial.print("Conectando a ");
  Serial.print(server);
  Serial.print(":");
  Serial.println(port);
  
  if (aprsClient.connect(server, port)) {
    Serial.println(getTimestamp() + "✓ Conectado a APRS-IS");
    delay(1000);
    
    // Limpiar buffer inicial
    while (aprsClient.available()) {
      String response = aprsClient.readStringUntil('\n');
      Serial.println(getTimestamp() + "SRV_INIT: " + response);
    }
    
    // Autenticación
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
        if (response.indexOf("verified") >= 0 || response.indexOf("logresp") >= 0) {
          authSuccess = true;
        }
      }
      delay(100);
    }
    
    if (authSuccess) {
      Serial.println(getTimestamp() + "✓ Autenticación exitosa, esperando tráfico...");
      lastAPRSTrafficTime = millis(); // Reset timer de tráfico
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

// ===== Verificar estado de conexión APRS-IS =====
bool checkAPRSISConnectionHealth() {
  if (!aprsClient.connected()) {
    return false;
  }
  
  // Verificar si hay timeout por falta de tráfico
  if (millis() - lastAPRSTrafficTime > APRS_TIMEOUT) {
    Serial.println(getTimestamp() + "✗ Timeout APRS-IS (sin tráfico por " + String(APRS_TIMEOUT/1000) + "s)");
    return false;
  }
  
  // Verificar si el cliente está realmente conectado
  if (!aprsClient.connected()) {
    Serial.println(getTimestamp() + "✗ Conexión APRS-IS perdida");
    return false;
  }
  
  return true;
}

// ===== Enviar ping al servidor APRS-IS =====
void sendServerPing() {
  if (aprsClient.connected()) {
    aprsClient.print("# Ping TTGO-iGate " + String(millis()) + "\n");
    Serial.println(getTimestamp() + "Ping enviado al servidor");
    lastServerPing = millis();
  }
}

// ===== Verificar y reconectar WiFi =====
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConnected) {
      Serial.println(getTimestamp() + "WiFi desconectado");
      wifiConnected = false;
    }
    
    if (millis() - lastWifiReconnectAttempt > WIFI_RECONNECT_INTERVAL) {
      Serial.println(getTimestamp() + "Intentando reconectar WiFi...");
      lastWifiReconnectAttempt = millis();
      connectToWiFi();
    }
  } else {
    if (!wifiConnected) {
      Serial.println(getTimestamp() + "✓ WiFi reconectado");
      wifiConnected = true;
      // Forzar reconexión a APRS-IS después de recuperar WiFi
      lastReconnectAttempt = 0; // Permitir reconexión inmediata
    }
  }
}

// ===== Reconexión APRS-IS =====
void checkAPRSISConnection() {
  bool needsReconnect = false;
  
  // Verificar si necesita reconexión
  if (WiFi.status() == WL_CONNECTED) {
    if (!aprsClient.connected()) {
      needsReconnect = true;
      Serial.println(getTimestamp() + "APRS-IS desconectado");
    } else if (!checkAPRSISConnectionHealth()) {
      needsReconnect = true;
    }
  }
  
  // Intentar reconexión si es necesario
  if (needsReconnect && millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
    Serial.println(getTimestamp() + "Intentando reconectar a APRS-IS...");
    lastReconnectAttempt = millis();
    
    if (connectToAPRSIS()) {
      // Enviar beacon inmediatamente después de reconectar
      lastBeaconTime = 0;
      Serial.println(getTimestamp() + "✓ Reconexión APRS-IS exitosa");
    } else {
      Serial.println(getTimestamp() + "✗ Falló reconexión APRS-IS");
    }
  }
  
  // Enviar ping periódico para mantener conexión activa
  if (aprsClient.connected() && millis() - lastServerPing > SERVER_PING_INTERVAL) {
    sendServerPing();
  }
}

// ===== Beacon =====
void sendBeacon() {
  if (!aprsClient.connected()) {
    Serial.println(getTimestamp() + "No conectado a APRS-IS, no se puede enviar beacon");
    return;
  }
  
  Serial.println(getTimestamp() + "Preparando beacon...");
  
  int lat_deg = abs((int)BEACON_LAT);
  float lat_min = (abs(BEACON_LAT) - lat_deg) * 60.0;
  char lat_dir = BEACON_LAT >= 0 ? 'N' : 'S';
  
  int lon_deg = abs((int)BEACON_LON);
  float lon_min = (abs(BEACON_LON) - lon_deg) * 60.0;
  char lon_dir = BEACON_LON >= 0 ? 'E' : 'W';
  
  String beaconPacket = String(callsign);
  beaconPacket += ">APRS,TCPIP:=";
  
  char position[30];
  sprintf(position, "%02d%05.2f%c/%03d%05.2f%c", 
          lat_deg, lat_min, lat_dir,
          lon_deg, lon_min, lon_dir);
  
  beaconPacket += String(position);
  beaconPacket += "&";
  beaconPacket += BEACON_COMMENT;
  beaconPacket += "\n";
  
  Serial.print(getTimestamp() + "BEACON_TX: ");
  Serial.print(beaconPacket);
  
  int bytesSent = aprsClient.print(beaconPacket);
  Serial.print(getTimestamp() + "BEACON_BYTES: ");
  Serial.println(bytesSent);
  
  if (bytesSent > 0) {
    packetsSentToAPRSIS++;
    Serial.println(getTimestamp() + "✓ Beacon enviado correctamente");
    lastAPRSTrafficTime = millis(); // Actualizar tiempo de último tráfico
  } else {
    Serial.println(getTimestamp() + "✗ Error enviando beacon");
  }
  
  lastBeaconTime = millis();
  lastStatusTime = millis();
}

// ===== Procesar APRS entrante =====
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
          lastAPRSTrafficTime = millis(); // Actualizar tiempo de último tráfico
        }
        buffer = "";
      }
    }
  }
}

// ===== Procesar LoRa entrante =====
void processLoRaPacket(int packetSize) {
  if (packetSize == 0) return;
  
  packetsReceived++; // Incrementar contador de paquetes LoRa recibidos
  
  // Intentar leer como texto ASCII primero
  String packetText = "";
  bool isText = true;
  
  for (int i = 0; i < packetSize; i++) {
    char c = (char)LoRa.read();
    packetText += c;
    
    // Verificar si el carácter es imprimible ASCII
    if (c < 32 || c > 126) {
      if (c != 10 && c != 13) { // Excluir newline y carriage return
        isText = false;
      }
    }
  }
  
  if (isText && packetText.length() > 0) {
    // Es texto legible
    packetText.trim();
    Serial.println(getTimestamp() + "LORA_RX [" + String(packetsReceived) + "]: " + packetText);
  } else {
    // Es datos binarios, mostrar en formato hexadecimal
    Serial.print(getTimestamp() + "LORA_RX_BIN [" + String(packetsReceived) + "]: ");
    
    // Reiniciar la lectura del paquete
    LoRa.peek(); // Esto reinicia la lectura interna
    
    for (int i = 0; i < packetSize; i++) {
      byte b = LoRa.read();
      if (b < 16) Serial.print("0");
      Serial.print(b, HEX);
      Serial.print(" ");
    }
    Serial.println();
    
    // También mostrar información del paquete
    Serial.println(getTimestamp() + "LORA_INFO: RSSI=" + String(LoRa.packetRssi()) + 
                   "dBm, SNR=" + String(LoRa.packetSnr()) + "dB, Size=" + String(packetSize) + "bytes");
  }
}

// ===== Función para mostrar información en tiempo real en OLED =====
void updateOLEDStatus() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  
  // Línea 1: WiFi
  String line1 = "WiFi: ";
  if (WiFi.status() == WL_CONNECTED) {
    String ssidShort = WiFi.SSID();
    if (ssidShort.length() > 12) {
      ssidShort = ssidShort.substring(0, 12) + "..";
    }
    line1 += ssidShort;
  } else {
    line1 += "DESCONECTADO";
  }
  display.println(line1);

  // Línea 2: RSSI WiFi (si está conectado)
  String line2 = "";
  if (WiFi.status() == WL_CONNECTED) {
    line2 = "WiFi RSSI: " + String(WiFi.RSSI()) + "dBm";
  } else {
    line2 = "Reconectando...";
  }
  display.println(line2);
  
  // Línea 3: Servidor APRS
  String line3 = "Srv: ";
  if (aprsClient.connected()) {
    String serverShort = String(server);
    if (serverShort.length() > 14) {
      serverShort = serverShort.substring(0, 14) + "..";
    }
    line3 += serverShort;
  } else {
    line3 += "DESCONECTADO";
  }
  display.println(line3);
  
  // Línea 4: Paquetes LoRa RX
  String line4 = "LoRa RX: " + String(packetsReceived);
  display.println(line4);
  
  // Línea 5: Paquetes APRS TX/RX
  String line5 = "APRS TX/RX: ";
  line5 += String(packetsSentToAPRSIS);
  line5 += "/";
  line5 += String(packetsReceivedFromAPRSIS);
  display.println(line5);
  
  // Línea 6: Estado del dispositivo
  String line6 = "Estado: ";
  if (WiFi.status() == WL_CONNECTED && aprsClient.connected()) {
    line6 += "OPERATIVO";
  } else if (WiFi.status() == WL_CONNECTED) {
    line6 += "WIFI-SOLO";
  } else {
    line6 += "OFFLINE";
  }
  display.println(line6);
  
  display.display();
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  

  // Inicializar OLED
  Wire.begin(21, 22);
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("❌ No se encontró OLED en dirección 0x3C");
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Hola LilyGO LoRa32!");
  display.println("OLED funcionando :)");
  display.display();

  Serial.println("\n" + getTimestamp() + "=== INICIANDO iGATE APRS ===");
  
  // Conexión inicial WiFi
  connectToWiFi();

  // Inicializar LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  
  // Configurar LoRa
  LoRa.setSignalBandwidth(125E3);
  LoRa.setSpreadingFactor(7);
  LoRa.setCodingRate4(5);
  
  if (!LoRa.begin(LORA_BAND)) {
    Serial.println(getTimestamp() + "✗ Error iniciando LoRa!");
    return;
  }
  Serial.println(getTimestamp() + "✓ LoRa iniciado");

  // Conexión inicial APRS-IS
  if (connectToAPRSIS()) {
    delay(2000);
    sendBeacon();
  }
  
  lastAPRSTrafficTime = millis(); // Inicializar timer de tráfico
}

// ===== Loop =====
void loop() {
  // Procesar LoRa
  int packetSize = LoRa.parsePacket();
  if (packetSize) processLoRaPacket(packetSize);
  
  // Verificar y mantener conexiones
  checkWiFiConnection();
  checkAPRSISConnection();
  
  // Procesar tráfico APRS-IS si está conectado
  if (aprsClient.connected()) {
    processAPRSTraffic();
    if (millis() - lastBeaconTime > BEACON_INTERVAL) {
      sendBeacon();
    }
  }
  
  // Actualizar OLED cada segundo
  static unsigned long lastOLEDUpdate = 0;
  if (millis() - lastOLEDUpdate > 1000) {
    updateOLEDStatus();
    lastOLEDUpdate = millis();
  }
  
  delay(50);
}




