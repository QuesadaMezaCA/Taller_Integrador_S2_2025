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

WiFiClient aprsClient;
unsigned long packetsReceived = 0;
unsigned long packetsSentToAPRSIS = 0;
unsigned long packetsReceivedFromAPRSIS = 0;
unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 30000;

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

// ===== Conexión APRS-IS =====
bool connectToAPRSIS() {
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
        if (response.indexOf("verified") >= 0) {
          authSuccess = true;
        }
      }
      delay(100);
    }
    if (authSuccess) {
      Serial.println(getTimestamp() + "✓ Autenticación exitosa, esperando tráfico...");
    } else {
      Serial.println(getTimestamp() + "✗ Problema con autenticación");
    }
    return true;
  } else {
    Serial.println(getTimestamp() + "✗ Fallo conexión APRS-IS");
    return false;
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

// ===== Mostrar estado en OLED =====
void showOLED(const String &line1, const String &line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println(line1);
  display.println(line2);
  display.display();
}

// ===== Función para mostrar información en tiempo real en OLED =====
void updateOLEDStatus() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  
  // Línea 1: Estado WiFi
  String wifiStatus = "WiFi: ";
  if (WiFi.status() == WL_CONNECTED) {
    // Acortar el SSID si es muy largo
    String ssidShort = WiFi.SSID();
    if (ssidShort.length() > 12) {
      ssidShort = ssidShort.substring(0, 12) + "...";
    }
    wifiStatus += ssidShort;
  } else {
    wifiStatus += "Desconectado";
  }
  display.println(wifiStatus);
  
  // Línea 2: Estado APRS-IS
  String aprsStatus = "APRS-IS: ";
  if (aprsClient.connected()) {
    aprsStatus += "OK ";
    // Mostrar servidor abreviado
    String serverShort = String(server);
    if (serverShort.length() > 10) {
      serverShort = serverShort.substring(0, 10) + "...";
    }
    aprsStatus += serverShort;
  } else {
    aprsStatus += "OFF";
  }
  display.println(aprsStatus);
  
  // Línea 3: Paquetes recibidos por LoRa
  String loraRx = "LoRa RX: ";
  loraRx += String(packetsReceived);
  display.println(loraRx);
  
  // Línea 4: Paquetes enviados/recibidos APRS-IS
  String aprsPackets = "APRS TX/RX: ";
  aprsPackets += String(packetsSentToAPRSIS);
  aprsPackets += "/";
  aprsPackets += String(packetsReceivedFromAPRSIS);
  display.println(aprsPackets);
  
  // Línea 5: RSSI actual de LoRa si hay señal
  if (packetsReceived > 0) {
    String rssiLine = "RSSI: ";
    rssiLine += String(LoRa.packetRssi());
    rssiLine += "dBm";
    display.println(rssiLine);
  } else {
    display.println("Esperando datos LoRa...");
  }
  
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
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    updateOLEDStatus(); // Actualizar OLED durante conexión
  }
  Serial.println("\n" + getTimestamp() + "✓ WiFi conectado!");
  updateOLEDStatus();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  
  // Configurar LoRa con parámetros más robustos
  LoRa.setSignalBandwidth(125E3);
  LoRa.setSpreadingFactor(7);
  LoRa.setCodingRate4(5);
  
  if (!LoRa.begin(LORA_BAND)) {
    Serial.println(getTimestamp() + "✗ Error iniciando LoRa!");
    updateOLEDStatus();
    return;
  }
  Serial.println(getTimestamp() + "✓ LoRa iniciado");
  updateOLEDStatus();

  if (connectToAPRSIS()) {
    delay(2000);
    sendBeacon();
    updateOLEDStatus();
  }
}

// ===== Loop =====
void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) processLoRaPacket(packetSize);
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




